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

namespace ts {

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

    // Do bounded work toward completing the current cycle. Returns the
    // number of operations consumed (gray-drains or sweep-steps). Called
    // from VM::safepointPoll() under a budget. Phase change is automatic:
    // Mark -> Sweep when worklist empty; Sweep -> Idle when list walked.
    u32 step(u32 budget);

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
    u32 step_mark(u32 budget);
    u32 step_sweep(u32 budget);
};

} // namespace ts

#endif /* tracing_gc_hpp */
