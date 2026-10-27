// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  tzpl_silo.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_silo.hpp"
#include <cstring>
#include "tzpl_engine.hpp"
#include "tzpl_chanadapt.hpp"
#include "tzpl_mixer.hpp"
#include "tzpl_xfader.hpp"
#include "tzpl_note_voicer.hpp"
#include "tzpl_denormals.hpp"
#include <chrono>
#include <cmath>
#ifndef __APPLE__
#include <mutex>
#include <sys/mman.h>
#endif

namespace engine {

const int kCmdFifoSize = 1024;

Silo::Silo()
    : to_nrt_(kCmdFifoSize),
    from_nrt_(kCmdFifoSize),
    dead_nodes_(kCmdFifoSize),
    nrt_nodeTable_(kHashBins),
    rt_nodeTable_(kHashBins)
{}

Silo::~Silo() {
    deleteNodes(removeAllNodes());
    delete inputNode_;
    delete outputNode_;
}

void Silo::mixDown(int numFrames, f32* out) {
    // This performs a tree reduce sum. All silos are summed in a parallel binary tree.
    int numSilos = int(engine_->silos_.size());
    int numSamples = numFrames * engine_->streamParams_.channels;
    int a = index_; // The amount of summing work done by a silo depends on its index.
    int b = 1; // sibling offset.
    while (!(a&1) && index_+b < numSilos) {
        Silo& other = engine_->silos_[index_+b];
        other.done_sem_.wait();
        
        f32* siloOut = other.outbuf_;
        for (int i = 0; i < numSamples; ++i) {
            out[i] += siloOut[i];
        }
        a >>= 1;
        b <<= 1;
    }
}

tzpl_SErr Silo::installTap(TapSlot* slot, Node* node, int outlet, i64 tapID) {
    // The NRT side prechecks the budget, so a full table here means the
    // registry and the RT table disagreed; TapOutletCmd::doNRT cleans up.
    if (numTaps_ >= kMaxTaps) return tzpl_errInternal;
    RTTap& t = rt_taps_[numTaps_];
    t.slot = slot;
    t.node = node;
    t.bufs = node->outs[outlet].bufs_;
    // slot->chans was set at bundle submit (capped for scope capture).
    t.chans = slot->chans;
    t.tapID = tapID;
    ++numTaps_;
    return tzpl_errNone;
}

TapSlot* Silo::rt_findTap(i64 tapID) {
    for (int i = 0; i < numTaps_; ++i) {
        if (rt_taps_[i].tapID == tapID) return rt_taps_[i].slot;
    }
    if (index_ == 0) return engine_->rt_findMasterTap(tapID);
    return nullptr;
}

// Drop entry i by moving the last live entry into the hole. Keeps [0, numTaps_)
// dense; the caller must NOT advance its index afterwards.
void Silo::eraseTapAt(int i) {
    rt_taps_[i] = rt_taps_[numTaps_ - 1];
    rt_taps_[numTaps_ - 1] = RTTap{};
    --numTaps_;
}

void Silo::removeTap(i64 tapID) {
    for (int i = 0; i < numTaps_; ) {
        if (rt_taps_[i].tapID == tapID) eraseTapAt(i);
        else ++i;
    }
}

void Silo::clearTapsForNode(Node* node) {
    for (int i = 0; i < numTaps_; ) {
        if (rt_taps_[i].node == node) {
            // Zero the published levels so a meter on a dead node reads
            // silence rather than freezing at its last value. The registry
            // slot itself stays alive until someone calls untap.
            rt_taps_[i].slot->peak.store(0.0f, std::memory_order_relaxed);
            rt_taps_[i].slot->rms.store(0.0f, std::memory_order_relaxed);
            eraseTapAt(i);
        } else {
            ++i;
        }
    }
}

void Silo::processTaps(int lo, int hi) {
    for (int i = 0; i < numTaps_; ++i) {
        RTTap& t = rt_taps_[i];
        TapSlot* ts = t.slot;
        int chans = t.chans;
        // A node that did not run these samples (not feeding the output, or
        // not yet connected) is silent: its buffers hold whatever it last
        // wrote. The input node's are filled every block.
        bool ran = t.node == inputNode_ || t.node->ranTo_ == blockStart_ + hi;

        f32 pk = ts->accumPeak;
        f32 sq = 0.0f;
        for (int c = 0; c < chans && ran; ++c) {
            f32 const* buf = static_cast<f32 const*>(t.bufs[c]);
            for (int k = lo; k < hi; ++k) {
                f32 v = buf[k];
                f32 a = std::fabs(v);
                if (a > pk) pk = a;
                sq += v * v;
            }
        }
        ts->accumPeak = pk;
        ts->accumSq += sq / (f32)chans;
        ts->accumCount += hi - lo;
        if (ts->accumCount >= ts->publishPeriod) {
            ts->peak.store(ts->accumPeak, std::memory_order_relaxed);
            ts->rms.store(std::sqrt(ts->accumSq / (f32)ts->accumCount),
                          std::memory_order_relaxed);
            ts->accumPeak = 0.0f;
            ts->accumSq = 0.0f;
            ts->accumCount = 0;
        }
        if (ts->mode == tapScope) {
            // Whole interleaved frames or nothing, so a full FIFO drops
            // frames without slipping channel alignment.
            for (int k = lo; k < hi; ++k) {
                if (ts->fifo.space() < chans) break;
                for (int c = 0; c < chans; ++c)
                    ts->fifo.push(ran ? static_cast<f32 const*>(t.bufs[c])[k] : 0.0f);
            }
        }
    }
}

void Silo::runNodes(int lo, int hi) {
    for (Node* node = rt_sortedNodeList_; node; ) {
        if (node->sccLen_ > 0) {
            int len = node->sccLen_;
            runCycle(node, len, lo, hi);
            for (int k = 0; k < len; ++k) node = node->sorted_next;
        } else {
            node->runRange(this, lo, hi);
            node = node->sorted_next;
        }
    }
}

void Silo::runTo(int hi) {
    int lo = ranTo_;
    sortNodes();
    runNodes(lo, hi);
    processTaps(lo, hi);
    writeOutput(lo, hi);
    ranTo_ = hi;
}

// Interleave the output node's input into this silo's device buffer. The
// input reads its source's buffers directly, and an edit can change the
// source mid-block, so each part of the block is copied as it is run.
void Silo::writeOutput(int lo, int hi) {
    f32* out = index_ > 0 ? outbuf_ : engine_->out_;
    int outChannels = engine_->streamParams_.channels;
    int offset = std::min(channelOffset_, outChannels);
    InPort& op = outputNode_->ins[0];
    int nc = std::min(op.chans_, outChannels - offset);
    f32* outp = out + (usize)frameOffset_ * outChannels + offset;
    for (int c = 0; c < nc; ++c) {
        f32 const* src = static_cast<f32 const*>(op.chanPtrs_[c]);
        for (int k = lo; k < hi; ++k) outp[(usize)k * outChannels + c] = src[k];
    }
}

//=============================================================================================
#pragma mark EVENTS

void Silo::runCommandChain(Command* head) {
    Command* cmd = head;
    while (cmd) {
        Command* next = cmd->next_;
        cmd->run(this);
        blockDone_.add(cmd);
        cmd = next;
    }
}

// The first sample of the block at which clock `clk` has reached `beat`
// (the per-sample engine fired a command at the first sample whose beat was
// >= its beat), and the fractional time where it reaches it exactly.
static void clockEventTime(TempoClock const& clk, f64 beat, i64 b0, int n,
                           f64& t, int& off) {
    t = clk.sampleAtBeat(beat);
    if (!(t >= (f64)b0)) t = (f64)b0; // late (or NaN): at the block start
    f64 rel = std::ceil(t - (f64)b0);
    int i = rel >= (f64)(n - 1) ? n - 1 : (int)rel;
    if (i < 0) i = 0;
    while (i > 0 && clk.beatAtSample(b0 + i - 1) >= beat) --i;
    while (i < n - 1 && clk.beatAtSample(b0 + i) < beat) ++i;
    off = i;
    if (t > (f64)(b0 + i)) t = (f64)(b0 + i);
}

void Silo::collectAndRunEvents(int n) {
    i64 b0 = blockStart_;
    // Pop everything due in this block from every clock: a command is due
    // when the clock's beat at the block's last sample has reached it.
    int numClocks = (int)tempoClocks_.size();
    for (int k = 0; k < numClocks; ++k) {
        TempoClock& clk = tempoClocks_[k];
        if (!clk.queue_.head) continue;
        CommandList popped = clk.queue_.pop(clk.beatAtSample(b0 + n - 1));
        if (!popped.head) continue;
        CommandList& due = dueLists_[k];
        if (due.tail) { due.tail->next_ = popped.head; due.tail = popped.tail; }
        else due = popped;
    }

    // Merge clock commands and task wakeups in time order.
    constexpr int kMaxTaskRuns = 4096;
    int taskRuns = 0;
    f64 blockEnd = (f64)(b0 + n);
    for (;;) {
        int best = -1;
        f64 bestT = INFINITY;
        int bestOff = 0;
        for (int k = 0; k < numClocks; ++k) {
            Command* head = dueLists_[k].head;
            if (!head) continue;
            f64 t; int off;
            clockEventTime(tempoClocks_[k], head->beatTime_, b0, n, t, off);
            if (t < bestT) { bestT = t; bestOff = off; best = k; }
        }
        if (taskNextFn_ && taskRuns < kMaxTaskRuns) {
            f64 tt = taskNextFn_(taskSched_, this);
            if (tt < blockEnd && tt <= bestT) {
                if (!(tt >= (f64)b0)) tt = (f64)b0;
                f64 rel = std::ceil(tt - (f64)b0);
                int off = rel >= (f64)(n - 1) ? n - 1 : (int)rel;
                setNow(tt, off);
                taskRunFn_(taskSched_, this, tt);
                ++taskRuns;
                continue;
            }
        }
        if (best < 0) break;
        // Run the head command and every following one on the same beat
        // (one bundle), in order.
        CommandList& due = dueLists_[best];
        f64 beat = due.head->beatTime_;
        setNow(bestT, bestOff);
        while (due.head && due.head->beatTime_ == beat) {
            Command* cmd = due.head;
            due.head = cmd->next_;
            if (!due.head) due.tail = nullptr;
            cmd->next_ = nullptr;
            cmd->run(this);
            blockDone_.add(cmd);
        }
    }
    setNow((f64)b0, 0);
}

Command* Silo::takeAllScheduled() {
    Command* out = nullptr;
    auto take = [&](Command* cmd) {
        while (cmd) {
            Command* next = cmd->next_;
            cmd->prev_ = nullptr;
            cmd->next_ = out;
            out = cmd;
            cmd = next;
        }
    };
    for (TempoClock& clk : tempoClocks_) {
        take(clk.queue_.head);
        clk.queue_.head = clk.queue_.tail = nullptr;
    }
    for (CommandList& due : dueLists_) {
        take(due.head);
        due.head = due.tail = nullptr;
    }
    return out;
}

//=============================================================================================
#pragma mark BLOCK LOOP

void Silo::processBlock(int frameOffset, int n) {
    blockStart_ = sampleTime_;
    prevBlockLen_ = blockLen_;
    blockLen_ = n;
    ranTo_ = 0;
    frameOffset_ = frameOffset;
    inBlock_ = true;
    setNow((f64)blockStart_, 0);

    // Hardware input: deinterleave this chunk into the input node's outlet.
    // First, since a graph edit mid-block runs the nodes up to it.
    int inputChannels = engine_->streamParams_.inputChannels;
    if (engine_->in_ && inputChannels > 0 && !inputNode_->outs.empty()) {
        OutPort& ip = inputNode_->outs[0];
        f32 const* in = engine_->in_ + (usize)frameOffset * inputChannels;
        int nc = std::min(ip.chans_, inputChannels);
        for (int c = 0; c < nc; ++c) {
            f32* dst = static_cast<f32*>(ip.bufs_[c]);
            for (int k = 0; k < n; ++k) dst[k] = in[(usize)k * inputChannels + c];
        }
    }

    processRTCommands();
    collectAndRunEvents(n);
    runTo(n);

    finishBlock();
    sampleTime_ += n;
    inBlock_ = false;
}

void Silo::finishBlock() {
    // Faders that reached their end retire now: nothing reads this block's
    // buffers any more. Collected first: retiring one can drop other hidden
    // nodes (adapters) from the list being walked. Any beyond the batch stay
    // done and retire after the next block.
    constexpr int kMaxRetire = 256;
    Node* done[kMaxRetire];
    int numDone = 0;
    for (Node* node = rt_sortedNodeList_; node && numDone < kMaxRetire; node = node->sorted_next) {
        if (node->isXFader_ && node->faderDone_) done[numDone++] = node;
    }
    for (int i = 0; i < numDone; ++i) retireXFader(this, done[i]);
    while (pendingFree_) {
        Node* node = pendingFree_;
        pendingFree_ = node->pendingFreeNext_;
        node->pendingFreeNext_ = nullptr;
        removeNodeNow(node);
    }
    if (blockDone_.head) to_nrt_.push(blockDone_.popAll());
}

void Silo::processFrames() {
    // Two clock reads per device buffer (commpage on macOS, vDSO on Linux).
    // The split at mixDown matters: silo 0's mixDown blocks on the worker
    // silos' semaphores, so folding that wait into the DSP figure would make
    // silo 0 look pathologically slow.
    bool timing = engine_->statsEnabled_.load(std::memory_order_relaxed);
    auto t0 = timing ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};

