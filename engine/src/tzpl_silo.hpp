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
    std::vector<Node*> nrt_nodeTable_;
    std::vector<Node*> rt_nodeTable_;
    GraphShadow shadow_; // guarded by engine_->shadowMtx_
    Node* rt_sortedNodeList_ = nullptr;
    Node* inputNode_ = nullptr;
    Node* outputNode_ = nullptr;
    f32* outbuf_ = nullptr;
    // Sample time of the start of the block being processed (or of the next
    // block, between blocks).
    i64 sampleTime_ = 0;
    int channelOffset_ = 0;
    bool needsSort_ = false;

    // ---- The current block (RT) ----
    // Commands and tasks are applied in time order before any node runs.
    // Each runs with now_ set to its time: a fractional absolute sample time
    // (sub-sample fade starts) and nowOffset_, the first sample of the block
    // at or after it (discrete events).
    i64 blockStart_ = 0;
    int blockLen_ = 0;
    bool inBlock_ = false;
    f64 now_ = 0.;
    int nowOffset_ = 0;
    // Commands run this block. They go back to the NRT thread only after the
    // block, because a node freed mid-block still runs until its offset.
    CommandList blockDone_;
    // Nodes freed this block, removed after it runs.
    Node* pendingFree_ = nullptr;
    // Per-clock commands popped as due in this block, not yet run.
    std::vector<CommandList> dueLists_;
    u64 droppedNodeEvents_ = 0;
    u32 visitStamp_ = 0;
    u32 sortGen_ = 0;

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

    // Task-scheduler hooks for the bridge-side silo task pool (coroutines
    // spawned on this silo, scheduled by beat on the tempo clocks). Once per
    // block the silo merges task wakeups with clock-scheduled commands in
    // time order:
    //   taskNextFn_  the absolute fractional sample time of the next due task
    //                (or actor timer); +inf if none
    //   taskRunFn_   run everything due at or before `time`, with now_ set to
    //                it, so commands the tasks issue land at that time
    using TaskNextFn = f64 (*)(void* sched, Silo* s);
    using TaskRunFn = void (*)(void* sched, Silo* s, f64 time);
    void* taskSched_ = nullptr;
    TaskNextFn taskNextFn_ = nullptr;
    TaskRunFn taskRunFn_ = nullptr;

    // Signal taps (meters/scopes). Fixed table, RT-thread only: installed
    // and removed by Tap commands, scanned once per block by processTaps.
    // The TapSlot objects are owned by the Engine's tap registry; entries
    // whose node dies are cleared by removeNode.
    // Live entries occupy the DENSE PREFIX [0, numTaps_): removal moves the
    // last entry down into the hole. Entry order is therefore not stable.
    static constexpr int kMaxTaps = 128;
    struct RTTap {
        TapSlot* slot = nullptr;
        Node* node = nullptr;
        void* const* bufs = nullptr;  // node outlet channel buffers (f32)
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
    // Free a node at the current event time: it runs up to the offset and
    // is removed after the block (immediately when no block is running).
    void freeNodeAt(Node* node);
    // Remove now, collapsing any fan-in mixer chains left with one source.
    void removeNodeNow(Node* node);

    // Point dst's channels at src's (broadcasting). The shapes must
    // broadcast (no folding: the caller inserts a fold adapter).
    tzpl_SErr connect(OutPort* src, InPort* dst);
    tzpl_SErr disconnect(InPort* dst);
    void disconnect(OutPort* port);
    void disconnectNode(Node* node);
    tzpl_SErr reconnectOutput(OutPort* oldSrc, OutPort* newSrc);
    // True if connecting an outlet of srcNode to an inlet of dstNode would
    // close a cycle (dstNode already reaches srcNode).
    bool wouldCycle(Node* srcNode, Node* dstNode);

    void sortVisitNode(Node* node, Node*& lastSorted);
    void sortNodes();
    void sortedListRemove(Node* node);

    void unlink(InPort* dst);

    // The block offset of an event at the current time.
    int eventOffset() const { return inBlock_ ? nowOffset_ : 0; }
    f64 eventTime() const { return inBlock_ ? now_ : (f64)sampleTime_; }
    void setNow(f64 t, int offset) { now_ = t; nowOffset_ = offset; }

    // Value-stage operations on an input, at the current event time.
    // `chans`/`values` are parallel; out-of-range channels are skipped.
    void setInputValues(InPort* dst, int n, int const* chans, f64 const* values,
                        i64 rampCount, FadeCurve curve);
    void impulseInputValues(InPort* dst, int n, int const* chans, f64 const* values);

    // Engine-side notes (voiced nodes), at the current event time.
    tzpl_SErr rt_noteOn(Node* node, int noteID, int n, f32 const* params);
    tzpl_SErr rt_noteOff(Node* node, int noteID);
    void rt_allNotesOff(Node* node);
    tzpl_SErr rt_noteSetParam(Node* node, int noteID, int index, f32 value, i64 rampCount);

    // Every command still waiting on a tempo clock, unlinked for NRT
    // disposal (no deletion here: safe on the RT thread).
    Command* takeAllScheduled();

    void processRTCommands();
    void runCommandChain(Command* head);
    void processFrames();
    void processBlock(int frameOffset, int n);
    void collectAndRunEvents(int n);
    void runNodes(int n);
    void finishBlock();
    void mixDown(int numFrames, f32* out);

    // Taps (RT thread).
    tzpl_SErr installTap(TapSlot* slot, Node* node, int outlet, i64 tapID);
    // Resolve a tapID through THIS silo's RT table. Callable with no lock and
    // no map lookup, because rt_taps_ is owned by this silo's processing
    // thread -- and the only legal caller is that same thread. Silo 0
    // additionally sees the engine's master taps. Returns null for a tap on
    // another silo.
    TapSlot* rt_findTap(i64 tapID);
    void eraseTapAt(int i);
    void removeTap(i64 tapID);
    void clearTapsForNode(Node* node);
    void processTaps(int n);

    static void workLoop(Silo* s);

    void pushDeadNode(Node* node);
};

// Same element type and shape (port-for-port replacement).
tzpl_SErr compatibleTypes(tzpl_SignalType const& a, tzpl_SignalType const& b);
// A source of type a can feed a destination of type b: same element type;
// any shapes (broadcast by pointer, fold by an adapter).
tzpl_SErr connectableTypes(tzpl_SignalType const& a, tzpl_SignalType const& b);

}


#endif /* tzpl_silo_hpp */
