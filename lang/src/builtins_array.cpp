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
//  builtins_array.cpp -- Array and random number builtins
//  lang
//

#include "builtins_internal.hpp"
#include <algorithm>
#include <numeric>

namespace ts {

// ============================================================================
// Simple array builtins: reverse, push, pop, muss, sort
// ============================================================================

void builtin_reverse_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    if (arrayBackendFor(at->elemType_) == ArrayBackend::Inline) {
        auto* s = static_cast<InlineArray*>(src);
        auto* r = new InlineArray(at);
        size_t n = s->size();
        r->reserve(n);
        for (size_t i = n; i > 0; --i) r->pushSlot(s->slot(i-1));
        vm.reg(dst).o = r;
        return;
    }
    txArray(vm, dst, src, at, [](auto& sv, auto& rv) {
        size_t n = sv.size(); rv.resize(n);
        for (size_t i = 0; i < n; i++) rv[i] = sv[n-1-i];
    });
}

// Phase 4e: append takes Complex/Fraction as 2 consecutive Words at ab+1.
void builtin_push_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex: {
            auto* s = static_cast<PodArray<x64>*>(src);
            auto* r = new PodArray<x64>(at); r->v = s->v;
            f64 re = vm.reg(ab + 1).f;
            f64 im = vm.reg(ab + 2).f;
            r->v.push_back(x64(re, im));
            vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* s = static_cast<PodArray<r64>*>(src);
            auto* r = new PodArray<r64>(at); r->v = s->v;
            i64 n = vm.reg(ab + 1).i;
            i64 d = vm.reg(ab + 2).i;
            r->v.push_back(r64(n, d, true));
            vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Float: {
            auto* s = static_cast<PodArray<f64>*>(src);
            auto* r = new PodArray<f64>(at); r->v = s->v;
            r->v.push_back(vm.reg(ab + 1).f);
            vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Int: {
            auto* s = static_cast<PodArray<i64>*>(src);
            auto* r = new PodArray<i64>(at); r->v = s->v;
            r->v.push_back(vm.reg(ab + 1).i);
            vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Inline: {
            auto* s = static_cast<InlineArray*>(src);
            auto* r = new InlineArray(at); r->copyFrom(s);
            r->pushSlot(&vm.reg(ab + 1));
            vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Obj: {
            auto* s = static_cast<ObjArray*>(src);
            auto* r = new ObjArray(at); r->copyFrom(s);
            r->push(vm.reg(ab + 1).o);
            vm.reg(dst).o = r;
            return;
        }
    }
}

void builtin_pop_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    if (arrayBackendFor(at->elemType_) == ArrayBackend::Inline) {
        auto* s = static_cast<InlineArray*>(src);
        auto* r = new InlineArray(at);
        size_t n = s->size();
        if (n) {
            r->reserve(n - 1);
            for (size_t i = 0; i < n - 1; ++i) r->pushSlot(s->slot(i));
        }
        vm.reg(dst).o = r;
        return;
    }
    txArray(vm, dst, src, at, [](auto& sv, auto& rv) {
        if (!sv.empty()) { rv.resize(sv.size()-1);
            for (size_t i = 0; i < rv.size(); i++) rv[i] = sv[i]; }
    });
}

// --- Mutating variants registered as `push!` and `pop!` ---
//
// These mutate in place and return the array (push) or the popped element
// (pop). They are distinct registrations from the non-mutating `push`/`pop`
// above, so existing code referring to `push`/`pop` is unaffected.

void builtin_push_bang_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex: {
            auto* a = static_cast<PodArray<x64>*>(arr);
            f64 re = vm.reg(ab + 1).f;
            f64 im = vm.reg(ab + 2).f;
            a->v.push_back(x64(re, im));
            break;
        }
        case ArrayBackend::Fraction: {
            auto* a = static_cast<PodArray<r64>*>(arr);
            i64 n = vm.reg(ab + 1).i;
            i64 d = vm.reg(ab + 2).i;
            a->v.push_back(r64(n, d, true));
            break;
        }
        case ArrayBackend::Float: {
            auto* a = static_cast<PodArray<f64>*>(arr);
            a->v.push_back(vm.reg(ab + 1).f);
            break;
        }
        case ArrayBackend::Int: {
            auto* a = static_cast<PodArray<i64>*>(arr);
            a->v.push_back(vm.reg(ab + 1).i);
            break;
        }
        case ArrayBackend::Inline: {
            auto* a = static_cast<InlineArray*>(arr);
            a->pushSlot(&vm.reg(ab + 1));
            break;
        }
        case ArrayBackend::Obj: {
            auto* a = static_cast<ObjArray*>(arr);
            a->push(vm.reg(ab + 1).o);
            break;
        }
    }
    vm.reg(dst).o = arr;
}

void builtin_pop_bang_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex: {
            auto* a = static_cast<PodArray<x64>*>(arr);
            x64 v = a->v.back();
            a->v.pop_back();
            vm.reg(dst).f = v.real();
            vm.reg((u16)(dst + 1)).f = v.imag();
            return;
        }
        case ArrayBackend::Fraction: {
            auto* a = static_cast<PodArray<r64>*>(arr);
            r64 v = a->v.back();
            a->v.pop_back();
            vm.reg(dst).i = v.numer();
            vm.reg((u16)(dst + 1)).i = v.denom();
            return;
        }
        case ArrayBackend::Float: {
            auto* a = static_cast<PodArray<f64>*>(arr);
            f64 v = a->v.back();
            a->v.pop_back();
            vm.reg(dst).f = v;
            return;
        }
        case ArrayBackend::Int: {
            auto* a = static_cast<PodArray<i64>*>(arr);
            i64 v = a->v.back();
            a->v.pop_back();
            vm.reg(dst).i = v;
            return;
        }
        case ArrayBackend::Inline: {
            auto* a = static_cast<InlineArray*>(arr);
            size_t n = a->size();
            a->getSlot(n - 1, &vm.reg(dst));
            a->resize(n - 1);
            return;
        }
        case ArrayBackend::Obj: {
            auto* a = static_cast<ObjArray*>(arr);
            auto& v = a->rawVec();
            Obj* o = v.back();
            v.pop_back();
            vm.reg(dst).o = o;
            return;
        }
    }
}

// clear!: [T] -> [T]  -- mutating; removes every element, returns the same
// array. InlineArray/ObjArray resize(0) shades dropped elements for the
// SATB GC.
void builtin_clear_bang_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex:  static_cast<PodArray<x64>*>(arr)->v.clear(); break;
        case ArrayBackend::Fraction: static_cast<PodArray<r64>*>(arr)->v.clear(); break;
        case ArrayBackend::Float:    static_cast<PodArray<f64>*>(arr)->v.clear(); break;
        case ArrayBackend::Int:      static_cast<PodArray<i64>*>(arr)->v.clear(); break;
        case ArrayBackend::Inline:   static_cast<InlineArray*>(arr)->resize(0); break;
        case ArrayBackend::Obj:      static_cast<ObjArray*>(arr)->resize(0); break;
    }
    vm.reg(dst).o = arr;
}

// isEmpty: [T] -> Bool
void builtin_isEmpty_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    vm.reg(dst).i = (getArraySize(vm, src, at->elemType_) == 0) ? 1 : 0;
}

// POD half of append!. Reserve-then-push so self-append (a append!(a)) stays
// valid: the source length is snapshotted and no reallocation happens while
// the source is being read.
template <typename T>
static void appendPodArray(Obj* dstO, Obj* srcO) {
    auto& d = static_cast<PodArray<T>*>(dstO)->v;
    auto const& s = static_cast<PodArray<T>*>(srcO)->v;
    size_t n = s.size();
    d.reserve(d.size() + n);
    for (size_t i = 0; i < n; ++i) d.push_back(s[i]);
}

