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
//  tzpl_tap.hpp
//  audio engine
//
//  Signal taps: RT -> NRT readback of a node outlet for meters and
//  scopes. A TapSlot is created at bundle submit (NRT), owned by the
//  Engine's tap registry, and installed on a silo's fixed tap table by
//  TapOutletCmd on the RT thread. Each sample the silo accumulates
//  peak/RMS (published to atomics every publishPeriod samples) and, in
//  scope mode, pushes ALL channels of the outlet into a drop-on-full
//  FIFO as interleaved frames (whole frames or nothing, so channel
//  alignment never slips) -- the RT thread never blocks and never
//  allocates.
//

#ifndef tzpl_tap_hpp
#define tzpl_tap_hpp

#include "tzpl_atomic_fifo.hpp"
#include "tzpl_common.hpp"
#include <atomic>

namespace engine {

enum TapMode : int {
    tapMeter = 0,  // peak/rms only
    tapScope = 1,  // peak/rms + sample FIFO
};

struct TapSlot {
    // Published by the RT thread every publishPeriod samples; read anywhere.
    std::atomic<f32> peak{0.0f};
    std::atomic<f32> rms{0.0f};

    // Scope samples, interleaved frames of `chans` channels. Pushed on RT
    // (drop-on-full, whole frames only), drained via tapDrain().
    AtomicFifo<f32> fifo;

    TapMode const mode;
    int const publishPeriod;

    // Channel count of the tapped outlet (set at bundle submit, before the
    // RT install; capped at kMaxScopeChans for scope capture).
    int chans = 1;
    static constexpr int kMaxScopeChans = 16;

    // RT-thread-only accumulation state.
    f32 accumPeak = 0.0f;
    f32 accumSq = 0.0f;
    int accumCount = 0;

    static constexpr int kScopeFifoSize = 32768;
    static constexpr int kDefaultPublishPeriod = 512;

    explicit TapSlot(TapMode m, int period = kDefaultPublishPeriod)
        : fifo(m == tapScope ? kScopeFifoSize : 1)
        , mode(m)
        , publishPeriod(period)
    {}
};

} // namespace engine

#endif /* tzpl_tap_hpp */
