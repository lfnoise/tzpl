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
#include "tracing_gc.hpp"
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
        int clockSlot = 0;       // engine TempoClock slot this beat lives on
        bool oneShot = false;    // after()/at(): never reschedule, whatever
                                 // the (Void) handler's return register holds
        Obj* handler;            // Callable (Lambda or Primitive); kept
                                 // reachable by markRoots() while queued.
                                 // nullptr for internal tempo-change events.
        i64 timerID;

        // For tempo-change events (handler == nullptr)
        f64 targetTempo;         // end tempo (beats per second)
        f64 rampBeats;           // duration of ramp in beats (0 = instant)

        // For musical awaitable delays (delayBeats): the external Future to
        // resolve when this beat arrives. Rides the beat queue ON PURPOSE --
        // unlike a wall-clock delayReal deadline, a musical delay must move
        // with tempo changes and ramps. Checked BEFORE the handler==nullptr
        // tempo-change test. Rooted by the VM's external-future list and by
        // markRoots while queued; clearAll() keeps these entries (dropping
        // one would strand a parked await).
        Future* resolveFut = nullptr;

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

    // GC root scanner; registered with the VM at construction.
    void markRoots(TracingGC& gc);

    // Schedule a handler at an absolute beat.
    i64 schedAbs(int slot, f64 beat, Obj* handler, bool oneShot = false);

    // Schedule a handler relative to the current logical beat of `slot`.
    i64 sched(int slot, f64 deltaBeats, Obj* handler, bool oneShot = false);

    // --- Engine clock following -------------------------------------------
    // When the host installs this hook, beat entries are slaved to the
    // ENGINE's TempoClock slots: the hook reports slot `slot`'s current beat
    // and the seconds until it reaches `targetBeat` (under its current ramp),
    // returning false when the clock cannot advance (no engine, audio not
    // running) -- entries then fall back to the internal ramp. The run loop
    // derives wall deadlines from the hook and re-checks on a capped sleep,
    // so engine-side tempo changes and ramps (scheduled from anywhere, any
    // slot) move NRT callbacks exactly as they move engine commands, minus
    // the scheduling latency.
    using EngineClockFn = std::function<bool(int slot, f64 targetBeat,
                                             f64& beatsNow, f64& secsUntil)>;
    void setEngineClockHook(EngineClockFn hook);

    // Current beat of `slot`: the engine clock when the hook can read it,
    // else the internal ramp.
    f64 beats(int slot) const;

    // Schedule the resolution of an external (registered) Future a number of
    // WALL-CLOCK seconds from now -- the engine of the awaitable delayReal().
    // Deadlines live in their own queue in seconds-since-epoch, never in
    // beats, so tempo changes during the wait cannot move them (the run loop
    // recomputes its sleep from both queues on every wake). No scheduling
    // latency is applied: the future resolves at the actual deadline. In
    // manual mode "wall-clock" is the logical tickTo() clock.
    i64 schedResolveFutureSecs(f64 seconds, Future* fut);

    // Schedule the resolution of an external (registered) Future a number of
    // BEATS from the current logical beat of `slot` -- the engine of the
    // awaitable delayBeats(). The entry rides the beat queue like a sched()
    // handler: tempo changes and ramps move it, and it fires latency-early
    // exactly as handlers do (so code resumed by the await can submit engine
    // commands that land on the beat). Like sched(), calling from within a
    // handler on the same slot schedules relative to the handler's beat.
    i64 schedResolveFutureBeats(int slot, f64 deltaBeats, Future* fut);

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

    // Panic: drop every queued entry -- handlers (players, sched()
    // callbacks) and scheduled tempo changes alike. An in-flight handler
    // finishes on its own; anything it schedules afterwards lands in the
    // now-empty queue. Returns the number of entries dropped.
    int clearAll();

    // Current state (thread-safe reads)
    f64 tempo() const;     // current tempo in beats per second
    f64 tempoBPM() const;  // current tempo in BPM
    f64 beats() const;     // current beat position
    f64 beatDur() const;   // duration of one beat in seconds

    // The current logical beat (set before calling a handler, used by
    // sched() for relative scheduling).
    f64 logicalBeat() const { return logicalBeat_; }
    int logicalSlot() const { return logicalSlot_; }

    f64 latency() const { return latencySeconds_; }
    void setLatency(f64 seconds) { latencySeconds_ = seconds; }

    // --- Manual mode (for NRT/offline rendering) ---
    // In manual mode the scheduler does NOT spawn a worker thread; the host
    // drives it by calling tickTo(seconds) to advance the logical clock and
    // fire any due handlers synchronously on the calling thread. Wall-clock
    // queries (beats(), tempo()) return values relative to manualSeconds_
    // instead of std::chrono::steady_clock.
    //
    // Must be called BEFORE start(). Once in manual mode, start() is a no-op.
    void setManualMode(bool on) { manualMode_ = on; }
    bool isManualMode() const { return manualMode_; }

    // Advance the logical clock to `seconds` (since epoch) and fire any
    // entries whose fire-time has come. Manual mode only; no-op otherwise.
    void tickTo(f64 seconds);

    // True iff the scheduler queue holds no pending entries. Useful for the
    // NRT renderer to detect "no more work to drive" idle conditions.
    bool isIdle() const;

