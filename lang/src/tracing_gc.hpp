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
    u32 step(u64 deadlineNanos);

    // Run a full cycle synchronously. Useful for tests and for the
    // between-events heartbeat path.
    void runFullCycle();

    Phase phase() const { return phase_; }

    // Stats from the most recently completed cycle.
    u32 lastWhiteCount() const { return lastWhiteCount_; }
    u32 lastBlackCount() const { return lastBlackCount_; }
    u32 lastRootCount() const { return lastRootCount_; }

    // Mark an object: if currently white, transition to gray and push to
    // worklist. No-op for immortals, blacks, and grays. Called by the
    // root walker and by future write barriers.
    void mark(GCObj* obj);

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
    GCObj* sweepCursor_ = nullptr;
    u32 lastWhiteCount_ = 0;
    u32 lastBlackCount_ = 0;
    u32 lastRootCount_ = 0;
    u32 currentWhiteCount_ = 0;
    u32 currentBlackCount_ = 0;
    u32 allocsSinceLastCycle_ = 0;
    u32 nextTriggerAllocs_ = kMinTriggerAllocs;

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
};

} // namespace ts

#endif /* tracing_gc_hpp */
