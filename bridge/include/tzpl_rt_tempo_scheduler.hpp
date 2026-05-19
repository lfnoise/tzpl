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
//  tzpl_rt_tempo_scheduler.hpp
//  bridge
//
//  Real-time tempo-based scheduler. Lives on a Silo's RT thread.
//  Uses sample time as the time base -- no latency compensation needed.
//  Events are scheduled by beat position using a TempoRamp for conversion.
//
//  Uses a pre-allocated entry pool (no RT allocation for scheduling).
//  Entries are added via engine commands through the from_nrt_ FIFO.
//

#ifndef tzpl_rt_tempo_scheduler_hpp
#define tzpl_rt_tempo_scheduler_hpp

#include "tempo_ramp.hpp"
#include "vm.hpp"
#include "tracing_gc.hpp"

namespace bridge {

// Pre-allocated entry for the RT scheduler's sorted linked list.
struct RTSchedEntry {
    RTSchedEntry* next_ = nullptr;
    RTSchedEntry* prev_ = nullptr;
    f64 beatTime;
    ts::Obj* handler = nullptr;   // Callable (nullptr = tempo change). Kept
                                  // reachable by RTTempoScheduler::markRoots.
    i64 timerID = 0;

    // Tempo change fields (used when handler == nullptr)
    f64 targetTempo = 0.;         // beats per second
    f64 rampBeats = 0.;
};

class RTTempoScheduler {
public:
    static constexpr int kMaxEntries = 1024;

    RTTempoScheduler(f64 bpm, f64 sampleRate, i64 startSample);

    // Register this scheduler's list as a GC root scanner on the given VM.
    // Call once after the scheduler is wired up to its owning Silo/VM. The
    // RT thread is single-threaded (scheduler and GC both run there), so no
    // locking is required during the scan.
    void attachVM(ts::VM& vm);

    // Walk the sorted list and the in-flight slot, marking every live
    // handler. Called by the extra-root scanner registered via attachVM.
    void markRoots(ts::TracingGC& gc);

    // Add a scheduled entry (called from doRT() of a command, on RT thread).
    // Returns the entry's timer ID, or -1 if the pool is exhausted.
    i64 addEntry(f64 beatTime, ts::Obj* handler);

    // Add a tempo change entry.
    i64 addTempoChange(f64 beatTime, f64 targetTempoBPS, f64 rampBeats);

    // Cancel an entry by timer ID. Returns true if found.
    bool cancel(i64 timerID);

    // Process all due events for the current sample time.
    // Called once per sample from the Silo's processing loop.
    void processSample(i64 sampleTime, ts::VM* vm);

    // Static callback matching Silo::TempoSchedFn signature.
    static void processSampleCallback(void* scheduler, i64 sampleTime, void* vm);

    // Set tempo immediately (beats per second).
    void setTempo(f64 beatsPerSecond, i64 currentSample);

    // Queries
    f64 tempo(i64 sampleTime) const;
    f64 beats(i64 sampleTime) const;
    f64 sampleRate() const { return sampleRate_; }

    i64 nextTimerID() { return nextTimerID_++; }

private:
    RTSchedEntry* allocEntry();
    void freeEntry(RTSchedEntry* entry);

    // Insert into sorted list (by beatTime)
    void insertSorted(RTSchedEntry* entry);

    // Remove from sorted list
    void removeFromList(RTSchedEntry* entry);

    f64 samplesToSeconds(i64 sampleTime) const {
        return (f64)(sampleTime - epochSample_) / sampleRate_;
    }

    ts::TempoRamp ramp_;
    f64 sampleRate_;
    i64 epochSample_;
    i64 nextTimerID_ = 1;

    // Sorted doubly-linked list of pending entries (earliest beat first)
    RTSchedEntry* head_ = nullptr;
    RTSchedEntry* tail_ = nullptr;

    // Handler popped from the list but not yet returned from callCallable.
    // The RT thread is single-threaded so no atomic is needed; the GC scan
    // and the list mutations all run on the RT thread.
    ts::Obj* inFlightHandler_ = nullptr;

    // Pre-allocated entry pool (free list)
    RTSchedEntry pool_[kMaxEntries];
    RTSchedEntry* freeList_ = nullptr;
};

} // namespace bridge

#endif /* tzpl_rt_tempo_scheduler_hpp */
