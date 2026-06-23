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
//  tempo_ramp.hpp
//  shared
//
//  A tempo ramp that is linear with respect to beats (exponential wrt seconds).
//  Used by tempo-based schedulers to convert between beats and seconds.
//  After the ramp duration elapses, the ending tempo is held constant.
//

#ifndef tempo_ramp_hpp
#define tempo_ramp_hpp

#include <cmath>
#include <algorithm>

namespace ts {

using f64 = double;

class TempoRamp {
    f64 startTempo_;   // starting tempo (beats per second)
    f64 startBeat_;    // starting beat
    f64 startSeconds_; // starting seconds
    f64 endTempo_;     // ending tempo (beats per second)
    f64 endBeat_;      // ending beat
    f64 endSeconds_;   // ending seconds
    f64 tempoSlope_;   // rate of tempo change per beat

public:
    TempoRamp(f64 startTempo, f64 startBeat, f64 startSeconds,
              f64 endTempo, f64 beatDuration)
        : startTempo_(startTempo)
        , startBeat_(startBeat)
        , startSeconds_(startSeconds)
        , endTempo_(endTempo)
        , endBeat_(startBeat + beatDuration)
        , tempoSlope_((endTempo - startTempo) / beatDuration)
    {
        if (beatDuration <= 0.) {
            endSeconds_ = startSeconds_;
            tempoSlope_ = 0.;
        } else if (endTempo_ == startTempo_) {
            // constant tempo segment
            endSeconds_ = startSeconds_ + beatDuration / startTempo_;
            tempoSlope_ = 0.;
        } else {
            // tempo ramp
            endSeconds_ = startSeconds_
                + beatDuration * std::log(endTempo_ / startTempo_)
                / (endTempo_ - startTempo_);
        }
    }

    // Constant-tempo convenience constructor
    TempoRamp(f64 tempo, f64 startBeat, f64 startSeconds)
        : TempoRamp(tempo, startBeat, startSeconds, tempo, 0.)
    {}

    // Default: 120 BPM = 2 beats per second, starting at beat 0, time 0
    TempoRamp() : TempoRamp(2.0, 0., 0.) {}

    // The tempo at a given beat
    f64 beatsToTempo(f64 beats) const {
        if (tempoSlope_ == 0.) return startTempo_;
        f64 rampBeats = std::clamp(beats, startBeat_, endBeat_) - startBeat_;
        return rampBeats * tempoSlope_ + startTempo_;
    }

    // The tempo at a given time in seconds
    f64 secondsToTempo(f64 seconds) const {
        if (tempoSlope_ == 0.) return startTempo_;
        f64 rampSeconds = std::clamp(seconds, startSeconds_, endSeconds_) - startSeconds_;
        return startTempo_ * std::exp(tempoSlope_ * rampSeconds);
    }

    f64 secondsToBeats(f64 seconds) const {
        if (seconds < startSeconds_ || tempoSlope_ == 0.0) {
            // extrapolate assuming constant tempo
            return startBeat_ + startTempo_ * (seconds - startSeconds_);
        } else if (seconds > endSeconds_) {
            // extrapolate assuming constant tempo
            return endBeat_ + endTempo_ * (seconds - endSeconds_);
        } else {
            return startBeat_
                + (startTempo_ / tempoSlope_)
                * (std::exp(tempoSlope_ * (seconds - startSeconds_)) - 1.0);
        }
    }

    f64 beatsToSeconds(f64 beats) const {
        if (beats < startBeat_ || tempoSlope_ == 0.0) {
            // extrapolate assuming constant tempo
            return startSeconds_ + (beats - startBeat_) / startTempo_;
        } else if (beats > endBeat_) {
            // extrapolate assuming constant tempo
            return endSeconds_ + (beats - endBeat_) / endTempo_;
        } else {
            return startSeconds_
                + (1.0 / tempoSlope_)
                * std::log(((beats - startBeat_) * tempoSlope_) / startTempo_ + 1.0);
        }
    }

    f64 beatsDuration() const { return endBeat_ - startBeat_; }
    f64 secondsDuration() const { return endSeconds_ - startSeconds_; }

    f64 startTempo() const { return startTempo_; }
    f64 endTempo() const { return endTempo_; }
    f64 startBeat() const { return startBeat_; }
    f64 endBeat() const { return endBeat_; }
    f64 startSeconds() const { return startSeconds_; }
    f64 endSeconds() const { return endSeconds_; }
};

} // namespace ts

#endif /* tempo_ramp_hpp */
