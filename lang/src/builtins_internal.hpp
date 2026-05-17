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
//  builtins_internal.hpp
//  lang
//
//  Shared helpers for builtin function implementation files
//

#pragma once

#include "builtins.hpp"
#include "compiler.hpp"
#include "value.hpp"
#include "opcodes.hpp"

namespace ts {

// ============================================================================
// Registration helpers
// ============================================================================

using FuncMap = std::unordered_map<std::string, std::deque<FuncInfo>>;

inline void registerOne(Compiler& compiler, FuncMap& functions,
    const std::string& name, Type* returnType,
    std::vector<Type*> paramTypes, CFun cfun,
    bool pure = true, bool rtSafe = true)
{
    u32 idx = compiler.addGlobal(true);
    auto* prim = new Primitive(compiler.voidType());
    prim->cfun_ = cfun;
    prim->pure_ = pure;
    prim->rtSafe_ = rtSafe;
    compiler.global(idx).o = prim;

    FuncInfo info;
    info.returnType = returnType;
    info.paramTypes = std::move(paramTypes);
    info.globalIndex = idx;
    info.bodyChecked = true;
    info.isBuiltin = true;
    info.rtSafe = rtSafe;
    functions[name].push_back(info);
}

inline void registerTemplate(Compiler& compiler, FuncMap& functions,
    const std::string& name, BuiltinTemplateResolver resolver,
    bool rtSafe = true, bool acceptsInlineArgs = false)
{
    FuncInfo info{};
    info.isTemplate = true;
    info.builtinTemplate = resolver;
    info.bodyChecked = true;
    info.isBuiltin = true;
    info.rtSafe = rtSafe;
    info.acceptsInlineArgs = acceptsInlineArgs;
    functions[name].push_back(info);
}

// ============================================================================
// Array helpers
// ============================================================================

// Phase 4e: getArrayElem still returns a single Word, so the Complex/Fraction
// fast paths can't go through it (their values are 2 words). Callers that
// need to read inline elements should use the array opcodes instead. We
// fall through to ObjArray here so it's a clear runtime mistake if hit.
inline Word getArrayElem(VM& vm, Obj* a, Type* et, size_t i) {
    // Phase 4g.21: dispatch through arrayBackendFor so Complex/Fraction
    // PodArray<x64>/<r64> backends are handled. Box on read (Inline
    // composites and Complex/Fraction) so legacy 1-Word helpers keep
    // working. Caller owns the freshly-boxed Obj*.
    switch (arrayBackendFor(et)) {
        case ArrayBackend::Int:
            return Word(static_cast<PodArray<i64>*>(a)->v[i]);
        case ArrayBackend::Float:
            return Word(static_cast<PodArray<f64>*>(a)->v[i]);
        case ArrayBackend::Complex: {
            x64 const& x = static_cast<PodArray<x64>*>(a)->v[i];
            auto* c = new ts::Complex(x);
            return Word(static_cast<Obj*>(c));
        }
        case ArrayBackend::Fraction: {
            r64 const& r = static_cast<PodArray<r64>*>(a)->v[i];
            auto* fr = new ts::Fraction(r);
            return Word(static_cast<Obj*>(fr));
        }
        case ArrayBackend::Inline: {
            auto* arr = static_cast<InlineArray*>(a);
            return Word(boxInlineDeepFrom(vm, et, arr->slot(i)));
        }
        case ArrayBackend::Obj:
            return Word(static_cast<ObjArray*>(a)->get(i));
    }
    return Word();
}

// Backend dispatch: switch ONCE on a runtime ArrayBackend and call a
// templated lambda specialized for that backend. The loop body inside
// the lambda is fully compile-time specialized (no per-iteration
// switch), so each backend gets its own straight-line code path.
//
// Usage:
//   dispatchBackend(ba, [&]<ArrayBackend B>() {
//       for (size_t i = 0; i < getArraySize_t<B>(src); ++i) {
//           placeLambdaArgFromArrayElem_t<B>(vm, sb, src, et, i);
//           ...
//       }
//   });
template<typename F>
inline auto dispatchBackend(ArrayBackend b, F&& f) {
    switch (b) {
        case ArrayBackend::Int:      return f.template operator()<ArrayBackend::Int>();
        case ArrayBackend::Float:    return f.template operator()<ArrayBackend::Float>();
        case ArrayBackend::Complex:  return f.template operator()<ArrayBackend::Complex>();
        case ArrayBackend::Fraction: return f.template operator()<ArrayBackend::Fraction>();
        case ArrayBackend::Inline:   return f.template operator()<ArrayBackend::Inline>();
        case ArrayBackend::Obj:      return f.template operator()<ArrayBackend::Obj>();
    }
    return f.template operator()<ArrayBackend::Obj>();  // unreachable
}

// Backend-templated size accessor: `if constexpr` collapses to the right
// branch when called with a constant ArrayBackend.
template<ArrayBackend B>
inline size_t getArraySize_t(Obj* a) {
    if constexpr (B == ArrayBackend::Complex)       return static_cast<PodArray<x64>*>(a)->v.size();
    else if constexpr (B == ArrayBackend::Fraction) return static_cast<PodArray<r64>*>(a)->v.size();
    else if constexpr (B == ArrayBackend::Float)    return static_cast<PodArray<f64>*>(a)->v.size();
    else if constexpr (B == ArrayBackend::Int)      return static_cast<PodArray<i64>*>(a)->v.size();
    else if constexpr (B == ArrayBackend::Inline)   return static_cast<InlineArray*>(a)->size();
    else if constexpr (B == ArrayBackend::Obj)      return static_cast<ObjArray*>(a)->size();
}

inline size_t getArraySize(Obj* a, ArrayBackend b) {
    switch (b) {
        case ArrayBackend::Complex:  return getArraySize_t<ArrayBackend::Complex>(a);
        case ArrayBackend::Fraction: return getArraySize_t<ArrayBackend::Fraction>(a);
        case ArrayBackend::Float:    return getArraySize_t<ArrayBackend::Float>(a);
        case ArrayBackend::Int:      return getArraySize_t<ArrayBackend::Int>(a);
        case ArrayBackend::Inline:   return getArraySize_t<ArrayBackend::Inline>(a);
        case ArrayBackend::Obj:      return getArraySize_t<ArrayBackend::Obj>(a);
    }
    return 0;
}

// Compatibility wrapper for sites that still pass Type*. Callers in
// hot loops should hoist arrayBackendFor outside and use the
// ArrayBackend overload above.
inline size_t getArraySize(VM& vm, Obj* a, Type* et) {
    (void)vm;
    return getArraySize(a, arrayBackendFor(et));
}

inline Obj* makeEmptyArray(ArrayType* at) {
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex:  return new PodArray<x64>(at);
        case ArrayBackend::Fraction: return new PodArray<r64>(at);
        case ArrayBackend::Float:    return new PodArray<f64>(at);
        case ArrayBackend::Int:      return new PodArray<i64>(at);
        case ArrayBackend::Inline:   return new InlineArray(at);
        case ArrayBackend::Obj:      return new ObjArray(at);
    }
    return nullptr;
}