// append!: [T], [T] -> [T]  -- mutating; appends every element of the source
// array in place. The mutating analogue of `$` concatenation. Returns the
// same array for chaining.
void builtin_append_bang_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* src = vm.reg(ab + 1).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex:  appendPodArray<x64>(arr, src); break;
        case ArrayBackend::Fraction: appendPodArray<r64>(arr, src); break;
        case ArrayBackend::Float:    appendPodArray<f64>(arr, src); break;
        case ArrayBackend::Int:      appendPodArray<i64>(arr, src); break;
        case ArrayBackend::Inline: {
            auto* d = static_cast<InlineArray*>(arr);
            auto* s = static_cast<InlineArray*>(src);
            size_t n = s->size();
            d->reserve(d->size() + n);
            for (size_t i = 0; i < n; ++i) d->pushSlot(s->slot(i));
            break;
        }
        case ArrayBackend::Obj: {
            auto& d = static_cast<ObjArray*>(arr)->rawVec();
            auto const& s = static_cast<ObjArray*>(src)->rawVec();
            size_t n = s.size();
            d.reserve(d.size() + n);
            for (size_t i = 0; i < n; ++i) d.push_back(s[i]);
            break;
        }
    }
    vm.reg(dst).o = arr;
}

// append!: [T], List<T> -> [T]  -- mutating; appends every list element in
// place, forcing the list as it walks (an infinite list will not terminate).
void builtin_append_bang_array_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* node = static_cast<ListNode*>(vm.reg(ab + 1).o);
    auto* at = static_cast<ArrayType*>(arr->type_);
    ArrayBackend b = arrayBackendFor(at->elemType_);
    while (node) {
        node->force(vm);
        Word const* h = node->headData();
        switch (b) {
            case ArrayBackend::Complex:
                static_cast<PodArray<x64>*>(arr)->v.push_back(x64(h[0].f, h[1].f));
                break;
            case ArrayBackend::Fraction:
                static_cast<PodArray<r64>*>(arr)->v.push_back(r64(h[0].i, h[1].i));
                break;
            case ArrayBackend::Float:
                static_cast<PodArray<f64>*>(arr)->v.push_back(h[0].f);
                break;
            case ArrayBackend::Int:
                static_cast<PodArray<i64>*>(arr)->v.push_back(h[0].i);
                break;
            case ArrayBackend::Inline:
                static_cast<InlineArray*>(arr)->pushSlot(h);
                break;
            case ArrayBackend::Obj:
                static_cast<ObjArray*>(arr)->push(h[0].o);
                break;
        }
        node = node->tail_;
    }
    vm.reg(dst).o = arr;
}

// --- at / put!: subscript protocol as ordinary functions ---
//
// Same semantics as a[i] and a[i] = v (cyclic index, negative wraps), so
// generic code can call at/put! uniformly over arrays and user indexable
// types (see 4.12 Indexable Objects in Tzopilotl_by_Example).

// Cyclic index matching op_array_get/op_array_set in opcodes.cpp.
static size_t cyclicElemIndex(i64 idx, size_t size) {
    if (size == 0) {
        throw std::runtime_error("Cannot index an empty array");
    }
    idx = idx % static_cast<i64>(size);
    if (idx < 0) idx += static_cast<i64>(size);
    return static_cast<size_t>(idx);
}

// at: [T], Int -> T
void builtin_at_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    ArrayBackend b = arrayBackendFor(et);
    size_t i = cyclicElemIndex(vm.reg((u16)(ab + 1)).i, getArraySize(arr, b));
    Word tmp[2];
    Word const* slot = arrayElemSlot(arr, et, i, tmp);
    u32 sw = strideForType(et);
    for (u32 w = 0; w < sw; ++w) vm.reg((u16)(dst + w)) = slot[w];
}

// at: [T], [Int] -> [T]  -- gather form, mirroring a[[i1,i2,...]]
void builtin_at_multi_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto& idxs = static_cast<PodArray<i64>*>(vm.reg((u16)(ab + 1)).o)->v;
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(arr, arrayBackendFor(et));
    auto* result = makeEmptyArray(at);
    Word tmp[2];
    for (i64 idx : idxs) {
        Word const* slot = arrayElemSlot(arr, et, cyclicElemIndex(idx, n), tmp);
        arrayPushFromSlot(vm, result, et, slot);
    }
    vm.reg(dst).o = result;
}

// put!: [T], Int, T -> [T]  -- mutating; returns the same array for chaining
void builtin_put_bang_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    ArrayBackend b = arrayBackendFor(et);
    size_t i = cyclicElemIndex(vm.reg((u16)(ab + 1)).i, getArraySize(arr, b));
    Word const* src = &vm.reg((u16)(ab + 2));
    switch (b) {
        case ArrayBackend::Int:
            static_cast<PodArray<i64>*>(arr)->v[i] = src[0].i; break;
        case ArrayBackend::Float:
            static_cast<PodArray<f64>*>(arr)->v[i] = src[0].f; break;
        case ArrayBackend::Complex:
            static_cast<PodArray<x64>*>(arr)->v[i] = x64(src[0].f, src[1].f); break;
        case ArrayBackend::Fraction:
            static_cast<PodArray<r64>*>(arr)->v[i] = r64(src[0].i, src[1].i, true); break;
        case ArrayBackend::Inline:
            static_cast<InlineArray*>(arr)->setSlot(i, src); break;
        case ArrayBackend::Obj:
            // ObjArray::set applies the SATB write barrier.
            static_cast<ObjArray*>(arr)->set(i, src[0].o); break;
    }
    vm.reg(dst).o = arr;
}

// --- copy: shallow copy of an Array, preserving backend & element types ---
void builtin_copy_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex: {
            auto* s = static_cast<PodArray<x64>*>(src);
            auto* r = new PodArray<x64>(at); r->v = s->v;
            vm.reg(dst).o = r; return;
        }
        case ArrayBackend::Fraction: {
            auto* s = static_cast<PodArray<r64>*>(src);
            auto* r = new PodArray<r64>(at); r->v = s->v;
            vm.reg(dst).o = r; return;
        }
        case ArrayBackend::Float: {
            auto* s = static_cast<PodArray<f64>*>(src);
            auto* r = new PodArray<f64>(at); r->v = s->v;
            vm.reg(dst).o = r; return;
        }
        case ArrayBackend::Int: {
            auto* s = static_cast<PodArray<i64>*>(src);
            auto* r = new PodArray<i64>(at); r->v = s->v;
            vm.reg(dst).o = r; return;
        }
        case ArrayBackend::Inline: {
            auto* s = static_cast<InlineArray*>(src);
            auto* r = new InlineArray(at); r->copyFrom(s);
            vm.reg(dst).o = r; return;
        }
        case ArrayBackend::Obj: {
            auto* s = static_cast<ObjArray*>(src);
            auto* r = new ObjArray(at); r->copyFrom(s);
            vm.reg(dst).o = r; return;
        }
    }
}

void builtin_muss_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    auto& rng = vm.rng();
    if (arrayBackendFor(at->elemType_) == ArrayBackend::Inline) {
        auto* s = static_cast<InlineArray*>(src);
        auto* r = new InlineArray(at);
        r->copyFrom(s);
        u32 sw = r->stride();
        for (size_t i = r->size(); i > 1; --i) {
            size_t j = rng.next() % i;
            // Raw-word swap: both slots already hold properly-retained
            // references, so swapping words preserves ARC invariants.
            Word* a = r->slot(i - 1);
            Word* b = r->slot(j);
            for (u32 k = 0; k < sw; ++k) std::swap(a[k], b[k]);
        }
        vm.reg(dst).o = r;
        return;
    }
    txArray(vm, dst, src, at, [&rng](auto& sv, auto& rv) {
        rv = sv;
        for (size_t i = rv.size(); i > 1; i--) {
            size_t j = rng.next() % i;
            std::swap(rv[i-1], rv[j]);
        }
    });
}

// ============================================================================
// Random number generation builtins
// ============================================================================

// urand() -> Float  uniform [0.0, 1.0)
static void builtin_urand(VM& vm, u16 dst, u16, u16) {
    // Map a 64-bit integer to [0.0, 1.0) by using the upper 53 bits
    u64 r = vm.rng().next();
    vm.reg(dst).f = (r >> 11) * (1.0 / (1ULL << 53));
}

