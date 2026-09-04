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
//  tzpl_engine.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_engine.hpp"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>

namespace engine {

//=============================================================================================
#pragma mark MASTER METER

// One extra pass over the block, on the audio thread, right after the safety
// limiter. Peak/RMS are integrated over publishFrames so a master meter and a
// node tap read the same way; peakHold falls slowly so a GUI polling at any
// rate sees the true maximum rather than whichever block it landed on.
void MasterMeter::processBlock(f32 const* out, int frames, int outChans) {
    int n = std::min(outChans, kMaxMasterChans);
    if (n < 1 || frames < 1) return;
    chans.store(n, std::memory_order_relaxed);

    f32 blockPeak[kMaxMasterChans];
    for (int c = 0; c < n; ++c) blockPeak[c] = 0.f;
    u32 clips = 0;

    for (int i = 0; i < frames; ++i) {
        f32 const* frame = out + (size_t)i * outChans;
        for (int c = 0; c < n; ++c) {
            f32 v = frame[c];
            f32 a = std::fabs(v);
            if (a > blockPeak[c]) blockPeak[c] = a;
            accumSq[c] += (f64)v * (f64)v;
            if (a >= 0.999f) ++clips;
        }
    }
    accumFrames += frames;

    f32 holdAll = 0.f;
    for (int c = 0; c < n; ++c) {
        if (blockPeak[c] > accumPeak[c]) accumPeak[c] = blockPeak[c];
        f32 h = peakHold[c].load(std::memory_order_relaxed) * holdDecayPerBlock;
        if (blockPeak[c] > h) h = blockPeak[c];
        peakHold[c].store(h, std::memory_order_relaxed);
        if (h > holdAll) holdAll = h;
    }
    peakHoldAll.store(holdAll, std::memory_order_relaxed);
    if (clips) clipCount.fetch_add(clips, std::memory_order_relaxed);

    if (accumFrames < publishFrames) return;

    f32 peakAllV = 0.f;
    f64 msSum = 0.;
    for (int c = 0; c < n; ++c) {
        peak[c].store(accumPeak[c], std::memory_order_relaxed);
        f64 ms = accumSq[c] / (f64)accumFrames;
        rms[c].store((f32)std::sqrt(ms), std::memory_order_relaxed);
        msSum += ms;
        if (accumPeak[c] > peakAllV) peakAllV = accumPeak[c];
        accumPeak[c] = 0.f;
        accumSq[c] = 0.;
    }
    peakAll.store(peakAllV, std::memory_order_relaxed);
    // Mean square across channels, then root -- the same convention
    // Silo::processTaps uses, so master and node meters are comparable.
    rmsAll.store((f32)std::sqrt(msSum / (f64)n), std::memory_order_relaxed);
    accumFrames = 0;
}

//=============================================================================================
#pragma mark MASTER TAPS

tzpl_SErr Engine::installMasterTap(TapSlot* slot) {
    if (numMasterTaps_ >= kMaxMasterTaps) return tzpl_errResourceLimit;
    rt_masterTaps_[numMasterTaps_++] = slot;
    return tzpl_errNone;
}

TapSlot* Engine::rt_findMasterTap(i64 tapID) {
    for (int i = 0; i < numMasterTaps_; ++i) {
        if (rt_masterTaps_[i]->tapID == tapID) return rt_masterTaps_[i];
    }
    return nullptr;
}

void Engine::removeMasterTap(i64 tapID) {
    for (int i = 0; i < numMasterTaps_; ) {
        if (rt_masterTaps_[i]->tapID == tapID) {
            rt_masterTaps_[i] = rt_masterTaps_[numMasterTaps_ - 1];
            rt_masterTaps_[numMasterTaps_ - 1] = nullptr;
            --numMasterTaps_;
        } else {
            ++i;
        }
    }
}

// Block-rate, unlike Silo::processTaps which runs per sample -- here the whole
// block is already in memory. Same accumulation convention, so a master tap
// and a node tap on the same signal read identically.
void Engine::processMasterTaps(f32 const* out, int frames, int outChans) {
    if (numMasterTaps_ == 0) return;

    for (int t = 0; t < numMasterTaps_; ++t) {
        TapSlot* ts = rt_masterTaps_[t];
        int chans = std::min(ts->chans, outChans);
        if (chans < 1) continue;

        for (int i = 0; i < frames; ++i) {
            f32 const* frame = out + (size_t)i * outChans;
            f32 sq = 0.f;
            f32 pk = ts->accumPeak;
            for (int c = 0; c < chans; ++c) {
                f32 v = frame[c];
                f32 a = std::fabs(v);
                if (a > pk) pk = a;
                sq += v * v;
            }
            ts->accumPeak = pk;
            ts->accumSq += sq / (f32)chans;
            if (++ts->accumCount >= ts->publishPeriod) {
                ts->peak.store(ts->accumPeak, std::memory_order_relaxed);
                ts->rms.store(std::sqrt(ts->accumSq / (f32)ts->accumCount),
                              std::memory_order_relaxed);
                ts->accumPeak = 0.f;
                ts->accumSq = 0.f;
                ts->accumCount = 0;
            }
            if (ts->mode == tapScope) {
                // Whole interleaved frames or nothing, so a full FIFO drops
                // frames without slipping channel alignment.
                if (ts->fifo.space() >= chans) {
                    for (int c = 0; c < chans; ++c) ts->fifo.push(frame[c]);
                }
            }
        }
    }
}

void Engine::configureStats() {
    masterMeter_.configure(streamParams_.sampleRate, streamParams_.bufferFrames);
    u64 budget = 0;
    if (streamParams_.sampleRate > 0.) {
        budget = (u64)(1e9 * (f64)streamParams_.bufferFrames
                       / streamParams_.sampleRate);
    }
    stats_.budgetNanos.store(budget, std::memory_order_relaxed);
}

//=============================================================================================
#pragma mark NON REAL TIME ENGINE METHODS

void initAudio(Engine* e);

Engine::Engine(EngineConfig const& config, AudioStreamParameters& asp,
               std::unique_ptr<AudioBackend> backend)
    :
    silos_(config.numSilos),
    defs_(kHashBins),
    backend_(std::move(backend)),
    nrt_cmd_thread_(processNRTCommands, this),
    dead_node_thread_(processDeadNodes, this),
    streamParams_(asp)
{
    numTempoClocks_ = std::max(1, config.numTempoClocks);
    { int i = 0; for (Silo& s : silos_) {
        s.engine_ = this;
        s.index_ = i;
        ++i;
    }}

    try {
        initAudio(this);
    } catch (...) {
        // The NRT/dead-node threads are members already running by the time
        // the body executes; unwinding past their joinable destructors would
        // std::terminate, turning "no audio device" into an abort. Join them
        // so the error propagates as an exception the caller can handle.
        runBackgroundThreads_ = false;
        nrt_cmd_thread_.join();
        dead_node_thread_.join();
        throw;
    }
    postInit();
}

// NRT (offline) constructor: skips RtAudio device setup. Allocates the same
// per-silo buffers and safety limiter that initAudio() would. Background
// NRT/dead-node threads are still spawned but exit immediately because
// nrtMode_ is true (set in declaration order before the std::thread fields).
Engine::Engine(EngineConfig const& config, AudioStreamParameters& asp, bool /*nrt*/)
    :
    silos_(config.numSilos),
    defs_(kHashBins),
    nrtMode_(true),
    nrt_cmd_thread_(processNRTCommands, this),
    dead_node_thread_(processDeadNodes, this),
    streamParams_(asp)
{
    numTempoClocks_ = std::max(1, config.numTempoClocks);
    { int i = 0; for (Silo& s : silos_) {
        s.engine_ = this;
        s.index_ = i;
        ++i;
    }}

    // Allocate the same per-silo state initAudio() would have set up.
    int byteSize = streamParams_.bufferFrames * streamParams_.channels * sizeof(f32);

    safetyLimiter_ = std::make_unique<SafetyLimiter>(
        streamParams_.bufferFrames,
        streamParams_.channels,
        int((.25 * streamParams_.sampleRate) / streamParams_.bufferFrames));

    for (Silo& s : silos_) {
        s.sampleTime_ = 0;
        if (s.index_ > 0) {
            s.outbuf_ = (f32*)malloc(byteSize);
        }
    }
    configureStats();

    // Mark as running so sendCmds() routes commands through the FIFO instead
    // of executing synchronously -- the renderer dispatches them sample-accurately.
    audioState_ = AudioState::running;
    muteGain_ = 1.f;

    postInit();
}

void Engine::postInit() {
    defOutputNode(streamParams_.channels);
    defInputNode(streamParams_.inputChannels);

    // Size each silo's TempoClock slots. Done here (before worker threads
    // start) so streamParams_.sampleRate reflects any device negotiation.
    // Default 60 BPM => 1 beat == 1 second.
    for (Silo& s : silos_) {
        s.tempoClocks_.clear();
        s.tempoClocks_.reserve(numTempoClocks_);
        for (int k = 0; k < numTempoClocks_; ++k) {
            s.tempoClocks_.emplace_back(60.0, streamParams_.sampleRate, 0);
        }
    }

    // start work loops
    for (int i = 1; i < silos_.size(); ++i) {
        auto& s = silos_[i];
        s.run_thread_ = std::thread(Silo::workLoop, &s);
    }
}

Engine::~Engine() {
    runBackgroundThreads_ = false;
    nrt_cmd_thread_.join();
    dead_node_thread_.join();
    runSilos_ = 0;
    for (int i = 1; i < silos_.size(); ++i) {
        silos_[i].start_sem_.signal();
        silos_[i].run_thread_.join();
    }

    // It is a race condition if the client tries to free the Engine while any
    // other thread might be operating on it.
    // I don't try to solve this race. The client should ensure this.
    // Nevertheless, a check is put here to possibly catch a programming error.
    {
        std::unique_lock<std::mutex> ulck(nrt_lock_, std::defer_lock);
        if (!ulck.try_lock()) {
            fprintf(stderr, "The engine is being destructed while in use.\n");
            fprintf(stderr, "If terrible things happen now, I told you so..\n");
        }
    }

    // Drop every tap BEFORE any member is destroyed. Members go in reverse
    // declaration order, so taps_ (declared after silos_) would otherwise be
    // freed first -- and then ~Silo -> removeAllNodes -> removeNode ->
    // clearTapsForNode would write published levels through dangling TapSlot
    // pointers. Clearing the RT tables first makes that loop a no-op.
    for (Silo& s : silos_) s.numTaps_ = 0;
    numMasterTaps_ = 0;
    taps_.clear();


//    printf("from_nrt_.numPushed %d\n", from_nrt_.numPushed());
//    printf("from_nrt_.numPopped %d\n", from_nrt_.numPopped());
//    
//    printf("to_nrt_.numPushed %d\n", to_nrt_.numPushed());
//    printf("to_nrt_.numPopped %d\n", to_nrt_.numPopped());
//    
//    printf("dead_nodes_.numPushed %d\n", dead_nodes_.numPushed());
//    printf("dead_nodes_.numPopped %d\n", dead_nodes_.numPopped());
}

f64 Engine::getStreamTime() {
    if (nrtMode_) {
        return (f64)anchorSampleTime_ / streamParams_.sampleRate;
    }
    return backend_->streamTime();
}

void plugInAudioNoOp(tzpl_SynthData*) {}

struct OutputNode : tzpl_SynthData {
    f32* data;
};

tzpl_SynthData* OutputNode_alloc() {
    return (tzpl_SynthData*)new OutputNode();
}

tzpl_SErr OutputNode_free(tzpl_SynthData* synth) {
    delete (OutputNode*)synth;
    return tzpl_errNone;
}

tzpl_SErr OutputNode_init(tzpl_SynthData* synth) {
    OutputNode* o = (OutputNode*)synth;
    Engine* e = (Engine*)o->engine;
    o->data = new f32[e->streamParams_.channels];
    return tzpl_errNone;
}

tzpl_SErr OutputNode_uninit(tzpl_SynthData* synth) {
    OutputNode* o = (OutputNode*)synth;
    delete [] o->data;
    return tzpl_errNone;
}

void Engine::defOutputNode(int numChannels) {
    // called only from Engine constructor
    NodeDefInfo nodeDefInfo;
    memset(&nodeDefInfo, 0, sizeof(NodeDefInfo));

    nodeDefInfo.name = "Audio Out";
    nodeDefInfo.num_ins = 1;

    PortInfo inputPortInfo{"in", {tzpl_kF32, tzpl_audioRate, numChannels}};
    nodeDefInfo.ins = (PortInfo*)calloc(1, sizeof(PortInfo));
    nodeDefInfo.ins[0] = inputPortInfo;

    nodeDefInfo.funs.alloc  = OutputNode_alloc;
    nodeDefInfo.funs.free   = OutputNode_free;
    nodeDefInfo.funs.init   = OutputNode_init;
    nodeDefInfo.funs.uninit = OutputNode_uninit;
    nodeDefInfo.funs.reset  = nullptr;
    nodeDefInfo.funs.event  = nullptr;
    nodeDefInfo.funs.processAudio    = plugInAudioNoOp;

    NodeDef* def = new NodeDef(nodeDefInfo);

    u32 bin = def->hash_ & kHashMask;
    def->next_ = defs_[bin];
    defs_[bin] = def;

    for (Silo& s : silos_) {
        s.outputNode_ = new Node(this, &s, def, 0);
        s.addNode(s.outputNode_);
        s.shadow_.nodes[0] = "Audio Out"; // Engine ctor: no locking needed yet
    }
}

struct InputNode : tzpl_SynthData {};

tzpl_SynthData* InputNode_alloc() {
    return (tzpl_SynthData*)new InputNode();
}

tzpl_SErr InputNode_free(tzpl_SynthData* synth) {
    delete (InputNode*)synth;
    return tzpl_errNone;
}

void Engine::defInputNode(int inputChannels) {
    // called only from Engine constructor
    NodeDefInfo nodeDefInfo;
    memset(&nodeDefInfo, 0, sizeof(NodeDefInfo));

    nodeDefInfo.name = "Audio In";

    if (inputChannels > 0) {
        nodeDefInfo.num_outs = 1;
        PortInfo outputPortInfo{"out", {tzpl_kF32, tzpl_audioRate, inputChannels}};
        nodeDefInfo.outs = (PortInfo*)calloc(1, sizeof(PortInfo));
        nodeDefInfo.outs[0] = outputPortInfo;
    }

    nodeDefInfo.funs.alloc  = InputNode_alloc;
    nodeDefInfo.funs.free   = InputNode_free;
    nodeDefInfo.funs.init   = nullptr;
    nodeDefInfo.funs.uninit = nullptr;
    nodeDefInfo.funs.reset  = nullptr;
    nodeDefInfo.funs.event  = nullptr;
    nodeDefInfo.funs.processAudio = plugInAudioNoOp;

    NodeDef* def = new NodeDef(nodeDefInfo);

    u32 bin = def->hash_ & kHashMask;
    def->next_ = defs_[bin];
    defs_[bin] = def;

    for (Silo& s : silos_) {
        s.inputNode_ = new Node(this, &s, def, 1);
        s.addNode(s.inputNode_);
        s.shadow_.nodes[1] = "Audio In"; // Engine ctor: no locking needed yet
    }
}

void Engine::defXFaderNode() {
    // called only from Engine constructor
    NodeDefInfo nodeDefInfo;
    memset(&nodeDefInfo, 0, sizeof(NodeDefInfo));
    
    nodeDefInfo.name = "XFaderF32";
    nodeDefInfo.num_ins = 2;
    nodeDefInfo.num_outs = 1;
    
    PortInfo a{"a", {tzpl_kF32, tzpl_audioRate, 0}}; // actual number of channels may vary
    PortInfo b{"b", {tzpl_kF32, tzpl_audioRate, 0}}; // actual number of channels may vary
    nodeDefInfo.ins = (PortInfo*)calloc(nodeDefInfo.num_ins, sizeof(PortInfo));
    nodeDefInfo.ins[0] = a;
    nodeDefInfo.ins[0] = b;

    PortInfo out{"out", {tzpl_kF32, tzpl_audioRate, 0}};
    nodeDefInfo.outs = (PortInfo*)calloc(nodeDefInfo.num_outs, sizeof(PortInfo));
    nodeDefInfo.outs[0] = out;
    
//    nodeDefInfo.funs.alloc  = OutputNode_alloc;
//    nodeDefInfo.funs.free   = OutputNode_free;
//    nodeDefInfo.funs.init   = OutputNode_init;
//    nodeDefInfo.funs.uninit = OutputNode_uninit;
//    nodeDefInfo.funs.reset  = nullptr;
//    nodeDefInfo.funs.processAudio   = plugInAudioNoOp;
//    nodeDefInfo.funs.push   = nullptr;
    
    NodeDef* def = new NodeDef(nodeDefInfo);

    u32 bin = def->hash_ & kHashMask;
    def->next_ = defs_[bin];
    defs_[bin] = def;
}

//=============================================================================================
#pragma mark REAL TIME ENGINE METHODS

i64 Engine::streamTimeToSampleTime(f64 streamTime) {
    return anchorSampleTime_ + i64(round((streamTime - anchorStreamTime_) * streamParams_.sampleRate));
}


// Drain to_nrt_ and dead_nodes_ FIFOs for all silos. Used by both the
// background polling threads and (in NRT mode) the renderer between blocks.
void Engine::drainNRTQueues() {
    std::lock_guard<std::mutex> lck(nrt_lock_);

    for (Silo& s : silos_) {
        Command* head;
        while (s.to_nrt_.pop(head)) {
            CommandList toNRTList;
            Command* cmd = head;
            while (cmd) {
                Command* next = cmd->next_;
                if (cmd->stage_ == 0 && cmd->err_ != tzpl_errNone) {
                    // Dropped before doRT ever ran (schedOnTimeOnly too
                    // late). Running it here would execute the command on
                    // the NRT thread; destroy it instead (the stage_ == 0
                    // dtor guards free anything it pre-allocated).
                    delete cmd;
                } else {
                    bool done = cmd->run(&s);
                    if (done) delete cmd;
                    else toNRTList.add(cmd);
                }
                cmd = next;
            }
            if (toNRTList.head) s.from_nrt_.push(toNRTList.head);
        }
        Node* node;
        while (s.dead_nodes_.pop(node)) {
            delete node;
        }
    }
}

void Engine::processNRTCommands(Engine* e) {
    if (e->nrtMode_) return; // NRT mode: renderer drains inline.
    while (e->runBackgroundThreads_) {
        {
            std::lock_guard<std::mutex> lck(e->nrt_lock_);

            for (Silo& s : e->silos_) {
                Command* head;
                while (s.to_nrt_.pop(head)) {
                    CommandList toNRTList;
                    Command* cmd = head;
                    while (cmd) {
                        Command* next = cmd->next_;
                        if (cmd->stage_ == 0 && cmd->err_ != tzpl_errNone) {
                            // Dropped before doRT ever ran (schedOnTimeOnly
                            // too late) -- destroy, never run on this thread.
                            delete cmd;
                        } else {
                            bool done = cmd->run(&s);
                            if (done) delete cmd;
                            else toNRTList.add(cmd);
                        }
                        cmd = next;
                    }
                    if (toNRTList.head) s.from_nrt_.push(toNRTList.head);
                }
            }
        }
        // Handle externally-triggered sample rate change (macOS HAL listener).
        // Done outside nrt_lock_ because stopAudio acquires it.
        if (e->sampleRateChanged_.exchange(false, std::memory_order_relaxed)
            && e->audioState_ == AudioState::running) {
            fprintf(stderr,
                "\n*** Audio device sample rate changed externally to a "
                "value other than %.0f Hz.\n"
                "*** The audio stream has been stopped to avoid "
                "pitch-shifted output.\n"
                "*** Restore the device's rate (e.g. via Audio MIDI Setup) "
                "and restart audio.\n\n",
                e->streamParams_.sampleRate);
            stopAudio(e);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(25000));
    }
}

void Engine::processDeadNodes(Engine* e) {
    if (e->nrtMode_) return; // NRT mode: renderer drains inline.
    std::this_thread::sleep_for(std::chrono::microseconds(12500));
    while (e->runBackgroundThreads_) {
        {
            std::lock_guard<std::mutex> lck(e->nrt_lock_);
            for (Silo& s : e->silos_) {
                Node* node;
                while (s.dead_nodes_.pop(node)) {
                    delete node;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(25000));
    }
}


}


