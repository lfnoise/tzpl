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
//  nrt_tempo_scheduler.cpp
//  lang
//
//  Tempo-based NRT scheduler implementation.
//

#include "nrt_tempo_scheduler.hpp"
#include "nrt_vm.hpp"

namespace ts {

namespace {
// Walk the protected `c` member of std::priority_queue without changing the
// queue's type. See identical helper in nrt_scheduler.cpp.
template <class T, class S, class C>
struct PQContainerAccess : private std::priority_queue<T, S, C> {
    static S const& get(std::priority_queue<T, S, C> const& q) {
        return q.*&PQContainerAccess::c;
    }
};
} // anon

NRTTempoScheduler::NRTTempoScheduler(NRTVM* vm, f64 bpm, f64 latencySeconds)
    : vm_(vm)
    , ramp_(bpm / 60.0, 0., 0.)  // convert BPM to beats per second
    , epoch_(Clock::now())
    , latencySeconds_(latencySeconds)
{
    vm_->vm.addExtraRootScanner([this](TracingGC& gc) { markRoots(gc); });
}

void NRTTempoScheduler::markRoots(TracingGC& gc) {
    std::lock_guard lock(schedMtx_);
    auto const& vec = PQContainerAccess<Entry,
                                        std::vector<Entry>,
                                        std::greater<Entry>>::get(queue_);
    for (auto const& e : vec) {
        if (e.handler) gc.mark(static_cast<GCObj*>(e.handler));
        if (e.resolveFut) gc.mark(reinterpret_cast<GCObj*>(e.resolveFut));
    }
    for (auto const& w : wallQueue_) {
        if (w.fut) gc.mark(reinterpret_cast<GCObj*>(w.fut));
    }
    if (inFlightHandler_) gc.mark(static_cast<GCObj*>(inFlightHandler_));
}

NRTTempoScheduler::~NRTTempoScheduler() {
    stop();
    std::lock_guard lock(schedMtx_);
    while (!queue_.empty()) queue_.pop();
    inFlightHandler_ = nullptr;
}

void NRTTempoScheduler::start() {
    if (running_.load(std::memory_order_relaxed)) return;
    if (manualMode_) {
        // Manual mode: no worker thread. Mark as running so stop()/destructor
        // bookkeeping is consistent.
        running_.store(true, std::memory_order_relaxed);
        return;
    }
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread(&NRTTempoScheduler::run, this);
}

void NRTTempoScheduler::stop() {
    if (!running_.load(std::memory_order_relaxed)) return;
    running_.store(false, std::memory_order_relaxed);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

f64 NRTTempoScheduler::elapsedSeconds() const {
    if (manualMode_) {
        return manualSeconds_;
    }
    auto now = Clock::now();
    return std::chrono::duration<f64>(now - epoch_).count();
}

NRTTempoScheduler::TimePoint NRTTempoScheduler::beatToFireTime(f64 beat) const {
    f64 seconds = ramp_.beatsToSeconds(beat);
    // Fire early by latency so commands reach RT in time.
    seconds -= latencySeconds_;
    return epoch_ + std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<f64>(seconds));
}

f64 NRTTempoScheduler::tempo() const {
    return ramp_.secondsToTempo(elapsedSeconds());
}

f64 NRTTempoScheduler::tempoBPM() const {
    return tempo() * 60.0;
}

f64 NRTTempoScheduler::beats() const {
    return ramp_.secondsToBeats(elapsedSeconds());
}

f64 NRTTempoScheduler::beatDur() const {
    return 1.0 / tempo();
}

i64 NRTTempoScheduler::schedAbs(f64 beat, Obj* handler) {
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);


    Entry entry{};
    entry.beatTime = beat;
    entry.handler = handler;
    entry.timerID = id;

    {
        std::lock_guard lock(schedMtx_);
        queue_.push(entry);
    }
    queueChanged_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
    return id;
}

i64 NRTTempoScheduler::sched(f64 deltaBeats, Obj* handler) {
    // Schedule relative to the current logical beat.
    // If called from within a handler, logicalBeat_ is the handler's beat.
    // Otherwise, use the current wall-clock beat.
    f64 base = (logicalBeat_ >= 0.) ? logicalBeat_ : beats();
    return schedAbs(base + deltaBeats, handler);
}

