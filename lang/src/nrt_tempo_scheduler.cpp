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

NRTTempoScheduler::NRTTempoScheduler(NRTVM* vm, f64 bpm, f64 latencySeconds)
    : vm_(vm)
    , ramp_(bpm / 60.0, 0., 0.)  // convert BPM to beats per second
    , epoch_(Clock::now())
    , latencySeconds_(latencySeconds)
{}

NRTTempoScheduler::~NRTTempoScheduler() {
    stop();

    // Release all remaining handler references under the VM lock.
    std::lock_guard lock(vm_->mtx);
    vm_->vm.makeCurrent();
    while (!queue_.empty()) {
        auto entry = queue_.top();
        queue_.pop();
        if (entry.handler) {
            entry.handler->release();
        }
    }
    vm_->vm.gcHeartbeat();
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

    handler->retain();

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
    std::lock_guard lock(schedMtx_);
    f64 now = elapsedSeconds();
    f64 currentBeat = ramp_.secondsToBeats(now);
    ramp_ = TempoRamp(beatsPerSecond, currentBeat, now);
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
            if (entry.handler) {
                std::lock_guard vmLock(vm_->mtx);
                vm_->vm.makeCurrent();
                entry.handler->release();
                vm_->vm.gcHeartbeat();
            }
        } else {
            newQueue.push(entry);
        }
    }
    queue_ = std::move(newQueue);
    return found;
}

bool NRTTempoScheduler::isIdle() const {
    std::lock_guard lock(schedMtx_);
    return queue_.empty();
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
        Entry next;
        {
            std::lock_guard lock(schedMtx_);
            if (queue_.empty()) return;
            next = queue_.top();
            // beatToFireTime returns the wall-clock time in seconds at which
            // this entry should fire (with latency compensation). In manual
            // mode, "wall-clock" is manualSeconds_.
            f64 fireSeconds = ramp_.beatsToSeconds(next.beatTime) - latencySeconds_;
            if (fireSeconds > manualSeconds_) return;
            queue_.pop();
        }

        logicalBeat_ = next.beatTime;

        if (next.handler == nullptr) {
            // Tempo-change event: install new ramp.
            f64 epochSec = ramp_.beatsToSeconds(next.beatTime);
            f64 currentTempo = ramp_.beatsToTempo(next.beatTime);
            std::lock_guard lock2(schedMtx_);
            ramp_ = TempoRamp(currentTempo, next.beatTime, epochSec,
                              next.targetTempo, next.rampBeats);
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
            } else {
                std::lock_guard vmLock(vm_->mtx);
                vm_->vm.makeCurrent();
                next.handler->release();
                vm_->vm.gcHeartbeat();
            }
        }
        logicalBeat_ = -1.;
    }
}

void NRTTempoScheduler::run() {
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
        auto fireTime = beatToFireTime(next.beatTime);
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

        queue_.pop();
        lock.unlock();

        // Set logical beat for relative scheduling from within handlers.
        // Reset to -1 after handler returns (signals "not in a callback").
        logicalBeat_ = next.beatTime;

        if (next.handler == nullptr) {
            // Tempo-change event: install new ramp
            f64 seconds = ramp_.beatsToSeconds(next.beatTime);
            f64 currentTempo = ramp_.beatsToTempo(next.beatTime);
            std::lock_guard lock2(schedMtx_);
            ramp_ = TempoRamp(currentTempo, next.beatTime, seconds,
                               next.targetTempo, next.rampBeats);
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
            } else {
                // One-shot: release the handler
                std::lock_guard vmLock(vm_->mtx);
                vm_->vm.makeCurrent();
                next.handler->release();
                vm_->vm.gcHeartbeat();
            }
        }

        logicalBeat_ = -1.;
    }
}

} // namespace ts