// Phase 4g.27: push from a caller-owned native multi-word slot. Mirrors
// arrayPush but skips the boxing round-trip that the Word-shaped API
// forces for Inline composites / Complex / Fraction.
inline void arrayPushFromSlot(VM& vm, Obj* a, Type* et, Word const* src) {
    (void)vm;
    switch (arrayBackendFor(et)) {
        case ArrayBackend::Int:
            static_cast<PodArray<i64>*>(a)->v.push_back(src[0].i);
            return;
        case ArrayBackend::Float:
            static_cast<PodArray<f64>*>(a)->v.push_back(src[0].f);
            return;
        case ArrayBackend::Complex:
            static_cast<PodArray<x64>*>(a)->v.push_back(x64(src[0].f, src[1].f));
            return;
        case ArrayBackend::Fraction:
            static_cast<PodArray<r64>*>(a)->v.push_back(r64(src[0].i, src[1].i, true));
            return;
        case ArrayBackend::Inline:
            static_cast<InlineArray*>(a)->pushSlot(src);
            return;
        case ArrayBackend::Obj:
            static_cast<ObjArray*>(a)->push(src[0].o);
            return;
    }
}

// arrayPush takes a single Word source. Phase 4g.21: Complex/Fraction Word
// sources are expected to be heap Obj* pointers (the caller boxes if
// necessary) -- we read the value and store it natively into the
// PodArray<x64>/<r64> backend.
inline void arrayPush(VM& vm, Obj* a, Type* et, Word v) {
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) static_cast<PodArray<i64>*>(a)->v.push_back(v.i);
    else if (et == vm.floatType()) static_cast<PodArray<f64>*>(a)->v.push_back(v.f);
    else if (et == vm.complexType()) {
        auto* c = static_cast<ts::Complex*>(v.o);
        static_cast<PodArray<x64>*>(a)->v.push_back(c->x);
    }
    else if (et == vm.fractionType()) {
        auto* fr = static_cast<ts::Fraction*>(v.o);
        static_cast<PodArray<r64>*>(a)->v.push_back(fr->r);
    }
    else if (et && et->repr_ == ts::Type::Repr::Inline) {
        // Phase 4g.8: caller produced a boxed Obj* (e.g. round-trip through
        // a lambda); unbox into the InlineArray slot.
        auto* arr = static_cast<InlineArray*>(a);
        Word scratch[8] = {};
        unboxInlineDeepTo(vm, et, v.o, scratch);
        arr->pushSlot(scratch);
        inlineWalkPointers(scratch, et, /*release_=*/true);
    }
    else { static_cast<ObjArray*>(a)->push(v.o); }
}

