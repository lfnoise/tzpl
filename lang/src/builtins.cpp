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
//  builtins.cpp
//  lang
//
//  Template resolvers, container builtins, and registration
//

#include "builtins_internal.hpp"
#include "disassemble.hpp"
#include "tracing_gc.hpp"

namespace ts {

// ============================================================================
// Template resolvers for Array/List operations
// ============================================================================

// --- Resolvers for [T] -> [T] unary ops ---
#define RESOLVE_ARRAY_UNARY(fname, cfun) \
static bool resolve_##fname(Compiler& compiler, const std::vector<Type*>& args, \
    std::vector<Type*>& pt, Type*& rt, CFun& cf) { \
    if (args.size() != 1) return false; \
    auto* at = dynamic_cast<ArrayType*>(args[0]); \
    if (!at) return false; \
    pt = {at}; rt = at; cf = cfun; return true; \
}

RESOLVE_ARRAY_UNARY(reverse_a, builtin_reverse_array)
RESOLVE_ARRAY_UNARY(pop_a, builtin_pop_array)
RESOLVE_ARRAY_UNARY(muss_a, builtin_muss_array)
#undef RESOLVE_ARRAY_UNARY

// pick: [T] -> T  (choose a random element)
static bool resolve_pick(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at) return false;
    pt = {at}; rt = at->elemType_; cf = builtin_pick_array; return true;
}

// picks: [T] -> List<T>  or  ([T], Int) -> [T]
static bool resolve_picks(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() == 1) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at) return false;
        pt = {at}; rt = compiler.listType(at->elemType_); cf = builtin_picks_list; return true;
    }
    if (args.size() == 2) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at || args[1] != compiler.intType()) return false;
        pt = {at, compiler.intType()}; rt = at; cf = builtin_picks_array; return true;
    }
    return false;
}

// sort: ([Int|Float|String]) or ([T], (T,T)->Bool)
static bool resolve_sort(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() == 1) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at) return false;
        Type* et = at->elemType_;
        if (et == compiler.intType()) cf = builtin_sort_int_array;
        else if (et == compiler.floatType()) cf = builtin_sort_float_array;
        else if (et == compiler.stringType()) cf = builtin_sort_string_array;
        else return false;
        pt = {at}; rt = at; return true;
    }
    if (args.size() == 2) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at) return false;
        auto* ft = dynamic_cast<FunctionType*>(args[1]);
        if (!ft || ft->argTypes_.size() != 2) return false;
        Type* et = at->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et) return false;
        if (ft->returnType_ != compiler.boolType()) return false;
        pt = {at, args[1]}; rt = at; cf = builtin_sort_by_array; return true;
    }
    return false;
}

// grade: ([T], (T,T)->Bool) -> [Int]
static bool resolve_grade(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    Type* et = at->elemType_;
    if (ft->argTypes_[0] != et || ft->argTypes_[1] != et) return false;
    if (ft->returnType_ != compiler.boolType()) return false;
    pt = {at, args[1]}; rt = compiler.arrayType(compiler.intType());
    cf = builtin_grade_array; return true;
}

// push: [T], T -> [T]
static bool resolve_push(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at || args[1] != at->elemType_) return false;
    pt = {at, at->elemType_}; rt = at; cf = builtin_push_array; return true;
}

// --- [T], Int -> [T] ---
#define RESOLVE_ARRAY_INT(fname, cfun_a, cfun_l) \
static bool resolve_##fname(Compiler& compiler, const std::vector<Type*>& args, \
    std::vector<Type*>& pt, Type*& rt, CFun& cf) { \
    if (args.size() != 2 || args[1] != compiler.intType()) return false; \
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) { \
        pt = {at, compiler.intType()}; rt = at; cf = cfun_a; return true; \
    } \
    if (auto* lt = dynamic_cast<ListType*>(args[0])) { \
        pt = {lt, compiler.intType()}; rt = lt; cf = cfun_l; return true; \
    } \
    return false; \
}

RESOLVE_ARRAY_INT(take, builtin_take_array, builtin_take_list)
RESOLVE_ARRAY_INT(drop, builtin_drop_array, builtin_drop_list)
RESOLVE_ARRAY_INT(stride, builtin_stride_array, builtin_stride_list)
RESOLVE_ARRAY_INT(stutter, builtin_stutter_array, builtin_stutter_list)
#undef RESOLVE_ARRAY_INT

// repeat: (T, Int) -> [T]
static bool resolve_repeat(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2 || args[1] != compiler.intType()) return false;
    Type* et = args[0];
    pt = {et, compiler.intType()};
    rt = compiler.arrayType(et);
    if (et == compiler.intType())         cf = builtin_repeat_int;
    else if (et == compiler.floatType())  cf = builtin_repeat_float;
    else if (et == compiler.boolType())   cf = builtin_repeat_bool;
    else if (et == compiler.symbolType()) cf = builtin_repeat_symbol;
    else                                  cf = builtin_repeat_obj;
    return true;
}

// cat: ([T], [T]) -> [T]  or  (List[T], List[T]) -> List[T]
static bool resolve_cat(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (args[1] != at) return false;
        pt = {at, at}; rt = at; cf = builtin_cat_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (args[1] != lt) return false;
        pt = {lt, lt}; rt = lt; cf = builtin_cat_list; return true;
    }
    return false;
}

// join/flatten: [[T]] -> [T]  or  List<List<T>> -> List<T>
static bool resolve_join(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (dynamic_cast<ArrayType*>(at->elemType_)) {
            pt = {at}; rt = compiler.arrayType(static_cast<ArrayType*>(at->elemType_)->elemType_);
            cf = builtin_join_array; return true;
        }
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (dynamic_cast<ListType*>(lt->elemType_)) {
            pt = {lt}; rt = static_cast<ListType*>(lt->elemType_);
            cf = builtin_join_list; return true;
        }
    }
    return false;
}

static bool resolve_flatten(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    // Array: unwrap all Array layers to find leaf type
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Type* leaf = at->elemType_;
        bool nested = false;
        while (auto* inner = dynamic_cast<ArrayType*>(leaf)) {
            leaf = inner->elemType_; nested = true;
        }
        if (!nested) return false;
        pt = {at}; rt = compiler.arrayType(leaf);
        cf = builtin_flatten_array; return true;
    }
    // List: unwrap all List layers to find leaf type
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Type* leaf = lt->elemType_;
        bool nested = false;
        while (auto* inner = dynamic_cast<ListType*>(leaf)) {
            leaf = inner->elemType_; nested = true;
        }
        if (!nested) return false;
        pt = {lt}; rt = compiler.listType(leaf);
        cf = builtin_flatten_list; return true;
    }
    return false;
}

// toList: [T] -> List<T>
static bool resolve_toList_array(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at) return false;
    pt = {at}; rt = compiler.listType(at->elemType_); cf = builtin_toList_array; return true;
}

// toList: Coroutine[T] -> List[T]
static bool resolve_toList_coroutine(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* ct = dynamic_cast<CoroutineType*>(args[0]);
    if (!ct) return false;
    pt = {ct}; rt = compiler.listType(ct->yieldType_); cf = builtin_toList_coroutine; return true;
}

// codePoints: String -> List[Int]
static bool resolve_codePoints(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1 || args[0] != compiler.stringType()) return false;
    pt = {compiler.stringType()}; rt = compiler.listType(compiler.intType()); cf = builtin_codePoints; return true;
}

// collect: (List<T>, Int) -> [T]
static bool resolve_collect(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2 || args[1] != compiler.intType()) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt, compiler.intType()}; rt = compiler.arrayType(lt->elemType_); cf = builtin_collect; return true;
}

// --- HOF resolvers ---

// map: ([T], (T)->U) -> [U]  or  (List<T>, (T)->U) -> List<U>
static bool resolve_map(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[0] != at->elemType_) return false;
        pt = {at, args[1]}; rt = compiler.arrayType(ft->returnType_);
        cf = builtin_map_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[0] != lt->elemType_) return false;
        pt = {lt, args[1]}; rt = compiler.listType(ft->returnType_);
        cf = builtin_map_list; return true;
    }
    return false;
}

// filter: ([T], (T)->Bool) -> [T]  or  (List<T>, (T)->Bool) -> List<T>
static bool resolve_filter(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1 || ft->returnType_ != compiler.boolType()) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[0] != at->elemType_) return false;
        pt = {at, args[1]}; rt = at; cf = builtin_filter_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[0] != lt->elemType_) return false;
        pt = {lt, args[1]}; rt = lt; cf = builtin_filter_list; return true;
    }
    return false;
}

// fold: ([T], U, (U,T)->U) -> U  or  (List<T>, U, (U,T)->U) -> U
static bool resolve_fold(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 3) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[2]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (ft->argTypes_[0] != args[1] || ft->returnType_ != args[1]) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[1] != at->elemType_) return false;
        pt = {at, args[1], args[2]}; rt = args[1]; cf = builtin_fold_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[1] != lt->elemType_) return false;
        pt = {lt, args[1], args[2]}; rt = args[1]; cf = builtin_fold_list; return true;
    }
    return false;
}

// scan: ([T], U, (U,T)->U) -> [U]  or  (List<T>, U, (U,T)->U) -> List<U>
static bool resolve_scan(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 3) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[2]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (ft->argTypes_[0] != args[1] || ft->returnType_ != args[1]) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[1] != at->elemType_) return false;
        pt = {at, args[1], args[2]}; rt = compiler.arrayType(args[1]);
        cf = builtin_scan_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[1] != lt->elemType_) return false;
        pt = {lt, args[1], args[2]}; rt = compiler.listType(args[1]);
        cf = builtin_scan_list; return true;
    }
    return false;
}

