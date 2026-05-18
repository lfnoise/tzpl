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
//  arc.hpp
//  lang
//
//  Automatic Reference Counting infrastructure:
//  AutoReleasePool and DeferredDeleteQueue
//

#ifndef arc_hpp
#define arc_hpp

#include "gc.hpp"
#include "stl_allocator.hpp"

namespace ts {

// Holds newly created objects during an event. Each object enters the pool
// with refcount = 1 (the pool's reference). When the pool drains (between
// events), it releases each object. Objects that have been retained by heap
// references survive; temporaries with refcount 1 are enqueued for deletion.
class AutoReleasePool {
    rt::VMVector<GCObj*> pool_;
public:
    explicit AutoReleasePool(rt::TLSFAllocator* allocator)
        : pool_(rt::STLAllocator<GCObj*>(allocator))
    {
        pool_.reserve(128);
    }

    void add(GCObj* obj) {
        pool_.push_back(obj);
    }

    // Drain the pool, releasing each object's pool reference.
    // Objects whose refcount reaches zero are auto-enqueued for deferred deletion
    // by release() itself.
    void drain() {
        for (GCObj* obj : pool_) {
            obj->release();
        }
        pool_.clear();
    }

    u32 size() const { return (u32)pool_.size(); }
    void clear() { pool_.clear(); }

    // Iteration for the tracing GC: pool entries are roots (each contributes
    // +1 to its target's refcount), so the collector treats them as such.
    GCObj* const* begin() const { return pool_.data(); }
    GCObj* const* end()   const { return pool_.data() + pool_.size(); }
};

// Queue of objects awaiting destruction. Processes a bounded number of
// deletions per heartbeat to avoid unbounded cascading. When an object is
// deleted, its destructor may release children whose refcounts then hit zero,
// causing them to be enqueued here as well.
class DeferredDeleteQueue {
    rt::VMVector<GCObj*> queue_;
public:
    explicit DeferredDeleteQueue(rt::TLSFAllocator* allocator)
        : queue_(rt::STLAllocator<GCObj*>(allocator))
    {
        queue_.reserve(128);
    }

    void enqueue(GCObj* obj) {
        queue_.push_back(obj);
    }

    // Process up to `budget` deletions. Returns number of objects deleted.
    // releaseChildren() is called while the object is still fully intact
    // (vtable, fields, etc.), then delete frees the memory.
    u32 processN(u32 budget) {
        u32 deleted = 0;
        while (budget > 0 && !queue_.empty()) {
            GCObj* obj = queue_.back();
            queue_.pop_back();
            // Object was enqueued when refcount hit 0, but may have been
            // re-retained since (e.g. coroutine yield retaining saved regs).
            // Skip deletion if the object is alive again.
            if (obj->refcount() > 0) continue;
            obj->releaseChildren();
            delete obj;
            ++deleted;
            --budget;
        }
        return deleted;
    }

    bool empty() const { return queue_.empty(); }
    u32 size() const { return (u32)queue_.size(); }

    // Iteration for the tracing GC: queue entries have refcount 0 and are
    // awaiting deletion. They are NOT roots; the tracer marks them white and
    // expects ARC to free them. Exposed so the collector can validate.
    GCObj* const* begin() const { return queue_.data(); }
    GCObj* const* end()   const { return queue_.data() + queue_.size(); }
};

// Lock-free MPSC (multi-producer, single-consumer) queue for cross-thread
// object deletion. When an object's refcount reaches zero on a foreign thread
// (i.e., one that does not own the object's home allocator), the object is
// pushed onto this queue instead of the local DeferredDeleteQueue. The home
// VM's gcHeartbeat() drains this queue, performing deletion on the correct
// thread with the correct allocator.
//
// Implementation: Treiber stack. Push uses CAS (lock-free, RT-safe).
// Drain atomically swaps the head to null and returns the entire chain.
class ForeignDeleteQueue {
    std::atomic<GCObj*> head_{nullptr};
public:
    // Push an object for deferred deletion. Lock-free, safe from any thread.
    void enqueue(GCObj* obj) {
        obj->foreignDeleteNext_ = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(
            obj->foreignDeleteNext_, obj,
            std::memory_order_release, std::memory_order_relaxed)) {
            // CAS failed -- obj->foreignDeleteNext_ was updated to current head
        }
    }

    // Drain all enqueued objects into the local DeferredDeleteQueue.
    // Only called by the owning VM's thread (single consumer).
    void drainInto(DeferredDeleteQueue& localQueue) {
        GCObj* list = head_.exchange(nullptr, std::memory_order_acquire);
        while (list) {
            GCObj* next = list->foreignDeleteNext_;
            list->foreignDeleteNext_ = nullptr;
            localQueue.enqueue(list);
            list = next;
        }
    }

    bool empty() const { return head_.load(std::memory_order_relaxed) == nullptr; }
};

} // namespace ts

#endif /* arc_hpp */