// randSeed(seed) -> Void  re-seed the per-VM RNG for reproducible random
// streams (tests, replayable performances).
static void builtin_randseed(VM& vm, u16, u16, u16 ab) {
    vm.rng().seed((u64)vm.reg(ab).i);
}

// brand() -> Float  bipolar [-1.0, 1.0)
static void builtin_brand(VM& vm, u16 dst, u16, u16) {
    u64 r = vm.rng().next();
    vm.reg(dst).f = (r >> 11) * (2.0 / (1ULL << 53)) - 1.0;
}

// irand(lo, hi) -> Int  uniform integer [lo, hi]
static void builtin_irand(VM& vm, u16 dst, u16, u16 ab) {
    i64 lo = vm.reg(ab).i;
    i64 hi = vm.reg(ab + 1).i;
    if (lo > hi) std::swap(lo, hi);
    u64 range = (u64)(hi - lo) + 1;
    // Unbiased: reject values that would cause modulo bias
    u64 limit = (UINT64_MAX / range) * range;
    u64 r;
    do { r = vm.rng().next(); } while (r >= limit);
    vm.reg(dst).i = lo + (i64)(r % range);
}

// xrand(lo, hi) -> Float  exponentially distributed [lo, hi)
// Equal to lo * pow(hi/lo, urand())
static void builtin_xrand(VM& vm, u16 dst, u16, u16 ab) {
    f64 lo = vm.reg(ab).f;
    f64 hi = vm.reg(ab + 1).f;
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    vm.reg(dst).f = lo * std::pow(hi / lo, u);
}

// pick([T]) -> T  choose a random element.
// Phase 4g.27: write the result natively into dst.. so Inline composite
// elements (Complex/Fraction/Tuple/Struct) land as multi-word data, not
// a 1-Word boxed pointer that the caller would have to unbox.
void builtin_pick_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(arr);
        size_t idx = vm.rng().next() % n;
        placeLambdaArgFromArrayElem_t<B>(vm, dst, arr, et, idx);
    });
}

// urands() -> List<Float>: infinite lazy list of uniform [0, 1) floats
static void builtin_urands_list(VM& vm, u16 dst, u16, u16) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = ListNode::create(lt);
    auto* gen = new UrandsListGen(vm.typeType());
    gen->listType_ = lt;
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

// urands(n) -> [Float]: array of n uniform [0, 1) floats
static void builtin_urands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        r->v[i] = (x >> 11) * (1.0 / (1ULL << 53));
    }
    vm.reg(dst).o = r;
}

// brands() -> List<Float>: infinite lazy list of bipolar [-1, 1) floats
static void builtin_brands_list(VM& vm, u16 dst, u16, u16) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = ListNode::create(lt);
    auto* gen = new BrandsListGen(vm.typeType());
    gen->listType_ = lt;
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

// brands(n) -> [Float]: array of n bipolar [-1, 1) floats
static void builtin_brands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        r->v[i] = (x >> 11) * (2.0 / (1ULL << 53)) - 1.0;
    }
    vm.reg(dst).o = r;
}

// irands(lo, hi) -> List<Int>: infinite lazy list of uniform random ints [lo, hi]
static void builtin_irands_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* lt = vm.listType(vm.intType());
    auto* node = ListNode::create(lt);
    auto* gen = new IrandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).i;
    gen->hi_ = vm.reg(ab+1).i;
    gen->listType_ = lt;
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

// irands(n, lo, hi) -> [Int]: array of n uniform random ints [lo, hi]
static void builtin_irands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    i64 lo = vm.reg(ab+1).i, hi = vm.reg(ab+2).i;
    if (lo > hi) std::swap(lo, hi);
    if (n < 0) n = 0;
    u64 range = (u64)(hi - lo) + 1;
    u64 limit = (UINT64_MAX / range) * range;
    auto* r = new PodArray<i64>(vm.arrayType(vm.intType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x;
        do { x = vm.rng().next(); } while (x >= limit);
        r->v[i] = lo + (i64)(x % range);
    }
    vm.reg(dst).o = r;
}

// xrands(lo, hi) -> List<Float>: infinite lazy list of exponentially distributed floats [lo, hi)
static void builtin_xrands_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = ListNode::create(lt);
    auto* gen = new XrandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).f;
    gen->hi_ = vm.reg(ab+1).f;
    gen->listType_ = lt;
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

// xrands(n, lo, hi) -> [Float]: array of n exponentially distributed floats [lo, hi)
static void builtin_xrands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    f64 lo = vm.reg(ab+1).f, hi = vm.reg(ab+2).f;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        f64 u = (x >> 11) * (1.0 / (1ULL << 53));
        r->v[i] = lo * std::pow(hi / lo, u);
    }
    vm.reg(dst).o = r;
}

// rand(lo, hi) -> Float: uniform random float in [lo, hi)
static void builtin_rand(VM& vm, u16 dst, u16, u16 ab) {
    f64 lo = vm.reg(ab).f, hi = vm.reg(ab+1).f;
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    vm.reg(dst).f = lo + u * (hi - lo);
}

// rands(lo, hi) -> List<Float>: infinite lazy list of uniform floats [lo, hi)
static void builtin_rands_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = ListNode::create(lt);
    auto* gen = new RandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).f;
    gen->hi_ = vm.reg(ab+1).f;
    gen->listType_ = lt;
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

// rands(n, lo, hi) -> [Float]: array of n uniform floats [lo, hi)
static void builtin_rands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    f64 lo = vm.reg(ab+1).f, hi = vm.reg(ab+2).f;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        f64 u = (x >> 11) * (1.0 / (1ULL << 53));
        r->v[i] = lo + u * (hi - lo);
    }
    vm.reg(dst).o = r;
}

// picks([T]) -> List<T>: infinite lazy list of random picks
void builtin_picks_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    auto* lt = vm.listType(at->elemType_);
    auto* node = ListNode::create(lt);
    auto* gen = new PicksListGen(vm.typeType());
    gen->array_ = arr;
    gen->elemType_ = at->elemType_;
    gen->listType_ = lt;
    node->installGenerator(gen);
    // Retain the array held by the generator
    vm.reg(dst).o = node;
}

// picks([T], Int) -> [T]: array of n random picks
template <typename T>
static void podArrayPicks(PodArray<T>* src, ArrayType* at, i64 n, size_t len,
                          Xoshiro256& rng, Word& out) {
    auto* r = new PodArray<T>(at);
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) r->v[i] = src->v[rng.next() % len];
    out.o = r;
}

void builtin_picks_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    ArrayBackend ba = arrayBackendFor(et);
    size_t len = getArraySize(arr, ba);
    if (n < 0) n = 0;
    switch (ba) {
        case ArrayBackend::Complex:
            podArrayPicks(static_cast<PodArray<x64>*>(arr), at, n, len, vm.rng(), vm.reg(dst));
            return;
        case ArrayBackend::Fraction:
            podArrayPicks(static_cast<PodArray<r64>*>(arr), at, n, len, vm.rng(), vm.reg(dst));
            return;
        case ArrayBackend::Float:
            podArrayPicks(static_cast<PodArray<f64>*>(arr), at, n, len, vm.rng(), vm.reg(dst));
            return;
        case ArrayBackend::Int:
            podArrayPicks(static_cast<PodArray<i64>*>(arr), at, n, len, vm.rng(), vm.reg(dst));
            return;
        case ArrayBackend::Inline: {
            auto* src = static_cast<InlineArray*>(arr);
            auto* r = new InlineArray(at);
            r->reserve((size_t)n);
            for (i64 i = 0; i < n; ++i) r->pushSlot(src->slot(vm.rng().next() % len));
            vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Obj: {
            auto* src = static_cast<ObjArray*>(arr);
            auto* r = new ObjArray(at);
            r->reserve((size_t)n);
            for (i64 i = 0; i < n; i++) r->push(src->get(vm.rng().next() % len));
            vm.reg(dst).o = r;
            return;
        }
    }
}