// fold1: ([T], (T,T)->T) -> T  or  (List<T>, (T,T)->T) -> T
static bool resolve_fold1(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Type* et = at->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {at, args[1]}; rt = et; cf = builtin_fold1_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Type* et = lt->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {lt, args[1]}; rt = et; cf = builtin_fold1_list; return true;
    }
    return false;
}

// scan1: ([T], (T,T)->T) -> [T]  or  (List<T>, (T,T)->T) -> List<T>
static bool resolve_scan1(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Type* et = at->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {at, args[1]}; rt = at; cf = builtin_scan1_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Type* et = lt->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {lt, args[1]}; rt = lt; cf = builtin_scan1_list; return true;
    }
    return false;
}

// iter: (T, (T)->T) -> List[T]
static bool resolve_iter(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1) return false;
    Type* t = args[0];
    if (ft->argTypes_[0] != t || ft->returnType_ != t) return false;
    pt = {t, args[1]};
    rt = compiler.listType(t);
    cf = builtin_iter;
    return true;
}

// find: ([T], (T)->Bool) -> Int  or  (List<T>, (T)->Bool) -> Int
static bool resolve_find(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1 || ft->returnType_ != compiler.boolType()) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[0] != at->elemType_) return false;
        pt = {at, args[1]}; rt = compiler.intType(); cf = builtin_find_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[0] != lt->elemType_) return false;
        pt = {lt, args[1]}; rt = compiler.intType(); cf = builtin_find_list; return true;
    }
    return false;
}

// takeWhile/dropWhile: ([T], (T)->Bool) -> [T]  or  (List<T>, (T)->Bool) -> List<T>
#define RESOLVE_PREDICATE_OP(fname, cfun_a, cfun_l) \
static bool resolve_##fname(Compiler& compiler, const std::vector<Type*>& args, \
    std::vector<Type*>& pt, Type*& rt, CFun& cf) { \
    if (args.size() != 2) return false; \
    auto* ft = dynamic_cast<FunctionType*>(args[1]); \
    if (!ft || ft->argTypes_.size() != 1 || ft->returnType_ != compiler.boolType()) return false; \
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) { \
        if (ft->argTypes_[0] != at->elemType_) return false; \
        pt = {at, args[1]}; rt = at; cf = cfun_a; return true; \
    } \
    if (auto* lt = dynamic_cast<ListType*>(args[0])) { \
        if (ft->argTypes_[0] != lt->elemType_) return false; \
        pt = {lt, args[1]}; rt = lt; cf = cfun_l; return true; \
    } \
    return false; \
}
RESOLVE_PREDICATE_OP(takeWhile, builtin_takeWhile_array, builtin_takeWhile_list)
RESOLVE_PREDICATE_OP(dropWhile, builtin_dropWhile_array, builtin_dropWhile_list)
#undef RESOLVE_PREDICATE_OP

// zip: ([T], [U]) -> [(T,U)]  or  (List<T>, List<U>) -> List<(T,U)>
static bool resolve_zip(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* atA = dynamic_cast<ArrayType*>(args[0])) {
        auto* atB = dynamic_cast<ArrayType*>(args[1]);
        if (!atB) return false;
        auto alloc = rt::STLAllocator<Type*>{nullptr};
        Vec<Type*> fields{alloc}; fields.push_back(atA->elemType_); fields.push_back(atB->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {atA, atB}; rt = compiler.arrayType(tt); cf = builtin_zip_array; return true;
    }
    if (auto* ltA = dynamic_cast<ListType*>(args[0])) {
        auto* ltB = dynamic_cast<ListType*>(args[1]);
        if (!ltB) return false;
        auto alloc = rt::STLAllocator<Type*>{nullptr};
        Vec<Type*> fields{alloc}; fields.push_back(ltA->elemType_); fields.push_back(ltB->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {ltA, ltB}; rt = compiler.listType(tt); cf = builtin_zip_list; return true;
    }
    return false;
}

// enumerate: [T] -> [(Int,T)]  or  List<T> -> List<(Int,T)>
static bool resolve_enumerate(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto alloc = rt::STLAllocator<Type*>{nullptr};
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Vec<Type*> fields{alloc}; fields.push_back(compiler.intType()); fields.push_back(at->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {at}; rt = compiler.arrayType(tt); cf = builtin_enumerate_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Vec<Type*> fields{alloc}; fields.push_back(compiler.intType()); fields.push_back(lt->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {lt}; rt = compiler.listType(tt); cf = builtin_enumerate_list; return true;
    }
    return false;
}

// cyc: List[T] -> List[T]
static bool resolve_cyc(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt; cf = builtin_cyc_list; return true;
}

// ncyc: List[T], Int -> List[T]
static bool resolve_ncyc(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2 || args[1] != compiler.intType()) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt, compiler.intType()}; rt = lt; cf = builtin_ncyc_list; return true;
}

// hang: List[T] -> List[T]
static bool resolve_hang(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt; cf = builtin_hang_list; return true;
}

// head: List[T] -> T
static bool resolve_head(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt->elemType_; cf = builtin_head_list; return true;
}

// tail: List[T] -> List[T]
static bool resolve_tail(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt; cf = builtin_tail_list; return true;
}

// cons: (T, List[T]) -> List[T]
static bool resolve_cons(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* lt = dynamic_cast<ListType*>(args[1]);
    if (!lt) return false;
    if (args[0] != lt->elemType_) return false;
    pt = {lt->elemType_, lt}; rt = lt;
    // Dispatch to per-type variant so cons works even when the list is nil
    if (lt->elemType_ == compiler.intType())         cf = builtin_cons_list_int;
    else if (lt->elemType_ == compiler.floatType())   cf = builtin_cons_list_float;
    else if (lt->elemType_ == compiler.boolType())    cf = builtin_cons_list_bool;
    else if (lt->elemType_ == compiler.symbolType())  cf = builtin_cons_list_symbol;
    else                                              cf = builtin_cons_list_obj;
    return true;
}

// isNil: List[T] -> Bool
static bool resolve_isNil(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = compiler.boolType(); cf = builtin_isNil_list; return true;
}

// notNil: List[T] -> Bool
static bool resolve_notNil(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = compiler.boolType(); cf = builtin_notNil_list; return true;
}

// ============================================================================
// Map builtins
// ============================================================================

// Phase 4g.11: Map/Set builtins use the new flat-hash-table API and accept
// inline-composite keys/values as multi-Word slots (acceptsInlineArgs=true).
// Arg register layout: [map, key0..key{ks-1}, val0..val{vs-1}] etc.

// length: [K:V] -> Int
static void builtin_length_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    vm.reg(dst).i = (i64)map->size();
}

// get: [K:V], K -> Option<V>
//
// Phase 4g.11: with acceptsInlineArgs=true, the Option<V> return slot is
// either Inline (multi-word: discriminant + payload) or NullablePtrEnum
// (single nullable Obj*). Write directly into the multi-word slot instead
// of materializing a heap Enum*.
static void builtin_get_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    Type* vt = mt->valueType_;
    auto* optType = vm.optionType(vt);
    Word const* keyPtr = &vm.reg((u16)(ab + 1));
    u32 slot = map->findSlot(keyPtr);
    bool found = (slot != map->capacity());

    if (optType->repr_ == Type::Repr::NullablePtrEnum) {
        if (found) {
            Obj* o = map->slotVal(slot)[0].o;
            if (o) o->retain();
            vm.reg(dst).o = o;
        } else {
            vm.reg(dst).o = nullptr;
        }
        return;
    }
    if (optType->repr_ == Type::Repr::Inline) {
        if (found) {
            vm.reg(dst).i = 0;  // which_ = some
            Word const* v = map->slotVal(slot);
            for (u32 i = 0; i < map->valueStride_; ++i) {
                vm.reg((u16)(dst + 1 + i)) = v[i];
            }
            payloadRetain(&vm.reg((u16)(dst + 1)), vt);
        } else {
            vm.reg(dst).i = 1;  // which_ = none
            for (u32 i = 0; i < map->valueStride_; ++i) {
                vm.reg((u16)(dst + 1 + i)).i = 0;
            }
        }
        return;
    }
    // Fallback: heap Enum* with native multi-word payload.
    auto* e = Enum::create(optType, found ? 0 : 1);
    if (found) {
        Word const* src = map->slotVal(slot);
        u32 sw = (vt && vt->sizeWords_ > 0) ? vt->sizeWords_ : 1;
        for (u32 i = 0; i < sw; ++i) e->v[i] = src[i];
        payloadRetain(&e->v[0], vt);
    }
    vm.reg(dst).o = e;
}

// get: [K:V], K, V -> V  (with default)
static void builtin_get_map_default(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    u32 kS = map->keyStride_;
    u32 vS = map->valueStride_;
    Word const* keyPtr = &vm.reg((u16)(ab + 1));
    u32 slot = map->findSlot(keyPtr);
    if (slot != map->capacity()) {
        Word const* v = map->slotVal(slot);
        for (u32 i = 0; i < vS; ++i) vm.reg((u16)(dst + i)) = v[i];
        payloadRetain(&vm.reg(dst), map->valueType());
    } else {
        // Default value sits at args (ab + 1 + kS .. ab + 1 + kS + vS - 1).
        Word const* dflt = &vm.reg((u16)(ab + 1 + kS));
        for (u32 i = 0; i < vS; ++i) vm.reg((u16)(dst + i)) = dflt[i];
        payloadRetain(&vm.reg(dst), map->valueType());
    }
}

// put: [K:V], K, V -> [K:V]
static void builtin_put_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    u32 kS = map->keyStride_;
    auto* result = new MapObj(mt);
    result->copyFrom(*map);
    Word const* key = &vm.reg((u16)(ab + 1));
    Word const* val = &vm.reg((u16)(ab + 1 + kS));
    payloadRetain(key, mt->keyType_);
    payloadRetain(val, mt->valueType_);
    result->insertOrUpdate(key, val);
    vm.reg(dst).o = result;
}

