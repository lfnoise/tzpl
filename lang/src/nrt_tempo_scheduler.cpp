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
    for (auto const& e : queue_) {
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
    queue_.clear();
    inFlightHandler_ = nullptr;
}

void NRTTempoScheduler::setEngineClockHook(EngineClockFn hook) {
    {
        std::lock_guard lock(schedMtx_);
        engineClock_ = std::move(hook);
    }
    queueChanged_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
}

f64 NRTTempoScheduler::beats(int slot) const {
    if (engineClock_) {
        f64 beatsNow = 0., secsUntil = 0.;
        if (engineClock_(slot, 0., beatsNow, secsUntil)) return beatsNow;
    }
    return ramp_.secondsToBeats(elapsedSeconds());
}

NRTTempoScheduler::TimePoint
NRTTempoScheduler::entryFireTime(Entry const& e, bool& engineSynced) const {
    if (engineClock_) {
        f64 beatsNow = 0., secsUntil = 0.;
        if (engineClock_(e.clockSlot, e.beatTime, beatsNow, secsUntil)) {
            engineSynced = true;
            f64 dt = secsUntil - latencySeconds_;
            if (dt < 0.) dt = 0.;
            return Clock::now() + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<f64>(dt));
        }
    }
    engineSynced = false;
    return beatToFireTime(e.beatTime);
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
    return beats(0);
}

f64 NRTTempoScheduler::beatDur() const {
    return 1.0 / tempo();
}

i64 NRTTempoScheduler::schedAbs(int slot, f64 beat, Obj* handler, bool oneShot) {
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);

    Entry entry{};
    entry.beatTime = beat;
    entry.clockSlot = slot;
    entry.handler = handler;
    entry.oneShot = oneShot;
    entry.timerID = id;

    {
        std::lock_guard lock(schedMtx_);
        queue_.push_back(entry);
    }
    queueChanged_.store(true, std::memory_order_relaxed);
    cv_.notify_one();
    return id;
}

