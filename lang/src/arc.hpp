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

namespace ts {

// Holds newly created objects during an event. Each object enters the pool
// with refcount = 1 (the pool's reference). When the pool drains (between
// events), it releases each object. Objects that have been retained by heap
// references survive; temporaries with refcount 1 are enqueued for deletion.
class AutoReleasePool {
    std::vector<GCObj*> pool_;
public:
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
};

// Queue of objects awaiting destruction. Processes a bounded number of
// deletions per heartbeat to avoid unbounded cascading. When an object is
// deleted, its destructor may release children whose refcounts then hit zero,
// causing them to be enqueued here as well.
class DeferredDeleteQueue {
    std::vector<GCObj*> queue_;
public:
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
            obj->releaseChildren();  // release children before destruction
            delete obj;
            ++deleted;
            --budget;
        }
        return deleted;
    }

    bool empty() const { return queue_.empty(); }
    u32 size() const { return (u32)queue_.size(); }
};

} // namespace ts

#endif /* arc_hpp */