// remove: [K:V], K -> [K:V]
static void builtin_remove_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    auto* result = new MapObj(mt);
    result->copyFrom(*map);
    Word const* key = &vm.reg((u16)(ab + 1));
    result->eraseEntry(key);
    vm.reg(dst).o = result;
}

// contains: [K:V], K -> Bool
static void builtin_contains_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    Word const* keyPtr = &vm.reg((u16)(ab + 1));
    vm.reg(dst).i = (map->findSlot(keyPtr) != map->capacity()) ? 1 : 0;
}

// keys: [K:V] -> [K]
// Phase 4g.11: Map stores inline-composite keys natively, so we copy them
// directly into the array backend.
static void builtin_keys_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    Type* kt = mt->keyType_;
    auto* arrType = vm.arrayType(kt);
    u32 cap = map->capacity();
    switch (arrayBackendFor(kt)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                Word const* k = map->slotKey(i);
                arr->v.push_back(x64(k[0].f, k[1].f));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                Word const* k = map->slotKey(i);
                arr->v.push_back(r64(k[0].i, k[1].i));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->v.push_back(map->slotKey(i)[0].f);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->v.push_back(map->slotKey(i)[0].i);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Inline: {
            auto* arr = new InlineArray(arrType);
            arr->reserve(map->size());
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->pushSlot(map->slotKey(i));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->push(map->slotKey(i)[0].o);
            }
            vm.reg(dst).o = arr;
            return;
        }
    }
}

// values: [K:V] -> [V]
static void builtin_values_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    Type* vt = mt->valueType_;
    auto* arrType = vm.arrayType(vt);
    u32 cap = map->capacity();
    switch (arrayBackendFor(vt)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                Word const* v = map->slotVal(i);
                arr->v.push_back(x64(v[0].f, v[1].f));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                Word const* v = map->slotVal(i);
                arr->v.push_back(r64(v[0].i, v[1].i));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->v.push_back(map->slotVal(i)[0].f);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->v.push_back(map->slotVal(i)[0].i);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Inline: {
            auto* arr = new InlineArray(arrType);
            arr->reserve(map->size());
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->pushSlot(map->slotVal(i));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (map->slotState(i) != MapObj::SlotOccupied) continue;
                arr->push(map->slotVal(i)[0].o);
            }
            vm.reg(dst).o = arr;
            return;
        }
    }
}

// merge: [K:V], [K:V] -> [K:V]
static void builtin_merge_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<MapObj*>(vm.reg(ab).o);
    auto* b = static_cast<MapObj*>(vm.reg(ab + 1).o);
    auto* mt = static_cast<MapType*>(a->type_);
    auto* result = new MapObj(mt);
    result->copyFrom(*a);
    Type* kt = mt->keyType_;
    Type* vt = mt->valueType_;
    u32 bCap = b->capacity();
    for (u32 i = 0; i < bCap; ++i) {
        if (b->slotState(i) != MapObj::SlotOccupied) continue;
        Word const* k = b->slotKey(i);
        Word const* v = b->slotVal(i);
        payloadRetain(k, kt);
        payloadRetain(v, vt);
        result->insertOrUpdate(k, v);
    }
    vm.reg(dst).o = result;
}

// Forward declarations for set builtins used in shared resolvers
static void builtin_remove_set(VM& vm, u16 dst, u16, u16 ab);
static void builtin_contains_set(VM& vm, u16 dst, u16, u16 ab);

// Map resolvers
static bool resolve_get_map(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() == 2) {
        auto* mt = dynamic_cast<MapType*>(args[0]);
        if (!mt) return false;
        if (args[1] != mt->keyType_) return false;
        pt = {mt, mt->keyType_}; rt = compiler.optionType(mt->valueType_); cf = builtin_get_map; return true;
    }
    if (args.size() == 3) {
        auto* mt = dynamic_cast<MapType*>(args[0]);
        if (!mt) return false;
        if (args[1] != mt->keyType_) return false;
        if (args[2] != mt->valueType_) return false;
        pt = {mt, mt->keyType_, mt->valueType_}; rt = mt->valueType_; cf = builtin_get_map_default; return true;
    }
    return false;
}

static bool resolve_put(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 3) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    if (args[1] != mt->keyType_ || args[2] != mt->valueType_) return false;
    pt = {mt, mt->keyType_, mt->valueType_}; rt = mt; cf = builtin_put_map; return true;
}

static bool resolve_remove(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* mt = dynamic_cast<MapType*>(args[0])) {
        if (args[1] != mt->keyType_) return false;
        pt = {mt, mt->keyType_}; rt = mt; cf = builtin_remove_map; return true;
    }
    if (auto* st = dynamic_cast<SetType*>(args[0])) {
        if (args[1] != st->elemType_) return false;
        pt = {st, st->elemType_}; rt = st; cf = builtin_remove_set; return true;
    }
    return false;
}

static bool resolve_contains(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* mt = dynamic_cast<MapType*>(args[0])) {
        if (args[1] != mt->keyType_) return false;
        pt = {mt, mt->keyType_}; rt = compiler.boolType(); cf = builtin_contains_map; return true;
    }
    if (auto* st = dynamic_cast<SetType*>(args[0])) {
        if (args[1] != st->elemType_) return false;
        pt = {st, st->elemType_}; rt = compiler.boolType(); cf = builtin_contains_set; return true;
    }
    return false;
}

static bool resolve_keys(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    pt = {mt}; rt = compiler.arrayType(mt->keyType_); cf = builtin_keys_map; return true;
}

static bool resolve_values(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    pt = {mt}; rt = compiler.arrayType(mt->valueType_); cf = builtin_values_map; return true;
}

static bool resolve_merge(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    if (args[1] != mt) return false;
    pt = {mt, mt}; rt = mt; cf = builtin_merge_map; return true;
}

// pairs: [K:V] -> Array[(K, V)]
//
// Phase 4g.23: dispatch on the result tuple's array backend so that
// Inline-repr tuples (small enough to fit in <= 4 words) land in an
// InlineArray with native stride storage. The previous always-ObjArray
// implementation produced an array whose `length` / index / `@` paths
// were misinterpreted by callers that asked arrayBackendFor(tt) and got
// `Inline` -- arrayLen would static_cast to InlineArray and divide
// numTuples by stride, returning 0.
static void builtin_pairs_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc};
    fields.push_back(mt->keyType_);
    fields.push_back(mt->valueType_);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    Type* kt = mt->keyType_;
    Type* vt = mt->valueType_;
    u32 cap = map->capacity();
    auto const& kf = tt->layout_[0];
    auto const& vf = tt->layout_[1];

    if (arrayBackendFor(tt) == ArrayBackend::Inline) {
        auto* arr = new InlineArray(resAT);
        arr->reserve(map->size());
        u32 stride = tt->sizeWords_;
        Vec<Word> scratch(stride, Word{}, rt::STLAllocator<Word>{&vm.allocator()});
        for (u32 i = 0; i < cap; ++i) {
            if (map->slotState(i) != MapObj::SlotOccupied) continue;
            Word const* k = map->slotKey(i);
            Word const* v = map->slotVal(i);
            for (u8 j = 0; j < kf.sizeWords; ++j) scratch[kf.wordOffset + j] = k[j];
            for (u8 j = 0; j < vf.sizeWords; ++j) scratch[vf.wordOffset + j] = v[j];
            arr->pushSlot(scratch.data());
        }
        vm.reg(dst).o = arr;
        return;
    }

    auto* result = new ObjArray(resAT);
    for (u32 i = 0; i < cap; ++i) {
        if (map->slotState(i) != MapObj::SlotOccupied) continue;
        auto* tup = Tuple::create(tt, 2);
        Word const* k = map->slotKey(i);
        Word const* v = map->slotVal(i);
        // Phase 4g.13: tuple stores fields natively per layout. Copy words
        // directly from Map slots (which are already native) and use
        // payloadRetain for ARC across both 1-word and multi-word shapes.
        for (u8 j = 0; j < kf.sizeWords; ++j) tup->v[kf.wordOffset + j] = k[j];
        for (u8 j = 0; j < vf.sizeWords; ++j) tup->v[vf.wordOffset + j] = v[j];
        payloadRetain(&tup->v[kf.wordOffset], kt);
        payloadRetain(&tup->v[vf.wordOffset], vt);
        result->push(tup);
    }
    vm.reg(dst).o = result;
}

static bool resolve_pairs(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    auto alloc = rt::STLAllocator<Type*>{nullptr};
    Vec<Type*> fields{alloc};
    fields.push_back(mt->keyType_);
    fields.push_back(mt->valueType_);
    auto* tt = compiler.tupleType(fields);
    pt = {mt}; rt = compiler.arrayType(tt); cf = builtin_pairs_map; return true;
}

// ============================================================================
// Option builtins
// ============================================================================

static bool isOptionType(Type* t) {
    auto* et = dynamic_cast<EnumType*>(t);
    if (!et) return false;
    auto s = et->name_->str();
    return s.substr(0, 7) == "Option<";
}

// unwrap: Option<T> -> T
//
// Phase 4g.15: heap Enum payload stored natively in v[]. Copy the case's
// payload words into the multi-word dst slot.
static void builtin_unwrap(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    if (e->which_ != 0) {
        fprintf(vm.printOutput(), "Error: unwrap called on none\n");
        vm.setHalted(true);
        return;
    }
    auto* et = static_cast<EnumType*>(e->type_);
    Type* ct = et->cases_[0].type;
    u32 sw = (ct && ct->sizeWords_ > 0) ? ct->sizeWords_ : 1;
    for (u32 i = 0; i < sw; ++i) vm.reg((u16)(dst + i)) = e->v[i];
}

