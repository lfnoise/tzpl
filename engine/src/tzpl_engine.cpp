//
//  tzpl_engine.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_engine.hpp"
#include "RtAudio.h"
#include <chrono>
#include <thread>

namespace engine {

//=============================================================================================
#pragma mark NON REAL TIME ENGINE METHODS

void initAudio(Engine* e);

Engine::Engine(EngineConfig const& config, AudioStreamParameters& asp)
    :
    silos_(config.numSilos),
    defs_(kHashBins),
#ifdef __APPLE__
    rtaudio_(std::make_unique<RtAudio>(RtAudio::MACOSX_CORE)),
#elif defined(__linux__)
    rtaudio_(std::make_unique<RtAudio>(RtAudio::LINUX_ALSA)),
#endif
    nrt_cmd_thread_(processNRTCommands, this),
    dead_node_thread_(processDeadNodes, this),
    streamParams_(asp)
{
    { int i = 0; for (Silo& s : silos_) {
        s.engine_ = this;
        s.index_ = i;
        ++i;
    }}

    initAudio(this);

    defOutputNode(streamParams_.channels);
    
    // start work loops
    for (int i = 1; i < silos_.size(); ++i) {
        auto& s = silos_[i];
        s.run_thread_ = std::thread(Silo::workLoop, &s);
    }
    
//    printf("Engine size %d\n", int(sizeof(Engine)));
//    printf("Node size %d\n", int(sizeof(Node)));
//    printf("InPort size %d\n", int(sizeof(InPort)));
//    printf("OutPort size %d\n", int(sizeof(OutPort)));
//    printf("Control size %d\n", int(sizeof(Control)));
    
    
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
    return rtaudio_->getStreamTime();
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
        s.inputNode_ = new Node(this, &s, def, 1);
        s.addNode(s.inputNode_);
        s.outputNode_ = new Node(this, &s, def, 0);
        s.addNode(s.outputNode_);        
    }

//    NodeDef* def = new NodeDef(info);
//    u32 bin = def->hash_ & kHashMask;
//
//    std::lock_guard<std::mutex> lck(e->nrt_lock_);
//
//    def->next_ = e->defs_[bin];
//    e->defs_[bin] = def;
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


void Engine::processNRTCommands(Engine* e) {
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
        std::this_thread::sleep_for(std::chrono::microseconds(25000));
    }
}

void Engine::processDeadNodes(Engine* e) {
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


