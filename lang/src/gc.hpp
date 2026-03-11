//
//  gc.hpp
//  tiny-static-2
//
//  Created by James McCartney on 2/10/26.
//

#ifndef gc_hpp
#define gc_hpp

#include "base_types.hpp"

namespace ts {

// Forward declarations
class VM;
class GC;

// Sentinel gcindex: objects with this value are never in the GC table.
// Because isWhite() checks white_ <= gcindex_ < sweep_, and sweep_ maxes
// at ~16M, UINT32_MAX is always outside that range → GC ignores them.
static constexpr u32 kGCIndexSentinel = UINT32_MAX;

// Base class for all garbage-collected objects
class GCObj {
    friend class GC;
    mutable u32 gcindex_ = kGCIndexSentinel;
public:

    static constexpr uintptr_t kPtrMask = ~(uintptr_t)1;

    // Constructor — gcindex_ defaults to sentinel. addObj() overwrites for VM objects.
    GCObj();
    virtual ~GCObj() {}

    // Override new/delete to use VM's allocator
    static void* operator new(usize size);
    static void* operator new(usize size, void* ptr) noexcept { return ptr; }  // placement new
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, void*) noexcept {}  // placement delete

    virtual void gcScan(GC* gc, i32& ioWordsToScan) {}
    virtual void gcPartialScan(GC* gc, i32& ioWordsToScan, i32 start) {}
    
    virtual u32 numObjSlots() const = 0;
};


// One-pass incremental real-time garbage collector
class GC {
    // Allocation tracking
    u32 obj_slots_currently_allocated_ = 0;
    u32 allocated_after_last_gc_ = 0;
    u32 gc_trigger_threshold_ = 64 * 1024;

    u32 target_heartbeats_ = 100;
    u32 work_budget_ = 0;
    
    bool triggered_ = false;
    bool collecting_ = false;
    u32 collections_completed_ = 0;

    // objects with an index less than black_ are immortal.
    u32 black_ = 0; // index of first black object
    u32 grey_ = 0;  // index of first grey object
    u32 white_ = 0; // index of first white object
    u32 sweep_ = 0; // index of first sweepable object
    u32 free_ = 0;  // index of first free table entry
    u32 max_objects_ = 16 * 1024 * 1024;
    
    GCObj** table_;
    
    GCObj* partially_scanned_ = nullptr;
    u32 partial_scan_progress_ = 0;

    f64 average_pause_time_ = 0.;        // Microseconds per heartbeat
    
public:
    GC() {
        table_ = (GCObj**)calloc(max_objects_, sizeof(GCObj*));
    }

    void startCollection() {
        collecting_ = true;
        markRoots();
        calculateWorkBudget();
    }

    void endCollection() {
        sweep_ = white_; // all white become sweepable.
        white_ = grey_ = black_; // a black become white. 

        allocated_after_last_gc_ = obj_slots_currently_allocated_;
        
        collecting_ = false;
        triggered_ = false;
        ++collections_completed_;
    }
    
    void heartbeat() {
        auto start = std::chrono::high_resolution_clock::now();

        doMarking();
        
        if (grey_ == white_) {
            // no grey objects remaining
            endCollection();
        }

        auto end = std::chrono::high_resolution_clock::now();
        f64 elapsed = std::chrono::duration<f64, std::micro>(end - start).count();

        average_pause_time_ = 0.95 * average_pause_time_ + 0.05 * elapsed;
    }
    
    void markRoots();

    
    bool isCollecting() const { return collecting_; }
    bool isTriggered() const { return triggered_; }
    u32 getCollectionsCompleted() const { return collections_completed_; }

    u32 getNumImmortal() const { return black_; }
    u32 getNumBlack() const { return grey_ - black_; }
    u32 getNumGrey() const { return white_ - grey_; }
    u32 getNumWhite() const { return sweep_ - white_; }
    u32 getNumSweepable() const { return free_ - sweep_; }
    u32 getNumFree() const { return max_objects_ - free_; }
    u32 getNumTotal() const { return free_; }
    u32 getTriggerThreshold() const { return gc_trigger_threshold_; }
    u32 getWorkBudget() const { return work_budget_; }
    f64 getAveragePauseTime() const { return average_pause_time_; }

