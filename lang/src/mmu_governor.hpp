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
//  mmu_governor.hpp
//  lang
//
//  Minimum Mutator Utilization (MMU) scheduler for the tracing GC.
//
//  The tracing collector already bounds the *pause* of any single step()
//  (time-based deadline sampled every kCheckEvery work units). What it does
//  NOT bound on its own is the *fraction* of CPU the collector takes over a
//  trailing window -- in a tight allocation loop that fires a safepoint on
//  every loop back-edge, or an RT heartbeat that spends its whole budget
//  every audio buffer, the collector can take an unbounded share of short
//  windows even though each pause is short.
//
//  This governor closes that gap, in the spirit of Bacon/Cheng/Rajan's
//  Metronome time-based scheduler. It guarantees the mutator at least a
//  target fraction `u` of CPU over any trailing window of length W:
//
//      MMU(dt) >= u   for all dt >= W
//
//  Mechanism: a fixed ring of time buckets records collector-nanoseconds
//  spent in each slice of the trailing window. Before each collector step a
//  driver asks permittedNanos(now) how much collector time is still allowed
//  in the current window without violating (1-u)*W; the driver clamps the
//  step deadline to that (or skips the step when zero). record() adds the
//  measured step time back into the ring.
//
//  Real-time safety: fixed-size state (kMaxBuckets u64s), no allocation, no
//  lock, no syscall (the caller supplies the gcMonoNanos() timestamp). All
//  state is touched only on the VM's owning mutator thread -- the same
//  single-writer discipline the existing step() telemetry already relies on.
//
//  Safety valve: MMU alone could starve the collector and risk OOM if the
//  mutator over-allocates. The owner (TracingGC) drives setUrgent(true) when
//  allocation outruns the in-flight cycle; in urgency mode the cap is lifted
//  (permittedNanos returns kUnbounded) so the collector runs at its full step
//  budget and the cycle completes. The per-step pause bound is unaffected
//  either way, because the driver always takes min(stepBudget, permitted).
//

#ifndef mmu_governor_hpp
#define mmu_governor_hpp

#include "base_types.hpp"

namespace ts {

class MMUGovernor {
public:
    // "No constraint" sentinel -- equal in value to ts::kGCNoDeadline (a
    // static_assert in tracing_gc.hpp checks they match). A driver that gets
    // this back leaves its own step budget as the sole bound.
    static constexpr u64 kUnbounded = ~u64{0};

    // Fixed ring size. 16 buckets over W gives sub-window granularity of
    // W/16 (e.g. ~937 us for a 15 ms window) -- fine relative to the ms-scale
    // budgets being governed, and cheap to sum/expire.
    static constexpr u32 kMaxBuckets = 16;

    // ---- configuration ----------------------------------------------------

    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

    // mutatorPermille: target mutator share in parts-per-thousand (900 = 90%
    // mutator, i.e. collector capped at 10% of any window >= W). windowNanos:
    // the window length W. Reconfiguring clears accumulated history so the
    // new window starts clean.
    void setTarget(u32 mutatorPermille, u64 windowNanos) {
        if (mutatorPermille > 1000) mutatorPermille = 1000;
        mutatorPermille_ = mutatorPermille;
        if (windowNanos < kMaxBuckets) windowNanos = kMaxBuckets;  // keep bucketNanos_ >= 1
        windowNanos_ = windowNanos;
        bucketNanos_ = windowNanos_ / kMaxBuckets;
        reset();
    }

    u32 mutatorPermille() const { return mutatorPermille_; }
    u64 windowNanos()     const { return windowNanos_; }

    // Clear the sliding window and observed-MMU low-water. Keeps config.
    void reset() {
        for (u32 i = 0; i < kMaxBuckets; ++i) buckets_[i] = 0;
        windowSum_ = 0;
        lastBucketIndex_ = 0;
        haveIndex_ = false;
        minMutatorPermilleObserved_ = 1000;
    }

    // ---- urgency (safety valve) ------------------------------------------