void builtin_sort_int_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<PodArray<i64>*>(vm.reg(ab).o);
    auto* r = new PodArray<i64>(static_cast<ArrayType*>(s->type_));
    r->v = s->v; std::sort(r->v.begin(), r->v.end()); vm.reg(dst).o = r;
}
void builtin_sort_float_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<PodArray<f64>*>(vm.reg(ab).o);
    auto* r = new PodArray<f64>(static_cast<ArrayType*>(s->type_));
    r->v = s->v; std::sort(r->v.begin(), r->v.end()); vm.reg(dst).o = r;
}
void builtin_sort_string_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<ObjArray*>(vm.reg(ab).o);
    auto* r = new ObjArray(static_cast<ArrayType*>(s->type_)); r->copyFrom(s);
    std::sort(r->rawVec().begin(), r->rawVec().end(), [](Obj* a, Obj* b) {
        return static_cast<StringObj*>(a)->s < static_cast<StringObj*>(b)->s;
    });
    vm.reg(dst).o = r;
}

// sort: ([T], (T,T)->Bool) -> [T]
void builtin_sort_by_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    u16 elemWords = isLambdaInlineComposite(paramT) ? paramT->sizeWords_ : 1;
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* result = makeEmptyArray(at);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        std::vector<size_t> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, a);
            placeLambdaArgFromArrayElem_t<B>(vm, (u16)(sb + elemWords), src, et, b);
            callTwoArgs(vm, fn, sb);
            return vm.reg(sb).i != 0;
        });
        for (size_t i = 0; i < n; i++)
            arrayPush(vm, result, et, getArrayElem(vm, src, et, idx[i]));
    });
    vm.reg(dst).o = result;
}

// grade: ([T], (T,T)->Bool) -> [Int]
void builtin_grade_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    u16 elemWords = isLambdaInlineComposite(paramT) ? paramT->sizeWords_ : 1;
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* result = new PodArray<i64>(vm.arrayType(vm.intType()));
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        std::vector<size_t> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, a);
            placeLambdaArgFromArrayElem_t<B>(vm, (u16)(sb + elemWords), src, et, b);
            callTwoArgs(vm, fn, sb);
            return vm.reg(sb).i != 0;
        });
        result->v.resize(n);
        for (size_t i = 0; i < n; i++) result->v[i] = (i64)idx[i];
    });
    vm.reg(dst).o = result;
}

// ============================================================================
// Array: take, drop, stride, stutter, cat, join, flatten
// ============================================================================

void builtin_take_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    if (arrayBackendFor(at->elemType_) == ArrayBackend::Inline) {
        auto* s = static_cast<InlineArray*>(src);
        auto* r = new InlineArray(at);
        size_t take = n < 0 ? 0 : ((size_t)n > s->size() ? s->size() : (size_t)n);
        r->reserve(take);
        for (size_t i = 0; i < take; ++i) r->pushSlot(s->slot(i));
        vm.reg(dst).o = r;
        return;
    }
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        size_t take = n < 0 ? 0 : ((size_t)n > sv.size() ? sv.size() : (size_t)n);
        rv.resize(take);
        for (size_t i = 0; i < take; i++) rv[i] = sv[i];
    });
}

void builtin_drop_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    if (arrayBackendFor(at->elemType_) == ArrayBackend::Inline) {
        auto* s = static_cast<InlineArray*>(src);
        auto* r = new InlineArray(at);
        size_t drop = n < 0 ? 0 : ((size_t)n > s->size() ? s->size() : (size_t)n);
        r->reserve(s->size() - drop);
        for (size_t i = drop; i < s->size(); ++i) r->pushSlot(s->slot(i));
        vm.reg(dst).o = r;
        return;
    }
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        size_t drop = n < 0 ? 0 : ((size_t)n > sv.size() ? sv.size() : (size_t)n);
        rv.resize(sv.size() - drop);
        for (size_t i = 0; i < rv.size(); i++) rv[i] = sv[drop+i];
    });
}

void builtin_stride_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    if (arrayBackendFor(at->elemType_) == ArrayBackend::Inline) {
        auto* s = static_cast<InlineArray*>(src);
        auto* r = new InlineArray(at);
        if (n > 0) {
            for (size_t i = 0; i < s->size(); i += (size_t)n) r->pushSlot(s->slot(i));
        }
        vm.reg(dst).o = r;
        return;
    }
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        if (n <= 0) return;
        for (size_t i = 0; i < sv.size(); i += (size_t)n) rv.push_back(sv[i]);
    });
}

void builtin_stutter_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    if (arrayBackendFor(at->elemType_) == ArrayBackend::Inline) {
        auto* s = static_cast<InlineArray*>(src);
        auto* r = new InlineArray(at);
        if (n > 0) {
            for (size_t i = 0; i < s->size(); ++i) {
                for (i64 k = 0; k < n; ++k) r->pushSlot(s->slot(i));
            }
        }
        vm.reg(dst).o = r;
        return;
    }
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        if (n <= 0) return;
        for (size_t i = 0; i < sv.size(); i++)
            for (i64 j = 0; j < n; j++) rv.push_back(sv[i]);
    });
}

#define REPEAT_VALUETYPE(suffix, typeGetter, field, PodT) \
void builtin_repeat_##suffix(VM& vm, u16 dst, u16, u16 ab) { \
    auto val = vm.reg(ab).field; i64 n = vm.reg(ab+1).i; \
    if (n < 0) n = 0; \
    auto* at = vm.arrayType(vm.typeGetter()); \
    auto* arr = new PodArray<PodT>(at); \
    arr->v.resize((size_t)n, val); \
    vm.reg(dst).o = arr; \
}
REPEAT_VALUETYPE(int,    intType,    i, i64)
REPEAT_VALUETYPE(float,  floatType,  f, f64)
REPEAT_VALUETYPE(bool,   boolType,   i, i64)
REPEAT_VALUETYPE(symbol, symbolType, i, i64)
#undef REPEAT_VALUETYPE

// Phase 4g.27: dispatch on element type's array backend so Inline composite
// repeat lands in an InlineArray with the right stride. Reads the element
// natively from ab.. (multi-word for Inline composite, 1 word otherwise).
void builtin_repeat_obj(VM& vm, u16 dst, u16, u16 ab) {
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* et = primTT->fields_[0];
    u32 etSW = (et && et->sizeWords_ > 0) ? et->sizeWords_ : 1;
    i64 n = vm.reg((u16)(ab + etSW)).i;
    if (n < 0) n = 0;
    auto* at = vm.arrayType(et);
    Word const* src = &vm.reg(ab);
    switch (arrayBackendFor(et)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(at);
            arr->v.resize((size_t)n, x64(src[0].f, src[1].f));
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(at);
            arr->v.resize((size_t)n, r64(src[0].i, src[1].i, true));
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Inline: {
            auto* arr = new InlineArray(at);
            arr->reserve((size_t)n);
            for (i64 i = 0; i < n; ++i) arr->pushSlot(src);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(at);
            arr->reserve((size_t)n);
            for (i64 i = 0; i < n; ++i) arr->push(src[0].o);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Int:
        case ArrayBackend::Float:
            // _int/_float/_bool/_symbol resolve to dedicated implementations
            // above; we never reach here for those.
            break;
    }
}

// Phase 4e: dispatch via arrayBackendFor.
template <typename T>
static void podArrayCat(PodArray<T>* a, PodArray<T>* b, ArrayType* at, Word& out) {
    auto* r = new PodArray<T>(at);
    r->v = a->v;
    for (auto const& x : b->v) r->v.push_back(x);
    out.o = r;
}

void builtin_cat_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = vm.reg(ab).o; auto* b = vm.reg(ab+1).o;
    auto* at = static_cast<ArrayType*>(a->type_);
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex:
            podArrayCat(static_cast<PodArray<x64>*>(a), static_cast<PodArray<x64>*>(b), at, vm.reg(dst));
            return;
        case ArrayBackend::Fraction:
            podArrayCat(static_cast<PodArray<r64>*>(a), static_cast<PodArray<r64>*>(b), at, vm.reg(dst));
            return;
        case ArrayBackend::Float:
            podArrayCat(static_cast<PodArray<f64>*>(a), static_cast<PodArray<f64>*>(b), at, vm.reg(dst));
            return;
        case ArrayBackend::Int:
            podArrayCat(static_cast<PodArray<i64>*>(a), static_cast<PodArray<i64>*>(b), at, vm.reg(dst));
            return;
        case ArrayBackend::Inline: {
            auto* sa = static_cast<InlineArray*>(a);
            auto* sb = static_cast<InlineArray*>(b);
            auto* r = new InlineArray(at);
            r->reserve(sa->size() + sb->size());
            for (size_t i = 0; i < sa->size(); ++i) r->pushSlot(sa->slot(i));
            for (size_t i = 0; i < sb->size(); ++i) r->pushSlot(sb->slot(i));
            vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Obj: {
            auto* sa = static_cast<ObjArray*>(a);
            auto* sb = static_cast<ObjArray*>(b);
            auto* r = new ObjArray(at);
            r->reserve(sa->size() + sb->size());
            for (auto* obj : *sa) r->push(obj);
            for (auto* obj : *sb) r->push(obj);
            vm.reg(dst).o = r;
            return;
        }
    }
}

