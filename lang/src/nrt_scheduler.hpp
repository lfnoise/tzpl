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
//  nrt_scheduler.hpp
//  lang
//
//  Wall-clock NRT scheduler with logical time for drift-free scheduling.
//  Runs on its own thread. Fires timed events by calling into the NRT VM
//  (acquires the VM mutex on the scheduler thread).
//

#ifndef nrt_scheduler_hpp
#define nrt_scheduler_hpp

#include "vm.hpp"
#include "tracing_gc.hpp"
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <chrono>
#include <atomic>

namespace ts {

struct NRTVM;

class NRTScheduler {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::duration<double>;

    struct Entry {
        TimePoint logicalTime;     // when this event should logically occur
        Obj* handler;              // Callable (Lambda or Primitive); kept
                                   // reachable by the scheduler's markRoots()
        bool repeating;
        Duration interval;
        i64 timerID;               // unique ID for cancellation

        // Priority queue: earliest first
        bool operator>(const Entry& other) const {
            return logicalTime > other.logicalTime;
        }
    };

    // GC root scanner. Registered with the VM at construction; called once
    // per mark cycle from the tracing-GC's extra-roots substate. Walks the
    // queue under schedMtx_ plus the in-flight slot.
    void markRoots(TracingGC& gc);

    explicit NRTScheduler(NRTVM* vm);
    ~NRTScheduler();

    // Start/stop the scheduler thread
    void start();
    void stop();

    // Schedule a one-shot event at logicalTime + dt.
    // The handler stays a GC root via markRoots() while it sits in the queue.
    i64 scheduleAfter(Duration dt, Obj* handler);

    // Schedule a repeating event.
    i64 scheduleEvery(Duration interval, Obj* handler);

    // Schedule at an absolute logical time.
    i64 scheduleAt(TimePoint time, Obj* handler);

    // Cancel a timer by ID. Returns true if found and cancelled.
    bool cancel(i64 timerID);

private:
    void run();

    NRTVM* vm_;
    std::mutex schedMtx_;
    std::condition_variable cv_;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue_;
    // Handler popped from the queue but not yet fired (or currently firing).
    // Guarded by schedMtx_; the run loop sets this before releasing schedMtx_
    // and clears it after firing returns. markRoots() also marks this slot
    // so the in-flight handler can't be swept mid-firing.
    Obj* inFlightHandler_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<i64> nextTimerID_{1};
};

} // namespace ts

#endif /* nrt_scheduler_hpp */
