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

namespace rt {
class TLSFAllocator;
extern thread_local TLSFAllocator* gCurrentAllocator;
} // namespace rt

namespace ts {

// Forward declarations
class VM;

// Phase 3 of tracing-GC project: tri-color marking state. Stored in
// GCObj::color_. White = unmarked (potential garbage); Gray = marked but
// children not yet scanned (on worklist); Black = marked and children
// scanned (live). Sweep frees whites; mark transitions white -> gray -> black.
enum class GCColor : u8 {
    White = 0,
    Gray  = 1,
    Black = 2,
};

// Forward declaration
class GCObj;

// Link an object onto the owning VM's all-objects list (defined in vm.cpp).
// Called from registerNewObj() right after construction.
void linkObjToAllList(GCObj* obj);

// Unlink an object from the owning VM's all-objects list (defined in vm.cpp).
// Called from GCObj::operator delete just before memory is released.
void unlinkObjFromAllList(GCObj* obj);

// Base class for all garbage-collected objects
class GCObj {
    rt::TLSFAllocator* homeAllocator_ = nullptr;

    // Tracing-GC state. All accesses are single-threaded: the owning VM's
    // mutator (color reads) and the same VM's safepoint-driven collector
    // (color writes).
    GCColor color_ = GCColor::White;
    // Immortal objects (compiler-owned constants: types, symbols, immortal
    // strings) live outside any VM's all-objects list; mark() short-circuits
    // on them so the tracer never recurses through them.
    bool    immortal_ = true;
    GCObj*  allObjsPrev_ = nullptr;  // intrusive doubly-linked list of all
    GCObj*  allObjsNext_ = nullptr;  // mortal objects alive in the owning VM
    friend class TracingGC;
    friend void linkObjToAllList(GCObj*);
    friend void unlinkObjFromAllList(GCObj*);
public:
    GCColor color() const { return color_; }
    void setColor(GCColor c) { color_ = c; }
    GCObj* allObjsNext() const { return allObjsNext_; }

    static constexpr uintptr_t kPtrMask = ~(uintptr_t)1;

    // Constructor
    GCObj();
    virtual ~GCObj() {}

    // Override new/delete to use VM's allocator
    static void* operator new(usize size);
    static void* operator new(usize size, void* ptr) noexcept { return ptr; }  // placement new
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, void*) noexcept {}  // placement delete

    // Enumerate every direct Obj* child and call gc.mark() on each. The
    // tracing collector uses this to walk the live object graph transitively.
    // Default no-op; subclasses with GC-managed fields override.
    //
    // For containers with > TracingGC::kFanoutThreshold entries, the override
    // should call gc.pushPartial(this) and return immediately; the actual
    // child walk happens via gcScanChunk under a bounded per-call window so
    // a single scan can't overshoot the step-budget deadline.
    virtual void gcScanChildren(class TracingGC& /*gc*/) {}

    // Scan up to TracingGC::kFanoutChunk children starting at `cursor`.
    // Return the next cursor; UINT32_MAX signals "fully done" and removes
    // the object from the partial queue. Default implementation just signals
    // done -- only large containers override.
    virtual u32 gcScanChunk(class TracingGC& /*gc*/, u32 /*cursor*/) {
        return ~u32{0};
    }

    bool isImmortal() const { return immortal_; }
    void makeImmortal()     { immortal_ = true; }
    void setMortal()        { immortal_ = false; }

    rt::TLSFAllocator* homeAllocator() const { return homeAllocator_; }
};

} // ts

#endif /* gc_hpp */