void builtin_join_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* outerType = static_cast<ArrayType*>(src->type_);
    auto* innerType = dynamic_cast<ArrayType*>(outerType->elemType_);
    if (!innerType) { vm.reg(dst).o = src; return; }
    // Outer array is itself an Array[Array[_]]; outerType's elemType is
    // ArrayType, which is always a Pointer backend, so the outer array is an
    // ObjArray of inner array pointers.
    auto* outer = static_cast<ObjArray*>(src);
    Type* et = innerType->elemType_;
    ArrayBackend ba = arrayBackendFor(et);
    if (ba == ArrayBackend::Inline) {
        auto* result = new InlineArray(innerType);
        for (size_t i = 0; i < outer->size(); i++) {
            auto* inner = static_cast<InlineArray*>(outer->get(i));
            if (!inner) continue;
            for (size_t j = 0; j < inner->size(); ++j)
                result->pushSlot(inner->slot(j));
        }
        vm.reg(dst).o = result;
        return;
    }
    auto* result = makeEmptyArray(innerType);
    for (size_t i = 0; i < outer->size(); i++) {
        if (!outer->get(i)) continue;
        size_t n = getArraySize(outer->get(i), ba);
        for (size_t j = 0; j < n; j++) arrayPush(vm, result, et, getArrayElem(vm, outer->get(i), et, j));
    }
    vm.reg(dst).o = result;
}

// flatten = join for now (one level)
// Recursively collect leaf elements from nested arrays into result.
static void flattenArrayInto(VM& vm, Obj* arr, Obj* result, Type* leafType) {
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    if (dynamic_cast<ArrayType*>(et)) {
        // Elements are sub-arrays — recurse
        auto* oa = static_cast<ObjArray*>(arr);
        for (size_t i = 0; i < oa->size(); i++) {
            if (oa->get(i)) flattenArrayInto(vm, oa->get(i), result, leafType);
        }
    } else if (arrayBackendFor(leafType) == ArrayBackend::Inline) {
        auto* inArr = static_cast<InlineArray*>(arr);
        auto* outArr = static_cast<InlineArray*>(result);
        for (size_t i = 0; i < inArr->size(); ++i)
            outArr->pushSlot(inArr->slot(i));
    } else {
        // Leaf level — copy elements into result
        size_t n = getArraySize(vm, arr, et);
        for (size_t i = 0; i < n; i++) {
            arrayPush(vm, result, leafType, getArrayElem(vm, arr, et, i));
        }
    }
}

void builtin_flatten_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* at = static_cast<ArrayType*>(src->type_);
    // Find innermost non-Array element type
    Type* leafType = at->elemType_;
    int depth = 0;
    while (auto* inner = dynamic_cast<ArrayType*>(leafType)) {
        leafType = inner->elemType_; depth++;
    }
    (void)depth;
    // If element type was not an array, nothing to flatten
    if (leafType == at->elemType_) { vm.reg(dst).o = src; return; }
    auto* resultType = vm.arrayType(leafType);
    auto* result = makeEmptyArray(resultType);
    vm.reg(dst).o = result;  // root against GC
    flattenArrayInto(vm, src, result, leafType);
}

// ============================================================================
// Array HOF: map, filter, fold, scan, fold1, scan1, find, takeWhile, dropWhile
// ============================================================================

void builtin_map_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* srcType = static_cast<ArrayType*>(src->type_);
    Type* srcET = srcType->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* resET = fnType->returnType_;
    auto* resAT = vm.arrayType(resET);
    auto* result = makeEmptyArray(resAT);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});  // result lives only here across user calls
    u16 sb = vm.currentCodeBlock()->numRegs;
    dispatchBackend(arrayBackendFor(srcET), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, srcET, i);
            callOneArg(vm, fn, sb);
            readLambdaResult(vm, sb, resET);
            arrayPush(vm, result, resET, vm.reg(sb));
        }
    });
    vm.reg(dst).o = result;
}

void builtin_filter_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* result = makeEmptyArray(at);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});  // result lives only here across user calls
    u16 sb = vm.currentCodeBlock()->numRegs;
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, i);
            callOneArg(vm, fn, sb);
            if (vm.reg(sb).i) arrayPush(vm, result, et, getArrayElem(vm, src, et, i));
        }
    });
    vm.reg(dst).o = result;
}

void builtin_fold_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* accT = primTT->fields_[1];
    // Phase 4g.27: native ABI at the builtin boundary. acc spans accSW
    // words at ab+1..; fn lives just after. The acc stays in registers
    // sb..sb+accSW-1 across iterations -- the lambda reads its first arg
    // there and op_return writes the new acc back to the same slot.
    u32 accSW = (accT && accT->sizeWords_ > 0) ? accT->sizeWords_ : 1;
    auto* fn = static_cast<Callable*>(vm.reg((u16)(ab + 1 + accSW)).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    u16 sb = vm.currentCodeBlock()->numRegs;

    Word const* accSrc = &vm.reg((u16)(ab + 1));
    for (u32 i = 0; i < accSW; ++i) vm.reg(sb + i) = accSrc[i];
    u16 elemSb = (u16)(sb + accSW);

    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, elemSb, src, et, i);
            callTwoArgs(vm, fn, sb);
        }
    });

    for (u32 i = 0; i < accSW; ++i) vm.reg(dst + i) = vm.reg(sb + i);
}

void builtin_scan_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* accT = primTT->fields_[1];
    // Phase 4g.27: native ABI; see fold_array.
    u32 accSW = (accT && accT->sizeWords_ > 0) ? accT->sizeWords_ : 1;
    auto* fn = static_cast<Callable*>(vm.reg((u16)(ab + 1 + accSW)).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* accET = fnType->returnType_;
    auto* resAT = vm.arrayType(accET);
    auto* result = makeEmptyArray(resAT);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});
    u16 sb = vm.currentCodeBlock()->numRegs;

    Word const* accSrc = &vm.reg((u16)(ab + 1));
    for (u32 i = 0; i < accSW; ++i) vm.reg(sb + i) = accSrc[i];
    arrayPushFromSlot(vm, result, accET, &vm.reg(sb));
    u16 elemSb = (u16)(sb + accSW);

    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, elemSb, src, et, i);
            callTwoArgs(vm, fn, sb);
            arrayPushFromSlot(vm, result, accET, &vm.reg(sb));
        }
    });
    vm.reg(dst).o = result;
}

