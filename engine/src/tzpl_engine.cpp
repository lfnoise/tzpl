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
#include <algorithm>
#include <chrono>
#include <thread>

namespace engine {

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

    initAudio(this);
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
                bool done = cmd->run(&s);
                if (done) delete cmd;
                else toNRTList.add(cmd);
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
                        bool done = cmd->run(&s);
                        if (done) delete cmd;
                        else toNRTList.add(cmd);
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