i64 NRTTempoScheduler::schedResolveFutureSecs(f64 seconds, Future* fut) {
    if (!(seconds > 0.)) seconds = 0.;
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard lock(schedMtx_);
        // Deadline in seconds since epoch: tempo-independent, no scheduling
        // latency -- the future resolves at the actual wall deadline.
        wallQueue_.push_back(WallEntry{elapsedSeconds() + seconds, fut, id});
    }
    queueChanged_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
    return id;
}

i64 NRTTempoScheduler::schedResolveFutureBeats(f64 deltaBeats, Future* fut) {
    if (!(deltaBeats > 0.)) deltaBeats = 0.;
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);

    Entry entry{};
    entry.handler = nullptr;
    entry.resolveFut = fut;
    entry.timerID = id;
    // Same base as sched(): the handler's beat when called from within a
    // clock callback, else the current wall-clock beat.
    f64 base = (logicalBeat_ >= 0.) ? logicalBeat_ : beats();
    entry.beatTime = base + deltaBeats;

    {
        std::lock_guard lock(schedMtx_);
        queue_.push(entry);
    }
    queueChanged_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
    return id;
}

i64 NRTTempoScheduler::schedTempoChange(f64 beat, f64 targetTempo,
                                          f64 rampBeats) {
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);

    Entry entry{};
    entry.beatTime = beat;
    entry.handler = nullptr;  // signals tempo-change event
    entry.timerID = id;
    entry.targetTempo = targetTempo;
    entry.rampBeats = rampBeats;

    {
        std::lock_guard lock(schedMtx_);
        queue_.push(entry);
    }
    queueChanged_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
    return id;
}

void NRTTempoScheduler::setTempo(f64 beatsPerSecond) {
    {
        std::lock_guard lock(schedMtx_);
        f64 now = elapsedSeconds();
        f64 currentBeat = ramp_.secondsToBeats(now);
        ramp_ = TempoRamp(beatsPerSecond, currentBeat, now);
    }
    // Pass the run loop's wake predicate so it recomputes its sleep deadline
    // from the NEW ramp -- without this, a sped-up tempo would leave the loop
    // sleeping toward the old (later) fire time and fire entries late.
    queueChanged_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
}

void NRTTempoScheduler::setTempoBPM(f64 bpm) {
    setTempo(bpm / 60.0);
}

i64 NRTTempoScheduler::schedTempoChangeBPM(f64 beat, f64 targetBPM,
                                             f64 rampBeats) {
    return schedTempoChange(beat, targetBPM / 60.0, rampBeats);
}

