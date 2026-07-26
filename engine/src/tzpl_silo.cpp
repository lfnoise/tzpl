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
#include "tzpl_engine.hpp"
#include "tzpl_chanadapt.hpp"
#include <chrono>
#include <cmath>

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
    t.buf = static_cast<f32 const*>(node->synth->outlets[outlet]);
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

void Silo::processTaps() {
    for (int i = 0; i < numTaps_; ++i) {
        RTTap& t = rt_taps_[i];
        TapSlot* ts = t.slot;

        f32 sq = 0.0f;
        f32 pk = ts->accumPeak;
        for (int c = 0; c < t.chans; ++c) {
            f32 v = t.buf[c];
            f32 a = std::fabs(v);
            if (a > pk) pk = a;
            sq += v * v;
        }
        ts->accumPeak = pk;
        ts->accumSq += sq / (f32)t.chans;
        if (++ts->accumCount >= ts->publishPeriod) {
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
            if (ts->fifo.space() >= t.chans) {
                for (int c = 0; c < t.chans; ++c) ts->fifo.push(t.buf[c]);
            }
        }
    }
}

void Silo::runNodes() {
    Node* node = rt_sortedNodeList_;
    while (node) {
        // Run event processing before audio for any node whose controls
        // changed this sample (set by setControl / initial control priming).
        // processEvents re-evaluates the activated iso-groups, loading new
        // control values into instance vars that processAudio then reads.
        if (node->triggered) {
            node->triggered = false;
            if (node->funs.processEvents) node->funs.processEvents(node->synth);
        }
        node->funs.processAudio(node->synth);
        node = node->sorted_next;
    }
}

void Silo::processFrames() {
    // Two clock reads per block (commpage on macOS, vDSO on Linux -- no
    // syscall, ~20ns each) against a multi-millisecond block. The split at
    // mixDown matters: silo 0's mixDown blocks on the worker silos' semaphores,
    // so folding that wait into the DSP figure would make silo 0 look
    // pathologically slow.
    bool timing = engine_->statsEnabled_.load(std::memory_order_relaxed);
    auto t0 = timing ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};

    f32* out = index_ > 0 ? outbuf_ : engine_->out_;

    auto& streamParams = engine_->streamParams_;
    int numFrames = streamParams.bufferFrames;
    int outChannels = streamParams.channels;
    int outByteSize = streamParams.channels * sizeof(f32);

    // Determine if we have hardware audio input to route
    int inputChannels = streamParams.inputChannels;
    f32 const* inBuf = (inputChannels > 0) ? engine_->in_ : nullptr;
    int inputByteSize = inputChannels * sizeof(f32);
    bool hasInputNode = inBuf && inputNode_->outs.size() > 0;

    processRTCommands();

    InPort* dstPort = &outputNode_->ins[0];
    OutPort* srcPort = dstPort->srcPort_;

    // Clamp channelOffset_ so we don't write past the hardware buffer.
    int offset = std::min(channelOffset_, outChannels);
    int availableChannels = outChannels - offset;

    int copyByteSize;
    if (srcPort) {
        int srcBytes = calcByteSize(srcPort->type_);
        int availBytes = availableChannels * (int)sizeof(f32);
        copyByteSize = std::min(srcBytes, availBytes);
    } else {
        copyByteSize = availableChannels * (int)sizeof(f32);
    }

    // Zero the entire output buffer when the silo's channels don't fill it.
    if (copyByteSize < outByteSize) {
        memset(out, 0, numFrames * outByteSize);
    }

    f32* outp = out + offset;

    for (int i = 0; i < numFrames; ++i) {
        // Copy hardware input to the input node's outlet before running nodes
        if (hasInputNode) {
            f32* inputNodeOut = (f32*)inputNode_->synth->outlets[0];
            memcpy(inputNodeOut, inBuf + i * inputChannels, inputByteSize);
        }

        processScheduledEvents();

        for (auto& clk : tempoClocks_) {
            clk.process(sampleTime_, this);
        }

        // Fire/reschedule any beat-due silo tasks (coroutines spawned on this
        // silo). Bridge-side pool; reads beats from the tempo clocks above.
        if (taskTickFn_) {
            taskTickFn_(taskSched_, sampleTime_, this);
        }

        sortNodes();
        runNodes();
        processTaps();

        f32* data = (f32*)getInput(outputNode_->synth, 0);
        memcpy(outp, data, copyByteSize);

        outp += outChannels;
        ++sampleTime_;
    }
    // Per-buffer GC heartbeat: drain deferred-delete queue for the attached VM.
    if (heartbeatFn_ && vm_) {
        heartbeatFn_(vm_, this);
    }

    auto t1 = timing ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};

    mixDown(numFrames, out);
    done_sem_.signal();
    sortNodes(); // Why is this needed? Without it, there are glitches.

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
    while (e->runSilos_) {
        s->start_sem_.wait();
        if (!e->runSilos_) break;
        s->processFrames();
    }
}

