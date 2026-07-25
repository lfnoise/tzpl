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
//  tzpl_engine_stats.hpp
//  audio engine
//
//  Always-on RT instrumentation: the master output level and the per-block
//  timing / queue-depth / GC counters behind getEngineStats().
//
//  Every field here has exactly ONE writer -- the thread that owns the block
//  being measured -- and any number of lock-free readers. Nothing depends on
//  two fields agreeing with each other, so all loads and stores are relaxed;
//  a reader may see a mix of two adjacent blocks, which is invisible at the
//  few-Hz rates these are sampled at.
//
//  Resetting a max-since-read field would normally need a CAS. It doesn't
//  here: the host bumps `statsEpoch` and each writer, which keeps its running
//  maximum in a plain non-atomic member, notices the change on its next block
//  and starts over. One relaxed load per block, no contention.
//

#ifndef tzpl_engine_stats_hpp
#define tzpl_engine_stats_hpp

#include "tzpl_common.hpp"

#include <atomic>
#include <cmath>

namespace engine {

// Master metering is per channel up to this width; wider streams still get
// correct summary (peakAll/rmsAll) figures.
inline constexpr int kMaxMasterChans = 16;

// Post-limiter, post-gain master output level -- what the device actually
// plays. Written once per block by the audio thread.
struct MasterMeter {
    std::atomic<f32> peak[kMaxMasterChans];
    std::atomic<f32> rms[kMaxMasterChans];
    // Peak with a slow fall, so a reader polling at any rate sees the real
    // maximum rather than whichever block it happened to land on.
    std::atomic<f32> peakHold[kMaxMasterChans];
    std::atomic<f32> peakAll{0.f};
    std::atomic<f32> rmsAll{0.f};
    std::atomic<f32> peakHoldAll{0.f};
    std::atomic<int> chans{0};
    // Samples at or over full scale since the last reset. Monotone; readers
    // latch on a change rather than reading and clearing.
    std::atomic<u32> clipCount{0};

    // ---- RT-thread-only accumulation state. ----
    f32 accumPeak[kMaxMasterChans]{};
    f64 accumSq[kMaxMasterChans]{};
    int accumFrames = 0;
    // Publish window, in frames. Matches TapSlot::kDefaultPublishPeriod so a
    // master meter and a node meter integrate over the same span.
    int publishFrames = 512;
    // Multiplier applied to peakHold each block (set from the block rate for
    // a ~1.5 s fall).
    f32 holdDecayPerBlock = 1.f;

    MasterMeter() {
        for (int c = 0; c < kMaxMasterChans; ++c) {
            peak[c].store(0.f, std::memory_order_relaxed);
            rms[c].store(0.f, std::memory_order_relaxed);
            peakHold[c].store(0.f, std::memory_order_relaxed);
        }
    }

    // Set the fall rate and publish window from the stream format.
    void configure(f64 sampleRate, int bufferFrames) {
        if (sampleRate <= 0. || bufferFrames <= 0) return;
        f64 blocksPerSec = sampleRate / (f64)bufferFrames;
        f64 fallSeconds = 1.5;
        holdDecayPerBlock = (f32)std::pow(0.001, 1.0 / (blocksPerSec * fallSeconds));
        publishFrames = 512;
    }

    void reset() {
        for (int c = 0; c < kMaxMasterChans; ++c) {
            peak[c].store(0.f, std::memory_order_relaxed);
            rms[c].store(0.f, std::memory_order_relaxed);
            peakHold[c].store(0.f, std::memory_order_relaxed);
            accumPeak[c] = 0.f;
            accumSq[c] = 0.;
        }
        peakAll.store(0.f, std::memory_order_relaxed);
        rmsAll.store(0.f, std::memory_order_relaxed);
        peakHoldAll.store(0.f, std::memory_order_relaxed);
        clipCount.store(0, std::memory_order_relaxed);
        accumFrames = 0;
    }