bool NRTTempoScheduler::cancel(i64 timerID) {
    std::lock_guard lock(schedMtx_);
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

int NRTTempoScheduler::clearAll() {
    std::lock_guard lock(schedMtx_);
    // Awaitable delays survive the panic: wallQueue_ (delayReal) is untouched
    // and delayBeats entries (resolveFut) are kept when rebuilding the beat
    // queue. Dropping either would strand a thread parked in an await forever;
    // a pending wait makes no sound -- panic silences handlers, not awaits.
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> keep;
    int dropped = 0;
    while (!queue_.empty()) {
        Entry e = queue_.top();
        queue_.pop();
        if (e.resolveFut) keep.push(e); else ++dropped;
    }
    queue_ = std::move(keep);
    // Wake the run loop so it re-evaluates the rebuilt queue instead of
    // sleeping toward an entry that no longer exists.
    queueChanged_.store(true);
    cv_.notify_all();
    return dropped;
}

bool NRTTempoScheduler::isIdle() const {
    std::lock_guard lock(schedMtx_);
    // A pending delayReal deadline is work: in manual mode the renderer must
    // keep ticking so the await inside the render can resolve.
    return queue_.empty() && wallQueue_.empty();
}

void NRTTempoScheduler::tickTo(f64 seconds) {
    if (!manualMode_) return;

    // Advance the logical clock first so handlers reading beats()/getStreamTime
    // see the new "now" before they execute.
    if (seconds > manualSeconds_) {
        manualSeconds_ = seconds;
    }

    // Drain any entries whose fire-time has come. Loop because handlers can
    // schedule new entries (e.g. SuperCollider-style reschedule via a positive
    // return value) that may also be due immediately.
    while (true) {
        // Pick the earliest due event across the wall-clock delay queue and
        // the beat queue. Beat entries fire latency-early (their job is to
        // send engine commands ahead of their beat); wall-clock delay
        // deadlines fire exactly, in logical seconds, independent of the
        // tempo ramp. In manual mode, "wall-clock" is manualSeconds_.
        bool fireWall = false;
        Future* wallFut = nullptr;
        Entry next{};
        {
            std::lock_guard lock(schedMtx_);

            size_t wallIdx = 0;
            bool haveWall = !wallQueue_.empty();
            for (size_t i = 1; i < wallQueue_.size(); ++i) {
                if (wallQueue_[i].deadlineSeconds < wallQueue_[wallIdx].deadlineSeconds)
                    wallIdx = i;
            }
            f64 wallFire = haveWall ? wallQueue_[wallIdx].deadlineSeconds : 0.;
            if (haveWall && wallFire > manualSeconds_) haveWall = false;

            bool haveBeat = false;
            f64 beatFire = 0.;
            if (!queue_.empty()) {
                next = queue_.top();
                beatFire = ramp_.beatsToSeconds(next.beatTime) - latencySeconds_;
                haveBeat = beatFire <= manualSeconds_;
            }

            if (haveWall && (!haveBeat || wallFire <= beatFire)) {
                wallFut = wallQueue_[wallIdx].fut;
                wallQueue_.erase(wallQueue_.begin() + (std::ptrdiff_t)wallIdx);
                fireWall = true;
            } else if (haveBeat) {
                queue_.pop();
                inFlightHandler_ = next.handler;
            } else {
                return;   // nothing due
            }
        }

        if (fireWall) {
            // Wall-clock delay (delayReal): resolve under the VM mutex and
            // wake any thread parked in a top-level await.
            std::lock_guard vmLock(vm_->mtx);
            vm_->vm.makeCurrent();
            vm_->vm.resolveExternalFuture(wallFut);
            vm_->cv.notify_all();
            continue;
        }

        logicalBeat_ = next.beatTime;

        if (next.resolveFut) {
            // Musical delay (delayBeats): resolve under the VM mutex and wake
            // any thread parked in a top-level await.
            {
                std::lock_guard vmLock(vm_->mtx);
                vm_->vm.makeCurrent();
                vm_->vm.resolveExternalFuture(next.resolveFut);
                vm_->cv.notify_all();
            }
            std::lock_guard lock2(schedMtx_);
            inFlightHandler_ = nullptr;
        } else if (next.handler == nullptr) {
            // Tempo-change event: install new ramp.
            f64 epochSec = ramp_.beatsToSeconds(next.beatTime);
            f64 currentTempo = ramp_.beatsToTempo(next.beatTime);
            std::lock_guard lock2(schedMtx_);
            ramp_ = TempoRamp(currentTempo, next.beatTime, epochSec,
                              next.targetTempo, next.rampBeats);
            inFlightHandler_ = nullptr;
        } else {
            Word result;
            {
                std::lock_guard vmLock(vm_->mtx);
                vm_->vm.makeCurrent();
                result = vm_->vm.callCallable(next.handler, nullptr, 0);
                vm_->vm.gcHeartbeat();
            }
            if (result.f > 0. && std::isfinite(result.f)) {
                next.beatTime += result.f;
                std::lock_guard lock2(schedMtx_);
                queue_.push(next);
                inFlightHandler_ = nullptr;
            } else {
                std::lock_guard lock2(schedMtx_);
                inFlightHandler_ = nullptr;
            }
        }
        logicalBeat_ = -1.;
    }
}

void NRTTempoScheduler::run() {
    while (running_.load(std::memory_order_relaxed)) {
        std::unique_lock lock(schedMtx_);

        if (queue_.empty() && wallQueue_.empty()) {
            cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_relaxed)
                    || !queue_.empty() || !wallQueue_.empty();
            });
            if (!running_.load(std::memory_order_relaxed)) break;
            continue;
        }

        // Earliest pending deadline across both queues. Beat entries convert
        // through the CURRENT ramp on every pass (a tempo change notifies the
        // cv below, so the sleep re-targets); wall-clock delay deadlines are
        // fixed TimePoints a tempo change cannot move.
        size_t wallIdx = 0;
        bool haveWall = !wallQueue_.empty();
        for (size_t i = 1; i < wallQueue_.size(); ++i) {
            if (wallQueue_[i].deadlineSeconds < wallQueue_[wallIdx].deadlineSeconds)
                wallIdx = i;
        }
        bool wallFirst = false;
        TimePoint fireTime{};
        if (!queue_.empty()) {
            fireTime = beatToFireTime(queue_.top().beatTime);
        }
        if (haveWall) {
            TimePoint wallFire = epoch_ + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<f64>(wallQueue_[wallIdx].deadlineSeconds));
            if (queue_.empty() || wallFire < fireTime) {
                fireTime = wallFire;
                wallFirst = true;
            }
        }
        auto now = Clock::now();

        if (fireTime > now) {
            cv_.wait_until(lock, fireTime, [this] {
                return !running_.load(std::memory_order_relaxed)
                    || queueChanged_.load(std::memory_order_relaxed);
            });
            queueChanged_.store(false, std::memory_order_relaxed);
            if (!running_.load(std::memory_order_relaxed)) break;
            continue;
        }

        if (wallFirst) {
            // Wall-clock delay (delayReal): resolve the external future under
            // the VM mutex and wake a thread parked in a top-level await (the
            // host-wait hook sleeps on the NRTVM cv).
            Future* fut = wallQueue_[wallIdx].fut;
            wallQueue_.erase(wallQueue_.begin() + (std::ptrdiff_t)wallIdx);
            lock.unlock();
            std::lock_guard vmLock(vm_->mtx);
            vm_->vm.makeCurrent();
            vm_->vm.resolveExternalFuture(fut);
            vm_->cv.notify_all();
            continue;
        }

        auto next = queue_.top();
        queue_.pop();
        // Keep the popped handler reachable for GC across the call.
        inFlightHandler_ = next.handler;
        lock.unlock();

        // Set logical beat for relative scheduling from within handlers.
        // Reset to -1 after handler returns (signals "not in a callback").
        logicalBeat_ = next.beatTime;

        if (next.resolveFut) {
            // Musical delay (delayBeats): resolve the external future under
            // the VM mutex and wake a thread parked in a top-level await.
            {
                std::lock_guard vmLock(vm_->mtx);
                vm_->vm.makeCurrent();
                vm_->vm.resolveExternalFuture(next.resolveFut);
                vm_->cv.notify_all();
            }
            std::lock_guard lock2(schedMtx_);
            inFlightHandler_ = nullptr;
        } else if (next.handler == nullptr) {
            // Tempo-change event: install new ramp
            f64 seconds = ramp_.beatsToSeconds(next.beatTime);
            f64 currentTempo = ramp_.beatsToTempo(next.beatTime);
            std::lock_guard lock2(schedMtx_);
            ramp_ = TempoRamp(currentTempo, next.beatTime, seconds,
                               next.targetTempo, next.rampBeats);
            inFlightHandler_ = nullptr;
        } else {
            // User handler: call under VM mutex
            Word result;
            {
                std::lock_guard vmLock(vm_->mtx);
                vm_->vm.makeCurrent();
                result = vm_->vm.callCallable(next.handler, nullptr, 0);
                vm_->vm.gcHeartbeat();
            }

            // SuperCollider convention: if the handler returns a number,
            // reschedule after that many beats.
            if (result.f > 0. && std::isfinite(result.f)) {
                next.beatTime += result.f;
                std::lock_guard lock2(schedMtx_);
                queue_.push(next);
                inFlightHandler_ = nullptr;
            } else {
                std::lock_guard lock2(schedMtx_);
                inFlightHandler_ = nullptr;
            }
        }

        logicalBeat_ = -1.;
    }
}

} // namespace ts