void builtin_fold1_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    // Phase 4g.27: native ABI; acc spans accSW words at sb.
    u32 accSW = (et && et->sizeWords_ > 0) ? et->sizeWords_ : 1;
    u16 sb = vm.currentCodeBlock()->numRegs;
    u16 elemSb = (u16)(sb + accSW);
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        if (n == 0) { vm.reg(dst).i = 0; return; }
        placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, 0);  // acc = src[0]
        for (size_t i = 1; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, elemSb, src, et, i);
            callTwoArgs(vm, fn, sb);
        }
        for (u32 i = 0; i < accSW; ++i) vm.reg(dst + i) = vm.reg(sb + i);
    });
}

void builtin_scan1_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* result = makeEmptyArray(at);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});
    // Phase 4g.27: native ABI; acc spans accSW words at sb.
    u32 accSW = (et && et->sizeWords_ > 0) ? et->sizeWords_ : 1;
    u16 sb = vm.currentCodeBlock()->numRegs;
    u16 elemSb = (u16)(sb + accSW);
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        if (n == 0) return;
        placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, 0);
        arrayPushFromSlot(vm, result, et, &vm.reg(sb));
        for (size_t i = 1; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, elemSb, src, et, i);
            callTwoArgs(vm, fn, sb);
            arrayPushFromSlot(vm, result, et, &vm.reg(sb));
        }
    });
    vm.reg(dst).o = result;
}

void builtin_find_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    i64 found = -1;
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, i);
            callOneArg(vm, fn, sb);
            if (vm.reg(sb).i) { found = (i64)i; return; }
        }
    });
    vm.reg(dst).i = found;
}

void builtin_takeWhile_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* result = makeEmptyArray(at);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});
    u16 sb = vm.currentCodeBlock()->numRegs;
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, i);
            callOneArg(vm, fn, sb);
            if (!vm.reg(sb).i) return;
            arrayPush(vm, result, et, getArrayElem(vm, src, et, i));
        }
    });
    vm.reg(dst).o = result;
}

void builtin_dropWhile_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* result = makeEmptyArray(at);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn, result});
    u16 sb = vm.currentCodeBlock()->numRegs;
    bool dropping = true;
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            if (dropping) {
                placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, i);
                callOneArg(vm, fn, sb);
                if (vm.reg(sb).i) continue;
                dropping = false;
            }
            arrayPush(vm, result, et, getArrayElem(vm, src, et, i));
        }
    });
    vm.reg(dst).o = result;
}

// ============================================================================
// Array: zip, enumerate
// ============================================================================

void builtin_zip_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = vm.reg(ab).o; auto* b = vm.reg(ab+1).o;
    auto* atA = static_cast<ArrayType*>(a->type_);
    auto* atB = static_cast<ArrayType*>(b->type_);
    Type* etA = atA->elemType_; Type* etB = atB->elemType_;
    ArrayBackend baA = arrayBackendFor(etA);
    ArrayBackend baB = arrayBackendFor(etB);
    size_t na = getArraySize(a, baA), nb = getArraySize(b, baB);
    size_t n = na < nb ? na : nb;
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(etA); fields.push_back(etB);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    // Phase 4g.8: Array of Inline tuple lives in InlineArray, stride = the
    // tuple's sizeWords (e.g. 2 for (Int, Int), 2 for (Int, String)).
    if (tt->repr_ == ts::Type::Repr::Inline) {
        auto* result = new InlineArray(resAT);
        result->reserve(n);
        Word scratch[8] = {};
        for (size_t i = 0; i < n; i++) {
            // Lay the tuple's two field payloads at their wordOffsets.
            for (u32 k = 0; k < tt->sizeWords_; ++k) scratch[k].i = 0;
            auto const& f0 = tt->layout_[0];
            auto const& f1 = tt->layout_[1];
            Word wa = getArrayElem(vm, a, etA, i);
            Word wb = getArrayElem(vm, b, etB, i);
            // Phase 4g.23: unboxInlineDeepTo handles Complex/Fraction since
            // 4g.20, so the only single-Word case left is atom/pointer fields.
            if (f0.type && f0.type->repr_ == ts::Type::Repr::Inline) {
                unboxInlineDeepTo(vm, f0.type, wa.o, scratch + f0.wordOffset);
            } else {
                scratch[f0.wordOffset] = wa;
            }
            if (f1.type && f1.type->repr_ == ts::Type::Repr::Inline) {
                unboxInlineDeepTo(vm, f1.type, wb.o, scratch + f1.wordOffset);
            } else {
                scratch[f1.wordOffset] = wb;
            }
            result->pushSlot(scratch);
            // pushSlot retained again; balance by walking scratch with release.
        }
        vm.reg(dst).o = result;
        return;
    }
    // Phase 4g.13: write each field natively per layout. getArrayElem may
    // return a 1-Word boxed Inline composite; unbox into the field's
    // multi-word slot in that case.
    auto writeTupleField = [&](Tuple* t, ts::FieldLayout const& f, Word src) {
        Type* ft = f.type;
        if (f.sizeWords > 1) {
            if (ft == vm.complexType()) {
                auto* c = static_cast<Complex*>(src.o);
                t->v[f.wordOffset].f     = c->x.real();
                t->v[f.wordOffset + 1].f = c->x.imag();
            } else if (ft == vm.fractionType()) {
                auto* fr = static_cast<Fraction*>(src.o);
                t->v[f.wordOffset].i     = fr->r.numer();
                t->v[f.wordOffset + 1].i = fr->r.denom();
            } else {
                unboxInlineDeepTo(vm, ft, src.o, &t->v[f.wordOffset]);
            }
        } else {
            t->v[f.wordOffset] = src;
        }
    };
    auto const& f0 = tt->layout_[0];
    auto const& f1 = tt->layout_[1];
    auto* result = new ObjArray(resAT);
    for (size_t i = 0; i < n; i++) {
        auto* tup = Tuple::create(tt, 2);
        writeTupleField(tup, f0, getArrayElem(vm, a, etA, i));
        writeTupleField(tup, f1, getArrayElem(vm, b, etB, i));
        result->push(tup);
    }
    vm.reg(dst).o = result;
}

void builtin_enumerate_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    ArrayBackend ba = arrayBackendFor(et);
    size_t n = getArraySize(src, ba);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(vm.intType()); fields.push_back(et);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    // Phase 4g.8: Array of Inline tuple lives in InlineArray.
    if (tt->repr_ == ts::Type::Repr::Inline) {
        auto* result = new InlineArray(resAT);
        result->reserve(n);
        Word scratch[8] = {};
        for (size_t i = 0; i < n; i++) {
            for (u32 k = 0; k < tt->sizeWords_; ++k) scratch[k].i = 0;
            auto const& f0 = tt->layout_[0];
            auto const& f1 = tt->layout_[1];
            scratch[f0.wordOffset].i = (i64)i;
            Word elem = getArrayElem(vm, src, et, i);
            // Phase 4g.23: unboxInlineDeepTo handles Complex/Fraction.
            if (f1.type && f1.type->repr_ == ts::Type::Repr::Inline) {
                unboxInlineDeepTo(vm, f1.type, elem.o, scratch + f1.wordOffset);
            } else {
                scratch[f1.wordOffset] = elem;
            }
            result->pushSlot(scratch);
        }
        vm.reg(dst).o = result;
        return;
    }
    // Phase 4g.13: same layout-aware write as zip's ObjArray branch.
    auto writeTupleField = [&](Tuple* t, ts::FieldLayout const& f, Word src) {
        Type* ft = f.type;
        if (f.sizeWords > 1) {
            if (ft == vm.complexType()) {
                auto* c = static_cast<Complex*>(src.o);
                t->v[f.wordOffset].f     = c->x.real();
                t->v[f.wordOffset + 1].f = c->x.imag();
            } else if (ft == vm.fractionType()) {
                auto* fr = static_cast<Fraction*>(src.o);
                t->v[f.wordOffset].i     = fr->r.numer();
                t->v[f.wordOffset + 1].i = fr->r.denom();
            } else {
                unboxInlineDeepTo(vm, ft, src.o, &t->v[f.wordOffset]);
            }
        } else {
            t->v[f.wordOffset] = src;
        }
    };
    auto const& f0 = tt->layout_[0];
    auto const& f1 = tt->layout_[1];
    auto* result = new ObjArray(resAT);
    for (size_t i = 0; i < n; i++) {
        auto* tup = Tuple::create(tt, 2);
        writeTupleField(tup, f0, Word((i64)i));
        writeTupleField(tup, f1, getArrayElem(vm, src, et, i));
        result->push(tup);
    }
    vm.reg(dst).o = result;
}