tzpl_SErr compatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b) {
    if (a.rate != b.rate) return tzpl_errRateMismatch;
    if (a.elem != b.elem) return tzpl_errTypeMismatch;
    if (a.chans != b.chans) return tzpl_errChanMismatch;
    return tzpl_errNone;
}

tzpl_SErr relaxedCompatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b) {
    if (a.rate != b.rate) return tzpl_errRateMismatch;
    if (a.elem != b.elem) return tzpl_errTypeMismatch;
    return tzpl_errNone;
}

tzpl_SErr Silo::connect(OutPort* src, InPort* dst) {
    if (!src || !dst) return tzpl_errNodeNotFound;

    // Type-check before unlinking: a rejected connect must leave whatever
    // was already feeding the inlet alone rather than silently cutting it.
    if (dst->node_->nodeID == 0) { // destination is the output node. It deals with channel mismatch.
        tzpl_SErr err = relaxedCompatibleTypes(src->type_, dst->type_);
        if (err != tzpl_errNone) return err;
    } else {
        tzpl_SErr err = compatibleTypes(src->type_, dst->type_);
        if (err != tzpl_errNone) return err;
    }

    if (dst->srcPort_) {
        unlink(dst);
    }

    // link onto source port list.
    if (src->dstList_) src->dstList_->prev_ = dst;
    dst->prev_ = NULL;
    dst->next_ = src->dstList_;
    src->dstList_ = dst;

    // set connected.
    dst->srcPort_ = src;
    dst->node_->synth->inlets[dst->index_] = src->node_->synth->outlets[src->index_];

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
    // remove from list
    OutPort* src = dst->srcPort_;
    if (dst->next_) dst->next_->prev_ = dst->prev_;
    if (dst->prev_) dst->prev_->next_ = dst->next_;
    else src->dstList_ = dst->next_;

    // set to disconnected
    dst->srcPort_ = nullptr;
    dst->node_->synth->inlets[dst->index_] = dst->dataBuffer_;

    needsSort_ = true;

    // A hidden channel adapter exists only for the connection it was made
    // for. Cutting either end -- here, the only place a link is ever broken
    // -- retires it, so no orphan is left summing into a dead inlet.
    retireAdapter(this, dst->node_);
    retireAdapter(this, src->node_);
}

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
    
    return tzpl_errNone;
}

tzpl_SErr Silo::removeNode(Node* node) {
    if (!node->rt_active) return tzpl_errAlreadyRemoved;

    clearTapsForNode(node);
    disconnectNode(node);
    
    //sortedListRemove(node);
    
    if (node->nodeID >= 0) {
        // remove node from rt_nodeTable_
        int bin = node->nodeID & kHashMask;
        if (node->rt_list.next) node->rt_list.next->rt_list.prev = node->rt_list.prev;
        if (node->rt_list.prev) node->rt_list.prev->rt_list.next = node->rt_list.next;
        else rt_nodeTable_[bin] = node->rt_list.next;
    }
    
    node->rt_active = false;
    
    return tzpl_errNone;
}

Node* Silo::removeAllNodes() {
    Node* outNodes = nullptr;
    for (Node* nodes : rt_nodeTable_) {
        Node* node = nodes;
        while (node) {
            Node* next = node->rt_list.next;
            if (node->nodeID != 0 && node->nodeID != 1) {
                // don't remove input or output nodes.
                removeNode(node);
                node->rt_list.next = outNodes;
                outNodes = node;
            }
        
            node = next;
        }
    }
    inputNode_->rt_list.prev = nullptr;
    inputNode_->rt_list.next = nullptr;
    outputNode_->rt_list.prev = nullptr;
    outputNode_->rt_list.next = nullptr;
    return outNodes;
}

void Silo::sortNodes() {
    if (!needsSort_) return;
    // Clear the sorted list links.
    Node* node = rt_sortedNodeList_;
    while (node) {
        Node* next = node->sorted_next;
        node->sorted_next = nullptr;
        node->sortState = sortUnvisited;
        node = next;
    }
    
    // Perform a topological sort of the nodes in the silo. 
    rt_sortedNodeList_ = nullptr;
    Node* lastSorted = nullptr;
    sortVisitNode(outputNode_, lastSorted);

    needsSort_ = false;
}