private:
    void run();

    // Convert a beat time to a wall-clock time point, accounting for latency.
    TimePoint beatToFireTime(f64 beat) const;

    // Convert wall-clock now to seconds since epoch.
    f64 elapsedSeconds() const;

    // Wall-clock delay entries (delayReal): deadline in seconds since epoch_
    // (elapsedSeconds() units, so manual mode works too). Kept OUT of the
    // beat queue: these fire at their wall deadline regardless of tempo, and
    // clearAll() (panic) leaves them alone -- dropping one would strand a
    // thread parked in `await delayReal` forever. Unsorted vector, min-scanned
    // (delays in flight are few). The futures are rooted by the VM's
    // external-future list and by markRoots.
    struct WallEntry {
        f64 deadlineSeconds;
        Future* fut;
        i64 timerID;
    };

    NRTVM* vm_;
    TempoRamp ramp_;
    TimePoint epoch_;          // wall-clock time corresponding to ramp start
    f64 latencySeconds_;
    f64 logicalBeat_ = -1.;   // current logical beat (-1 = not in a handler callback)

    // Estimated wall fire time of a beat entry (schedMtx_ held). Sets
    // `engineSynced` when the estimate came from the engine clock hook --
    // those estimates go stale across engine-side scheduled tempo changes,
    // so the run loop caps its sleep and re-checks.
    TimePoint entryFireTime(Entry const& e, bool& engineSynced) const;

    mutable std::mutex schedMtx_;
    std::condition_variable cv_;
    // Beat entries, min-scanned by estimated fire time each pass. A plain
    // vector rather than a priority queue: entries on different engine clock
    // slots are not comparable by beat, only by estimated wall time.
    std::vector<Entry> queue_;
    std::vector<WallEntry> wallQueue_;  // wall-clock delay deadlines (see WallEntry)
    EngineClockFn engineClock_;         // see setEngineClockHook
    int logicalSlot_ = -1;              // slot of the handler being fired
    // Thread currently firing handlers (the run loop, or a tickTo caller).
    // logicalBeat_/logicalSlot_ are only meaningful ON that thread; sched()
    // from any other thread must base itself on the clock, never on a
    // handler's beat it happens to observe mid-fire.
    std::atomic<std::thread::id> firingThread_{};
    // Handler popped from the queue but not yet fired (or currently firing).
    // Guarded by schedMtx_; the run loop sets this before releasing schedMtx_
    // and clears it after firing returns.
    Obj* inFlightHandler_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> queueChanged_{false};  // wakes wait_until when new entries arrive
    std::atomic<i64> nextTimerID_{1};

    // Manual-mode state (NRT rendering). When manualMode_ is true, the
    // scheduler does not run its own thread; tickTo() drives it from the
    // renderer thread.
    bool manualMode_ = false;
    f64 manualSeconds_ = 0.0;
};

} // namespace ts

#endif /* nrt_tempo_scheduler_hpp */