// length: [T] -> Int
void builtin_length_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    vm.reg(dst).i = (i64)getArraySize(vm, src, at->elemType_);
}

// ============================================================================
// Aggregates: sum, product, mean, any, all, contains
// ============================================================================

void builtin_sum_int_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    i64 acc = 0;
    for (i64 x : v) acc += x;
    vm.reg(dst).i = acc;
}

void builtin_sum_float_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<f64>*>(vm.reg(ab).o)->v;
    f64 acc = 0.0;
    for (f64 x : v) acc += x;
    vm.reg(dst).f = acc;
}

void builtin_product_int_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    i64 acc = 1;
    for (i64 x : v) acc *= x;
    vm.reg(dst).i = acc;
}

void builtin_product_float_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<f64>*>(vm.reg(ab).o)->v;
    f64 acc = 1.0;
    for (f64 x : v) acc *= x;
    vm.reg(dst).f = acc;
}

// mean of an empty collection is nan (0/0), matching the float division.
void builtin_mean_int_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    f64 acc = 0.0;
    for (i64 x : v) acc += (f64)x;
    vm.reg(dst).f = acc / (f64)v.size();
}

void builtin_mean_float_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<f64>*>(vm.reg(ab).o)->v;
    f64 acc = 0.0;
    for (f64 x : v) acc += x;
    vm.reg(dst).f = acc / (f64)v.size();
}

void builtin_any_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    bool found = false;
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, i);
            callOneArg(vm, fn, sb);
            if (vm.reg(sb).i) { found = true; return; }
        }
    });
    vm.reg(dst).i = found ? 1 : 0;
}

void builtin_all_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    bool holds = true;
    dispatchBackend(arrayBackendFor(et), [&]<ArrayBackend B>() {
        size_t n = getArraySize_t<B>(src);
        for (size_t i = 0; i < n; i++) {
            placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, i);
            callOneArg(vm, fn, sb);
            if (!vm.reg(sb).i) { holds = false; return; }
        }
    });
    vm.reg(dst).i = holds ? 1 : 0;
}

void builtin_any_bool_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    for (i64 x : v) if (x) { vm.reg(dst).i = 1; return; }
    vm.reg(dst).i = 0;
}

void builtin_all_bool_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    for (i64 x : v) if (!x) { vm.reg(dst).i = 0; return; }
    vm.reg(dst).i = 1;
}

void builtin_contains_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    Word const* target = &vm.reg((u16)(ab + 1));
    size_t n = getArraySize(src, arrayBackendFor(et));
    Word tmp[2];
    for (size_t i = 0; i < n; ++i) {
        if (wordsEqual(arrayElemSlot(src, et, i, tmp), target, et)) {
            vm.reg(dst).i = 1;
            return;
        }
    }
    vm.reg(dst).i = 0;
}

// ============================================================================
// Reshaping: clump, spread, ncyc, toSet, fromCodePoints
// ============================================================================

// clump: [T], Int -> [[T]] -- group into rows of n; a short remainder row
// is kept. n <= 0 yields [].
void builtin_clump_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    i64 k = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* outer = new ObjArray(vm.arrayType(at));
    if (k > 0) {
        size_t n = getArraySize(src, arrayBackendFor(et));
        Word tmp[2];
        for (size_t start = 0; start < n; start += (size_t)k) {
            size_t end = std::min(n, start + (size_t)k);
            Obj* chunk = makeEmptyArray(at);
            for (size_t i = start; i < end; ++i)
                arrayPushFromSlot(vm, chunk, et, arrayElemSlot(src, et, i, tmp));
            outer->push(chunk);
        }
    }
    vm.reg(dst).o = outer;
}

// stutter: [T], [Int] -> [T] -- per-element counts overload: replicate
// element i counts[i] times; counts index cyclically (like array indexing).
// counts <= 0 drop the element.
void builtin_stutter_counts_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto& counts = static_cast<PodArray<i64>*>(vm.reg(ab+1).o)->v;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* result = makeEmptyArray(at);
    if (!counts.empty()) {
        size_t n = getArraySize(src, arrayBackendFor(et));
        Word tmp[2];
        for (size_t i = 0; i < n; ++i) {
            i64 c = counts[i % counts.size()];
            Word const* slot = arrayElemSlot(src, et, i, tmp);
            for (i64 j = 0; j < c; ++j) arrayPushFromSlot(vm, result, et, slot);
        }
    }
    vm.reg(dst).o = result;
}

// ncyc: [T], Int -> [T] -- the array repeated n times.
void builtin_ncyc_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    i64 k = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* result = makeEmptyArray(at);
    size_t n = getArraySize(src, arrayBackendFor(et));
    Word tmp[2];
    for (i64 r = 0; r < k; ++r)
        for (size_t i = 0; i < n; ++i)
            arrayPushFromSlot(vm, result, et, arrayElemSlot(src, et, i, tmp));
    vm.reg(dst).o = result;
}

void builtin_toSet_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* set = new SetObj(vm.setType(et));
    size_t n = getArraySize(src, arrayBackendFor(et));
    Word tmp[2];
    for (size_t i = 0; i < n; ++i)
        set->insertElem(arrayElemSlot(src, et, i, tmp));
    vm.reg(dst).o = set;
}

// fromCodePoints: [Int] -> String -- inverse of codePoints.
void builtin_fromCodePoints_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    auto* so = new StringObj();
    for (i64 cp : v) appendUtf8Cp(so->s, cp);
    vm.reg(dst).o = so;
}

// Fraction/Complex reductions write the native 2-word result at dst/dst+1.
void builtin_sum_fraction_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<r64>*>(vm.reg(ab).o)->v;
    r64 acc(0);
    for (r64 x : v) acc = acc + x;
    vm.reg(dst).i = acc.numer();
    vm.reg((u16)(dst + 1)).i = acc.denom();
}

void builtin_product_fraction_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<r64>*>(vm.reg(ab).o)->v;
    r64 acc(1);
    for (r64 x : v) acc = acc * x;
    vm.reg(dst).i = acc.numer();
    vm.reg((u16)(dst + 1)).i = acc.denom();
}

void builtin_sum_complex_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<x64>*>(vm.reg(ab).o)->v;
    x64 acc(0.0, 0.0);
    for (x64 x : v) acc += x;
    vm.reg(dst).f = acc.real();
    vm.reg((u16)(dst + 1)).f = acc.imag();
}

void builtin_product_complex_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<x64>*>(vm.reg(ab).o)->v;
    x64 acc(1.0, 0.0);
    for (x64 x : v) acc *= x;
    vm.reg(dst).f = acc.real();
    vm.reg((u16)(dst + 1)).f = acc.imag();
}

static void minMaxFractionArray(VM& vm, u16 dst, u16 ab, bool wantMax) {
    auto& v = static_cast<PodArray<r64>*>(vm.reg(ab).o)->v;
    // Seed from the first element rather than an INT64_MAX/MIN sentinel:
    // rational comparison cross-multiplies, so a sentinel that large
    // overflows and compares wrong. Empty input keeps the sentinel.
    r64 acc(wantMax ? std::numeric_limits<i64>::min()
                    : std::numeric_limits<i64>::max());
    bool first = true;
    for (r64 x : v) {
        if (first) { acc = x; first = false; }
        else if (wantMax ? (acc < x) : (x < acc)) acc = x;
    }
    vm.reg(dst).i = acc.numer();
    vm.reg((u16)(dst + 1)).i = acc.denom();
}

