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
//  tzpl_silo.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_silo_hpp
#define tzpl_silo_hpp

#include "tzpl_atomic_fifo.hpp"
#include "tzpl_node.hpp"
#include "tzpl_command.hpp"
#include <atomic>
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
    int channelOffset_ = 0;
    bool needsSort_ = false;

    // Optional attached VM for event-driven scripting (opaque pointer to ts::VM).
    // Set from an NRT thread via command; only accessed by the Silo's RT thread.
    void* vm_ = nullptr;

    // Optional tempo-based scheduler (opaque pointer).
    // Polled each sample to fire beat-timed events on the attached VM.
    void* rtTempoScheduler_ = nullptr;

    // Callback for per-sample tempo scheduler processing.
    // Set when a tempo scheduler is attached. Avoids engine depending on lang types.
    using TempoSchedFn = void (*)(void* scheduler, i64 sampleTime, void* vm);
    TempoSchedFn tempoSchedFn_ = nullptr;

    Silo();
    ~Silo();

    Node* nrt_getNode(i64 nodeID) const;
    tzpl_SErr nrt_getInPort(PortAddr addr, InPort*& port) const;
    tzpl_SErr nrt_getOutPort(PortAddr addr, OutPort*& port) const;

    Node* rt_getNode(i64 nodeID) const;
    tzpl_SErr rt_getInPort(PortAddr addr, InPort*& port) const;
    tzpl_SErr rt_getOutPort(PortAddr addr, OutPort*& port) const;

    tzpl_SErr addNode(Node* node);
    tzpl_SErr removeNode(Node* node);
    Node* removeAllNodes();

    tzpl_SErr connect(OutPort* src, InPort* dst);
    tzpl_SErr disconnect(InPort* dst);
    void disconnect(OutPort* port);
    void disconnectNode(Node* node);
    tzpl_SErr reconnectOutput(OutPort* oldSrc, OutPort* newSrc);
    
    void sortVisitNode(Node* node, Node*& lastSorted);
    void sortNodes();
    
    void setInput(InPort* dst, int numValues, void* values);

    tzpl_SErr setControl(Node* node, i64 controlID, int numValues, void* values);
    
    void unlink(InPort* dst);

    void processScheduledEvents();
    void processRTCommands();
    void processFrames();
    void runNodes();
    void mixDown(int numFrames, f32* out);

    static void workLoop(Silo* s);
    
    void pushDeadNode(Node* node);
};

tzpl_SErr compatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b);
tzpl_SErr relaxedCompatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b);

}


#endif /* tzpl_silo_hpp */