    f32* out = index_ > 0 ? outbuf_ : engine_->out_;
    int numFrames = engine_->curFrames_;
    int outChannels = engine_->streamParams_.channels;

    // Channels this silo does not write stay silent.
    memset(out, 0, (usize)numFrames * outChannels * sizeof(f32));

    // The device buffer runs in chunks of up to one block (D5); the last
    // chunk may be shorter. Synths handle any n, so no latency is added.
    int block = engine_->blockSize_;
    for (int pos = 0; pos < numFrames; pos += block) {
        processBlock(pos, std::min(block, numFrames - pos));
    }

    // Per-buffer GC heartbeat: drain deferred-delete queue for the attached VM.
    if (heartbeatFn_ && vm_) {
        heartbeatFn_(vm_, this);
    }

    auto t1 = timing ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};

    mixDown(numFrames, out);
    done_sem_.signal();

    if (timing) {
        auto t2 = std::chrono::steady_clock::now();
        using ns = std::chrono::nanoseconds;
        u32 epoch = engine_->stats_.statsEpoch.load(std::memory_order_relaxed);
        publishBlockNanos(stats_,
                          (u64)std::chrono::duration_cast<ns>(t1 - t0).count(),
                          epoch);
        stats_.mixWaitNanos.store(
            (u64)std::chrono::duration_cast<ns>(t2 - t1).count(),
            std::memory_order_relaxed);
        stats_.blockCount.fetch_add(1, std::memory_order_relaxed);
        stats_.toNrtDepth.store(to_nrt_.depth(), std::memory_order_relaxed);
        stats_.fromNrtDepth.store(from_nrt_.depth(), std::memory_order_relaxed);
        stats_.deadNodesDepth.store(dead_nodes_.depth(), std::memory_order_relaxed);
        stats_.numTaps.store(numTaps_, std::memory_order_relaxed);
    }
}

