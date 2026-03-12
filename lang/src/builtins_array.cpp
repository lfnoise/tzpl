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
    txArray(vm, dst, src, at, [](auto& sv, auto& rv) {
        size_t n = sv.size(); rv.resize(n);
        for (size_t i = 0; i < n; i++) rv[i] = sv[n-1-i];
    });
}

void builtin_push_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* s = static_cast<PodArray<i64>*>(src);
        auto* r = new PodArray<i64>(at); r->v = s->v;
        r->v.push_back(vm.reg(ab+1).i); vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* s = static_cast<PodArray<f64>*>(src);
        auto* r = new PodArray<f64>(at); r->v = s->v;
        r->v.push_back(vm.reg(ab+1).f); vm.reg(dst).o = r;
    } else {
        auto* s = static_cast<ObjArray*>(src);
        auto* r = new ObjArray(at); r->v = s->v;
        r->v.push_back(vm.reg(ab+1).o); vm.reg(dst).o = r;
    }
}

void builtin_pop_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [](auto& sv, auto& rv) {
        if (!sv.empty()) { rv.resize(sv.size()-1);
            for (size_t i = 0; i < rv.size(); i++) rv[i] = sv[i]; }
    });
}

void builtin_muss_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    auto& rng = vm.rng();
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

// pick(Array[T]) -> T  choose a random element
void builtin_pick_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    size_t n = getArraySize(vm, arr, at->elemType_);
    size_t idx = vm.rng().next() % n;
    vm.reg(dst) = getArrayElem(vm, arr, at->elemType_, idx);
}

// urands() -> List<Float>: infinite lazy list of uniform [0, 1) floats
static void builtin_urands_list(VM& vm, u16 dst, u16, u16) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = new ListNode(lt);
    auto* gen = new UrandsListGen(vm.typeType());
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// urands(n) -> Array<Float>: array of n uniform [0, 1) floats
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
    auto* node = new ListNode(lt);
    auto* gen = new BrandsListGen(vm.typeType());
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// brands(n) -> Array<Float>: array of n bipolar [-1, 1) floats
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
    auto* node = new ListNode(lt);
    auto* gen = new IrandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).i;
    gen->hi_ = vm.reg(ab+1).i;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// irands(n, lo, hi) -> Array<Int>: array of n uniform random ints [lo, hi]
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
    auto* node = new ListNode(lt);
    auto* gen = new XrandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).f;
    gen->hi_ = vm.reg(ab+1).f;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// xrands(n, lo, hi) -> Array<Float>: array of n exponentially distributed floats [lo, hi)
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
    auto* node = new ListNode(lt);
    auto* gen = new RandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).f;
    gen->hi_ = vm.reg(ab+1).f;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// rands(n, lo, hi) -> Array<Float>: array of n uniform floats [lo, hi)
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

// picks(Array<T>) -> List<T>: infinite lazy list of random picks
void builtin_picks_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    auto* lt = vm.listType(at->elemType_);
    auto* node = new ListNode(lt);
    auto* gen = new PicksListGen(vm.typeType());
    gen->array_ = arr;
    gen->elemType_ = at->elemType_;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// picks(Array<T>, Int) -> Array<T>: array of n random picks
void builtin_picks_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    size_t len = getArraySize(vm, arr, et);
    if (n < 0) n = 0;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* src = static_cast<PodArray<i64>*>(arr);
        auto* r = new PodArray<i64>(at);
        r->v.resize((size_t)n);
        for (i64 i = 0; i < n; i++) r->v[i] = src->v[vm.rng().next() % len];
        vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* src = static_cast<PodArray<f64>*>(arr);
        auto* r = new PodArray<f64>(at);
        r->v.resize((size_t)n);
        for (i64 i = 0; i < n; i++) r->v[i] = src->v[vm.rng().next() % len];
        vm.reg(dst).o = r;
    } else {
        auto* src = static_cast<ObjArray*>(arr);
        auto* r = new ObjArray(at);
        r->v.resize((size_t)n);
        for (i64 i = 0; i < n; i++) r->v[i] = src->v[vm.rng().next() % len];
        vm.reg(dst).o = r;
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
    auto* r = new ObjArray(static_cast<ArrayType*>(s->type_)); r->v = s->v;
    std::sort(r->v.begin(), r->v.end(), [](Obj* a, Obj* b) {
        return static_cast<StringObj*>(a)->s < static_cast<StringObj*>(b)->s;
    });
    vm.reg(dst).o = r;
}