// Phase 3 NullablePtrEnum variant: Option<T> stored as nullable Obj*.
// On null (None), halts; otherwise returns the pointer as the inner value.
static void builtin_unwrap_nullableptr(VM& vm, u16 dst, u16, u16 ab) {
    if (!vm.reg(ab).o) {
        fprintf(vm.printOutput(), "Error: unwrap called on none\n");
        vm.setHalted(true);
        return;
    }
    vm.reg(dst).o = vm.reg(ab).o;
}

// unwrapOr: Option<T>, T -> T
static void builtin_unwrapOr(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    auto* et = static_cast<EnumType*>(e->type_);
    Type* ct = et->cases_[0].type;
    u32 sw = (ct && ct->sizeWords_ > 0) ? ct->sizeWords_ : 1;
    if (e->which_ == 0) {
        // Phase 4g.15: native multi-word payload copy from heap Enum's v[].
        for (u32 i = 0; i < sw; ++i) vm.reg((u16)(dst + i)) = e->v[i];
    } else {
        u16 fallback = (u16)(ab + 1);
        for (u32 i = 0; i < sw; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(fallback + i));
    }
}

// Phase 3 NullablePtrEnum variant.
static void builtin_unwrapOr_nullableptr(VM& vm, u16 dst, u16, u16 ab) {
    if (vm.reg(ab).o) {
        vm.reg(dst).o = vm.reg(ab).o;
    } else {
        vm.reg(dst) = vm.reg(ab + 1);
    }
}

// isSome: Option<T> -> Bool
static void builtin_isSome(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    vm.reg(dst).i = (e->which_ == 0) ? 1 : 0;
}

// Phase 3 NullablePtrEnum variant: non-null pointer means Some.
static void builtin_isSome_nullableptr(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = vm.reg(ab).o ? 1 : 0;
}

// isNone: Option<T> -> Bool
static void builtin_isNone(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    vm.reg(dst).i = (e->which_ == 1) ? 1 : 0;
}

// Phase 3 NullablePtrEnum variant.
static void builtin_isNone_nullableptr(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = vm.reg(ab).o ? 0 : 1;
}

// Phase 4g.6: Inline Option variants -- arg arrives as a multi-word slot
// [discriminant, payload...]. Read the discriminant from word 0 without
// going through a heap Enum*. The payload starts at word 1 (layout_[which]
// .wordOffset = 1 for non-Void cases per classifyImpl). For non-void
// payloads we copy `payloadSizeWords` consecutive words into dst.
static void builtin_unwrap_inline(VM& vm, u16 dst, u16, u16 ab) {
    if (vm.reg(ab).i != 0) {
        fprintf(vm.printOutput(), "Error: unwrap called on none\n");
        vm.setHalted(true);
        return;
    }
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* primTT = static_cast<TupleType*>(prim->type_);
    auto* et = static_cast<EnumType*>(primTT->fields_[0]);
    u32 n = (u32)et->layout_[0].sizeWords;
    for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(ab + 1 + i));
}

static void builtin_unwrapOr_inline(VM& vm, u16 dst, u16, u16 ab) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* primTT = static_cast<TupleType*>(prim->type_);
    auto* et = static_cast<EnumType*>(primTT->fields_[0]);
    u32 n = (u32)et->layout_[0].sizeWords;
    if (vm.reg(ab).i == 0) {
        for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(ab + 1 + i));
    } else {
        u16 fallback = (u16)(ab + et->sizeWords_);
        for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(fallback + i));
    }
}

static void builtin_isSome_inline(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (vm.reg(ab).i == 0) ? 1 : 0;
}

static void builtin_isNone_inline(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (vm.reg(ab).i == 1) ? 1 : 0;
}

// Option resolvers. Phase 4g.6: route Inline Option through the inline-
// arg variants (read discriminant/payload directly from multi-word slots).
static bool resolve_unwrap(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = et->cases_[0].type;
    if (et->repr_ == Type::Repr::NullablePtrEnum) cf = builtin_unwrap_nullableptr;
    else if (et->repr_ == Type::Repr::Inline)     cf = builtin_unwrap_inline;
    else                                          cf = builtin_unwrap;
    return true;
}

static bool resolve_unwrapOr(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    Type* innerType = et->cases_[0].type;
    if (args[1] != innerType) return false;
    pt = {et, innerType}; rt = innerType;
    if (et->repr_ == Type::Repr::NullablePtrEnum) cf = builtin_unwrapOr_nullableptr;
    else if (et->repr_ == Type::Repr::Inline)     cf = builtin_unwrapOr_inline;
    else                                          cf = builtin_unwrapOr;
    return true;
}

static bool resolve_isSome(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = compiler.boolType();
    if (et->repr_ == Type::Repr::NullablePtrEnum) cf = builtin_isSome_nullableptr;
    else if (et->repr_ == Type::Repr::Inline)     cf = builtin_isSome_inline;
    else                                          cf = builtin_isSome;
    return true;
}

static bool resolve_isNone(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = compiler.boolType();
    if (et->repr_ == Type::Repr::NullablePtrEnum) cf = builtin_isNone_nullableptr;
    else if (et->repr_ == Type::Repr::Inline)     cf = builtin_isNone_inline;
    else                                          cf = builtin_isNone;
    return true;
}

// ============================================================================
// Ref builtins: ref, deref, setref
// ============================================================================

// ref(T) -> Ref<T>  -- per-type CFuns for value types

static void builtin_ref_int(VM& vm, u16 dst, u16, u16 ab) {
    auto* ref = new RefValue(vm.refType(vm.intType()));
    ref->value_ = vm.reg(ab);
    vm.reg(dst).o = ref;
}

static void builtin_ref_float(VM& vm, u16 dst, u16, u16 ab) {
    auto* ref = new RefValue(vm.refType(vm.floatType()));
    ref->value_ = vm.reg(ab);
    vm.reg(dst).o = ref;
}

static void builtin_ref_bool(VM& vm, u16 dst, u16, u16 ab) {
    auto* ref = new RefValue(vm.refType(vm.boolType()));
    ref->value_ = vm.reg(ab);
    vm.reg(dst).o = ref;
}

static void builtin_ref_symbol(VM& vm, u16 dst, u16, u16 ab) {
    auto* ref = new RefValue(vm.refType(vm.symbolType()));
    ref->value_ = vm.reg(ab);
    vm.reg(dst).o = ref;
}

// ref for object types
static void builtin_ref_obj(VM& vm, u16 dst, u16, u16 ab) {
    auto* ref = new RefValue(vm.refType(vm.reg(ab).o->type_));
    ref->value_ = vm.reg(ab);
    if (ref->value_.o) ref->value_.o->retain();
    vm.reg(dst).o = ref;
}

// ref(InlineComposite) -> InlineRef
// Phase 4g.6: ref(InlineComposite) now reads its arg as a multi-word slot
// directly (no boxing). The elem type is recovered from the resolved
// Primitive's TupleType so we can size the InlineRef and walk pointers.
static void builtin_ref_inline(VM& vm, u16 dst, u16, u16 ab) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* et = primTT->fields_[0];
    auto* refType = static_cast<RefType*>(vm.refType(et));
    auto* ref = InlineRef::create(refType);
    u32 n = ref->sizeWords_;
    for (u32 i = 0; i < n; ++i) ref->v[i] = vm.reg((u16)(ab + i));
    // Retain embedded Obj* fields in the new payload (the caller's regs are
    // about to be reclaimed). Mirrors op_make_ref_inline's ARC walk.
    inlineWalkPointers(&ref->v[0], et, /*release_=*/false);
    vm.reg(dst).o = ref;
}

// deref(Ref<T>) -> T. Phase 4g.6: for InlineRef, copy the inline payload
// directly into the multi-word dst slot -- no temp boxed Tuple/Struct/Enum.
// Phase 4g.23: Complex/Fraction are 2-word native at builtin boundaries, so
// they use InlineRef too.
static void builtin_deref(VM& vm, u16 dst, u16, u16 ab) {
    auto* obj = vm.reg(ab).o;
    auto* refType = static_cast<RefType*>(obj->type_);
    Type* et = refType->elemType_;
    if (et && et->repr_ == Type::Repr::Inline && et->sizeWords_ > 1) {
        auto* ref = static_cast<InlineRef*>(obj);
        u32 n = ref->sizeWords_;
        for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = ref->v[i];
        return;
    }
    auto* ref = static_cast<RefValue*>(obj);
    vm.reg(dst) = ref->value_;
}

// setref(T, Ref<T>) -> T. Phase 4g.6: for InlineRef, mutate the payload
// in place from the multi-word arg slot -- no temp box, no unboxInto.
static void builtin_setref(VM& vm, u16 dst, u16, u16 ab) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* et = primTT->fields_[0];
    // Phase 4g.23: Complex/Fraction (Inline, 2 words) also route through
    // InlineRef -- they are 2-word native at builtin boundaries since 4f.
    bool inlineComposite = et && et->repr_ == Type::Repr::Inline
        && et->sizeWords_ > 1;
    if (inlineComposite) {
        u32 n = (u32)et->sizeWords_;
        // Ref is the 2nd arg; it lives at ab + n (sizeWords of inline T).
        u16 refReg = (u16)(ab + n);
        auto* ref = static_cast<InlineRef*>(vm.reg(refReg).o);
        inlineWalkPointers(&ref->v[0], et, /*release_=*/true);
        for (u32 i = 0; i < n; ++i) ref->v[i] = vm.reg((u16)(ab + i));
        inlineWalkPointers(&ref->v[0], et, /*release_=*/false);
        // Result is the assigned value; copy back out if dst != ab.
        if (dst != ab) {
            for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(ab + i));
        }
        return;
    }
    auto* ref = static_cast<RefValue*>(vm.reg(ab + 1).o);
    auto* refType = static_cast<RefType*>(ref->type_);
    Word newVal = vm.reg(ab);
    if (storesObjPtr(refType->elemType_)) {
        if (newVal.o) newVal.o->retain();
        if (ref->value_.o) ref->value_.o->release();
    }
    ref->value_ = newVal;
    vm.reg(dst) = newVal;
}