void make_this_thread_realtime() {
    pthread_t thread = pthread_self();

    int policy;
    struct sched_param param;
    pthread_getschedparam(thread, &policy, &param);
#ifdef __APPLE__
    param.sched_priority = 63;
#else
    param.sched_priority = sched_get_priority_max(SCHED_RR);

    // Pin the process's pages so the RT threads never take a major page
    // fault mid-callback. Once per process; needs RLIMIT_MEMLOCK headroom
    // (see docs/LINUX.md), so failure is expected in unprivileged setups
    // and only costs the guarantee, not correctness.
    static std::once_flag mlockOnce;
    std::call_once(mlockOnce, [] {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            printf("mlockall failed; audio memory is not pinned "
                   "(raise memlock limits for the guarantee).\n");
        }
    });
#endif

    if (pthread_setschedparam(thread, SCHED_RR, &param) != 0) {
        printf("Failed to change thread priority.\n");
    }
}

void Silo::workLoop(Silo* s) {
    Engine* e = s->engine_;
    // Skip the SCHED_RR priority bump on NRT engines: there is no audio
    // device contending with us and an NRT render thread does not need
    // realtime priority (it runs as fast as the host can compute).
    if (!e->nrtMode_) {
        make_this_thread_realtime();
    }
    tzpl::flushDenormals();
    while (e->runSilos_) {
        s->start_sem_.wait();
        if (!e->runSilos_) break;
        s->processFrames();
    }
}

