//
//  jscs_silo.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef jscs_silo_hpp
#define jscs_silo_hpp

#include "jscs_atomic_fifo.hpp"
#include "jscs_node.hpp"
#include "jscs_command.hpp"
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore>
#endif
#include <thread>

namespace engine {

//=============================================================================================
#pragma mark SEMAPHORE

struct Semaphore {
#ifdef __APPLE__
    dispatch_semaphore_t sem_;
    Semaphore() : sem_(dispatch_semaphore_create(0)) {}
    void signal() { dispatch_semaphore_signal(sem_); }
    void wait()   { dispatch_semaphore_wait(sem_, DISPATCH_TIME_FOREVER); }
#else
    std::binary_semaphore sem_{0};
    Semaphore() = default;
    void signal() { sem_.release(); }
    void wait()   { sem_.acquire(); }
#endif
};

//=============================================================================================
#pragma mark SILO

class Engine;
class Command;

const u32 kHashBins = 2048;
const u32 kHashMask = kHashBins - 1;

struct Silo
{
    friend class Engine;

    Engine* engine_ = nullptr;
    int index_;
    Semaphore start_sem_;
    Semaphore done_sem_;
    AtomicFifo<Command*> to_nrt_;
    AtomicFifo<Command*> from_nrt_;
    AtomicFifo<Node*> dead_nodes_; // push on RTT, pop on NRTT.
    std::thread run_thread_;
    SchedulerQueue sched_;
    std::vector<Node*> nrt_nodeTable_;
    std::vector<Node*> rt_nodeTable_;
    Node* rt_sortedNodeList_ = nullptr;
    Node* inputNode_ = nullptr;
    Node* outputNode_ = nullptr;
    f32* outbuf_ = nullptr;
    i64 sampleTime_ = 0;
    bool needsSort_ = false;
        
    Silo();
    ~Silo();

    Node* nrt_getNode(i64 nodeID) const;
    jscs_SErr nrt_getInPort(PortAddr addr, InPort*& port) const;
    jscs_SErr nrt_getOutPort(PortAddr addr, OutPort*& port) const;

    Node* rt_getNode(i64 nodeID) const;
    jscs_SErr rt_getInPort(PortAddr addr, InPort*& port) const;
    jscs_SErr rt_getOutPort(PortAddr addr, OutPort*& port) const;

    jscs_SErr addNode(Node* node);
    jscs_SErr removeNode(Node* node);
    Node* removeAllNodes();

    jscs_SErr connect(OutPort* src, InPort* dst);
    jscs_SErr disconnect(InPort* dst);
    void disconnect(OutPort* port);
    void disconnectNode(Node* node);
    jscs_SErr reconnectOutput(OutPort* oldSrc, OutPort* newSrc);
    
    void sortVisitNode(Node* node, Node*& lastSorted);
    void sortNodes();
    
    void setInput(InPort* dst, int numValues, void* values);

    jscs_SErr setControl(Node* node, i64 controlID, int numValues, void* values);
    
    void unlink(InPort* dst);

    void processScheduledEvents();
    void processRTCommands();
    void processFrames();
    void runNodes();
    void mixDown(int numFrames, f32* out);

    static void workLoop(Silo* s);
    
    void pushDeadNode(Node* node);
};

jscs_SErr compatibleTypes(jscs_SignalType& a, jscs_SignalType& b);
jscs_SErr relaxedCompatibleTypes(jscs_SignalType& a, jscs_SignalType& b);

}


#endif /* jscs_silo_hpp */