// --- Ref template resolvers ---

static bool resolve_ref(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    Type* t = args[0];
    // Phase 4g.23: Complex/Fraction (Inline, 2 words) also route through
    // builtin_ref_inline since their values are 2-word native at the call
    // boundary; the previous resolve_ref path sent them to builtin_ref_obj
    // and tried to read a 1-Word Obj* that doesn't exist.
    bool inlineComposite = t && t->repr_ == Type::Repr::Inline
        && t->sizeWords_ > 1;
    if (t == compiler.intType())         cf = builtin_ref_int;
    else if (t == compiler.floatType())  cf = builtin_ref_float;
    else if (t == compiler.boolType())   cf = builtin_ref_bool;
    else if (t == compiler.symbolType()) cf = builtin_ref_symbol;
    else if (t && t->repr_ == Type::Repr::DiscriminantEnum) cf = builtin_ref_int;
    else if (inlineComposite)            cf = builtin_ref_inline;
    else if (t->isObjType())             cf = builtin_ref_obj;
    else return false;
    pt = {t};
    rt = compiler.refType(t);
    return true;
}

static bool resolve_deref(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* rft = dynamic_cast<RefType*>(args[0]);
    if (!rft) return false;
    pt = {rft};
    rt = rft->elemType_;
    cf = builtin_deref;
    return true;
}

static int numericRank(Compiler& c, Type* t) {
    if (t == c.boolType()) return 0;
    if (t == c.intType()) return 1;
    if (t == c.fractionType()) return 2;
    if (t == c.floatType()) return 3;
    if (t == c.complexType()) return 4;
    return -1;
}

static bool isNumericPromotion(Compiler& c, Type* from, Type* to) {
    int fromRank = numericRank(c, from);
    int toRank = numericRank(c, to);
    return fromRank >= 0 && toRank >= 0 && fromRank <= toRank;
}

// setref(T, Ref<T>) -> T
static bool resolve_setref(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* rft = dynamic_cast<RefType*>(args[1]);
    if (!rft) return false;
    if (args[0] != rft->elemType_ && !isNumericPromotion(compiler, args[0], rft->elemType_))
        return false;
    pt = {rft->elemType_, rft};
    rt = rft->elemType_;
    cf = builtin_setref;
    return true;
}

// setref(Ref<T>, T) -> T. Phase 4g.6: same migration as builtin_setref but
// with the Ref first and the new value second.
static void builtin_setref_rev(VM& vm, u16 dst, u16, u16 ab) {
    auto* obj = vm.reg(ab).o;
    auto* refType = static_cast<RefType*>(obj->type_);
    Type* et = refType->elemType_;
    if (et && et->repr_ == Type::Repr::Inline && et->sizeWords_ > 1) {
        auto* ref = static_cast<InlineRef*>(obj);
        // The new value is the 2nd arg; lives at ab + 1 (Ref is 1 word).
        u32 n = (u32)et->sizeWords_;
        u16 valReg = (u16)(ab + 1);
        inlineWalkPointers(&ref->v[0], et, /*release_=*/true);
        for (u32 i = 0; i < n; ++i) ref->v[i] = vm.reg((u16)(valReg + i));
        inlineWalkPointers(&ref->v[0], et, /*release_=*/false);
        if (dst != valReg) {
            for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(valReg + i));
        }
        return;
    }
    auto* ref = static_cast<RefValue*>(obj);
    Word newVal = vm.reg(ab + 1);
    if (storesObjPtr(refType->elemType_)) {
        if (newVal.o) newVal.o->retain();
        if (ref->value_.o) ref->value_.o->release();
    }
    ref->value_ = newVal;
    vm.reg(dst) = newVal;
}

static bool resolve_setref_rev(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* rft = dynamic_cast<RefType*>(args[0]);
    if (!rft) return false;
    if (args[1] != rft->elemType_ && !isNumericPromotion(compiler, args[1], rft->elemType_))
        return false;
    pt = {rft, rft->elemType_};
    rt = rft->elemType_;
    cf = builtin_setref_rev;
    return true;
}

// next: Coroutine<T> -> Option<T>
static bool resolve_next(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* ct = dynamic_cast<CoroutineType*>(args[0]);
    if (!ct) return false;
    pt = {ct};
    rt = compiler.optionType(ct->yieldType_);
    cf = nullptr;  // codegen handles this specially via op_coro_resume
    return true;
}

// yield: T -> Void
static bool resolve_yield(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    pt = {args[0]};
    rt = compiler.voidType();
    cf = nullptr;  // codegen handles this specially via op_yield
    return true;
}

// yieldAll: Coroutine<T> -> Void
static bool resolve_yieldAll(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* ct = dynamic_cast<CoroutineType*>(args[0]);
    if (!ct) return false;
    pt = {ct};
    rt = compiler.voidType();
    cf = nullptr;  // codegen handles this specially
    return true;
}

// ============================================================================
// Set builtins
// ============================================================================

// length: Set<T> -> Int
static void builtin_length_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    vm.reg(dst).i = (i64)set->size();
}

// add: Set<T>, T -> Set<T>
static void builtin_add_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(src->type_);
    auto* result = new SetObj(st);
    result->copyFrom(*src);
    Word const* elem = &vm.reg((u16)(ab + 1));
    payloadRetain(elem, st->elemType_);
    result->insertElem(elem);
    vm.reg(dst).o = result;
}

// remove: Set<T>, T -> Set<T>
static void builtin_remove_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(src->type_);
    auto* result = new SetObj(st);
    result->copyFrom(*src);
    Word const* elem = &vm.reg((u16)(ab + 1));
    result->eraseElem(elem);
    vm.reg(dst).o = result;
}

// contains: Set<T>, T -> Bool
static void builtin_contains_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    Word const* elem = &vm.reg((u16)(ab + 1));
    vm.reg(dst).i = (set->findSlot(elem) != set->capacity()) ? 1 : 0;
}

// union: Set<T>, Set<T> -> Set<T>
static void builtin_union_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    result->copyFrom(*a);
    Type* et = st->elemType_;
    u32 bCap = b->capacity();
    for (u32 i = 0; i < bCap; ++i) {
        if (b->slotState(i) != SetObj::SlotOccupied) continue;
        Word const* e = b->slotElem(i);
        payloadRetain(e, et);
        result->insertElem(e);
    }
    vm.reg(dst).o = result;
}

// intersection: Set<T>, Set<T> -> Set<T>
static void builtin_intersection_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    Type* et = st->elemType_;
    u32 aCap = a->capacity();
    for (u32 i = 0; i < aCap; ++i) {
        if (a->slotState(i) != SetObj::SlotOccupied) continue;
        Word const* e = a->slotElem(i);
        if (b->findSlot(e) == b->capacity()) continue;
        payloadRetain(e, et);
        result->insertElem(e);
    }
    vm.reg(dst).o = result;
}

// difference: Set<T>, Set<T> -> Set<T>
static void builtin_difference_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    Type* et = st->elemType_;
    u32 aCap = a->capacity();
    for (u32 i = 0; i < aCap; ++i) {
        if (a->slotState(i) != SetObj::SlotOccupied) continue;
        Word const* e = a->slotElem(i);
        if (b->findSlot(e) != b->capacity()) continue;
        payloadRetain(e, et);
        result->insertElem(e);
    }
    vm.reg(dst).o = result;
}

// toArray: Set<T> -> [T]
// Phase 4g.11: inline-composite elements are stored natively in the set, so
// we copy them directly into the inline array backend.
static void builtin_toArray_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(set->type_);
    Type* elemType = st->elemType_;
    auto* arrType = vm.arrayType(elemType);
    u32 cap = set->capacity();

    switch (arrayBackendFor(elemType)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (set->slotState(i) != SetObj::SlotOccupied) continue;
                Word const* e = set->slotElem(i);
                arr->v.push_back(x64(e[0].f, e[1].f));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (set->slotState(i) != SetObj::SlotOccupied) continue;
                Word const* e = set->slotElem(i);
                arr->v.push_back(r64(e[0].i, e[1].i));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (set->slotState(i) != SetObj::SlotOccupied) continue;
                arr->v.push_back(set->slotElem(i)[0].f);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (set->slotState(i) != SetObj::SlotOccupied) continue;
                arr->v.push_back(set->slotElem(i)[0].i);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Inline: {
            auto* arr = new InlineArray(arrType);
            arr->reserve(set->size());
            for (u32 i = 0; i < cap; ++i) {
                if (set->slotState(i) != SetObj::SlotOccupied) continue;
                arr->pushSlot(set->slotElem(i));
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrType);
            for (u32 i = 0; i < cap; ++i) {
                if (set->slotState(i) != SetObj::SlotOccupied) continue;
                arr->push(set->slotElem(i)[0].o);
            }
            vm.reg(dst).o = arr;
            return;
        }
    }
}

// Set resolvers
static bool resolve_add_set(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st) return false;
    if (args[1] != st->elemType_) return false;
    pt = {st, st->elemType_}; rt = st; cf = builtin_add_set; return true;
}

static bool resolve_union(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st || args[1] != st) return false;
    pt = {st, st}; rt = st; cf = builtin_union_set; return true;
}

static bool resolve_intersection(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st || args[1] != st) return false;
    pt = {st, st}; rt = st; cf = builtin_intersection_set; return true;
}

static bool resolve_difference(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st || args[1] != st) return false;
    pt = {st, st}; rt = st; cf = builtin_difference_set; return true;
}

static bool resolve_toArray_set(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st) return false;
    pt = {st}; rt = compiler.arrayType(st->elemType_); cf = builtin_toArray_set; return true;
}