//=============================================================================================
#pragma mark CONNECTIONS

tzpl_SErr compatibleTypes(tzpl_SignalType const& a, tzpl_SignalType const& b) {
    if (a.elem != b.elem) return tzpl_errTypeMismatch;
    if (!(a.shape == b.shape)) return tzpl_errChanMismatch;
    return tzpl_errNone;
}

tzpl_SErr connectableTypes(tzpl_SignalType const& a, tzpl_SignalType const& b) {
    if (a.elem != b.elem) return tzpl_errTypeMismatch;
    return tzpl_errNone;
}

tzpl_SErr Silo::connect(OutPort* src, InPort* dst) {
    if (!src || !dst) return tzpl_errNodeNotFound;

    // Type-check before unlinking: a rejected connect must leave whatever
    // was already feeding the inlet alone rather than silently cutting it.
    tzpl_SErr err = connectableTypes(src->type_, dst->type_);
    if (err != tzpl_errNone) return err;
    if (!tzpl::broadcastsTo(src->type_.shape, dst->type_.shape)) return tzpl_errChanMismatch;

    runToEdit();
    if (dst->srcPort_) {
        unlink(dst);
    }

    // link onto source port list.
    if (src->dstList_) src->dstList_->prev_ = dst;
    dst->prev_ = NULL;
    dst->next_ = src->dstList_;
    src->dstList_ = dst;

    // Point every destination channel at the source channel it broadcasts
    // from: zero copy, any shapes.
    dst->srcPort_ = src;
    for (int w = 0; w < dst->chans_; ++w) {
        int j = tzpl::broadcastChannel(src->type_.shape, dst->type_.shape, w);
        dst->srcMap_[w] = j;
        dst->chanPtrs_[w] = src->bufs_[j];
        ++dst->ownVersion_[w];
    }

    needsSort_ = true;
    return tzpl_errNone;
}

tzpl_SErr Silo::reconnectOutput(OutPort* oldSrc, OutPort* newSrc) {
    InPort* dst = oldSrc->dstList_;
    tzpl_SErr err = tzpl_errNone;
    while (dst) {
        InPort* next = dst->next_;
        err = connect(newSrc, dst);
        if (err != tzpl_errNone) return err;
        dst = next;
    }
    return err;
}

void Silo::disconnect(OutPort* src) {
    InPort* dst = src->dstList_;
    while (dst) {
        InPort* next = dst->next_;
        unlink(dst);
        dst = next;
    }
}

tzpl_SErr Silo::disconnect(InPort* dst) {
    if (dst->srcPort_) {
        unlink(dst);
    }
    return tzpl_errNone;
}

