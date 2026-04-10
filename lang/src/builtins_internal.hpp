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
    bool rtSafe = true)
{
    FuncInfo info{};
    info.isTemplate = true;
    info.builtinTemplate = resolver;
    info.bodyChecked = true;
    info.isBuiltin = true;
    info.rtSafe = rtSafe;
    functions[name].push_back(info);
}

// ============================================================================
// Array helpers
// ============================================================================

inline Word getArrayElem(VM& vm, Obj* a, Type* et, size_t i) {
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) return Word(static_cast<PodArray<i64>*>(a)->v[i]);
    if (et == vm.floatType()) return Word(static_cast<PodArray<f64>*>(a)->v[i]);
    return Word(static_cast<ObjArray*>(a)->get(i));
}

inline size_t getArraySize(VM& vm, Obj* a, Type* et) {
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) return static_cast<PodArray<i64>*>(a)->v.size();
    if (et == vm.floatType()) return static_cast<PodArray<f64>*>(a)->v.size();
    return static_cast<ObjArray*>(a)->size();
}

inline Obj* makeEmptyArray(ArrayType* at) {
    Type* et = at->elemType_;
    if (et == gCurrentVM->intType() || et == gCurrentVM->boolType() || et == gCurrentVM->symbolType()) return new PodArray<i64>(at);
    if (et == gCurrentVM->floatType()) return new PodArray<f64>(at);
    return new ObjArray(at);
}

inline void arrayPush(VM& vm, Obj* a, Type* et, Word v) {
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) static_cast<PodArray<i64>*>(a)->v.push_back(v.i);
    else if (et == vm.floatType()) static_cast<PodArray<f64>*>(a)->v.push_back(v.f);
    else { static_cast<ObjArray*>(a)->push(v.o); }
}

template<typename F>
inline void txArray(VM& vm, u16 dst, Obj* src, ArrayType* at, F&& f) {
    Type* et = at->elemType_;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* s = static_cast<PodArray<i64>*>(src);
        auto* r = new PodArray<i64>(at); f(s->v, r->v); vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* s = static_cast<PodArray<f64>*>(src);
        auto* r = new PodArray<f64>(at); f(s->v, r->v); vm.reg(dst).o = r;
    } else {
        auto* s = static_cast<ObjArray*>(src);
        auto* r = new ObjArray(at); f(s->rawVec(), r->rawVec());
        // Retain all Obj* elements copied into the new array.
        // releaseChildren will release them when the array is destroyed.
        for (auto* obj : *r) { if (obj) obj->retain(); }
        vm.reg(dst).o = r;
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

inline void callOneArg(VM& vm, Callable* fn, u16 sb) {
    if (fn->cfun_) {
        fn->cfun_(vm, sb, 1, sb);
    } else {
        auto* lam = static_cast<Lambda*>(fn);
        CodeBlock* cb = lam->codeBlock_;
        u32 callBase = vm.baseReg() + sb;
        for (u16 i = 0; i < lam->numFreeVars_; i++)
            vm.reg(sb + cb->numArgs + i) = lam->freeVars_[i];
        vm.pushFrame(&syncReturnCode(), cb, callBase, cb->numRegs, sb);
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
}

inline void callTwoArgs(VM& vm, Callable* fn, u16 sb) {
    if (fn->cfun_) {
        fn->cfun_(vm, sb, 2, sb);
    } else {
        auto* lam = static_cast<Lambda*>(fn);
        CodeBlock* cb = lam->codeBlock_;
        u32 callBase = vm.baseReg() + sb;
        for (u16 i = 0; i < lam->numFreeVars_; i++)
            vm.reg(sb + cb->numArgs + i) = lam->freeVars_[i];
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