// sort: (Array[T], (T,T)->Bool) -> Array[T]
void builtin_sort_by_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    u16 sb = vm.currentCodeBlock()->numRegs;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        vm.reg(sb) = getArrayElem(vm, src, et, a);
        vm.reg(sb+1) = getArrayElem(vm, src, et, b);
        callTwoArgs(vm, fn, sb);
        return vm.reg(sb).i != 0;
    });
    auto* result = makeEmptyArray(at);
    for (size_t i = 0; i < n; i++)
        arrayPush(vm, result, et, getArrayElem(vm, src, et, idx[i]));
    vm.reg(dst).o = result;
}

// grade: (Array[T], (T,T)->Bool) -> Array[Int]
void builtin_grade_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    u16 sb = vm.currentCodeBlock()->numRegs;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        vm.reg(sb) = getArrayElem(vm, src, et, a);
        vm.reg(sb+1) = getArrayElem(vm, src, et, b);
        callTwoArgs(vm, fn, sb);
        return vm.reg(sb).i != 0;
    });
    auto* result = new PodArray<i64>(vm.arrayType(vm.intType()));
    result->v.resize(n);
    for (size_t i = 0; i < n; i++) result->v[i] = (i64)idx[i];
    vm.reg(dst).o = result;
}

// ============================================================================
// Array: take, drop, stride, stutter, cat, join, flatten
// ============================================================================

void builtin_take_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        size_t take = n < 0 ? 0 : ((size_t)n > sv.size() ? sv.size() : (size_t)n);
        rv.resize(take);
        for (size_t i = 0; i < take; i++) rv[i] = sv[i];
    });
}

void builtin_drop_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        size_t drop = n < 0 ? 0 : ((size_t)n > sv.size() ? sv.size() : (size_t)n);
        rv.resize(sv.size() - drop);
        for (size_t i = 0; i < rv.size(); i++) rv[i] = sv[drop+i];
    });
}

void builtin_stride_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        if (n <= 0) return;
        for (size_t i = 0; i < sv.size(); i += (size_t)n) rv.push_back(sv[i]);
    });
}

void builtin_stutter_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
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

void builtin_repeat_obj(VM& vm, u16 dst, u16, u16 ab) {
    Obj* val = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    if (n < 0) n = 0;
    auto* at = vm.arrayType(val->type_);
    auto* arr = new ObjArray(at);
    arr->v.resize((size_t)n, val);
    vm.reg(dst).o = arr;
}

void builtin_cat_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = vm.reg(ab).o; auto* b = vm.reg(ab+1).o;
    auto* at = static_cast<ArrayType*>(a->type_);
    Type* et = at->elemType_;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* r = new PodArray<i64>(at);
        r->v = static_cast<PodArray<i64>*>(a)->v;
        auto& bv = static_cast<PodArray<i64>*>(b)->v;
        for (auto& x : bv) r->v.push_back(x); vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* r = new PodArray<f64>(at);
        r->v = static_cast<PodArray<f64>*>(a)->v;
        auto& bv = static_cast<PodArray<f64>*>(b)->v;
        for (auto& x : bv) r->v.push_back(x); vm.reg(dst).o = r;
    } else {
        auto* r = new ObjArray(at);
        r->v = static_cast<ObjArray*>(a)->v;
        auto& bv = static_cast<ObjArray*>(b)->v;
        for (auto& x : bv) r->v.push_back(x); vm.reg(dst).o = r;
    }
}