void Silo::disconnectNode(Node* node) {
    for (InPort& port : node->ins) {
        disconnect(&port);
    }
    for (OutPort& port : node->outs) {
        disconnect(&port);
    }
}

void Silo::unlink(InPort* dst) {
    runToEdit();
    // remove from list
    OutPort* src = dst->srcPort_;
    if (dst->next_) dst->next_->prev_ = dst->prev_;
    if (dst->prev_) dst->prev_->next_ = dst->next_;
    else src->dstList_ = dst->next_;

    // set to disconnected: the input reverts to its value stages.
    dst->srcPort_ = nullptr;
    dst->prev_ = dst->next_ = nullptr;
    dst->restoreOwnBuffers();

    needsSort_ = true;

    // A hidden channel adapter exists only for the connection it was made
    // for. Cutting either end -- here, the only place a link is ever broken
    // -- retires it, so no orphan is left summing into a dead inlet.
    retireAdapter(this, dst->node_);
    retireAdapter(this, src->node_);
}

//=============================================================================================
#pragma mark NODES

tzpl_SErr Silo::addNode(Node* node) {
    if (node->rt_active) return tzpl_errAlreadyAdded;

    if (node->nodeID >= 0) {
        u32 bin = node->nodeID & kHashMask;
        Node* head = rt_nodeTable_[bin];
        if (head) head->rt_list.prev = node;
        node->rt_list.prev = nullptr;
        node->rt_list.next = head;
        rt_nodeTable_[bin] = node;
    }

    node->silo_ = this;
    node->rt_active = true;
    // The node's first block starts at the event's offset.
    if (inBlock_ && !node->started_) {
        node->addEvent(this, eventOffset(), nodeEvStart);
    }
    node->started_ = true;

    return tzpl_errNone;
}

tzpl_SErr Silo::removeNode(Node* node) {
    if (!node->rt_active) return tzpl_errAlreadyRemoved;

    runToEdit();
    clearTapsForNode(node);
    disconnectNode(node);

    // Unlink from the RT sorted list eagerly. When the engine is stopped
    // commands run synchronously and both stages complete inline -- the node
    // is deleted before any re-sort could happen.
    sortedListRemove(node);

    if (node->nodeID >= 0) {
        int bin = node->nodeID & kHashMask;
        if (node->rt_list.next) node->rt_list.next->rt_list.prev = node->rt_list.prev;
        if (node->rt_list.prev) node->rt_list.prev->rt_list.next = node->rt_list.next;
        else rt_nodeTable_[bin] = node->rt_list.next;
    }

    node->rt_active = false;

    return tzpl_errNone;
}

void Silo::removeNodeNow(Node* node) {
    // Before disconnecting, find any mixer chains this node's outputs feed.
    // After removeNode, the dstList_ links will be gone.
    constexpr int kMaxMixerCollapse = 8;
    InPort* mixerDsts[kMaxMixerCollapse];
    int numMixerDsts = 0;
    for (auto& out : node->outs) {
        for (InPort* dst = out.dstList_; dst; dst = dst->next_) {
            InPort* owner = mixerChainOwner(dst->node_);
            if (!owner) continue;
            bool seen = false;
            for (int i = 0; i < numMixerDsts; ++i) {
                if (mixerDsts[i] == owner) { seen = true; break; }
            }
            if (!seen && numMixerDsts < kMaxMixerCollapse) {
                mixerDsts[numMixerDsts++] = owner;
            }
        }
    }

    removeNode(node);

    for (int i = 0; i < numMixerDsts; ++i) {
        InPort* dst = mixerDsts[i];
        if (dst->mixerNode_ && countActiveMixerInputs(dst->mixerNode_) <= 1) {
            collapseMixer(this, dst);
        }
    }
}

void Silo::freeNodeAt(Node* node) {
    if (node->freeing_) return;
    if (!inBlock_) {
        removeNodeNow(node);
        return;
    }
    node->freeing_ = true;
    node->addEvent(this, eventOffset(), nodeEvFree);
    node->pendingFreeNext_ = pendingFree_;
    pendingFree_ = node;
}

Node* Silo::removeAllNodes() {
    Node* outNodes = nullptr;
    for (Node* nodes : rt_nodeTable_) {
        Node* node = nodes;
        while (node) {
            Node* next = node->rt_list.next;
            if (node->nodeID != 0 && node->nodeID != 1 && !node->freeing_) {
                // don't remove input or output nodes, nor nodes already
                // being freed this block (their RemoveNodeCmd owns them).
                removeNode(node);
                node->rt_list.next = outNodes;
                outNodes = node;
            }
        
            node = next;
        }
    }
    // Null when Engine construction failed before postInit() created them
    // (e.g. no audio device) and the silos are being torn down mid-unwind.
    if (inputNode_) {
        inputNode_->rt_list.prev = nullptr;
        inputNode_->rt_list.next = nullptr;
    }
    if (outputNode_) {
        outputNode_->rt_list.prev = nullptr;
        outputNode_->rt_list.next = nullptr;
    }
    return outNodes;
}

