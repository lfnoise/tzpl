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
//  tzpl_rt_tempo_scheduler.cpp
//  bridge
//
//  RT tempo scheduler implementation.
//

#include "tzpl_rt_tempo_scheduler.hpp"
#include "value.hpp"

namespace bridge {

RTTempoScheduler::RTTempoScheduler(f64 bpm, f64 sampleRate, i64 startSample)
    : ramp_(bpm / 60.0, 0., 0.)  // BPM -> beats per second
    , sampleRate_(sampleRate)
    , epochSample_(startSample)
{
    // Initialize free list from pool
    for (int i = 0; i < kMaxEntries - 1; ++i) {
        pool_[i].next_ = &pool_[i + 1];
    }
    pool_[kMaxEntries - 1].next_ = nullptr;
    freeList_ = &pool_[0];
}

RTSchedEntry* RTTempoScheduler::allocEntry() {
    if (!freeList_) return nullptr;
    auto* e = freeList_;
    freeList_ = e->next_;
    e->next_ = nullptr;
    e->prev_ = nullptr;
    return e;
}

void RTTempoScheduler::freeEntry(RTSchedEntry* entry) {
    entry->handler = nullptr;
    entry->next_ = freeList_;
    freeList_ = entry;
}

void RTTempoScheduler::insertSorted(RTSchedEntry* entry) {
    // Insert into doubly-linked list sorted by beatTime (ascending).
    // Search backwards from tail since new entries often go near the end.
    if (!tail_) {
        head_ = tail_ = entry;
        entry->prev_ = entry->next_ = nullptr;
        return;
    }

    RTSchedEntry* pos = tail_;
    while (pos && pos->beatTime > entry->beatTime) {
        pos = pos->prev_;
    }

    if (pos) {
        // Insert after pos
        entry->next_ = pos->next_;
        entry->prev_ = pos;
        if (pos->next_) pos->next_->prev_ = entry;
        else tail_ = entry;
        pos->next_ = entry;
    } else {
        // Insert at head
        entry->next_ = head_;
        entry->prev_ = nullptr;
        head_->prev_ = entry;
        head_ = entry;
    }
}

void RTTempoScheduler::removeFromList(RTSchedEntry* entry) {
    if (entry->prev_) entry->prev_->next_ = entry->next_;
    else head_ = entry->next_;

    if (entry->next_) entry->next_->prev_ = entry->prev_;
    else tail_ = entry->prev_;

    entry->next_ = entry->prev_ = nullptr;
}

i64 RTTempoScheduler::addEntry(f64 beatTime, ts::Obj* handler) {
    auto* entry = allocEntry();
    if (!entry) return -1;

    entry->beatTime = beatTime;
    entry->handler = handler;
    if (handler) handler->retain();
    entry->timerID = nextTimerID_++;

    insertSorted(entry);
    return entry->timerID;
}

i64 RTTempoScheduler::addTempoChange(f64 beatTime, f64 targetTempoBPS,
                                       f64 rampBeats) {
    auto* entry = allocEntry();
    if (!entry) return -1;

    entry->beatTime = beatTime;
    entry->handler = nullptr;  // signals tempo change
    entry->targetTempo = targetTempoBPS;
    entry->rampBeats = rampBeats;
    entry->timerID = nextTimerID_++;

    insertSorted(entry);
    return entry->timerID;
}

bool RTTempoScheduler::cancel(i64 timerID) {
    for (auto* e = head_; e; e = e->next_) {
        if (e->timerID == timerID) {
            removeFromList(e);
            if (e->handler) e->handler->release();
            freeEntry(e);
            return true;
        }
    }
    return false;
}

void RTTempoScheduler::processSample(i64 sampleTime, ts::VM* vm) {
    f64 currentSeconds = samplesToSeconds(sampleTime);
    f64 currentBeat = ramp_.secondsToBeats(currentSeconds);

    while (head_ && head_->beatTime <= currentBeat) {
        RTSchedEntry* entry = head_;
        removeFromList(entry);

        if (entry->handler == nullptr) {
            // Tempo change event
            f64 seconds = ramp_.beatsToSeconds(entry->beatTime);
            f64 currentTempo = ramp_.beatsToTempo(entry->beatTime);
            ramp_ = ts::TempoRamp(currentTempo, entry->beatTime, seconds,
                                   entry->targetTempo, entry->rampBeats);
            freeEntry(entry);
        } else {
            // User handler
            vm->makeCurrent();
            ts::Word result = vm->callCallable(entry->handler, nullptr, 0);
            vm->gcHeartbeat();

            // Reschedule if handler returned a positive finite number
            if (result.f > 0. && std::isfinite(result.f)) {
                entry->beatTime += result.f;
                insertSorted(entry);
            } else {
                entry->handler->release();
                freeEntry(entry);
            }
        }
    }
}

void RTTempoScheduler::processSampleCallback(void* scheduler, i64 sampleTime,
                                               void* vm) {
    static_cast<RTTempoScheduler*>(scheduler)->processSample(
        sampleTime, static_cast<ts::VM*>(vm));
}

void RTTempoScheduler::setTempo(f64 beatsPerSecond, i64 currentSample) {
    f64 seconds = samplesToSeconds(currentSample);
    f64 currentBeat = ramp_.secondsToBeats(seconds);
    ramp_ = ts::TempoRamp(beatsPerSecond, currentBeat, seconds);
}

f64 RTTempoScheduler::tempo(i64 sampleTime) const {
    return ramp_.secondsToTempo(samplesToSeconds(sampleTime));
}

f64 RTTempoScheduler::beats(i64 sampleTime) const {
    return ramp_.secondsToBeats(samplesToSeconds(sampleTime));
}

} // namespace bridge
