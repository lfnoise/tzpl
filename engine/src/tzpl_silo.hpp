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
#include "tzpl_engine_stats.hpp"
#include "tzpl_tempo_clock.hpp"
#include "tzpl_tap.hpp"
#include <atomic>
#include <string>
#include <unordered_map>
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
#pragma mark GRAPH SHADOW

// NRT topology shadow for the graph view. Guarded by Engine::shadowMtx_
// (NOT nrt_lock_: with audio stopped, commands run inline while sched()
// holds nrt_lock_, so the commit cannot retake it). Shadow edits are
// journaled at bundle submit but applied when the bundle EXECUTES
// (ShadowCommitCmd::doNRT, ordered by the to_nrt_ FIFO), so the shadow
// tracks RT execution order even when bundles are scheduled to run out
// of submission order or tempo changes reorder clock-scheduled bundles.
// Hidden helper nodes (mixers/xfaders, nodeID < 0) never appear here.
struct ShadowConn {
    i64 srcNode; i32 srcPort;
    i64 dstNode; i32 dstPort;
    bool operator==(ShadowConn const&) const = default;
};

struct GraphShadow {
    std::unordered_map<i64, std::string> nodes; // nodeID -> defName
    std::vector<ShadowConn> conns;   // duplicates allowed (fan-in summed twice)
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
    GraphShadow shadow_; // guarded by engine_->shadowMtx_
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

    // Beat-based tempo-clock slots. Each slot maps beats<->samples through a
    // TempoRamp and holds a beat-sorted queue of scheduled bundles. Sized to
    // EngineConfig::numTempoClocks at construction. Slot k is kept in sync
    // across all Silos by broadcasting tempo changes to every Silo's slot k.
    std::vector<TempoClock> tempoClocks_;

    // Per-buffer heartbeat callback for VM GC. Called once per audio buffer
    // to drain the deferred-delete queue and reclaim dead objects. Takes the
    // Silo so it can republish the VM's GC counters into stats_ -- the RT
    // thread owns those counters, so this is the only place they can be read
    // without racing the collector.
    using HeartbeatFn = void (*)(void* vm, Silo* s);
    HeartbeatFn heartbeatFn_ = nullptr;

    // Per-sample task-scheduler tick. Drives the bridge-side silo task pool
    // (coroutines spawned on this silo, scheduled by beat on the tempo clocks):
    // it fires due tasks and reschedules them. Opaque scheduler pointer +
    // callback set from an NRT thread via command (mirrors vm_/heartbeatFn_).
    using TaskTickFn = void (*)(void* sched, i64 sampleTime, Silo* s);
    void* taskSched_ = nullptr;
    TaskTickFn taskTickFn_ = nullptr;

    // Signal taps (meters/scopes). Fixed table, RT-thread only: installed
    // and removed by Tap commands, scanned once per sample by processTaps.
    // The TapSlot objects are owned by the Engine's tap registry; entries
    // whose node dies are cleared by removeNode.
    // Live entries occupy the DENSE PREFIX [0, numTaps_): removal moves the
    // last entry down into the hole, so processTaps only ever touches live
    // taps and the table can be large without costing per-sample work.
    // Entry order is therefore not stable -- nothing depends on it.
    static constexpr int kMaxTaps = 128;
    struct RTTap {
        TapSlot* slot = nullptr;
        Node* node = nullptr;
        f32 const* buf = nullptr;  // node outlet buffer (chans wide)
        int chans = 0;
        i64 tapID = 0;
    };
    RTTap rt_taps_[kMaxTaps];
    int numTaps_ = 0;

    // Block timing, queue depths and (when a VM is attached) GC counters for
    // this silo. Written only by this silo's processing thread.
    SiloStats stats_;

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

    // Taps (RT thread).
    tzpl_SErr installTap(TapSlot* slot, Node* node, int outlet, i64 tapID);
    void eraseTapAt(int i);
    void removeTap(i64 tapID);
    void clearTapsForNode(Node* node);
    void processTaps();

    static void workLoop(Silo* s);
    
    void pushDeadNode(Node* node);
};

tzpl_SErr compatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b);
tzpl_SErr relaxedCompatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b);

}


#endif /* tzpl_silo_hpp */