// Unlink a node from the RT sorted list without triggering a full re-sort.
// The list is singly linked, so this is an O(n) walk -- fine for removal,
// which is already O(ports) and never happens per-sample. A node that was
// added but never sorted in simply isn't found; that's a no-op.
void Silo::sortedListRemove(Node* node) {
    if (rt_sortedNodeList_ == node) {
        rt_sortedNodeList_ = node->sorted_next;
        node->sorted_next = nullptr;
        return;
    }
    for (Node* n = rt_sortedNodeList_; n; n = n->sorted_next) {
        if (n->sorted_next == node) {
            n->sorted_next = node->sorted_next;
            node->sorted_next = nullptr;
            return;
        }
    }
}

void Silo::sortNodes() {
    if (!needsSort_) return;
    // A new sort generation marks every node unvisited at once. The old list
    // is never walked: a hidden node dropped from the graph since the last
    // sort may already have been deleted on the NRT side.
    if (++sortGen_ == 0) ++sortGen_;

    // A topological sort of the nodes, depth first from the output through
    // inputs, finding cycles as it goes (Tarjan's strongly connected
    // components). Each cycle's nodes come out together, in the order they
    // finished; a connection from a node still on the depth-first path is
    // the one that closes a cycle (fbEdge_), read a sample (or block) late.
    rt_sortedNodeList_ = nullptr;
    sccCounter_ = 0;
    finishCounter_ = 0;
    sccStack_ = nullptr;
    Node* lastSorted = nullptr;
    sortVisitNode(outputNode_, lastSorted);

    needsSort_ = false;
}

void Silo::sortAppend(Node* node, Node*& lastSorted) {
    node->sorted_next = nullptr;
    if (lastSorted) lastSorted->sorted_next = node;
    else rt_sortedNodeList_ = node;
    lastSorted = node;
}

void Silo::sortVisitNode(Node* node, Node*& lastSorted) {
    node->sortGen_ = sortGen_;
    node->sccIndex_ = node->sccLow_ = ++sccCounter_;
    node->sccLen_ = 0;
    node->visiting_ = true;
    node->onStack_ = true;
    node->stackNext_ = sccStack_;
    sccStack_ = node;
    bool selfLoop = false;

    for (InPort& inPort : node->ins) {
        OutPort* srcPort = inPort.srcPort_;
        inPort.fbEdge_ = false;
        if (!srcPort) continue;
        Node* src = srcPort->node_;
        if (src->sortGen_ != sortGen_) {
            sortVisitNode(src, lastSorted);
            node->sccLow_ = std::min(node->sccLow_, src->sccLow_);
        } else if (src->onStack_) {
            node->sccLow_ = std::min(node->sccLow_, src->sccIndex_);
            // still on the path: this input closes a cycle
            if (src->visiting_) inPort.fbEdge_ = true;
            if (src == node) selfLoop = true;
        }
        // A closing input reads its own buffers; any other, its source's.
        for (int c = 0; c < inPort.chans_; ++c)
            inPort.chanPtrs_[c] = inPort.fbEdge_ ? inPort.ownBufs_[c] : srcPort->bufs_[inPort.srcMap_[c]];
    }
    node->visiting_ = false;
    node->finishSeq_ = ++finishCounter_;
    if (node->sccLow_ != node->sccIndex_) return;   // a member of an open cycle

    // The root of a component: pop it, its members in finish order.
    Node* members = nullptr;
    int len = 0;
    for (;;) {
        Node* m = sccStack_;
        sccStack_ = m->stackNext_;
        m->onStack_ = false;
        // insert by finish order (components are small)
        Node** at = &members;
        while (*at && (*at)->finishSeq_ < m->finishSeq_) at = &(*at)->stackNext_;
        m->stackNext_ = *at;
        *at = m;
        ++len;
        if (m == node) break;
    }
    Node* first = members;
    for (Node* m = members; m; ) {
        Node* next = m->stackNext_;
        sortAppend(m, lastSorted);
        m = next;
    }
    if (len > 1 || selfLoop) first->sccLen_ = len;
}

