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
//  gc.hpp
//  tiny-static-2
//
//  Created by James McCartney on 2/10/26.
//

#ifndef gc_hpp
#define gc_hpp

#include "base_types.hpp"
#include <atomic>

namespace rt {
class TLSFAllocator;
extern thread_local TLSFAllocator* gCurrentAllocator;
} // namespace rt

namespace ts {

// Forward declarations
class VM;

// ARC constants
static constexpr u32 kImmortalRefcount = 0xFFFF0000;
static constexpr u32 kInitialRefcount = 1;

// Forward declaration
class GCObj;

// Enqueue an object for deferred deletion (defined in vm.cpp)
void arcEnqueueForDeletion(GCObj* obj);

// Base class for all garbage-collected objects
class GCObj {
    mutable std::atomic<u32> refcount_{kImmortalRefcount};
    rt::TLSFAllocator* homeAllocator_ = nullptr;
    GCObj* foreignDeleteNext_ = nullptr;  // intrusive link for cross-thread deletion
    friend class ForeignDeleteQueue;
public:

    static constexpr uintptr_t kPtrMask = ~(uintptr_t)1;

    // Constructor
    GCObj();
    virtual ~GCObj() {}

    // Override new/delete to use VM's allocator
    static void* operator new(usize size);
    static void* operator new(usize size, void* ptr) noexcept { return ptr; }  // placement new
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, void*) noexcept {}  // placement delete

    // Release all Obj* children (ARC counterpart of gcScan).
    // Called before destruction. Each child's refcount is decremented.
    // If a child's refcount reaches zero, it is enqueued for deferred deletion.
    virtual void releaseChildren() {}

    // ARC methods
    bool isImmortal() const {
        return refcount_.load(std::memory_order_relaxed) >= kImmortalRefcount;
    }

    void makeImmortal() const {
        refcount_.store(kImmortalRefcount, std::memory_order_relaxed);
    }

    void retain() const {
        u32 rc = refcount_.load(std::memory_order_relaxed);
        if (rc >= kImmortalRefcount) return;
        refcount_.fetch_add(1, std::memory_order_relaxed);
    }

    // Returns true if refcount reached zero.
    // When refcount reaches zero, the object is auto-enqueued for deferred deletion.
    bool release() const {
        u32 rc = refcount_.load(std::memory_order_relaxed);
        if (rc >= kImmortalRefcount) return false;
        if (refcount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            arcEnqueueForDeletion(const_cast<GCObj*>(this));
            return true;
        }
        return false;
    }

    u32 refcount() const {
        return refcount_.load(std::memory_order_relaxed);
    }

    void setInitialRefcount() const {
        refcount_.store(kInitialRefcount, std::memory_order_relaxed);
    }

    rt::TLSFAllocator* homeAllocator() const { return homeAllocator_; }
};

} // ts

#endif /* gc_hpp */