    // Called by the owner before permittedNanos(). Transitioning false->true
    // counts an escalation for telemetry.
    void setUrgent(bool on) {
        if (on && !urgent_) ++escalations_;
        urgent_ = on;
    }
    bool urgent() const { return urgent_; }

    // ---- scheduling query -------------------------------------------------

    // How many nanoseconds of collector time are still permitted in the
    // window ending at `now` without violating the target. kUnbounded when
    // disabled or in urgency mode.
    u64 permittedNanos(u64 now) {
        if (!enabled_ || urgent_) return kUnbounded;
        advance(now);
        u64 cap = collectorCapNanos();
        return windowSum_ >= cap ? 0 : cap - windowSum_;
    }

    // ---- accounting -------------------------------------------------------

    // Record that a collector step ran for `elapsedNanos`, ending at
    // `endNanos` (= start + elapsed). Attributes the whole step to the bucket
    // of its end time -- the standard single-bucket approximation; the window
    // *total* stays exact regardless. Updates the observed-MMU low-water.
    void record(u64 endNanos, u64 elapsedNanos) {
        if (!enabled_) return;
        advance(endNanos);
        u32 slot = (u32)(lastBucketIndex_ % kMaxBuckets);
        buckets_[slot] += elapsedNanos;
        windowSum_ += elapsedNanos;
        // Worst mutator utilization is observed right after a step, when the
        // window is fullest of collector time.
        u64 collector = windowSum_ > windowNanos_ ? windowNanos_ : windowSum_;
        u32 mutPermille = (u32)(((windowNanos_ - collector) * 1000) / windowNanos_);
        if (mutPermille < minMutatorPermilleObserved_)
            minMutatorPermilleObserved_ = mutPermille;
    }

    // ---- observability ----------------------------------------------------

    // Lowest mutator utilization (parts-per-thousand) observed over any
    // sampled window since the last reset(). Should stay >= mutatorPermille_
    // while the governor is enabled and not escalating.
    u32 minMutatorPermilleObserved() const { return minMutatorPermilleObserved_; }
    u64 escalations() const { return escalations_; }

private:
    // Collector's per-window budget = (1 - u) * W.
    u64 collectorCapNanos() const {
        return (windowNanos_ * (1000 - mutatorPermille_)) / 1000;
    }

    // Roll the ring forward to the bucket containing `now`, zeroing (and
    // discounting from windowSum_) every slot scrolled into -- those slices
    // have fallen out of the trailing window and are reused as fresh buckets.
    void advance(u64 now) {
        u64 idx = now / bucketNanos_;
        if (!haveIndex_) { lastBucketIndex_ = idx; haveIndex_ = true; return; }
        if (idx <= lastBucketIndex_) return;  // same bucket (or clock non-advance)
        u64 delta = idx - lastBucketIndex_;
        if (delta >= kMaxBuckets) {
            for (u32 i = 0; i < kMaxBuckets; ++i) buckets_[i] = 0;
            windowSum_ = 0;
        } else {
            for (u64 k = 1; k <= delta; ++k) {
                u32 slot = (u32)((lastBucketIndex_ + k) % kMaxBuckets);
                windowSum_ -= buckets_[slot];
                buckets_[slot] = 0;
            }
        }
        lastBucketIndex_ = idx;
    }

    bool enabled_ = false;
    bool urgent_ = false;
    bool haveIndex_ = false;
    u32 mutatorPermille_ = 900;          // 90% mutator target
    u64 windowNanos_ = 15'000'000;       // 15 ms
    u64 bucketNanos_ = 15'000'000 / kMaxBuckets;

    u64 buckets_[kMaxBuckets] = {};
    u64 windowSum_ = 0;                  // running sum of live buckets
    u64 lastBucketIndex_ = 0;            // absolute bucket index of current slice

    u32 minMutatorPermilleObserved_ = 1000;
    u64 escalations_ = 0;
};

} // namespace ts

#endif /* mmu_governor_hpp */
