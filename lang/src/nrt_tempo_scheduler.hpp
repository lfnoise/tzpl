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
//  nrt_tempo_scheduler.hpp
//  lang
//
//  Tempo-based NRT scheduler. Events are scheduled by beat position.
//  Uses a TempoRamp to convert beats to wall-clock time. Inspired by
//  SuperCollider's TempoClock.
//
//  The scheduler fires events slightly ahead of their actual beat time
//  by a user-specified latency, giving commands time to reach the RT
//  thread before they must be executed.
//

#ifndef nrt_tempo_scheduler_hpp
#define nrt_tempo_scheduler_hpp

#include "vm.hpp"
#include "tempo_ramp.hpp"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <chrono>
#include <atomic>

namespace ts {

struct NRTVM;

class NRTTempoScheduler {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct Entry {
        f64 beatTime;            // when this event fires (in beats)
        Obj* handler;            // Callable (Lambda or Primitive), retained.
                                 // nullptr for internal tempo-change events.
        i64 timerID;

        // For tempo-change events (handler == nullptr)
        f64 targetTempo;         // end tempo (beats per second)
        f64 rampBeats;           // duration of ramp in beats (0 = instant)

        // Priority queue: earliest beat first
        bool operator>(const Entry& other) const {
            return beatTime > other.beatTime;
        }
    };

    explicit NRTTempoScheduler(NRTVM* vm, f64 bpm = 60.0,
                                f64 latencySeconds = 0.05);
    ~NRTTempoScheduler();

    void start();
    void stop();

    // Schedule a handler at an absolute beat. Handler is retained.
    i64 schedAbs(f64 beat, Obj* handler);

    // Schedule a handler relative to the current logical beat.
    i64 sched(f64 deltaBeats, Obj* handler);

    // Schedule a tempo change at an absolute beat.
    // Installs a new TempoRamp starting at that beat, ramping to
    // targetTempo over rampBeats (0 = instant).
    // Tempo is in beats per second.
    i64 schedTempoChange(f64 beat, f64 targetTempo, f64 rampBeats = 0.);

    // Immediate tempo change at the current beat.
    void setTempo(f64 beatsPerSecond);

    // Schedule a tempo change in BPM for convenience.
    void setTempoBPM(f64 bpm);
    i64 schedTempoChangeBPM(f64 beat, f64 targetBPM, f64 rampBeats = 0.);

    bool cancel(i64 timerID);

    // Current state (thread-safe reads)
    f64 tempo() const;     // current tempo in beats per second
    f64 tempoBPM() const;  // current tempo in BPM
    f64 beats() const;     // current beat position
    f64 beatDur() const;   // duration of one beat in seconds

    // The current logical beat (set before calling a handler, used by
    // sched() for relative scheduling).
    f64 logicalBeat() const { return logicalBeat_; }

    f64 latency() const { return latencySeconds_; }
    void setLatency(f64 seconds) { latencySeconds_ = seconds; }

private:
    void run();

    // Convert a beat time to a wall-clock time point, accounting for latency.
    TimePoint beatToFireTime(f64 beat) const;

    // Convert wall-clock now to seconds since epoch.
    f64 elapsedSeconds() const;

    NRTVM* vm_;
    TempoRamp ramp_;
    TimePoint epoch_;          // wall-clock time corresponding to ramp start
    f64 latencySeconds_;
    f64 logicalBeat_ = -1.;   // current logical beat (-1 = not in a handler callback)

    std::mutex schedMtx_;
    std::condition_variable cv_;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<i64> nextTimerID_{1};
};

} // namespace ts

#endif /* nrt_tempo_scheduler_hpp */