void builtin_join_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* outerType = static_cast<ArrayType*>(src->type_);
    auto* innerType = dynamic_cast<ArrayType*>(outerType->elemType_);
    if (!innerType) { vm.reg(dst).o = src; return; }
    auto* outer = static_cast<ObjArray*>(src);
    auto* result = makeEmptyArray(innerType);
    Type* et = innerType->elemType_;
    for (size_t i = 0; i < outer->v.size(); i++) {
        if (!outer->v[i]) continue;
        size_t n = getArraySize(vm, outer->v[i], et);
        for (size_t j = 0; j < n; j++) arrayPush(vm, result, et, getArrayElem(vm, outer->v[i], et, j));
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
        for (size_t i = 0; i < oa->v.size(); i++) {
            if (oa->v[i]) flattenArrayInto(vm, oa->v[i], result, leafType);
        }
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
    size_t n = getArraySize(vm, src, srcET);
    // Determine result type from fn's type
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* resET = fnType->returnType_;
    auto* resAT = vm.arrayType(resET);
    auto* result = makeEmptyArray(resAT);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = getArrayElem(vm, src, srcET, i);
        callOneArg(vm, fn, sb);
        arrayPush(vm, result, resET, vm.reg(sb));
    }
    vm.reg(dst).o = result;
}

void builtin_filter_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        Word elem = getArrayElem(vm, src, et, i);
        vm.reg(sb) = elem;
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) arrayPush(vm, result, et, elem);
    }
    vm.reg(dst).o = result;
}

void builtin_fold_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
    }
    vm.reg(dst) = acc;
}

void builtin_scan_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* accET = fnType->returnType_;
    auto* resAT = vm.arrayType(accET);
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(resAT);
    arrayPush(vm, result, accET, acc);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
        arrayPush(vm, result, accET, acc);
    }
    vm.reg(dst).o = result;
}

void builtin_fold1_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    if (n == 0) { vm.reg(dst).i = 0; return; }
    Word acc = getArrayElem(vm, src, et, 0);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 1; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
    }
    vm.reg(dst) = acc;
}

void builtin_scan1_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    if (n == 0) { vm.reg(dst).o = result; return; }
    Word acc = getArrayElem(vm, src, et, 0);
    arrayPush(vm, result, et, acc);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 1; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
        arrayPush(vm, result, et, acc);
    }
    vm.reg(dst).o = result;
}

void builtin_find_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = getArrayElem(vm, src, et, i);
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) { vm.reg(dst).i = (i64)i; return; }
    }
    vm.reg(dst).i = -1;
}

void builtin_takeWhile_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        Word elem = getArrayElem(vm, src, et, i);
        vm.reg(sb) = elem;
        callOneArg(vm, fn, sb);
        if (!vm.reg(sb).i) break;
        arrayPush(vm, result, et, elem);
    }
    vm.reg(dst).o = result;
}

void builtin_dropWhile_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    u16 sb = vm.currentCodeBlock()->numRegs;
    bool dropping = true;
    for (size_t i = 0; i < n; i++) {
        Word elem = getArrayElem(vm, src, et, i);
        if (dropping) {
            vm.reg(sb) = elem;
            callOneArg(vm, fn, sb);
            if (vm.reg(sb).i) continue;
            dropping = false;
        }
        arrayPush(vm, result, et, elem);
    }
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
    size_t na = getArraySize(vm, a, etA), nb = getArraySize(vm, b, etB);
    size_t n = na < nb ? na : nb;
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(etA); fields.push_back(etB);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    auto* result = new ObjArray(resAT);
    for (size_t i = 0; i < n; i++) {
        auto* tup = Tuple::create(tt, 2);
        tup->v[0] = getArrayElem(vm, a, etA, i);
        tup->v[1] = getArrayElem(vm, b, etB, i);
        result->v.push_back(tup);
    }
    vm.reg(dst).o = result;
}

void builtin_enumerate_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(vm.intType()); fields.push_back(et);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    auto* result = new ObjArray(resAT);
    for (size_t i = 0; i < n; i++) {
        auto* tup = Tuple::create(tt, 2);
        tup->v[0] = Word((i64)i);
        tup->v[1] = getArrayElem(vm, src, et, i);
        result->v.push_back(tup);
    }
    vm.reg(dst).o = result;
}

// length: Array[T] -> Int
void builtin_length_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    vm.reg(dst).i = (i64)getArraySize(vm, src, at->elemType_);
}

// ============================================================================
// Registration
// ============================================================================

void registerArrayBuiltins(Compiler& compiler, FuncMap& functions)
{
    Type* Int = compiler.intType();
    Type* Float = compiler.floatType();

    // --- random number generation (impure) ---
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