void Silo::sortVisitNode(Node* node, Node*& lastSorted) {
    if (node->sortState != sortUnvisited) {
        // already visited.
        return;
    }
    node->sortState = sortVisiting;
    // Depth first visit of node inputs, after visiting all inputs, add node to sorted list.
    // Cycles in the graph are OK. Cycles will just cause a single sample delay.
    for (InPort& inPort : node->ins) {
        OutPort* srcPort = inPort.srcPort_;
        if (srcPort) {
            Node* srcNode = srcPort->node_;
            sortVisitNode(srcNode, lastSorted);
        }
    }
    
    node->sortState = sortVisited;
    // add to sorted list.
    if (lastSorted) {
        lastSorted->sorted_next = node;
    } else {
        // first node in sorted list.
        node->silo_->rt_sortedNodeList_ = node;
    }
    lastSorted = node;
}


void Silo::setInput(InPort* dst, int numValues, void* values) {
    if (numValues > dst->type_.chans) throw tzpl_errChanMismatch;
    memcpy(dst->dataBuffer_, values, numValues * elemSize(dst->type_.elem));
}

tzpl_SErr Silo::setControl(Node* node, i64 controlID, int numValues, void* values) {
    Control* c = node->getControl(controlID);
    if (!c) return tzpl_errControlNotFound;
    if (numValues > c->type_.chans) throw tzpl_errChanMismatch;
    // Route through the synth's event() so it updates the control buffer AND
    // sets the ctrlN_active flag, then flag the node so the next runNodes()
    // call runs processEvents and re-evaluates the affected iso-groups.
    // (Synths without an event() -- e.g. built-ins -- fall back to a plain
    // buffer write, matching the previous behaviour.)
    if (node->funs.event) {
        tzpl_Slice dst{0, nullptr};
        tzpl_Slice data{numValues, values};
        node->funs.event(node->synth, controlID, dst, data);
        node->triggered = true;
    } else {
        memcpy(c->data_, values, numValues * elemSize(c->type_.elem));
    }
    return tzpl_errNone;
}

void run_cmd(Silo* s, f64 time, Command* cmd) {
    cmd->run(s);
}

void Silo::processRTCommands() {
    Command* head;
    while (from_nrt_.pop(head)) {
        CommandList backToNRT;
        Command* cmd = head;
        while (cmd) {
            Command* next = cmd->next_;
            if (cmd->schedPolicy_ == schedImmediate) {
                cmd->run(this);
                backToNRT.add(cmd);
            } else if (cmd->clock_ >= 0 && cmd->clock_ < (int)tempoClocks_.size()) {
                // Beat-scheduled on a TempoClock: late-bound, fires when the
                // clock's beat reaches beatTime_.
                TempoClock& clk = tempoClocks_[cmd->clock_];
                if (cmd->beatTime_ < clk.beatAtSample(sampleTime_)) {
                    if (cmd->schedPolicy_ == schedOnTimeOnly) {
                        cmd->err_ = tzpl_errTooLate;
                    } else {
                        cmd->run(this); // betterLateThanNever: perform late.
                    }
                    backToNRT.add(cmd);
                } else {
                    clk.schedule(cmd);
                }
            } else {
                // Legacy sample-time path (no clock): schedule in stream seconds.
                cmd->sampleTime_ = engine_->streamTimeToSampleTime(cmd->streamTime_);
                if (cmd->sampleTime_ < sampleTime_) {
                    if (cmd->schedPolicy_ == schedOnTimeOnly) {
                        cmd->err_ = tzpl_errTooLate;
                    } else {
                        cmd->run(this); // perform late.
                    }
                    backToNRT.add(cmd);
                } else {
                    sched_.add(cmd);
                }
            }
            cmd = next;
        }
        to_nrt_.push(backToNRT.head); // send cmds back to NRT.
    }
}

void Silo::processScheduledEvents() {
    CommandList cmds = sched_.popForTime(sampleTime_);
    if (cmds.head) {
        Command* cmd = cmds.head;
        while (cmd) {
            cmd->run(this);
            cmd = cmd->next_;
        }
        to_nrt_.push(cmds.head); // send cmds back to NRT.
    }
}

void TempoClock::process(i64 sampleTime, Silo* s) {
    if (!queue_.head) return;
    f64 currentBeat = beatAtSample(sampleTime);
    CommandList cmds = queue_.pop(currentBeat);
    if (cmds.head) {
        Command* cmd = cmds.head;
        while (cmd) {
            cmd->run(s);
            cmd = cmd->next_;
        }
        s->to_nrt_.push(cmds.head); // send cmds back to NRT for stage-2 cleanup.
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
    dead_nodes_.push(node);
}

}