// A cycle (MF), over samples [lo, hi) of the block. Sample mode: every node
// runs one sample in turn, a closing input receiving its source's previous
// sample just before. Block mode: the nodes run [lo, hi) in order, a closing
// input holding its source's samples from the previous block. Inputs fed from
// inside the cycle are not constant: their sources have not run these
// samples when the inputs render.
void Silo::runCycle(Node* first, int len, int lo, int hi) {
    bool sampleMode = feedbackMode_.load(std::memory_order_relaxed) == 0;
    Node* end = first;
    for (int k = 0; k < len; ++k) end = end->sorted_next;
    auto inCycle = [&](Node* x) {
        for (Node* m = first; m != end; m = m->sorted_next) if (m == x) return true;
        return false;
    };
    for (Node* m = first; m != end; m = m->sorted_next) {
        for (InPort& p : m->ins) {
            p.render(this, hi);
            if (p.srcPort_ && inCycle(p.srcPort_->node_))
                for (int c = 0; c < p.chans_; ++c) p.isConst_[c] = 0;
        }
    }
    if (!sampleMode) {
        for (Node* m = first; m != end; m = m->sorted_next) m->runSpans(this, lo, hi);
        // the closing inputs' next block is this one
        for (Node* m = first; m != end; m = m->sorted_next) {
            m->ranTo_ = blockStart_ + hi;
            for (InPort& p : m->ins) {
                if (!p.fbEdge_) continue;
                int es = elemSize(p.type_.elem);
                for (int c = 0; c < p.chans_; ++c)
                    std::memcpy(static_cast<u8*>(p.ownBufs_[c]) + (usize)lo * es,
                                static_cast<u8 const*>(p.srcPort_->bufs_[p.srcMap_[c]]) + (usize)lo * es,
                                (usize)(hi - lo) * es);
            }
        }
        return;
    }
    for (int i = lo; i < hi; ++i) {
        for (Node* m = first; m != end; m = m->sorted_next) {
            for (InPort& p : m->ins) {
                if (!p.fbEdge_) continue;
                // The source's previous sample is still in its buffer: this
                // part's i - 1, or, at its first sample, where the source
                // last ran up to -- if that is here (a source that was not
                // running gives 0). The last block's final sample is at
                // prevBlockLen_ - 1.
                Node* src = p.srcPort_->node_;
                int prev = i > lo ? i - 1
                         : src->ranTo_ != blockStart_ + i ? -1
                         : i > 0 ? i - 1 : prevBlockLen_ - 1;
                int es = elemSize(p.type_.elem);
                for (int c = 0; c < p.chans_; ++c) {
                    u8* dst = static_cast<u8*>(p.ownBufs_[c]) + (usize)i * es;
                    if (prev < 0) std::memset(dst, 0, es);
                    else std::memcpy(dst, static_cast<u8 const*>(p.srcPort_->bufs_[p.srcMap_[c]]) + (usize)prev * es, es);
                }
            }
            m->runSpans(this, i, i + 1);
        }
    }
    for (Node* m = first; m != end; m = m->sorted_next) m->ranTo_ = blockStart_ + hi;
}

//=============================================================================================
#pragma mark VALUE STAGES

void Silo::setInputValues(InPort* dst, int n, int const* chans, f64 const* values,
                          i64 rampCount, FadeCurve curve) {
    int off = eventOffset();
    f64 t = eventTime();
    for (int k = 0; k < n; ++k) {
        int c = chans[k];
        if (c < 0 || c >= dst->chans_) continue;
        if (rampCount > 0) dst->rampTo(this, c, values[k], t, off, rampCount, curve);
        else dst->stepTo(this, c, values[k], off);
    }
}

void Silo::impulseInputValues(InPort* dst, int n, int const* chans, f64 const* values) {
    int off = eventOffset();
    for (int k = 0; k < n; ++k) {
        int c = chans[k];
        if (c < 0 || c >= dst->chans_) continue;
        dst->impulse(this, c, values[k], off);
    }
}

//=============================================================================================
#pragma mark NOTES

tzpl_SErr Silo::rt_noteOn(Node* node, int noteID, int n, f32 const* params) {
    NoteVoicer* vc = node->voicer_;
    if (!vc) return tzpl_errNotImplemented;
    InPort& notes = node->ins[node->notesPort_];
    int cols = (int)notes.type_.shape.dims[3];
    int off = eventOffset();
    i64 now = blockStart_ + off;
    int ended;
    int v = vc->noteOn(now, noteID, ended);
    if (ended >= 0 && ended != v) notes.stepTo(this, ended * cols, 0., off);
    // Split the node at the onset: the voice's state restarts here.
    node->addEvent(this, off, nodeEvResetVoice, v);
    notes.stepTo(this, v * cols, 1., off);
    // Parameters the note does not give take the column defaults.
    for (int i = 0; i < cols - 1; ++i) {
        f64 x = i < n ? params[i]
              : (usize)(i + 1) < vc->defaults.size() ? vc->defaults[i + 1] : 0.;
        if (i < n || !vc->defaults.empty()) notes.stepTo(this, v * cols + 1 + i, x, off);
    }
    return tzpl_errNone;
}

tzpl_SErr Silo::rt_noteOff(Node* node, int noteID) {
    NoteVoicer* vc = node->voicer_;
    if (!vc) return tzpl_errNotImplemented;
    InPort& notes = node->ins[node->notesPort_];
    int cols = (int)notes.type_.shape.dims[3];
    int off = eventOffset();
    int v = vc->noteOff(blockStart_ + off, noteID);
    if (v < 0) return tzpl_errNoteNotFound;
    notes.stepTo(this, v * cols, 0., off);
    return tzpl_errNone;
}

