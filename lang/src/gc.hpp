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

// Tri-color marking state. White = unmarked (potential garbage);
// Gray = marked but children not yet scanned (on worklist);
// Black = marked and children scanned (live). Sweep frees whites;
// mark transitions white -> gray -> black.
enum class GCColor : u8 {
    White = 0,
    Gray  = 1,
    Black = 2,
};

// Tag for tag-dispatched gcScan. Stored in GCObj's header padding
// (a free byte; no size cost). The marker uses it to skip indirect-call
// dispatch for tagged types, going through a switch in gcScanByTag
// instead. Untagged objects (Default) fall through to the existing
// virtual gcScanChildren, so this is a strict optimization -- subclasses
// can opt in incrementally.
//
// Specific tags exist for the highest-allocation-frequency Obj subclasses
// (Tuple, Struct, Enum, ObjArray, ListNode, RefValue, MapObj, SetObj,
// Lambda). For these, the scan path is a non-virtual qualified call.
enum class GCTag : u8 {
    Default = 0,      // fall back to virtual gcScanChildren
    None,             // leaf: no GC children to scan (short-circuit)
    ObjArray,
    InlineArray,
    ListNode,
    RefValue,
    InlineRef,
    Struct,
    Tuple,
    Enum,
    MapObj,
    SetObj,
    Lambda,
    CoroutineObj,
    CoroutineFrame,
};

// Forward declaration
class GCObj;

// Link an object onto the owning VM's all-objects list (defined in vm.cpp).
// Called from registerNewObj() right after construction.
void linkObjToAllList(GCObj* obj);

// Tag-dispatched scan. Implemented in tracing_gc.cpp; declared here so
// GCObj remains a clean header.
class TracingGC;
void gcScanByTag(GCObj* obj, TracingGC& gc);
u32  gcScanChunkByTag(GCObj* obj, TracingGC& gc, u32 cursor);

// Base class for all garbage-collected objects.
//
// Layout (with virtual destructor for subclass cleanup):
//     vptr               : 8 bytes
//     color_             : 1 byte
//     immortal_          : 1 byte
//     gcTag_             : 1 byte
//     padding            : 5 bytes
//     allObjsNext_       : 8 bytes  (singly-linked list head -> tail)
//                          = 24 bytes total
//
// The all-objects list is singly-linked. The collector sweep walks it
// forward and unlinks freed objects inline via the slot-pointer idiom
// (no per-node prev pointer needed). New allocations prepend to the head
// in O(1); registerNewObj is the only writer outside of sweep.
//
// No homeAllocator_ field: there is exactly one VM heap per thread, the
// thread-local rt::gCurrentAllocator names it at every alloc and dealloc
// site, and sweep only deletes objects from the VM-owned all-objects
// list (so the deleting thread's gCurrentAllocator matches the
// allocating thread's). Cross-VM Obj sharing is no longer supported.
class GCObj {
    GCColor color_ = GCColor::White;
    // Immortal objects (compiler-owned constants: types, symbols, immortal
    // strings) live outside any VM's all-objects list; mark() short-circuits
    // on them so the tracer never recurses through them.
    bool    immortal_ = true;
    GCTag   gcTag_ = GCTag::None;
    GCObj*  allObjsNext_ = nullptr;
    friend class TracingGC;
    friend void linkObjToAllList(GCObj*);
public:
    GCColor color() const { return color_; }
    void setColor(GCColor c) { color_ = c; }
    GCTag gcTag() const { return gcTag_; }
    void setGCTag(GCTag t) { gcTag_ = t; }
    GCObj* allObjsNext() const { return allObjsNext_; }

    static constexpr uintptr_t kPtrMask = ~(uintptr_t)1;

    GCObj() = default;
    virtual ~GCObj() {}

    // Override new/delete to use VM's allocator
    static void* operator new(usize size);
    static void* operator new(usize size, void* ptr) noexcept { return ptr; }  // placement new
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, void*) noexcept {}  // placement delete

    bool isImmortal() const { return immortal_; }
    void makeImmortal()     { immortal_ = true; }
    void setMortal()        { immortal_ = false; }

    // Virtuals retained for `override` compatibility in 40+ subclasses.
    // The marker no longer calls these directly -- it dispatches through
    // gcScanByTag / gcScanChunkByTag, which use qualified non-virtual
    // calls keyed on gcTag_. The virtuals stay as a documented fallback
    // and so the override keyword keeps compiling.
    virtual void gcScanChildren(class TracingGC& /*gc*/) {}
    virtual u32 gcScanChunk(class TracingGC& /*gc*/, u32 /*cursor*/) {
        return ~u32{0};
    }
};

} // ts

#endif /* gc_hpp */
