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
//  nrt_scheduler.cpp
//  lang
//
//  NRT wall-clock scheduler implementation.
//

#include "nrt_scheduler.hpp"
#include "nrt_vm.hpp"

namespace ts {

namespace {
// Hack to walk the underlying container of a std::priority_queue without
// switching it to a manually-managed heap-vector. Subclassing exposes the
// protected `c` member via a pointer-to-member; we only ever read it.
template <class T, class S, class C>
struct PQContainerAccess : private std::priority_queue<T, S, C> {
    static S const& get(std::priority_queue<T, S, C> const& q) {
        return q.*&PQContainerAccess::c;
    }
};
} // anon

NRTScheduler::NRTScheduler(NRTVM* vm) : vm_(vm) {
    // Register as a GC root scanner so handlers sitting in the queue (or
    // popped-and-in-flight) stay reachable for the tracing collector.
    vm_->vm.addExtraRootScanner([this](TracingGC& gc) { markRoots(gc); });
}

void NRTScheduler::markRoots(TracingGC& gc) {
    std::lock_guard lock(schedMtx_);
    auto const& vec = PQContainerAccess<Entry,
                                        std::vector<Entry>,
                                        std::greater<Entry>>::get(queue_);
    for (auto const& e : vec) {
        if (e.handler) gc.mark(static_cast<GCObj*>(e.handler));
    }
    if (inFlightHandler_) gc.mark(static_cast<GCObj*>(inFlightHandler_));
}

NRTScheduler::~NRTScheduler() {
    stop();

    // Drop remaining entries. Handlers become collectable once the queue is
    // gone and nothing else holds them.
    std::lock_guard lock(schedMtx_);
    while (!queue_.empty()) queue_.pop();
    inFlightHandler_ = nullptr;
}

void NRTScheduler::start() {
    if (running_.load(std::memory_order_relaxed)) return;
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread(&NRTScheduler::run, this);
}

void NRTScheduler::stop() {
    if (!running_.load(std::memory_order_relaxed)) return;
    running_.store(false, std::memory_order_relaxed);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

i64 NRTScheduler::scheduleAfter(Duration dt, Obj* handler) {
    TimePoint base = vm_->logicalTime();
    if (base == TimePoint{}) base = Clock::now();
    return scheduleAt(base + std::chrono::duration_cast<Clock::duration>(dt),
                      handler);
}

i64 NRTScheduler::scheduleEvery(Duration interval, Obj* handler) {
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);

    Entry entry;
    TimePoint base = vm_->logicalTime();
    if (base == TimePoint{}) base = Clock::now();
    entry.logicalTime = base + std::chrono::duration_cast<Clock::duration>(interval);
    entry.handler = handler;
    entry.repeating = true;
    entry.interval = interval;
    entry.timerID = id;

    {
        std::lock_guard lock(schedMtx_);
        queue_.push(entry);
    }
    cv_.notify_one();
    return id;
}

i64 NRTScheduler::scheduleAt(TimePoint time, Obj* handler) {
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);

    Entry entry;
    entry.logicalTime = time;
    entry.handler = handler;
    entry.repeating = false;
    entry.interval = Duration(0);
    entry.timerID = id;

    {
        std::lock_guard lock(schedMtx_);
        queue_.push(entry);
    }
    cv_.notify_one();
    return id;
}

bool NRTScheduler::cancel(i64 timerID) {
    std::lock_guard lock(schedMtx_);
    // Rebuild the queue without the cancelled entry.
    // O(n) but cancellation is infrequent.
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> newQueue;
    bool found = false;
    while (!queue_.empty()) {
        auto entry = queue_.top();
        queue_.pop();
        if (entry.timerID == timerID) {
            found = true;
        } else {
            newQueue.push(entry);
        }
    }
    queue_ = std::move(newQueue);
    return found;
}

void NRTScheduler::run() {
    while (running_.load(std::memory_order_relaxed)) {
        std::unique_lock lock(schedMtx_);

        if (queue_.empty()) {
            cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_relaxed) || !queue_.empty();
            });
            if (!running_.load(std::memory_order_relaxed)) break;
            if (queue_.empty()) continue;
        }

        auto next = queue_.top();
        auto now = Clock::now();

        if (next.logicalTime > now) {
            cv_.wait_until(lock, next.logicalTime, [this] {
                return !running_.load(std::memory_order_relaxed);
            });
            if (!running_.load(std::memory_order_relaxed)) break;
            continue;
        }

        queue_.pop();
        // Park the popped handler in inFlightHandler_ so it stays a GC root
        // for the brief window between the queue pop and the call.
        inFlightHandler_ = next.handler;
        lock.unlock();

        // Set logical time so that after()/every() schedule relative to it
        vm_->setLogicalTime(next.logicalTime);

        // Call the handler on the scheduler thread (acquires VM mutex).
        {
            std::lock_guard vmLock(vm_->mtx);
            vm_->vm.makeCurrent();
            vm_->vm.callCallable(next.handler, nullptr, 0);
            vm_->vm.gcHeartbeat();
        }

        if (next.repeating && running_.load(std::memory_order_relaxed)) {
            next.logicalTime += std::chrono::duration_cast<Clock::duration>(next.interval);
            std::lock_guard lock2(schedMtx_);
            queue_.push(next);
            inFlightHandler_ = nullptr;
        } else {
            // One-shot: the handler is done; drop the in-flight reference so
            // it can be collected if nothing else references it.
            std::lock_guard lock2(schedMtx_);
            inFlightHandler_ = nullptr;
        }
    }
}

} // namespace ts