    // `out` is interleaved, `frames` x `chans`. RT thread only.
    void processBlock(f32 const* out, int frames, int outChans);
};

// Per-silo block accounting. Written by that silo's processing thread (silo 0
// on the audio thread, workers on their own).
struct SiloStats {
    std::atomic<u64> blockCount{0};
    // processFrames() up to mixDown() -- the silo's own DSP.
    std::atomic<u64> lastNanos{0};
    std::atomic<u64> maxNanos{0};   // since the last stats epoch
    std::atomic<u64> ewmaNanos{0};  // alpha = 1/32
    // mixDown(), which for silo 0 includes waiting on the worker silos. Kept
    // separate so silo 0's DSP figure isn't dominated by sync time.
    std::atomic<u64> mixWaitNanos{0};
    std::atomic<int> toNrtDepth{0};
    std::atomic<int> fromNrtDepth{0};
    std::atomic<int> deadNodesDepth{0};
    std::atomic<int> numTaps{0};
    // Attached VM's GC counters, republished each block by the RT heartbeat.
    // Monotone; the host does delta arithmetic.
    std::atomic<u8>  hasVM{0};
    std::atomic<u64> gcStepCount{0};
    std::atomic<u64> gcCycles{0};
    std::atomic<u64> gcRtStepCount{0};
    std::atomic<u64> gcRtMaxNanos{0};

    // RT-thread-only.
    u64 localMax = 0;
    u32 lastEpoch = 0;

    void reset() {
        blockCount.store(0, std::memory_order_relaxed);
        lastNanos.store(0, std::memory_order_relaxed);
        maxNanos.store(0, std::memory_order_relaxed);
        ewmaNanos.store(0, std::memory_order_relaxed);
        mixWaitNanos.store(0, std::memory_order_relaxed);
        localMax = 0;
    }
};

// Engine-wide block accounting.
struct EngineStatsRT {
    std::atomic<u64> blockCount{0};
    std::atomic<u64> lastNanos{0};
    std::atomic<u64> maxNanos{0};
    std::atomic<u64> ewmaNanos{0};
    // One block's worth of wall time: bufferFrames / sampleRate.
    std::atomic<u64> budgetNanos{0};
    std::atomic<u64> overBudgetCount{0};
    // Every RT-side way audio can be lost: over-budget blocks, a block of the
    // wrong size, an escaped exception, a device-reported under/overflow.
    std::atomic<u64> dropoutCount{0};
    std::atomic<u64> badBlockSizeCount{0};
    std::atomic<u64> rtExceptionCount{0};
    // Bumped by resetEngineStats to restart every max-since-read field.
    std::atomic<u32> statsEpoch{0};

    // RT-thread-only.
    u64 localMax = 0;
    u32 lastEpoch = 0;

    void reset() {
        lastNanos.store(0, std::memory_order_relaxed);
        maxNanos.store(0, std::memory_order_relaxed);
        ewmaNanos.store(0, std::memory_order_relaxed);
        overBudgetCount.store(0, std::memory_order_relaxed);
        dropoutCount.store(0, std::memory_order_relaxed);
        badBlockSizeCount.store(0, std::memory_order_relaxed);
        rtExceptionCount.store(0, std::memory_order_relaxed);
        localMax = 0;
        statsEpoch.fetch_add(1, std::memory_order_relaxed);
    }
};

// Fold one measurement into last/max/ewma, restarting the max when the host
// has bumped the epoch. Single writer; `localMax`/`lastEpoch` are its own.
template <class Stats>
inline void publishBlockNanos(Stats& st, u64 nanos, u32 epoch) {
    if (epoch != st.lastEpoch) {
        st.lastEpoch = epoch;
        st.localMax = 0;
    }
    st.lastNanos.store(nanos, std::memory_order_relaxed);
    if (nanos > st.localMax) {
        st.localMax = nanos;
        st.maxNanos.store(nanos, std::memory_order_relaxed);
    }
    u64 prev = st.ewmaNanos.load(std::memory_order_relaxed);
    // alpha = 1/32, integer form; seeds directly on the first block. The
    // difference is computed signed -- a block faster than the average would
    // otherwise wrap.
    u64 next = nanos;
    if (prev != 0) {
        i64 delta = (i64)nanos - (i64)prev;
        next = (u64)((i64)prev + delta / 32);
    }
    st.ewmaNanos.store(next, std::memory_order_relaxed);
}

} // namespace engine

#endif /* tzpl_engine_stats_hpp */
