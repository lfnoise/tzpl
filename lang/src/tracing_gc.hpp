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
//  tracing_gc.hpp
//  lang
//
//  Phase 3 of tracing-GC project: incremental tri-color mark-sweep
//  tracing collector. Runs alongside ARC in shadow mode -- ARC still
//  drives object lifetime, tracing observes and records statistics.
//  Phase 5 will remove ARC and let tracing own reclamation.
//

#ifndef tracing_gc_hpp
#define tracing_gc_hpp

#include "base_types.hpp"
#include "gc.hpp"
#include <vector>
#include <time.h>

namespace ts {

// Monotonic wall-clock in nanoseconds. RT-safe on macOS (vDSO, no syscall),
// invariant across CPU migrations, unaffected by NTP adjustments. Linux gets
// CLOCK_MONOTONIC for the same guarantees.
inline u64 gcMonoNanos() {
#if defined(__APPLE__)
    return ::clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1'000'000'000ull + (u64)ts.tv_nsec;
#endif
}

// Sentinel for "no deadline" (used by runFullCycle and tests).
inline constexpr u64 kGCNoDeadline = ~u64{0};

// Phase 6: who called step()? Used to split telemetry buckets so the
// audio-thread max-pause (the one the user cares about) doesn't get
// hidden in an aggregate with NRT heartbeats that intentionally use a
// larger budget. Passed in by the caller; the VM helpers (rtTick /
// nrtTick / safepointPoll) wire the right value through.
enum class GCStepSource : u8 {
    Safepoint = 0,   // inside the interpreter, from op_safepoint
    NrtTick   = 1,   // host NRT heartbeat (scheduler / REPL / gcHeartbeat)
    RtTick    = 2,   // host RT heartbeat (audio callback)
    Sync      = 3,   // synchronous __gc_trace_cycle (unbounded)
    kCount    = 4,
};

class VM;
class GCObj;

class TracingGC {
public:
    enum class Phase : u8 {
        Idle,    // No cycle in progress
        Mark,    // Draining the gray worklist
        Sweep,   // Walking allObjs list to find whites
    };

    explicit TracingGC(VM& vm) : vm_(vm) {}

    // Start a new cycle. Resets all object colors to White (skipping
    // immortals, which stay Black-equivalent and never participate), seeds
    // the gray worklist from the root set (autoReleasePool, globals,
    // dynVars, deferredDeleteQueue), and transitions to Mark.
    void requestCycle();

    // Advance the in-flight cycle until gcMonoNanos() reaches deadlineNanos.
    // Returns the number of work units consumed (gray-drains + sweep-steps),
    // useful for telemetry. Called from VM::safepointPoll (deadline derived
    // from GCConfig::stepBudgetNanos) and from VM::rtTick / VM::nrtTick (host
    // picks the deadline). Phase changes are automatic: Mark -> Sweep when
    // the gray worklist empties; Sweep -> Idle once the all-objs list is
    // walked. To run a cycle to completion, pass kGCNoDeadline (used by
    // runFullCycle and the __gc_trace_cycle builtin).
    //
    // The deadline is sampled every kCheckEvery work units (~64) so a single
    // call cannot stall the mutator for more than (kCheckEvery * worstStep).
    // Phase 6 bounds worstStep by splitting fan-out-heavy gcScanChildren
    // overrides into push-and-return units.
    //
    // The source parameter classifies the caller for per-driver telemetry.
    // Default GCStepSource::Safepoint keeps the existing in-VM call site
    // (vm.cpp safepointPoll) ergonomically a one-arg call; rtTick / nrtTick
    // pass the appropriate source explicitly.
    u32 step(u64 deadlineNanos, GCStepSource source = GCStepSource::Safepoint);

    // Run a full cycle synchronously. Useful for tests and for the
    // between-events heartbeat path.
    void runFullCycle();

    Phase phase() const { return phase_; }

    // Stats from the most recently completed cycle.
    u32 lastWhiteCount() const { return lastWhiteCount_; }
    u32 lastBlackCount() const { return lastBlackCount_; }
    u32 lastRootCount() const { return lastRootCount_; }