i64 NRTTempoScheduler::sched(int slot, f64 deltaBeats, Obj* handler, bool oneShot) {
    // Schedule relative to the current logical beat: the handler's beat when
    // called from WITHIN a handler firing on the SAME slot (i.e. on the
    // firing thread), else the slot's current beat. The thread check matters:
    // logicalBeat_ is set while handlers run, and another thread scheduling
    // concurrently must not inherit that (possibly stale) beat.
    bool inHandler = firingThread_.load(std::memory_order_relaxed)
                         == std::this_thread::get_id()
                     && logicalBeat_ >= 0. && logicalSlot_ == slot;
    f64 base = inHandler ? logicalBeat_ : beats(slot);
    return schedAbs(slot, base + deltaBeats, handler, oneShot);
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

i64 NRTTempoScheduler::schedResolveFutureBeats(int slot, f64 deltaBeats, Future* fut) {
    if (!(deltaBeats > 0.)) deltaBeats = 0.;
    i64 id = nextTimerID_.fetch_add(1, std::memory_order_relaxed);

    Entry entry{};
    entry.handler = nullptr;
    entry.resolveFut = fut;
    entry.clockSlot = slot;
    entry.timerID = id;
    // Same base as sched(): the handler's beat when called from within a
    // clock callback on the same slot (on the firing thread), else the
    // slot's current beat.
    bool inHandler = firingThread_.load(std::memory_order_relaxed)
                         == std::this_thread::get_id()
                     && logicalBeat_ >= 0. && logicalSlot_ == slot;
    f64 base = inHandler ? logicalBeat_ : beats(slot);
    entry.beatTime = base + deltaBeats;

    {
        std::lock_guard lock(schedMtx_);
        queue_.push_back(entry);
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
        queue_.push_back(entry);
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
    for (size_t i = 0; i < queue_.size(); ++i) {
        if (queue_[i].timerID == timerID) {
            queue_.erase(queue_.begin() + (std::ptrdiff_t)i);
            return true;
        }
    }
    return false;
}

int NRTTempoScheduler::clearAll() {
    std::lock_guard lock(schedMtx_);
    // Awaitable delays survive the panic: wallQueue_ (delayReal) is untouched
    // and delayBeats entries (resolveFut) are kept when rebuilding the beat
    // queue. Dropping either would strand a thread parked in an await forever;
    // a pending wait makes no sound -- panic silences handlers, not awaits.
    std::vector<Entry> keep;
    int dropped = 0;
    for (Entry const& e : queue_) {
        if (e.resolveFut) keep.push_back(e); else ++dropped;
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
    firingThread_.store(std::this_thread::get_id(), std::memory_order_relaxed);

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
            size_t beatIdx = 0;
            for (size_t i = 0; i < queue_.size(); ++i) {
                // Beat -> logical-seconds fire time from the ENGINE clock
                // when the hook can read it (the render pump keeps
                // manualSeconds_ equal to the engine's seconds, so
                // manualSeconds_ + secsUntil is the entry's deadline on this
                // timeline and engine-side setTempo/ramps are honored), else
                // from the internal ramp as before.
                f64 fs;
                bool synced = false;
                if (engineClock_) {
                    f64 beatsNow = 0., secsUntil = 0.;
                    if (engineClock_(queue_[i].clockSlot, queue_[i].beatTime,
                                     beatsNow, secsUntil)) {
                        fs = manualSeconds_ + secsUntil - latencySeconds_;
                        synced = true;
                    }
                }
                if (!synced) {
                    fs = ramp_.beatsToSeconds(queue_[i].beatTime) - latencySeconds_;
                }
                if (i == 0 || fs < beatFire) { beatFire = fs; beatIdx = i; }
            }
            if (!queue_.empty() && beatFire <= manualSeconds_) {
                next = queue_[beatIdx];
                haveBeat = true;
            }

            if (haveWall && (!haveBeat || wallFire <= beatFire)) {
                wallFut = wallQueue_[wallIdx].fut;
                wallQueue_.erase(wallQueue_.begin() + (std::ptrdiff_t)wallIdx);
                fireWall = true;
            } else if (haveBeat) {
                queue_.erase(queue_.begin() + (std::ptrdiff_t)beatIdx);
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
        logicalSlot_ = next.clockSlot;

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
            if (!next.oneShot && result.f > 0. && std::isfinite(result.f)) {
                next.beatTime += result.f;
                std::lock_guard lock2(schedMtx_);
                queue_.push_back(next);
                inFlightHandler_ = nullptr;
            } else {
                std::lock_guard lock2(schedMtx_);
                inFlightHandler_ = nullptr;
            }
        }
        logicalBeat_ = -1.;
        logicalSlot_ = -1;
    }
}

void NRTTempoScheduler::run() {
    firingThread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
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

        // Earliest pending deadline across both queues. Beat entries are
        // estimated per pass -- from the engine clock's slot state when the
        // hook can read it, else from the internal ramp -- so every wake
        // re-targets after tempo changes; wall-clock delay deadlines are
        // fixed TimePoints nothing can move.
        size_t wallIdx = 0;
        bool haveWall = !wallQueue_.empty();
        for (size_t i = 1; i < wallQueue_.size(); ++i) {
            if (wallQueue_[i].deadlineSeconds < wallQueue_[wallIdx].deadlineSeconds)
                wallIdx = i;
        }
        size_t beatIdx = 0;
        bool anyEngineSynced = false;
        TimePoint beatFire{};
        for (size_t i = 0; i < queue_.size(); ++i) {
            bool es = false;
            TimePoint ft = entryFireTime(queue_[i], es);
            anyEngineSynced = anyEngineSynced || es;
            if (i == 0 || ft < beatFire) { beatFire = ft; beatIdx = i; }
        }
        bool wallFirst = false;
        TimePoint fireTime{};
        if (!queue_.empty()) fireTime = beatFire;
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
            // Engine-synced estimates go stale across engine-side scheduled
            // tempo changes (which never notify this thread), so cap the
            // sleep and re-derive from the live clock on each wake.
            TimePoint sleepUntil = fireTime;
            if (anyEngineSynced) {
                TimePoint cap = now + std::chrono::milliseconds(50);
                if (cap < sleepUntil) sleepUntil = cap;
            }
            cv_.wait_until(lock, sleepUntil, [this] {
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

        Entry next = queue_[beatIdx];
        queue_.erase(queue_.begin() + (std::ptrdiff_t)beatIdx);
        // Keep the popped handler reachable for GC across the call.
        inFlightHandler_ = next.handler;
        lock.unlock();

        // Set logical beat/slot for relative scheduling from within handlers.
        // Reset after the handler returns (signals "not in a callback").
        logicalBeat_ = next.beatTime;
        logicalSlot_ = next.clockSlot;

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
            if (!next.oneShot && result.f > 0. && std::isfinite(result.f)) {
                next.beatTime += result.f;
                std::lock_guard lock2(schedMtx_);
                queue_.push_back(next);
                inFlightHandler_ = nullptr;
            } else {
                std::lock_guard lock2(schedMtx_);
                inFlightHandler_ = nullptr;
            }
        }

        logicalBeat_ = -1.;
        logicalSlot_ = -1;
    }
}

} // namespace ts