// txArray: per-element transformation. Phase 4e dispatches on
// arrayBackendFor so Complex / Fraction backends are visited too. The
// lambda is invoked with the source value-vector and destination
// value-vector, both already typed correctly (Vec<x64>, Vec<r64>, etc).
template<typename F>
inline void txArray(VM& vm, u16 dst, Obj* src, ArrayType* at, F&& f) {
    switch (arrayBackendFor(at->elemType_)) {
        case ArrayBackend::Complex: {
            auto* s = static_cast<PodArray<x64>*>(src);
            auto* r = new PodArray<x64>(at); f(s->v, r->v); vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* s = static_cast<PodArray<r64>*>(src);
            auto* r = new PodArray<r64>(at); f(s->v, r->v); vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Float: {
            auto* s = static_cast<PodArray<f64>*>(src);
            auto* r = new PodArray<f64>(at); f(s->v, r->v); vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Int: {
            auto* s = static_cast<PodArray<i64>*>(src);
            auto* r = new PodArray<i64>(at); f(s->v, r->v); vm.reg(dst).o = r;
            return;
        }
        case ArrayBackend::Inline: {
            // txArray's PodArray-shaped lambda uses sv.size()/rv.resize() in
            // element units, but InlineArray's backing Vec is in Words.
            // Callers that need Inline support must call a separate path.
            // Fall through to keep this case explicit -- assignment below
            // catches the missing impl.
            (void)src; (void)at; (void)f;
            vm.reg(dst).o = nullptr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* s = static_cast<ObjArray*>(src);
            auto* r = new ObjArray(at); f(s->rawVec(), r->rawVec());
            for (auto* obj : *r) { if (obj) obj->retain(); }
            vm.reg(dst).o = r;
            return;
        }
    }
}

// ============================================================================
// Synchronous call helpers for higher-order builtins
// ============================================================================

inline void op_sync_return(VM&, Code*) { return; }

inline Code& syncReturnCode() {
    static Code code(op_sync_return);
    return code;
}

// Phase 4g.2: lambda call args/results may be Inline composites that travel
// through the VM as multi-word values, but builtins read them as 1-Word
// boxed pointers from container backends. Unbox before placing into the
// lambda's arg slot, and re-box after reading back.
//
// Phase 4g.22: include Complex/Fraction. Their lambda param ABI is the same
// as Tuple/Struct/Enum: 2-Word native (sizeWords_ slots). The boxInlineDeep
// /unboxInlineDeep helpers handle them since Phase 4g.21.
inline bool isLambdaInlineComposite(Type* t) {
    if (!t) return false;
    if (t->repr_ != Type::Repr::Inline) return false;
    return dynamic_cast<TupleType*>(t)
        || dynamic_cast<StructType*>(t)
        || dynamic_cast<EnumType*>(t)
        || dynamic_cast<ComplexType*>(t)
        || dynamic_cast<FractionType*>(t);
}

inline void placeLambdaArg(VM& vm, u16 sb, Word w, Type* paramType) {
    if (isLambdaInlineComposite(paramType)) {
        unboxInlineDeep(vm, paramType, w.o, sb);
    } else {
        vm.reg(sb) = w;
    }
}

inline void readLambdaResult(VM& vm, u16 sb, Type* returnType) {
    if (isLambdaInlineComposite(returnType)) {
        Obj* boxed = boxInlineDeep(vm, returnType, sb);
        vm.reg(sb).o = boxed;
    }
}

// Phase 4g.26: slot-based lambda ABI -- avoid the box-then-unbox round-trip
// that placeLambdaArg(Word, paramType) requires for Inline composite args.
// Callers used to call boxPayload/boxInlineDeepFrom to produce a 1-Word
// Obj* just so placeLambdaArg could turn it back into a multi-word native
// slot via unboxInlineDeep. The slot-based path copies the words directly.

// Place a lambda arg by reading sizeWords_ Words directly from a
// caller-owned multi-word slot. Non-Inline-composite paramTypes copy a
// single Word as before.
inline void placeLambdaArgSlot(VM& vm, u16 sb, Word const* src, Type* paramType) {
    if (isLambdaInlineComposite(paramType)) {
        u32 sw = paramType->sizeWords_;
        for (u32 i = 0; i < sw; ++i) vm.reg(sb + i) = src[i];
    } else {
        vm.reg(sb) = src[0];
    }
}

// Backend-templated lambda-arg placer. `if constexpr` collapses to the
// right branch when called with a constant ArrayBackend.
template<ArrayBackend B>
inline void placeLambdaArgFromArrayElem_t(VM& vm, u16 sb, Obj* a, Type* et,
                                          size_t i) {
    if constexpr (B == ArrayBackend::Int) {
        vm.reg(sb).i = static_cast<PodArray<i64>*>(a)->v[i];
    } else if constexpr (B == ArrayBackend::Float) {
        vm.reg(sb).f = static_cast<PodArray<f64>*>(a)->v[i];
    } else if constexpr (B == ArrayBackend::Complex) {
        x64 const& x = static_cast<PodArray<x64>*>(a)->v[i];
        vm.reg(sb).f     = x.real();
        vm.reg(sb + 1).f = x.imag();
    } else if constexpr (B == ArrayBackend::Fraction) {
        r64 const& r = static_cast<PodArray<r64>*>(a)->v[i];
        vm.reg(sb).i     = r.numer();
        vm.reg(sb + 1).i = r.denom();
    } else if constexpr (B == ArrayBackend::Inline) {
        auto* arr = static_cast<InlineArray*>(a);
        Word const* src = arr->slot(i);
        u32 sw = (et && et->sizeWords_ > 0) ? et->sizeWords_ : 1;
        for (u32 k = 0; k < sw; ++k) vm.reg(sb + k) = src[k];
    } else if constexpr (B == ArrayBackend::Obj) {
        vm.reg(sb).o = static_cast<ObjArray*>(a)->get(i);
    }
}

// Place a lambda arg by reading array element i directly into the lambda's
// arg slot window at sb. Works across all array backends; for
// PodArray<x64>/PodArray<r64>/InlineArray the read is multi-word native.
// Hot-loop callers should hoist arrayBackendFor outside and pass the
// resulting enum so the switch becomes a jump table on 6 cases.
inline void placeLambdaArgFromArrayElem(VM& vm, u16 sb, Obj* a, ArrayBackend b,
                                        Type* et, size_t i) {
    switch (b) {
        case ArrayBackend::Int:      placeLambdaArgFromArrayElem_t<ArrayBackend::Int>(vm, sb, a, et, i); return;
        case ArrayBackend::Float:    placeLambdaArgFromArrayElem_t<ArrayBackend::Float>(vm, sb, a, et, i); return;
        case ArrayBackend::Complex:  placeLambdaArgFromArrayElem_t<ArrayBackend::Complex>(vm, sb, a, et, i); return;
        case ArrayBackend::Fraction: placeLambdaArgFromArrayElem_t<ArrayBackend::Fraction>(vm, sb, a, et, i); return;
        case ArrayBackend::Inline:   placeLambdaArgFromArrayElem_t<ArrayBackend::Inline>(vm, sb, a, et, i); return;
        case ArrayBackend::Obj:      placeLambdaArgFromArrayElem_t<ArrayBackend::Obj>(vm, sb, a, et, i); return;
    }
}

// Compatibility wrapper for sites that still pass Type*.
inline void placeLambdaArgFromArrayElem(VM& vm, u16 sb, Obj* a, Type* et,
                                        size_t i) {
    placeLambdaArgFromArrayElem(vm, sb, a, arrayBackendFor(et), et, i);
}

// Sum of slot words for the first `numArgs` parameter types (Phase 4g.2:
// inline composite params occupy multiple slots, so free vars must be placed
// after the cumulative arg-words, not after numArgs).
inline u16 lambdaParamSlotWords(Lambda* lam) {
    auto* fnType = static_cast<FunctionType*>(lam->type_);
    u16 sum = 0;
    for (Type* t : fnType->argTypes_) {
        u16 sw = (t && t->sizeWords_ > 0) ? t->sizeWords_ : 1;
        // Phase 4g.2/4g.22: builtin lambda calling convention places Inline
        // composite args (including Complex/Fraction) as multi-word native;
        // other types as 1 word.
        if (isLambdaInlineComposite(t)) {
            sum += sw;
        } else {
            sum += 1;
        }
    }
    return sum;
}

inline void callOneArg(VM& vm, Callable* fn, u16 sb) {
    if (fn->cfun_) {
        fn->cfun_(vm, sb, 1, sb);
    } else {
        auto* lam = static_cast<Lambda*>(fn);
        CodeBlock* cb = lam->codeBlock_;
        u32 callBase = vm.baseReg() + sb;
        u16 paramWords = lambdaParamSlotWords(lam);
        for (u16 i = 0; i < lam->numFreeVars_; i++)
            vm.reg(sb + paramWords + i) = lam->freeVars_[i];
        vm.pushFrame(&syncReturnCode(), cb, callBase, cb->numRegs, sb);
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
}

// Phase 4g.27: legacyBoundarySlotW / readBoundaryArg / writeBoundaryResult
// removed -- every builtin now uses the native multi-word ABI at its call
// boundary (acceptsInlineArgs=true), so the Complex/Fraction-vs-Inline
// asymmetry that motivated those helpers no longer exists.

inline void callTwoArgs(VM& vm, Callable* fn, u16 sb) {
    if (fn->cfun_) {
        fn->cfun_(vm, sb, 2, sb);
    } else {
        auto* lam = static_cast<Lambda*>(fn);
        CodeBlock* cb = lam->codeBlock_;
        u32 callBase = vm.baseReg() + sb;
        u16 paramWords = lambdaParamSlotWords(lam);
        for (u16 i = 0; i < lam->numFreeVars_; i++)
            vm.reg(sb + paramWords + i) = lam->freeVars_[i];
        vm.pushFrame(&syncReturnCode(), cb, callBase, cb->numRegs, sb);
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
}

// ============================================================================
// Sub-registration function declarations
// ============================================================================

void registerMathBuiltins(Compiler& compiler, FuncMap& functions);
void registerArrayBuiltins(Compiler& compiler, FuncMap& functions);
void registerListGenBuiltins(Compiler& compiler, FuncMap& functions);

// ============================================================================
// Forward declarations of exported builtin functions
// ============================================================================
// These are defined in builtins_array.cpp and builtins_listgen.cpp,
// and referenced by template resolvers in builtins.cpp.

// --- Array builtins (builtins_array.cpp) ---
void builtin_reverse_array(VM&, u16, u16, u16);
void builtin_push_array(VM&, u16, u16, u16);
void builtin_pop_array(VM&, u16, u16, u16);
void builtin_muss_array(VM&, u16, u16, u16);
void builtin_pick_array(VM&, u16, u16, u16);
void builtin_picks_list(VM&, u16, u16, u16);
void builtin_picks_array(VM&, u16, u16, u16);
void builtin_sort_int_array(VM&, u16, u16, u16);
void builtin_sort_float_array(VM&, u16, u16, u16);
void builtin_sort_string_array(VM&, u16, u16, u16);
void builtin_sort_by_array(VM&, u16, u16, u16);
void builtin_grade_array(VM&, u16, u16, u16);
void builtin_take_array(VM&, u16, u16, u16);
void builtin_drop_array(VM&, u16, u16, u16);
void builtin_stride_array(VM&, u16, u16, u16);
void builtin_stutter_array(VM&, u16, u16, u16);
void builtin_repeat_int(VM&, u16, u16, u16);
void builtin_repeat_float(VM&, u16, u16, u16);
void builtin_repeat_bool(VM&, u16, u16, u16);
void builtin_repeat_symbol(VM&, u16, u16, u16);
void builtin_repeat_obj(VM&, u16, u16, u16);
void builtin_cat_array(VM&, u16, u16, u16);
void builtin_join_array(VM&, u16, u16, u16);
void builtin_flatten_array(VM&, u16, u16, u16);
void builtin_map_array(VM&, u16, u16, u16);
void builtin_filter_array(VM&, u16, u16, u16);
void builtin_fold_array(VM&, u16, u16, u16);
void builtin_scan_array(VM&, u16, u16, u16);
void builtin_fold1_array(VM&, u16, u16, u16);
void builtin_scan1_array(VM&, u16, u16, u16);
void builtin_find_array(VM&, u16, u16, u16);
void builtin_takeWhile_array(VM&, u16, u16, u16);
void builtin_dropWhile_array(VM&, u16, u16, u16);
void builtin_zip_array(VM&, u16, u16, u16);
void builtin_enumerate_array(VM&, u16, u16, u16);
void builtin_length_array(VM&, u16, u16, u16);

// --- List/generator builtins (builtins_listgen.cpp) ---
void builtin_take_list(VM&, u16, u16, u16);
void builtin_drop_list(VM&, u16, u16, u16);
void builtin_stride_list(VM&, u16, u16, u16);
void builtin_stutter_list(VM&, u16, u16, u16);
void builtin_cat_list(VM&, u16, u16, u16);
void builtin_join_list(VM&, u16, u16, u16);
void builtin_flatten_list(VM&, u16, u16, u16);
void builtin_map_list(VM&, u16, u16, u16);
void builtin_filter_list(VM&, u16, u16, u16);
void builtin_fold_list(VM&, u16, u16, u16);
void builtin_scan_list(VM&, u16, u16, u16);
void builtin_fold1_list(VM&, u16, u16, u16);
void builtin_scan1_list(VM&, u16, u16, u16);
void builtin_find_list(VM&, u16, u16, u16);
void builtin_iter(VM&, u16, u16, u16);
void builtin_takeWhile_list(VM&, u16, u16, u16);
void builtin_dropWhile_list(VM&, u16, u16, u16);
void builtin_zip_list(VM&, u16, u16, u16);
void builtin_enumerate_list(VM&, u16, u16, u16);
void builtin_length_list(VM&, u16, u16, u16);
void builtin_head_list(VM&, u16, u16, u16);
void builtin_tail_list(VM&, u16, u16, u16);
void builtin_cons_list_int(VM&, u16, u16, u16);
void builtin_cons_list_float(VM&, u16, u16, u16);
void builtin_cons_list_bool(VM&, u16, u16, u16);
void builtin_cons_list_symbol(VM&, u16, u16, u16);
void builtin_cons_list_obj(VM&, u16, u16, u16);
void builtin_isNil_list(VM&, u16, u16, u16);
void builtin_notNil_list(VM&, u16, u16, u16);
void builtin_toList_array(VM&, u16, u16, u16);
void builtin_toList_coroutine(VM&, u16, u16, u16);
void builtin_codePoints(VM&, u16, u16, u16);
void builtin_collect(VM&, u16, u16, u16);
void builtin_cyc_list(VM&, u16, u16, u16);
void builtin_ncyc_list(VM&, u16, u16, u16);
void builtin_hang_list(VM&, u16, u16, u16);

} // namespace ts
