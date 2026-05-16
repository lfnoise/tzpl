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

inline size_t getArraySize(VM& vm, Obj* a, Type* et) {
    switch (arrayBackendFor(et)) {
        case ArrayBackend::Complex:  return static_cast<PodArray<x64>*>(a)->v.size();
        case ArrayBackend::Fraction: return static_cast<PodArray<r64>*>(a)->v.size();
        case ArrayBackend::Float:    return static_cast<PodArray<f64>*>(a)->v.size();
        case ArrayBackend::Int:      return static_cast<PodArray<i64>*>(a)->v.size();
        case ArrayBackend::Inline:   return static_cast<InlineArray*>(a)->size();
        case ArrayBackend::Obj:      return static_cast<ObjArray*>(a)->size();
    }
    return 0;
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

// Phase 4g.22: helpers to read the call-site arg layout for legacy
// (acceptsInlineArgs=false) builtins. Phase 4f keeps Complex/Fraction as
// multi-word native at the boundary; Tuple/Struct/Enum are boxed to a
// 1-Word Obj* slot. These helpers paper over that asymmetry so reducer
// builtins like fold can iterate the call frame correctly.
inline u16 legacyBoundarySlotW(VM& vm, Type* t) {
    if (!t) return 1;
    if (t->repr_ != Type::Repr::Inline) return 1;
    if (t == vm.complexType() || t == vm.fractionType()) {
        return (u16)t->sizeWords_;
    }
    return 1;  // Tuple/Struct/Enum boxed to a single Obj*.
}

// Read a boundary arg into a single Word. For multi-word native types
// (Complex/Fraction), this boxes into a heap Obj*.
inline Word readBoundaryArg(VM& vm, Word const* slot, Type* t) {
    if (t && t->repr_ == Type::Repr::Inline
        && (t == vm.complexType() || t == vm.fractionType())) {
        return boxPayload(vm, t, slot);
    }
    return *slot;
}

// Write a Word result into the caller's dst slot. For multi-word inline
// types (Complex/Fraction), unbox a heap Obj* into the native multi-Word
// dst slot. For 1-Word types just copies the Word.
inline void writeBoundaryResult(VM& vm, u16 dst, Word w, Type* t) {
    if (t && t->repr_ == Type::Repr::Inline
        && (t == vm.complexType() || t == vm.fractionType())) {
        unboxInlineDeepTo(vm, t, w.o, &vm.reg(dst));
    } else {
        vm.reg(dst) = w;
    }
}

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