// ============================================================================
// length, ordinal, tag resolvers
// ============================================================================

// length resolver: [T] -> Int  or  List<T> -> Int  or  Map<K,V> -> Int  or  Set<T> -> Int
static bool resolve_length(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        pt = {at}; rt = compiler.intType(); cf = builtin_length_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        pt = {lt}; rt = compiler.intType(); cf = builtin_length_list; return true;
    }
    if (auto* mt = dynamic_cast<MapType*>(args[0])) {
        pt = {mt}; rt = compiler.intType(); cf = builtin_length_map; return true;
    }
    if (auto* st = dynamic_cast<SetType*>(args[0])) {
        pt = {st}; rt = compiler.intType(); cf = builtin_length_set; return true;
    }
    return false;
}

// --- ordinal (enum case index) ---

static void builtin_ordinal_enum(VM& vm, u16 dst, u16, u16 argBase) {
    auto* e = static_cast<Enum*>(vm.reg(argBase).o);
    vm.reg(dst).i = e->which_;
}

// Phase 2: DiscriminantEnum value IS the ordinal.
static void builtin_ordinal_discenum(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = vm.reg(argBase).i;
}

// Phase 3: NullablePtrEnum -- recover the void/data case index from the
// static type, then map (null -> voidIdx, non-null -> dataIdx).
static void builtin_ordinal_nullableptr(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    auto* et = static_cast<EnumType*>(tt->fields_[0]);
    int voidIdx = nullablePtrVoidCaseIndex(et);
    int dataIdx = (voidIdx == 0) ? 1 : 0;
    vm.reg(dst).i = vm.reg(argBase).o ? dataIdx : voidIdx;
}

// Phase 4g.6: Inline enum -- word 0 IS the discriminant.
static void builtin_ordinal_inline(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = vm.reg(argBase).i;
}

static bool resolve_ordinal(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* et = dynamic_cast<EnumType*>(args[0])) {
        pt = {et};
        rt = compiler.intType();
        if (et->repr_ == Type::Repr::DiscriminantEnum)        cf = builtin_ordinal_discenum;
        else if (et->repr_ == Type::Repr::NullablePtrEnum)    cf = builtin_ordinal_nullableptr;
        else if (et->repr_ == Type::Repr::Inline)             cf = builtin_ordinal_inline;
        else                                                   cf = builtin_ordinal_enum;
        return true;
    }
    return false;
}

// --- tag (enum case name as Symbol) ---

static void builtin_tag_enum(VM& vm, u16 dst, u16, u16 argBase) {
    auto* e = static_cast<Enum*>(vm.reg(argBase).o);
    auto* et = static_cast<EnumType*>(e->type_);
    vm.reg(dst).s = const_cast<Symbol*>(et->cases_[e->which_].name);
}

// Phase 2: DiscriminantEnum -- value IS the case index, recover EnumType from primitive.
static void builtin_tag_discenum(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    auto* et = static_cast<EnumType*>(tt->fields_[0]);
    vm.reg(dst).s = const_cast<Symbol*>(et->cases_[vm.reg(argBase).i].name);
}

// Phase 3: NullablePtrEnum -- look up case name based on null vs non-null.
static void builtin_tag_nullableptr(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    auto* et = static_cast<EnumType*>(tt->fields_[0]);
    int voidIdx = nullablePtrVoidCaseIndex(et);
    int dataIdx = (voidIdx == 0) ? 1 : 0;
    int idx = vm.reg(argBase).o ? dataIdx : voidIdx;
    vm.reg(dst).s = const_cast<Symbol*>(et->cases_[idx].name);
}

// Phase 4g.6: Inline enum -- word 0 IS the discriminant.
static void builtin_tag_inline(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    auto* et = static_cast<EnumType*>(tt->fields_[0]);
    vm.reg(dst).s = const_cast<Symbol*>(et->cases_[vm.reg(argBase).i].name);
}

static bool resolve_tag(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* et = dynamic_cast<EnumType*>(args[0])) {
        pt = {et};
        rt = compiler.symbolType();
        if (et->repr_ == Type::Repr::DiscriminantEnum)        cf = builtin_tag_discenum;
        else if (et->repr_ == Type::Repr::NullablePtrEnum)    cf = builtin_tag_nullableptr;
        else if (et->repr_ == Type::Repr::Inline)             cf = builtin_tag_inline;
        else                                                   cf = builtin_tag_enum;
        return true;
    }
    return false;
}

// ============================================================================
// toString builtins
// ============================================================================