void builtin_min_fraction_array(VM& vm, u16 dst, u16, u16 ab) { minMaxFractionArray(vm, dst, ab, false); }
void builtin_max_fraction_array(VM& vm, u16 dst, u16, u16 ab) { minMaxFractionArray(vm, dst, ab, true); }

// min/max reductions. Empty arrays yield the reduction identity (Int:
// INT64_MAX/MIN, Float: +/-inf, String: "").
void builtin_min_int_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    i64 acc = std::numeric_limits<i64>::max();
    for (i64 x : v) if (x < acc) acc = x;
    vm.reg(dst).i = acc;
}

void builtin_max_int_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<i64>*>(vm.reg(ab).o)->v;
    i64 acc = std::numeric_limits<i64>::min();
    for (i64 x : v) if (x > acc) acc = x;
    vm.reg(dst).i = acc;
}

void builtin_min_float_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<f64>*>(vm.reg(ab).o)->v;
    f64 acc = std::numeric_limits<f64>::infinity();
    for (f64 x : v) if (x < acc) acc = x;
    vm.reg(dst).f = acc;
}

void builtin_max_float_array(VM& vm, u16 dst, u16, u16 ab) {
    auto& v = static_cast<PodArray<f64>*>(vm.reg(ab).o)->v;
    f64 acc = -std::numeric_limits<f64>::infinity();
    for (f64 x : v) if (x > acc) acc = x;
    vm.reg(dst).f = acc;
}

static void minMaxStringArray(VM& vm, u16 dst, u16 ab, bool wantMax) {
    auto* a = static_cast<ObjArray*>(vm.reg(ab).o);
    StringObj* acc = nullptr;
    for (size_t i = 0; i < a->size(); ++i) {
        auto* x = static_cast<StringObj*>(a->get(i));
        if (!acc || (wantMax ? (x->s > acc->s) : (x->s < acc->s))) acc = x;
    }
    vm.reg(dst).o = acc ? (Obj*)acc : (Obj*)new StringObj();
}

void builtin_min_string_array(VM& vm, u16 dst, u16, u16 ab) { minMaxStringArray(vm, dst, ab, false); }
void builtin_max_string_array(VM& vm, u16 dst, u16, u16 ab) { minMaxStringArray(vm, dst, ab, true); }

// Running reductions: sums / products / mins / maxs. Element i of the
// result is op(x0..xi); same length as the input.
template <typename T, typename F>
static void runningPodArray(VM& vm, u16 dst, u16 ab, F&& step) {
    auto* s = static_cast<PodArray<T>*>(vm.reg(ab).o);
    auto* r = new PodArray<T>(static_cast<ArrayType*>(s->type_));
    r->v.reserve(s->v.size());
    T acc{};
    bool first = true;
    for (T x : s->v) {
        acc = first ? x : step(acc, x);
        first = false;
        r->v.push_back(acc);
    }
    vm.reg(dst).o = r;
}

void builtin_sums_int_array(VM& vm, u16 dst, u16, u16 ab)       { runningPodArray<i64>(vm, dst, ab, [](i64 a, i64 b) { return a + b; }); }
void builtin_sums_float_array(VM& vm, u16 dst, u16, u16 ab)     { runningPodArray<f64>(vm, dst, ab, [](f64 a, f64 b) { return a + b; }); }
void builtin_products_int_array(VM& vm, u16 dst, u16, u16 ab)   { runningPodArray<i64>(vm, dst, ab, [](i64 a, i64 b) { return a * b; }); }
void builtin_products_float_array(VM& vm, u16 dst, u16, u16 ab) { runningPodArray<f64>(vm, dst, ab, [](f64 a, f64 b) { return a * b; }); }
void builtin_mins_int_array(VM& vm, u16 dst, u16, u16 ab)       { runningPodArray<i64>(vm, dst, ab, [](i64 a, i64 b) { return b < a ? b : a; }); }
void builtin_mins_float_array(VM& vm, u16 dst, u16, u16 ab)     { runningPodArray<f64>(vm, dst, ab, [](f64 a, f64 b) { return b < a ? b : a; }); }
void builtin_maxs_int_array(VM& vm, u16 dst, u16, u16 ab)       { runningPodArray<i64>(vm, dst, ab, [](i64 a, i64 b) { return b > a ? b : a; }); }
void builtin_maxs_float_array(VM& vm, u16 dst, u16, u16 ab)     { runningPodArray<f64>(vm, dst, ab, [](f64 a, f64 b) { return b > a ? b : a; }); }
void builtin_sums_fraction_array(VM& vm, u16 dst, u16, u16 ab)     { runningPodArray<r64>(vm, dst, ab, [](r64 a, r64 b) { return a + b; }); }
void builtin_products_fraction_array(VM& vm, u16 dst, u16, u16 ab) { runningPodArray<r64>(vm, dst, ab, [](r64 a, r64 b) { return a * b; }); }
void builtin_mins_fraction_array(VM& vm, u16 dst, u16, u16 ab)     { runningPodArray<r64>(vm, dst, ab, [](r64 a, r64 b) { return b < a ? b : a; }); }
void builtin_maxs_fraction_array(VM& vm, u16 dst, u16, u16 ab)     { runningPodArray<r64>(vm, dst, ab, [](r64 a, r64 b) { return a < b ? b : a; }); }
void builtin_sums_complex_array(VM& vm, u16 dst, u16, u16 ab)      { runningPodArray<x64>(vm, dst, ab, [](x64 a, x64 b) { return a + b; }); }
void builtin_products_complex_array(VM& vm, u16 dst, u16, u16 ab)  { runningPodArray<x64>(vm, dst, ab, [](x64 a, x64 b) { return a * b; }); }

// ============================================================================
// Registration
// ============================================================================

void registerArrayBuiltins(Compiler& compiler, FuncMap& functions)
{
    Type* Int = compiler.intType();
    Type* Float = compiler.floatType();

    // --- random number generation (impure) ---
    registerOne(compiler, functions, "randSeed", compiler.voidType(), {Int}, builtin_randseed, /*pure=*/false);
    registerOne(compiler, functions, "urand", Float, {}, builtin_urand, /*pure=*/false);
    registerOne(compiler, functions, "brand", Float, {}, builtin_brand, /*pure=*/false);
    registerOne(compiler, functions, "irand", Int,   {Int, Int},     builtin_irand, /*pure=*/false);
    registerOne(compiler, functions, "rand",  Float, {Float, Float}, builtin_rand, /*pure=*/false);
    registerOne(compiler, functions, "xrand", Float, {Float, Float}, builtin_xrand, /*pure=*/false);

    auto* FloatList = compiler.listType(Float);
    auto* IntList   = compiler.listType(Int);
    auto* FloatArr  = compiler.arrayType(Float);
    auto* IntArr    = compiler.arrayType(Int);
    registerOne(compiler, functions, "urands", FloatList, {}, builtin_urands_list, /*pure=*/false);
    registerOne(compiler, functions, "urands", FloatArr, {Int}, builtin_urands_array, /*pure=*/false);
    registerOne(compiler, functions, "brands", FloatList, {}, builtin_brands_list, /*pure=*/false);
    registerOne(compiler, functions, "brands", FloatArr, {Int}, builtin_brands_array, /*pure=*/false);
    registerOne(compiler, functions, "irands", IntList, {Int, Int}, builtin_irands_list, /*pure=*/false);
    registerOne(compiler, functions, "irands", IntArr, {Int, Int, Int}, builtin_irands_array, /*pure=*/false);
    registerOne(compiler, functions, "rands", FloatList, {Float, Float}, builtin_rands_list, /*pure=*/false);
    registerOne(compiler, functions, "rands", FloatArr, {Int, Float, Float}, builtin_rands_array, /*pure=*/false);
    registerOne(compiler, functions, "xrands", FloatList, {Float, Float}, builtin_xrands_list, /*pure=*/false);
    registerOne(compiler, functions, "xrands", FloatArr, {Int, Float, Float}, builtin_xrands_array, /*pure=*/false);
}

} // namespace ts