    // Phase 6 step() telemetry. step() measures its own elapsed wall time
    // and accumulates into these counters. stepMaxNanos is the worst-case
    // pause observed; it should stay close to the configured budget plus
    // (kCheckEvery * worstUnitCost). A step that exceeds budget by orders
    // of magnitude usually means a single gcScanChildren ran with very high
    // fan-out -- a signal to add a bounded-fan-out override for that type.
    u64 stepCount()       const { return stepCount_; }
    u64 stepMaxNanos()    const { return stepMaxNanos_; }
    u64 stepSumNanos()    const { return stepSumNanos_; }
    u64 cyclesCompleted() const { return cyclesCompleted_; }

    // Driver-split breakdown of the same counters. Index with GCStepSource.
    // Audio-thread max pause is bySource[RtTick].max -- this is the single
    // number a music-app developer should be watching to confirm RT safety.
    u64 stepCountBySource(GCStepSource s) const { return stepCountBySource_[(u8)s]; }
    u64 stepMaxNanosBySource(GCStepSource s) const { return stepMaxNanosBySource_[(u8)s]; }
    u64 stepSumNanosBySource(GCStepSource s) const { return stepSumNanosBySource_[(u8)s]; }

    void resetStepStats() {
        stepCount_ = 0; stepMaxNanos_ = 0; stepSumNanos_ = 0;
        for (u8 i = 0; i < (u8)GCStepSource::kCount; ++i) {
            stepCountBySource_[i] = 0;
            stepMaxNanosBySource_[i] = 0;
            stepSumNanosBySource_[i] = 0;
        }
    }

    // Mark an object: if currently white, transition to gray and push to
    // worklist. No-op for immortals, blacks, and grays. Called by the
    // root walker and by future write barriers.
    void mark(GCObj* obj);

    // Phase 6 step 3: bounded-fan-out scan. Large containers (Map, Set,
    // ObjArray, InlineArray with > kFanoutThreshold entries) defer their
    // child scan to a partial-container queue instead of looping inline.
    // step_mark services the partial queue in chunks of kFanoutChunk so a
    // single 100k-element container can no longer overshoot the deadline.
    //
    // The container's gcScanChildren override calls pushPartial(this);
    // it must transition itself to Black before doing so (the partial
    // queue does not re-color, only walks children). Pending partials
    // hold a Gray-allocated dst register reference so the container's
    // own liveness is unaffected by being on the queue.
    void pushPartial(GCObj* container);

    static constexpr u32 kFanoutThreshold = 64;  // <= this: inline-scan
    static constexpr u32 kFanoutChunk     = 64;  // scanChunk window

    // SATB write barrier. Insert before any store that overwrites an Obj*
    // slot; oldVal is the slot's current contents. Hot path: one comparison
    // (phase != Mark) + early return. Only when a mark cycle is in flight
    // do we touch the gray worklist, preserving the "snapshot at the
    // beginning" invariant -- objects reachable when the cycle started stay
    // marked, even if the mutator overwrites the path that led to them.
    inline void writeBarrier(GCObj* oldVal) {
        if (phase_ != Phase::Mark) return;
        if (!oldVal) return;
        if (oldVal->isImmortal()) return;
        if (oldVal->color_ != GCColor::White) return;
        oldVal->color_ = GCColor::Gray;
        grayWorklist_.push_back(oldVal);
    }

    // Phase 5.4: proportional auto-trigger. After each completed cycle the
    // next trigger threshold is set to max(kMinTrigger, lastBlackCount *
    // growthFactor) -- i.e., let the heap grow some multiple of the post-
    // cycle live set before cycling again. This keeps mark cost amortized
    // O(1) per allocation rather than O(liveSet) per kCycleTriggerAllocs
    // window. Programs whose live set is tiny still cycle frequently (good
    // for latency); programs with large stable live sets cycle rarely (good
    // for throughput).
    void recordAllocation() { ++allocsSinceLastCycle_; }
    u32  allocsSinceLastCycle() const { return allocsSinceLastCycle_; }
    u32  cycleTriggerAllocs() const { return nextTriggerAllocs_; }
    static constexpr u32 kMinTriggerAllocs = 4096;
    static constexpr u32 kGrowthFactor = 4;  // trigger at 5x post-cycle live

private:
    VM& vm_;
    Phase phase_ = Phase::Idle;
    std::vector<GCObj*> grayWorklist_;
    // Singly-linked sweep cursor: pointer to the slot that holds the
    // current candidate. Lets sweep unlink freed objects inline without
    // a per-object prev pointer. Initialized to &vm_.allObjsHead_ on
    // sweep start; advanced by `&obj->allObjsNext_` after a keep.
    GCObj** sweepSlot_ = nullptr;

