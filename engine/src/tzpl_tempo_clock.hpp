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
//  tzpl_tempo_clock.hpp
//  audio engine
//
//  A TempoClock is a beat-based scheduler slot owned by a Silo. It maps
//  beats <-> seconds <-> samples through a TempoRamp and holds a beat-sorted
//  queue of scheduled command bundles. Each Silo owns N clocks (a command
//  line argument). Clock k is kept in sync across all Silos by broadcasting
//  every tempo change to slot k on every Silo (see tzpl_client_interface).
//

#ifndef tzpl_tempo_clock_hpp
#define tzpl_tempo_clock_hpp

#include "tzpl_common.hpp"
#include "tzpl_command.hpp"
#include "tempo_ramp.hpp"

namespace engine {

struct Silo;

struct TempoClock
{
    ts::TempoRamp ramp_;            // beat <-> second mapping
    f64 sampleRate_ = 44100.;
    i64 epochSample_ = 0;          // sample at which this clock's seconds == 0
    BeatSortedCommandList queue_;  // bundles scheduled on this clock (by beatTime_)

    TempoClock() = default;

    TempoClock(f64 bpm, f64 sampleRate, i64 epochSample)
        : ramp_(bpm / 60.0, 0., 0.)  // BPM -> beats per second
        , sampleRate_(sampleRate)
        , epochSample_(epochSample)
    {}

    f64 secondsAtSample(i64 sample) const {
        return (f64)(sample - epochSample_) / sampleRate_;
    }

    // Current beat position at a given sample time.
    f64 beatAtSample(i64 sample) const {
        return ramp_.secondsToBeats(secondsAtSample(sample));
    }

    // Current tempo (beats per second) at a given sample time.
    f64 tempoAtSample(i64 sample) const {
        return ramp_.secondsToTempo(secondsAtSample(sample));
    }

    // Insert a command into the beat-sorted queue (uses cmd->beatTime_).
    void schedule(Command* cmd) { queue_.add(cmd); }

    // Immediate tempo reset, anchored at the given sample. All Silos that
    // apply this at the same sample produce identical ramps.
    void setTempoNow(f64 beatsPerSecond, i64 sample) {
        f64 seconds = secondsAtSample(sample);
        f64 beat = ramp_.secondsToBeats(seconds);
        ramp_ = ts::TempoRamp(beatsPerSecond, beat, seconds);
    }

    // Install a tempo ramp toward targetBPS over rampBeats, anchored at the
    // beat atBeat (in this clock's beats). Beat-anchored so it is identical
    // across Silos regardless of sub-sample firing jitter.
    void applyRamp(f64 atBeat, f64 targetBPS, f64 rampBeats) {
        f64 seconds = ramp_.beatsToSeconds(atBeat);
        f64 currentTempo = ramp_.beatsToTempo(atBeat);
        ramp_ = ts::TempoRamp(currentTempo, atBeat, seconds, targetBPS, rampBeats);
    }

    // Pop and run every command whose beatTime_ has been reached at this
    // sample, then ship them back to the NRT thread for stage-2 cleanup.
    // Defined in tzpl_silo.cpp (needs the full Silo definition).
    void process(i64 sampleTime, Silo* s);

    void clear() { queue_.clear(); }
};

} // namespace engine

#endif /* tzpl_tempo_clock_hpp */