    bool isImmortal(GCObj const* o) const {
        return o->gcindex_ < black_;
    }
    bool isBlack(GCObj const* o) const {
        return black_ <= o->gcindex_ && o->gcindex_ < grey_;
    }
    bool isGrey(GCObj const* o) const {
        return grey_ <= o->gcindex_ && o->gcindex_ < white_;
    }
    bool isWhite(GCObj const* o) const {
        return white_ <= o->gcindex_ && o->gcindex_ < sweep_;
    }
    bool isSweep(GCObj const* o) const {
        return sweep_ <= o->gcindex_ && o->gcindex_ < free_;
    }

    u32 getNumLiveObjects() const { return sweep_; }
    u32 getNumLiveWords() const { return obj_slots_currently_allocated_; }
    
    void makeAllImmortal() {
        black_ = grey_ = white_ = sweep_ = free_;
        allocated_after_last_gc_ = 0;
    }
    
    void swap(GCObj* a, GCObj* b) {
        if (a->gcindex_ == b->gcindex_) return;
        std::swap(table_[a->gcindex_], table_[b->gcindex_]);
        std::swap(a->gcindex_, b->gcindex_);
    }
        
    void mark(GCObj* o) {
        if(!isWhite(o)) return;
        swap(o, table_[white_]);
        ++white_;
    }
    
    bool scanPartial(i32& ioNumWords) {
        GCObj* o = partially_scanned_;
        if (!o) return false;
        o->gcPartialScan(this, ioNumWords, partial_scan_progress_);
        // make it black
        if (!partially_scanned_) ++grey_;
        return ioNumWords == 0; // is done
    }
    
    void scanOne(i32& ioNumWords) {
        // scan the first grey object
        GCObj* o = table_[grey_];
        o->gcScan(this, ioNumWords);
        // make it black
        ++grey_;
    }
    
    void writeBarrier(GCObj* o) {
        if (collecting_) {
            mark(o);
        }
    }
        
    void sweepOne() {
        if (sweep_ < free_) { // if there are any sweepable objects
            // delete the last sweepable object, it becomes free.
            GCObj* o = table_[--free_];
            obj_slots_currently_allocated_ -= o->numObjSlots();
            delete o;
        }
    }

    void sweepAll() {
        while (sweep_ < free_) {
            sweepOne();
        }
    }
    
    void addObj(GCObj* o) {
        sweepOne(); 
        
        // add object to table in first free slot
        table_[free_] = o;
        o->gcindex_ = free_;
        
        // swap the first sweep slot to the first free slot
        swap(table_[sweep_], table_[free_]);
        
        // the first sweep slot becomes white
        ++sweep_;
        // the swapped sweep slot remains a sweep.
        ++free_;
        
        // account for the number of scannable slots that have been allocated.
        obj_slots_currently_allocated_ += o->numObjSlots();
        
        // check if a gc is triggered
        if (!triggered_) {
            u32 triggerLevel = allocated_after_last_gc_ + gc_trigger_threshold_;
            if (obj_slots_currently_allocated_ >= triggerLevel) {
                triggered_ = true;
            }
        }
    }
    
    void doMarking() {
        i32 wordsRemaining = work_budget_;
            
        if (scanPartial(wordsRemaining)) return;
        
        while (grey_ < white_ && wordsRemaining > 0) {
            scanOne(wordsRemaining);
        }
    }
    
    void setPartialScan(GCObj* o, u32 progress) {
        partially_scanned_ = o;
        partial_scan_progress_ = progress;
    }
    
    void calculateWorkBudget() {
        work_budget_ = std::max((u32)10, obj_slots_currently_allocated_ / target_heartbeats_ + 10);
    }
};

} // ts

#endif /* gc_hpp */