    // Phase 6 step 3: bounded-fan-out partial-scan queue. Each entry is a
    // container that has already been Blacked (so it can't be swept) and
    // whose children are being walked incrementally. step_mark processes
    // one chunk of kFanoutChunk children per work unit; if more remain,
    // the entry stays on the queue. Cycle finishes only when this queue
    // AND the gray worklist are both empty.
    struct PartialEntry { GCObj* obj; u32 cursor; };
    std::vector<PartialEntry> pendingContainers_;
    u32 lastWhiteCount_ = 0;
    u32 lastBlackCount_ = 0;
    u32 lastRootCount_ = 0;
    u32 currentWhiteCount_ = 0;
    u32 currentBlackCount_ = 0;
    u32 allocsSinceLastCycle_ = 0;
    u32 nextTriggerAllocs_ = kMinTriggerAllocs;

    // Phase 6 telemetry counters; written only inside step(). Cheap to
    // maintain (single add + max each call) so we leave them always-on.
    u64 stepCount_ = 0;
    u64 stepMaxNanos_ = 0;
    u64 stepSumNanos_ = 0;
    u64 cyclesCompleted_ = 0;
    u64 stepCountBySource_[(u8)GCStepSource::kCount]    = {};
    u64 stepMaxNanosBySource_[(u8)GCStepSource::kCount] = {};
    u64 stepSumNanosBySource_[(u8)GCStepSource::kCount] = {};

    void resetColors();
    void markRoots();
    u32 step_mark(u64 deadlineNanos);
    u32 step_sweep(u64 deadlineNanos);

    // Sample the clock every kCheckEvery work units rather than after every
    // pop. With ~30 ns per unit (a gray-pop + null-check + flag-test path)
    // the overshoot bound is kCheckEvery * worstUnitCost ~ 2 us. The largest
    // single object's gcScanChildren is the remaining variance source until
    // the fan-out refactor lands.
    static constexpr u32 kCheckEvery = 64;

    // Phase 6 step 5: incremental root scan substate. While phase_ == Mark,
    // rootPhase_ tracks which root subset is currently being scanned. Each
    // substate keeps a cursor so the scan can pause and resume across step()
    // calls under the same deadline budget. SATB barrier is active during
    // every Mark substate, so heap stores that overwrite roots-not-yet-
    // visited still preserve reachable objects.
    enum class RootPhase : u8 {
        Done    = 0,
        Globals = 1,
        DynVars = 2,
        Frames  = 3,
        Extras  = 4,  // host-registered scanners (e.g. NRTVM HandlerTable)
    };
    RootPhase rootPhase_ = RootPhase::Done;
    u32 rootGlobalCursor_ = 0;
    u32 rootDynVarCursor_ = 0;
    u32 rootFrameCursor_  = 0;  // walks 0..frameCount_; new frames at the top
                                // are picked up as cursor catches up.
    u32 rootExtraCursor_  = 0;

    void step_root_globals(u64 deadlineNanos, u32& sinceCheck, u32& done);
    void step_root_dynvars(u64 deadlineNanos, u32& sinceCheck, u32& done);
    void step_root_frames(u64 deadlineNanos, u32& sinceCheck, u32& done);
    void step_root_extras(u64 deadlineNanos, u32& sinceCheck, u32& done);
    void scanExecSnapshot(struct ExecSnapshot const& s);
};

} // namespace ts

#endif /* tracing_gc_hpp */