void Silo::rt_allNotesOff(Node* node) {
    NoteVoicer* vc = node->voicer_;
    if (!vc) return;
    InPort& notes = node->ins[node->notesPort_];
    int cols = (int)notes.type_.shape.dims[3];
    int off = eventOffset();
    for (int v = 0; v < vc->maxVoices; ++v) {
        if (!vc->gate[v]) continue;
        vc->gate[v] = 0;
        vc->offTime[v] = blockStart_ + off;
        notes.stepTo(this, v * cols, 0., off);
    }
    vc->active = 0;
}

tzpl_SErr Silo::rt_noteSetParam(Node* node, int noteID, int index, f32 value, i64 rampCount) {
    NoteVoicer* vc = node->voicer_;
    if (!vc) return tzpl_errNotImplemented;
    int v = vc->find(noteID);
    if (v < 0) return tzpl_errNoteNotFound;
    InPort& notes = node->ins[node->notesPort_];
    int cols = (int)notes.type_.shape.dims[3];
    if (index < 0 || index >= cols - 1) return tzpl_errInputOutOfRange;
    int c = v * cols + 1 + index;
    if (rampCount > 0) notes.rampTo(this, c, value, eventTime(), eventOffset(), rampCount, fadeLinear);
    else notes.stepTo(this, c, value, eventOffset());
    return tzpl_errNone;
}

//=============================================================================================
#pragma mark COMMANDS

void Silo::processRTCommands() {
    Command* head;
    while (from_nrt_.pop(head)) {
        Command* cmd = head;
        while (cmd) {
            Command* next = cmd->next_;
            cmd->next_ = nullptr;
            if (cmd->schedPolicy_ == schedImmediate
                || cmd->clock_ < 0 || cmd->clock_ >= (int)tempoClocks_.size()) {
                cmd->run(this);
                blockDone_.add(cmd);
            } else {
                // Beat-scheduled on a TempoClock: late-bound, fires when the
                // clock's beat reaches beatTime_.
                TempoClock& clk = tempoClocks_[cmd->clock_];
                if (cmd->beatTime_ < clk.beatAtSample(sampleTime_)) {
                    if (cmd->schedPolicy_ == schedOnTimeOnly) {
                        cmd->err_ = tzpl_errTooLate;
                    } else {
                        cmd->run(this); // betterLateThanNever: perform late.
                    }
                    blockDone_.add(cmd);
                } else {
                    clk.schedule(cmd);
                }
            }
            cmd = next;
        }
    }
}

Node* Silo::rt_getNode(i64 nodeID) const
{
    int bin = nodeID & kHashMask;
    
    Node* node = rt_nodeTable_[bin];
    while (node) {
        if (node->nodeID == nodeID) return node;
        node = node->rt_list.next;
    }
    return nullptr;
}

Node* Silo::nrt_getNode(i64 nodeID) const
{
    int bin = nodeID & kHashMask;
    
    Node* node = nrt_nodeTable_[bin];
    while (node) {
        if (node->nodeID == nodeID) return node;
        node = node->nrt_list.next;
    }
    return nullptr;
}

tzpl_SErr Silo::nrt_getInPort(PortAddr addr, InPort*& port) const
{
    Node* node = nrt_getNode(addr.nodeID);
    if (!node) {
        return tzpl_errNodeNotFound;
    }
    if (addr.index < 0 || addr.index >= node->ins.size()) {
        return tzpl_errInputOutOfRange;
    }
    port = &node->ins[addr.index];
    return tzpl_errNone;
}

tzpl_SErr Silo::rt_getInPort(PortAddr addr, InPort*& port) const
{
    Node* node = rt_getNode(addr.nodeID);
    if (!node) return tzpl_errNodeNotFound;
    // don't need to check index because it was checked in NRT.
    port = &node->ins[addr.index];
    return tzpl_errNone;
}

tzpl_SErr Silo::nrt_getOutPort(PortAddr addr, OutPort*& port) const
{
    Node* node = nrt_getNode(addr.nodeID);
    if (!node)
        return tzpl_errNodeNotFound;
    if (addr.index < 0 || addr.index >= node->outs.size())
        return tzpl_errOutputOutOfRange;
    port = &node->outs[addr.index];
    return tzpl_errNone;
}

tzpl_SErr Silo::rt_getOutPort(PortAddr addr, OutPort*& port) const
{
    Node* node = rt_getNode(addr.nodeID);
    if (!node) return tzpl_errNodeNotFound; // node was deleted 
    // don't need to check index because it was checked in NRT.
    port = &node->outs[addr.index];
    return tzpl_errNone;
}

void Silo::pushDeadNode(Node* node) {
    // The NRT side may delete it at any moment from here on: it must not stay
    // reachable from the sorted list (removeNode and the fader retirement
    // walk it before the next sort).
    sortedListRemove(node);
    needsSort_ = true;
    dead_nodes_.push(node);
}

}