static void builtin_toString_bool(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = rt::vmstr(vm.reg(argBase).i ? "true" : "false");
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_int(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = rt::fmt("{}", vm.reg(argBase).i);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_float(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = rt::fmtFloat(vm.reg(argBase).f);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_symbol(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = vm.reg(argBase).s->str();
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_obj(VM& vm, u16 dst, u16, u16 argBase) {
    Obj* obj = vm.reg(argBase).o;
    auto* result = new StringObj();
    result->s = obj ? obj->str() : rt::vmstr("nil");
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// Phase 4f / 4g.6: handle all Inline-classified types -- Complex, Fraction,
// inline tuples/structs/enums -- with one handler that delegates to
// slotToString (which itself routes to wordsToString for the multi-word
// walk). Replaces the per-type Complex/Fraction inline handlers.
static void builtin_toString_inline(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    Type* t = tt->fields_[0];
    auto* result = new StringObj();
    result->s = slotToString(vm, argBase, t);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// Generic toString that recovers the static arg type from currentPrimitive()
// and dispatches through wordToString. Used for DiscriminantEnum (value is i64
// but printed via the static EnumType's case table).
static void builtin_toString_word(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    Type* t = tt->fields_[0];
    auto* result = new StringObj();
    result->s = wordToString(vm.reg(argBase), t);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static bool resolve_toString(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    Type* t = args[0];
    rt = compiler.stringType();
    pt = {t};
    if (t == compiler.boolType()) {
        cf = builtin_toString_bool; return true;
    }
    if (t == compiler.intType()) {
        cf = builtin_toString_int; return true;
    }
    if (t == compiler.floatType()) {
        cf = builtin_toString_float; return true;
    }
    if (t == compiler.symbolType()) {
        cf = builtin_toString_symbol; return true;
    }
    if (t && t->repr_ == Type::Repr::DiscriminantEnum) {
        cf = builtin_toString_word; return true;
    }
    // Phase 4g.6: Complex, Fraction, inline tuples/structs/enums all flow
    // through builtin_toString_inline (slotToString reads multi-word
    // payloads directly).
    if (t && t->repr_ == Type::Repr::Inline) {
        cf = builtin_toString_inline; return true;
    }
    if (t->isObjType()) {
        cf = builtin_toString_obj; return true;
    }
    return false;
}

// ============================================================================
// fmt builtin
// ============================================================================

static void builtin_fmt(VM& vm, u16 dst, u16, u16 argBase) {
    Obj* fmtObj = vm.reg(argBase).o;
    Obj* tupObj = vm.reg(argBase + 1).o;
    auto* fmtStr = static_cast<StringObj*>(fmtObj);
    auto* tup = static_cast<Tuple*>(tupObj);
    auto* tt = static_cast<TupleType*>(tup->type_);

    const VMString& fmt = fmtStr->s;
    u32 numFields = tup->numFields_;
    u32 nextPos = 0; // next positional index for %^

    VMString result = rt::vmstr("");
    size_t len = fmt.size();
    for (size_t i = 0; i < len; ++i) {
        if (fmt[i] == '%' && i + 1 < len) {
            char next = fmt[i + 1];
            if (next == '%') {
                result += '%';
                ++i;
            } else if (next == '^') {
                if (nextPos < numFields) {
                    auto const& f = tt->layout_[nextPos];
                    if (f.sizeWords > 1) result += wordsToString(&tup->v[f.wordOffset], f.type);
                    else                 result += wordToString(tup->v[f.wordOffset], f.type);
                    ++nextPos;
                }
                ++i;
            } else if (next >= '0' && next <= '9') {
                u32 idx = (u32)(next - '0');
                if (idx < numFields) {
                    auto const& f = tt->layout_[idx];
                    if (f.sizeWords > 1) result += wordsToString(&tup->v[f.wordOffset], f.type);
                    else                 result += wordToString(tup->v[f.wordOffset], f.type);
                }
                ++i;
            } else {
                result += fmt[i];
            }
        } else {
            result += fmt[i];
        }
    }

    auto* resultObj = new StringObj();
    resultObj->s = std::move(result);
    registerNewObj(resultObj);
    vm.reg(dst).o = resultObj;
}

static bool resolve_fmt(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.empty() || args[0] != compiler.stringType()) return false;

    // Variadic form: fmt(String, v1, v2, ...) -- build TupleType from args[1..]
    // Also handles fmt(String) with zero variadic args
    TypeVec fields(rt::STLAllocator<Type*>(nullptr));
    for (size_t i = 1; i < args.size(); ++i)
        fields.push_back(args[i]);
    auto* tt = compiler.tupleType(fields);
    pt = {compiler.stringType(), tt};
    rt = compiler.stringType();
    cf = builtin_fmt;
    return true;
}

// ============================================================================
// print/println builtins
// ============================================================================

static void printArgs(VM& vm, u16 argc, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    FILE* out = vm.printOutput();

    // Phase 4g.6: walk via cumulative offset. Every Inline-classified type
    // arrives as a multi-word slot now (acceptsInlineArgs=true on the
    // print/println FuncInfo) -- Complex/Fraction, inline structs, inline
    // tuples, inline enums all share the same multi-word path via
    // slotToString. Non-Inline types (atoms, Obj*) are a single Word.
    u32 cumOffset = 0;
    for (u16 i = 0; i < argc; ++i) {
        if (i > 0) std::fputc(' ', out);
        Type* t = tt->fields_[i];
        bool inlineMulti = t && t->repr_ == Type::Repr::Inline;
        u32 sw = inlineMulti ? (u32)t->sizeWords_ : 1u;
        if (sw == 0) sw = 1;
        VMString s = inlineMulti
            ? slotToString(vm, (u16)(argBase + cumOffset), t)
            : wordToString(vm.reg((u16)(argBase + cumOffset)), t);
        std::fprintf(out, "%.*s", (int)s.size(), s.data());
        cumOffset += sw;
    }
}

static void builtin_print(VM& vm, u16 dst, u16 argc, u16 argBase) {
    printArgs(vm, argc, argBase);
    vm.reg(dst).i = 0;
}

static void builtin_println(VM& vm, u16 dst, u16 argc, u16 argBase) {
    printArgs(vm, argc, argBase);
    FILE* out = vm.printOutput();
    std::fputc('\n', out);
    std::fflush(out);
    vm.reg(dst).i = 0;
}

static bool resolve_print(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    pt = args;
    rt = compiler.voidType();
    cf = builtin_print;
    return true;
}

static bool resolve_println(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    pt = args;
    rt = compiler.voidType();
    cf = builtin_println;
    return true;
}

// ============================================================================
// hash builtins
// ============================================================================

static void builtin_hash_int(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = (i64)std::hash<i64>{}(vm.reg(argBase).i);
}

static void builtin_hash_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = (i64)std::hash<f64>{}(vm.reg(argBase).f);
}

static void builtin_hash_symbol(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = (i64)std::hash<const void*>{}(vm.reg(argBase).s);
}

static void builtin_hash_obj(VM& vm, u16 dst, u16, u16 argBase) {
    Obj* obj = vm.reg(argBase).o;
    Type* type = obj->type_;
    WordHash hasher{type};
    vm.reg(dst).i = (i64)hasher(vm.reg(argBase));
}

// Phase 4g.6: hash an inline composite arg directly out of its multi-word
// register slot, no box round-trip. Looks up the arg's type from the
// monomorphized Primitive (TupleType of paramTypes) so the walk knows the
// layout. Used for inline tuples/structs/enums, Complex, and Fraction --
// any Inline-classified type.
static void builtin_hash_inline(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    Type* t = tt->fields_[0];
    vm.reg(dst).i = (i64)hashWords(&vm.reg(argBase), t);
}

static bool resolve_hash(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    Type* t = args[0];
    rt = compiler.intType();
    pt = {t};
    if (t == compiler.intType() || t == compiler.boolType()) {
        cf = builtin_hash_int; return true;
    }
    if (t == compiler.floatType()) {
        cf = builtin_hash_float; return true;
    }
    if (t == compiler.symbolType()) {
        cf = builtin_hash_symbol; return true;
    }
    if (t && t->repr_ == Type::Repr::DiscriminantEnum) {
        cf = builtin_hash_int; return true;
    }
    // Phase 4g.6: Inline-classified types (Tuple/Struct/Enum + Complex/Fraction)
    // hash via hashWords on a multi-word slot. The resolver flags this overload
    // for native-inline arg passing via acceptsInlineArgs on the FuncInfo (see
    // registration block below).
    if (t && t->repr_ == Type::Repr::Inline) {
        cf = builtin_hash_inline; return true;
    }
    if (t->isObjType()) {
        cf = builtin_hash_obj; return true;
    }
    return false;
}

// ============================================================================
// any() and toAnyArray() builtins
// ============================================================================

// any(x) -- wrap a single value of any type into Any
//
// Phase 4g.27: with acceptsInlineArgs=true, Inline composite args arrive
// natively at argBase..argBase+sw-1; boxPayload turns them into a single
// heap Obj* that fits in AnyObj's 1-Word value_ slot.
static void builtin_any_single(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = vm.currentPrimitive();
    auto* ft = static_cast<FunctionType*>(prim->type_);
    Type* wrappedType = ft->argTypes_[0];
    auto* any = new AnyObj(vm.anyType());
    any->value_ = boxPayload(vm, wrappedType, &vm.reg(argBase));
    any->wrappedType_ = wrappedType;
    any->isObjType_ = storesObjPtr(wrappedType);
    // boxPayload already retained for caller ownership; transfer to any
    // without an extra retain.
    vm.reg(dst).o = any;
}

static bool resolve_any_single(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    pt = {args[0]};
    rt = compiler.anyType();
    cf = builtin_any_single;
    return true;
}

// any(x, y, ...) -- wrap multiple values into [Any]
//
// Phase 4g.13: heap Tuple now stores fields natively per layout. For Inline
// composite fields (multi-word) we re-box via boxPayload so each AnyObj's
// single-Word value_ slot can hold them.
static void builtin_any_variadic(VM& vm, u16 dst, u16, u16 argBase) {
    auto* tuple = static_cast<Tuple*>(vm.reg(argBase).o);
    auto* tupleType = static_cast<TupleType*>(tuple->type_);
    auto* anyArrayType = vm.arrayType(vm.anyType());
    auto* arr = new ObjArray(anyArrayType);
    size_t n = tuple->numFields_;
    arr->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto const& f = tupleType->layout_[i];
        auto* any = new AnyObj(vm.anyType());
        any->wrappedType_ = f.type;
        any->isObjType_ = storesObjPtr(f.type) || (f.type && f.type->repr_ == Type::Repr::Inline);
        if (f.sizeWords > 1) {
            any->value_ = boxPayload(vm, f.type, &tuple->v[f.wordOffset]);
        } else {
            any->value_ = tuple->v[f.wordOffset];
            if (any->isObjType_ && any->value_.o) any->value_.o->retain();
        }
        arr->push(any);
    }
    vm.reg(dst).o = arr;
}

static bool resolve_any_variadic(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() < 2) return false;
    TypeVec fields(rt::STLAllocator<Type*>(nullptr));
    for (auto* a : args)
        fields.push_back(a);
    auto* tt = compiler.tupleType(fields);
    pt = {tt};
    rt = compiler.arrayType(compiler.anyType());
    cf = builtin_any_variadic;
    return true;
}

// toAnyArray(tuple) -- convert a tuple to [Any]
//
// Phase 4g.27: with acceptsInlineArgs=true, Inline tuples arrive as a
// multi-word native slot at argBase.., heap tuples as a 1-Word Tuple*.
// Read the field words from whichever storage the static type indicates.
static void builtin_toAnyArray(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    auto* tupleType = static_cast<TupleType*>(primTT->fields_[0]);
    Word const* base = nullptr;
    if (tupleType->repr_ == Type::Repr::Inline) {
        base = &vm.reg(argBase);
    } else {
        auto* tuple = static_cast<Tuple*>(vm.reg(argBase).o);
        base = &tuple->v[0];
    }
    auto* anyArrayType = vm.arrayType(vm.anyType());
    auto* arr = new ObjArray(anyArrayType);
    size_t n = tupleType->fields_.size();
    arr->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto const& f = tupleType->layout_[i];
        auto* any = new AnyObj(vm.anyType());
        any->wrappedType_ = f.type;
        any->isObjType_ = storesObjPtr(f.type);
        any->value_ = boxPayload(vm, f.type, &base[f.wordOffset]);
        arr->push(any);
    }
    vm.reg(dst).o = arr;
}

static bool resolve_toAnyArray(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* tt = dynamic_cast<TupleType*>(args[0]);
    if (!tt) return false;
    pt = {tt};
    rt = compiler.arrayType(compiler.anyType());
    cf = builtin_toAnyArray;
    return true;
}

// ============================================================================
// gc builtin -- drain the deferred-delete queue
// ============================================================================

static void builtin_gc(VM& vm, u16 dst, u16, u16) {
    // Process ONLY the deferred delete queue. Do NOT drain the auto-release
    // pool -- it keeps register-referenced objects alive during execution.
    // processN skips objects with refcount > 0 (still pool-referenced),
    // so only truly dead objects are deleted.
    vm.foreignDeleteQueue().drainInto(vm.deferredDeleteQueue());
    while (!vm.deferredDeleteQueue().empty()) {
        vm.foreignDeleteQueue().drainInto(vm.deferredDeleteQueue());
        vm.deferredDeleteQueue().processN(4096);
    }
    vm.reg(dst).i = 0;
}

// ============================================================================
// __gc_trace_cycle builtin -- run one tracing-GC cycle (shadow mode)
// Returns the number of objects rooted by the cycle (lastRootCount). Useful
// for Phase 3b smoke tests: verifies the collector walks roots without
// crashing and produces a plausible count. Phase 3 keeps ARC as the actual
// reclaimer; the cycle here observes but does not free.
// ============================================================================

static void builtin_gc_trace_cycle(VM& vm, u16 dst, u16, u16) {
    vm.tracingGC().runFullCycle();
    vm.reg(dst).i = (i64)vm.tracingGC().lastRootCount();
}

// Returns lastBlackCount: total objects reached transitively from the roots.
static void builtin_gc_trace_blacks(VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = (i64)vm.tracingGC().lastBlackCount();
}

// Returns lastWhiteCount: objects on the all-objects list that were not
// reached by the last cycle (potential garbage from tracing's POV).
static void builtin_gc_trace_whites(VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).i = (i64)vm.tracingGC().lastWhiteCount();
}

// ============================================================================
// typeRepr builtin (Phase 0 debug helper)
// Prints the static type's representation classification.
// Usage: typeRepr(value)
// ============================================================================

static const char* reprName(Type::Repr r) {
    switch (r) {
        case Type::Repr::Atom:                 return "Atom";
        case Type::Repr::Pointer:              return "Pointer";
        case Type::Repr::DiscriminantEnum:     return "DiscriminantEnum";
        case Type::Repr::NullablePtrEnum:      return "NullablePtrEnum";
        case Type::Repr::UnwrappedTupleStruct: return "UnwrappedTupleStruct";
        case Type::Repr::Inline:               return "Inline";
        case Type::Repr::Heap:                 return "Heap";
    }
    return "?";
}

static void builtin_typeRepr(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    Type* t = tt->fields_[0];
    FILE* out = vm.printOutput();
    auto typeStr = t->str();
    std::fprintf(out, "%.*s: repr=%s sizeWords=%u value=%d recursive=%d",
        (int)typeStr.size(), typeStr.data(),
        reprName(t->repr_),
        (unsigned)t->sizeWords_,
        t->isValueType_ ? 1 : 0,
        t->isRecursive_ ? 1 : 0);
    // Phase 4g.1: surface inline-promotion eligibility for composites that
    // qualify but haven't yet been runtime-promoted.
    if (t->couldBeInline_) {
        std::fprintf(out, " inline=%u", (unsigned)t->inlineLayoutWords_);
    }

    // Print layout if available
    auto printLayout = [out](const Vec<FieldLayout>& layout) {
        if (layout.empty()) return;
        std::fprintf(out, " layout=[");
        for (size_t i = 0; i < layout.size(); ++i) {
            if (i > 0) std::fprintf(out, ",");
            auto fs = layout[i].type ? layout[i].type->str() : rt::vmstr("?");
            std::fprintf(out, "(@%u,%uw,%.*s)",
                (unsigned)layout[i].wordOffset,
                (unsigned)layout[i].sizeWords,
                (int)fs.size(), fs.data());
        }
        std::fprintf(out, "]");
    };
    if (auto* st = dynamic_cast<StructType*>(t)) printLayout(st->layout_);
    else if (auto* en = dynamic_cast<EnumType*>(t)) printLayout(en->layout_);
    else if (auto* tu = dynamic_cast<TupleType*>(t)) printLayout(tu->layout_);

    std::fputc('\n', out);
    std::fflush(out);
    vm.reg(dst).i = 0;
}

static bool resolve_typeRepr(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    pt = args;
    rt = compiler.voidType();
    cf = builtin_typeRepr;
    return true;
}

// ============================================================================
// disassemble builtin
// ============================================================================

static void builtin_disassemble(VM& vm, u16 dst, u16, u16 argBase) {
    auto* callable = static_cast<Callable*>(vm.reg(argBase).o);
    auto* lambda = dynamic_cast<Lambda*>(callable);
    if (lambda && lambda->codeBlock_) {
        disassembleCodeBlock(lambda->codeBlock_, vm.printOutput());
    } else {
        std::fprintf(vm.printOutput(), "[builtin function -- no bytecode]\n");
    }
    std::fflush(vm.printOutput());
    vm.reg(dst).i = 0;
}

static bool resolve_disassemble(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    // Accept any function type (FunctionType, LambdaType)
    if (!dynamic_cast<FunctionType*>(args[0]) && !dynamic_cast<TemplateLambdaType*>(args[0]))
        return false;
    pt = args;
    rt = compiler.voidType();
    cf = builtin_disassemble;
    return true;
}

// ============================================================================
// Registration
// ============================================================================

void registerBuiltinFunctions(Compiler& compiler,
    std::unordered_map<std::string, std::deque<FuncInfo>>& functions)
{
    // Register sub-module builtins
    registerMathBuiltins(compiler, functions);
    registerArrayBuiltins(compiler, functions);
    registerListGenBuiltins(compiler, functions);

    // --- Collection builtins (template-resolved) ---
    // Phase 4g.27: all higher-order builtins now use the native multi-word
    // ABI at the call boundary. Args/returns that are Inline composites
    // (Complex/Fraction/Tuple/Struct/Enum) travel as sizeWords_-wide
    // register windows -- no box-then-unbox round-trip at the boundary.
    registerTemplate(compiler, functions, "reverse",   resolve_reverse_a, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "pop",       resolve_pop_a,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "muss",      resolve_muss_a,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "sort",      resolve_sort,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "grade",     resolve_grade,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "take",      resolve_take,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "drop",      resolve_drop,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "stride",    resolve_stride,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "stutter",   resolve_stutter,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "repeat",    resolve_repeat,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "cat",       resolve_cat,       /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "join",      resolve_join,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "flatten",   resolve_flatten,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "map",       resolve_map,       /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "filter",    resolve_filter,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "fold",      resolve_fold,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "scan",      resolve_scan,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "fold1",     resolve_fold1,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "scan1",     resolve_scan1,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "find",      resolve_find,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "iter",      resolve_iter,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "takeWhile", resolve_takeWhile, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "dropWhile", resolve_dropWhile, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "zip",       resolve_zip,       /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "enumerate", resolve_enumerate, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "cyc",       resolve_cyc,       /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "ncyc",      resolve_ncyc,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "hang",      resolve_hang,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "head",      resolve_head,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "tail",      resolve_tail,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "cons",      resolve_cons,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "push",      resolve_push,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "isNil",     resolve_isNil,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "notNil",    resolve_notNil,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "length",    resolve_length,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    // Phase 4g.6: ordinal/tag only read the discriminant (word 0); for
    // Inline enums that lets us skip the per-call box-then-read.
    registerTemplate(compiler, functions, "ordinal",   resolve_ordinal, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "tag",       resolve_tag,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    registerTemplate(compiler, functions, "toList",    resolve_toList_array,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "toList",    resolve_toList_coroutine, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "codePoints", resolve_codePoints,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "collect",   resolve_collect,          /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "pick",      resolve_pick,             /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "picks",     resolve_picks,            /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- Map builtins ---
    // Phase 4g.11: Map keys/values are stored natively (multi-word for inline
    // composites) so the builtins read args as multi-word slots.
    registerTemplate(compiler, functions, "get",          resolve_get_map,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "put",          resolve_put,        /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "remove",       resolve_remove,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "contains",     resolve_contains,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "keys",         resolve_keys,       /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "values",       resolve_values,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "pairs",        resolve_pairs,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "merge",        resolve_merge,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- Option builtins ---
    // Phase 4g.6: Inline Option dispatches to *_inline variants that read
    // discriminant + payload directly from the multi-word slot. Auto-map
    // sites unbox container elements before the call (see genAutoMapCall),
    // so `[Option<Int>] @ unwrap` works.
    registerTemplate(compiler, functions, "unwrap",       resolve_unwrap,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "unwrapOr",     resolve_unwrapOr, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "isSome",       resolve_isSome,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "isNone",       resolve_isNone,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- Coroutine builtins ---
    // Phase 4g.12: yield/next handle inline composite yield types natively.
    registerTemplate(compiler, functions, "next",         resolve_next,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "yield",        resolve_yield,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "yieldAll",     resolve_yieldAll, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- Set builtins ---
    // Phase 4g.11: Set elements are stored natively (multi-word for inline
    // composites). The Map "remove"/"contains" templates above also handle
    // Set arguments through the shared resolvers.
    registerTemplate(compiler, functions, "add",          resolve_add_set,         /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "union",        resolve_union,           /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "intersection", resolve_intersection,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "difference",   resolve_difference,      /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "toArray",      resolve_toArray_set,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- hash builtin ---
    // Phase 4g.6: hash resolves builtin_hash_inline for Inline-classified
    // types and reads its arg directly out of a multi-word slot.
    registerTemplate(compiler, functions, "hash",         resolve_hash, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- toString builtin ---
    // Phase 4g.6: toString reads inline composites natively via slotToString.
    registerTemplate(compiler, functions, "toString",     resolve_toString, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- fmt builtin ---
    registerTemplate(compiler, functions, "fmt",          resolve_fmt,         /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- print/println builtins (allowed on RT for debugging) ---
    // Phase 4g.6: opted in to inline-composite arg passing (printArgs reads
    // multi-word inline slots directly via slotToString).
    registerTemplate(compiler, functions, "print",        resolve_print,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "println",      resolve_println, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- disassemble builtin (not RT-safe: writes to stdout) ---
    registerTemplate(compiler, functions, "disassemble",  resolve_disassemble, /*rtSafe=*/false, /*acceptsInlineArgs=*/true);

    // --- typeRepr builtin (Phase 0 debug helper, not RT-safe: writes to stdout) ---
    // Phase 4g.6: typeRepr only looks at the static type, never reads the
    // arg's value. Skip the boxing for inline composites.
    registerTemplate(compiler, functions, "typeRepr",     resolve_typeRepr, /*rtSafe=*/false, /*acceptsInlineArgs=*/true);

    // --- gc builtin: drain deferred-delete queue to reclaim dead objects ---
    registerOne(compiler, functions, "gc", compiler.voidType(), {}, builtin_gc, /*pure=*/false, /*rtSafe=*/false);

    // --- __gc_trace_cycle builtin: run one Phase 3 tracing cycle (shadow mode) ---
    // Returns Int (root count). Underscored to discourage normal-program use.
    registerOne(compiler, functions, "__gc_trace_cycle",  compiler.intType(), {}, builtin_gc_trace_cycle,  /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "__gc_trace_blacks", compiler.intType(), {}, builtin_gc_trace_blacks, /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "__gc_trace_whites", compiler.intType(), {}, builtin_gc_trace_whites, /*pure=*/false, /*rtSafe=*/false);

    // --- Ref builtins ---
    // Phase 4g.6: ref / deref / setref read inline-composite args directly
    // out of the multi-word slot, eliminating the box-then-unbox round trip
    // that the old builtin calling convention required.
    registerTemplate(compiler, functions, "ref",          resolve_ref,       /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "deref",        resolve_deref,     /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "setref",       resolve_setref,    /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "setref",       resolve_setref_rev,/*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- Any builtins ---
    registerTemplate(compiler, functions, "any",          resolve_any_single,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "any",          resolve_any_variadic, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "toAnyArray",   resolve_toAnyArray,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
}

} // namespace ts
