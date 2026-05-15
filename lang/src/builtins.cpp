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

// length: [K:V] -> Int
static void builtin_length_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    vm.reg(dst).i = (i64)map->entries_.size();
}

// get: [K:V], K -> Option<V>
static void builtin_get_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    Word key = vm.reg(ab + 1);
    auto it = map->entries_.find(key);
    auto* optType = vm.optionType(mt->valueType_);
    if (it != map->entries_.end()) {
        auto* e = new Enum(optType);
        e->which_ = 0;  // some
        e->word_ = it->second;
        // Phase 4c: case 0 = some; check layout_[0] for pointer storage.
        if (!optType->layout_.empty()
            && storesObjPtr(optType->layout_[0].type)
            && e->word_.o) {
            e->word_.o->retain();
        }
        vm.reg(dst).o = e;
    } else {
        auto* e = new Enum(optType);
        e->which_ = 1;  // none
        vm.reg(dst).o = e;
    }
}

// get: [K:V], K, V -> V  (with default)
static void builtin_get_map_default(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    Word key = vm.reg(ab + 1);
    auto it = map->entries_.find(key);
    if (it != map->entries_.end()) {
        vm.reg(dst) = it->second;
    } else {
        vm.reg(dst) = vm.reg(ab + 2);
    }
}

// put: [K:V], K, V -> [K:V]
static void builtin_put_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    auto* result = new MapObj(mt);
    result->entries_ = map->entries_;
    result->entries_[vm.reg(ab + 1)] = vm.reg(ab + 2);
    // Retain all Obj* keys and values in the new map
    bool keyIsObj = storesObjPtr(mt->keyType_);
    bool valIsObj = storesObjPtr(mt->valueType_);
    if (keyIsObj || valIsObj) {
        for (auto& [k, v] : result->entries_) {
            if (keyIsObj && k.o) k.o->retain();
            if (valIsObj && v.o) v.o->retain();
        }
    }
    vm.reg(dst).o = result;
}

// remove: [K:V], K -> [K:V]
static void builtin_remove_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    auto* result = new MapObj(mt);
    result->entries_ = map->entries_;
    result->entries_.erase(vm.reg(ab + 1));
    // Retain all Obj* keys and values in the new map
    bool keyIsObj = storesObjPtr(mt->keyType_);
    bool valIsObj = storesObjPtr(mt->valueType_);
    if (keyIsObj || valIsObj) {
        for (auto& [k, v] : result->entries_) {
            if (keyIsObj && k.o) k.o->retain();
            if (valIsObj && v.o) v.o->retain();
        }
    }
    vm.reg(dst).o = result;
}

// contains: [K:V], K -> Bool
static void builtin_contains_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    vm.reg(dst).i = map->entries_.count(vm.reg(ab + 1)) ? 1 : 0;
}

// keys: [K:V] -> [K]
// Phase 4e: dispatch via arrayBackendFor so Map[Complex,_].keys() and
// Map[Fraction,_].keys() unbox the still-boxed map keys into the inline
// array backend.
static void builtin_keys_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    Type* kt = mt->keyType_;
    auto* arrType = vm.arrayType(kt);
    switch (arrayBackendFor(kt)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrType);
            for (auto const& [k, v] : map->entries_) {
                auto* c = static_cast<Complex*>(k.o);
                arr->v.push_back(c->x);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrType);
            for (auto const& [k, v] : map->entries_) {
                auto* f = static_cast<Fraction*>(k.o);
                arr->v.push_back(f->r);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrType);
            for (auto const& [k, v] : map->entries_) arr->v.push_back(k.f);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrType);
            for (auto const& [k, v] : map->entries_) arr->v.push_back(k.i);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrType);
            for (auto const& [k, v] : map->entries_) arr->push(k.o);
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
    switch (arrayBackendFor(vt)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrType);
            for (auto const& [k, v] : map->entries_) {
                auto* c = static_cast<Complex*>(v.o);
                arr->v.push_back(c->x);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrType);
            for (auto const& [k, v] : map->entries_) {
                auto* f = static_cast<Fraction*>(v.o);
                arr->v.push_back(f->r);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrType);
            for (auto const& [k, v] : map->entries_) arr->v.push_back(v.f);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrType);
            for (auto const& [k, v] : map->entries_) arr->v.push_back(v.i);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrType);
            for (auto const& [k, v] : map->entries_) arr->push(v.o);
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
    result->entries_ = a->entries_;
    for (auto& [k, v] : b->entries_) {
        result->entries_[k] = v;
    }
    // Retain all Obj* keys and values in the new map
    bool keyIsObj = storesObjPtr(mt->keyType_);
    bool valIsObj = storesObjPtr(mt->valueType_);
    if (keyIsObj || valIsObj) {
        for (auto& [k, v] : result->entries_) {
            if (keyIsObj && k.o) k.o->retain();
            if (valIsObj && v.o) v.o->retain();
        }
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
static void builtin_pairs_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc};
    fields.push_back(mt->keyType_);
    fields.push_back(mt->valueType_);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    auto* result = new ObjArray(resAT);
    for (auto& [k, v] : map->entries_) {
        auto* tup = Tuple::create(tt, 2);
        tup->v[0] = k;
        tup->v[1] = v;
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
static void builtin_unwrap(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    if (e->which_ != 0) {
        fprintf(vm.printOutput(), "Error: unwrap called on none\n");
        vm.setHalted(true);
        return;
    }
    auto* et = static_cast<EnumType*>(e->type_);
    if (storesObjPtr(et->cases_[0].type)) {
        vm.reg(dst).o = e->word_.o;
    } else {
        vm.reg(dst) = e->word_;
    }
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
    if (e->which_ == 0) {
        auto* et = static_cast<EnumType*>(e->type_);
        if (storesObjPtr(et->cases_[0].type)) {
            vm.reg(dst).o = e->word_.o;
        } else {
            vm.reg(dst) = e->word_;
        }
    } else {
        vm.reg(dst) = vm.reg(ab + 1);
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

// Option resolvers
static bool resolve_unwrap(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = et->cases_[0].type;
    cf = (et->repr_ == Type::Repr::NullablePtrEnum) ? builtin_unwrap_nullableptr : builtin_unwrap;
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
    cf = (et->repr_ == Type::Repr::NullablePtrEnum) ? builtin_unwrapOr_nullableptr : builtin_unwrapOr;
    return true;
}

static bool resolve_isSome(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = compiler.boolType();
    cf = (et->repr_ == Type::Repr::NullablePtrEnum) ? builtin_isSome_nullableptr : builtin_isSome;
    return true;
}

static bool resolve_isNone(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = compiler.boolType();
    cf = (et->repr_ == Type::Repr::NullablePtrEnum) ? builtin_isNone_nullableptr : builtin_isNone;
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

// Phase 4g.5: ref for inline composite element types (Struct/Tuple/Enum
// classified as Repr::Inline). The arg arrives as a 1-word boxed Obj*
// because emitArgPlacementForCall boxes inline composites at builtin call
// boundaries; we unbox it into a fresh InlineRef's flex-array payload so
// later REF_GET_INLINE / REF_SET_INLINE can mutate in place.
//
// Internal: unbox a heap Tuple/Struct/Enum* directly into Word* dst (no VM
// regs). Mirrors unboxInlineDeep but writes into raw memory. Matches the
// boxField semantics used by boxInlineDeep.
static void unboxInto(VM& vm, Type* type, Obj* obj, Word* dst) {
    auto unboxField = [&](Type* ft, Word src, Word* fdst) {
        if (!ft) { fdst->i = 0; return; }
        if (ft->repr_ == Type::Repr::Inline
            && ft != gCurrentVM->complexType() && ft != gCurrentVM->fractionType()
            && (dynamic_cast<StructType*>(ft) || dynamic_cast<TupleType*>(ft)
                || dynamic_cast<EnumType*>(ft))) {
            unboxInto(vm, ft, src.o, fdst);
        } else if (ft == gCurrentVM->complexType()) {
            auto* c = static_cast<Complex*>(src.o);
            fdst[0].f = c->x.real();
            fdst[1].f = c->x.imag();
        } else if (ft == gCurrentVM->fractionType()) {
            auto* fr = static_cast<Fraction*>(src.o);
            fdst[0].i = fr->r.numer();
            fdst[1].i = fr->r.denom();
        } else {
            *fdst = src;
            if (storesObjPtr(ft) && src.o) src.o->retain();
        }
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(obj);
        for (size_t i = 0; i < st->fields_.size(); ++i) {
            auto const& f = st->layout_[i];
            unboxField(f.type, s->v[i], dst + f.wordOffset);
        }
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* t = static_cast<Tuple*>(obj);
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            auto const& f = tt->layout_[i];
            unboxField(f.type, t->v[i], dst + f.wordOffset);
        }
        return;
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(obj);
        dst[0].i = e->which_;
        for (u8 i = 1; i < en->sizeWords_; ++i) dst[i].i = 0;
        if (e->which_ >= 0 && (size_t)e->which_ < en->layout_.size()) {
            auto const& f = en->layout_[e->which_];
            if (f.type) {
                bool isVoid = !f.type->isObjType()
                           && (dynamic_cast<VoidType*>(f.type) != nullptr);
                if (!isVoid && f.sizeWords > 0) {
                    unboxField(f.type, e->word_, dst + 1);
                }
            }
        }
        return;
    }
}

// ref(InlineComposite) -> InlineRef
static void builtin_ref_inline(VM& vm, u16 dst, u16, u16 ab) {
    Obj* boxed = vm.reg(ab).o;
    if (!boxed) { vm.reg(dst).o = nullptr; return; }
    auto* refType = static_cast<RefType*>(vm.refType(boxed->type_));
    auto* ref = InlineRef::create(refType);
    unboxInto(vm, refType->elemType_, boxed, &ref->v[0]);
    vm.reg(dst).o = ref;
}

// deref(Ref<T>) -> T
// Phase 4g.5: when the ref is an InlineRef, allocate a fresh boxed
// Tuple/Struct/Enum* and copy the payload through it -- the builtin caller
// expects a 1-word boxed result that emitBuiltinResultUnbox will then unbox
// into the multi-word slot. (The operator form `*r` skips this round-trip.)
static void builtin_deref(VM& vm, u16 dst, u16, u16 ab) {
    auto* obj = vm.reg(ab).o;
    auto* refType = static_cast<RefType*>(obj->type_);
    Type* et = refType->elemType_;
    if (et && et->repr_ == Type::Repr::Inline
        && et != gCurrentVM->complexType() && et != gCurrentVM->fractionType()
        && (dynamic_cast<StructType*>(et) || dynamic_cast<TupleType*>(et)
            || dynamic_cast<EnumType*>(et))) {
        auto* ref = static_cast<InlineRef*>(obj);
        // Box the inline payload back through a temp register window.
        u16 base = vm.currentCodeBlock()->numRegs;
        for (u32 i = 0; i < ref->sizeWords_; ++i) vm.reg((u16)(base + i)) = ref->v[i];
        vm.reg(dst).o = boxInlineDeep(vm, et, base);
        return;
    }
    auto* ref = static_cast<RefValue*>(obj);
    vm.reg(dst) = ref->value_;
}

// setref(T, Ref<T>) -> T
static void builtin_setref(VM& vm, u16 dst, u16, u16 ab) {
    auto* obj = vm.reg(ab + 1).o;
    auto* refType = static_cast<RefType*>(obj->type_);
    Type* et = refType->elemType_;
    if (et && et->repr_ == Type::Repr::Inline
        && et != gCurrentVM->complexType() && et != gCurrentVM->fractionType()
        && (dynamic_cast<StructType*>(et) || dynamic_cast<TupleType*>(et)
            || dynamic_cast<EnumType*>(et))) {
        auto* ref = static_cast<InlineRef*>(obj);
        Obj* boxed = vm.reg(ab).o;
        // Release embedded Obj* in the old payload, overwrite, retain new.
        inlineWalkPointers(&ref->v[0], et, /*release_=*/true);
        unboxInto(vm, et, boxed, &ref->v[0]);
        // The new payload's Obj* fields are already retained by unboxInto
        // (it calls retain on storesObjPtr leaves). The boxed source itself
        // is borrowed -- the builtin call boundary handles its lifetime.
        vm.reg(dst) = vm.reg(ab);  // result = the assigned (still boxed) value
        return;
    }
    auto* ref = static_cast<RefValue*>(obj);
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
    bool inlineComposite = t && t->repr_ == Type::Repr::Inline
        && t != compiler.complexType() && t != compiler.fractionType()
        && (dynamic_cast<StructType*>(t) || dynamic_cast<TupleType*>(t)
            || dynamic_cast<EnumType*>(t));
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

// setref(Ref<T>, T) -> T
static void builtin_setref_rev(VM& vm, u16 dst, u16, u16 ab) {
    auto* obj = vm.reg(ab).o;
    auto* refType = static_cast<RefType*>(obj->type_);
    Type* et = refType->elemType_;
    if (et && et->repr_ == Type::Repr::Inline
        && et != gCurrentVM->complexType() && et != gCurrentVM->fractionType()
        && (dynamic_cast<StructType*>(et) || dynamic_cast<TupleType*>(et)
            || dynamic_cast<EnumType*>(et))) {
        auto* ref = static_cast<InlineRef*>(obj);
        Obj* boxed = vm.reg(ab + 1).o;
        inlineWalkPointers(&ref->v[0], et, /*release_=*/true);
        unboxInto(vm, et, boxed, &ref->v[0]);
        vm.reg(dst) = vm.reg(ab + 1);
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
    vm.reg(dst).i = (i64)set->entries_.size();
}

// add: Set<T>, T -> Set<T>
static void builtin_add_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(src->type_);
    auto* result = new SetObj(st);
    result->entries_ = src->entries_;
    result->entries_.insert(vm.reg(ab + 1));
    // Retain all Obj* elements
    if (storesObjPtr(st->elemType_)) {
        for (auto& elem : result->entries_) {
            if (elem.o) elem.o->retain();
        }
    }
    vm.reg(dst).o = result;
}

// remove: Set<T>, T -> Set<T>
static void builtin_remove_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(src->type_);
    auto* result = new SetObj(st);
    result->entries_ = src->entries_;
    result->entries_.erase(vm.reg(ab + 1));
    // Retain all Obj* elements
    if (storesObjPtr(st->elemType_)) {
        for (auto& elem : result->entries_) {
            if (elem.o) elem.o->retain();
        }
    }
    vm.reg(dst).o = result;
}

// contains: Set<T>, T -> Bool
static void builtin_contains_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    vm.reg(dst).i = set->entries_.count(vm.reg(ab + 1)) ? 1 : 0;
}

// union: Set<T>, Set<T> -> Set<T>
static void builtin_union_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    result->entries_ = a->entries_;
    for (auto& elem : b->entries_) result->entries_.insert(elem);
    // Retain all Obj* elements
    if (storesObjPtr(st->elemType_)) {
        for (auto& elem : result->entries_) {
            if (elem.o) elem.o->retain();
        }
    }
    vm.reg(dst).o = result;
}

// intersection: Set<T>, Set<T> -> Set<T>
static void builtin_intersection_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    for (auto& elem : a->entries_) {
        if (b->entries_.count(elem)) result->entries_.insert(elem);
    }
    // Retain all Obj* elements
    if (storesObjPtr(st->elemType_)) {
        for (auto& elem : result->entries_) {
            if (elem.o) elem.o->retain();
        }
    }
    vm.reg(dst).o = result;
}

// difference: Set<T>, Set<T> -> Set<T>
static void builtin_difference_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    for (auto& elem : a->entries_) {
        if (!b->entries_.count(elem)) result->entries_.insert(elem);
    }
    // Retain all Obj* elements
    if (storesObjPtr(st->elemType_)) {
        for (auto& elem : result->entries_) {
            if (elem.o) elem.o->retain();
        }
    }
    vm.reg(dst).o = result;
}

// toArray: Set<T> -> [T]
// Phase 4e: dispatch via arrayBackendFor; Set still stores boxed
// Complex/Fraction so we unbox on extraction.
static void builtin_toArray_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(set->type_);
    Type* elemType = st->elemType_;
    auto* arrType = vm.arrayType(elemType);

    switch (arrayBackendFor(elemType)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrType);
            for (auto const& elem : set->entries_) {
                auto* c = static_cast<Complex*>(elem.o);
                arr->v.push_back(c->x);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrType);
            for (auto const& elem : set->entries_) {
                auto* f = static_cast<Fraction*>(elem.o);
                arr->v.push_back(f->r);
            }
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrType);
            for (auto const& elem : set->entries_) arr->v.push_back(elem.f);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrType);
            for (auto const& elem : set->entries_) arr->v.push_back(elem.i);
            vm.reg(dst).o = arr;
            return;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrType);
            for (auto const& elem : set->entries_) arr->push(elem.o);
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

static bool resolve_ordinal(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* et = dynamic_cast<EnumType*>(args[0])) {
        pt = {et};
        rt = compiler.intType();
        if (et->repr_ == Type::Repr::DiscriminantEnum)        cf = builtin_ordinal_discenum;
        else if (et->repr_ == Type::Repr::NullablePtrEnum)    cf = builtin_ordinal_nullableptr;
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

static bool resolve_tag(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* et = dynamic_cast<EnumType*>(args[0])) {
        pt = {et};
        rt = compiler.symbolType();
        if (et->repr_ == Type::Repr::DiscriminantEnum)        cf = builtin_tag_discenum;
        else if (et->repr_ == Type::Repr::NullablePtrEnum)    cf = builtin_tag_nullableptr;
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
                    result += wordToString(tup->v[nextPos], tt->fields_[nextPos]);
                    ++nextPos;
                }
                ++i;
            } else if (next >= '0' && next <= '9') {
                u32 idx = (u32)(next - '0');
                if (idx < numFields) {
                    result += wordToString(tup->v[idx], tt->fields_[idx]);
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
static void builtin_any_single(VM& vm, u16 dst, u16, u16 argBase) {
    auto* prim = vm.currentPrimitive();
    auto* ft = static_cast<FunctionType*>(prim->type_);
    Type* wrappedType = ft->argTypes_[0];
    auto* any = new AnyObj(vm.anyType());
    // Phase 4f: inline value types live as multi-word slots in registers; box
    // them into a heap Obj before storing in AnyObj's single-Word value_ slot.
    if (wrappedType == vm.complexType()) {
        f64 re = vm.reg(argBase).f;
        f64 im = vm.reg((u16)(argBase + 1)).f;
        any->value_.o = new Complex(x64(re, im));
    } else if (wrappedType == vm.fractionType()) {
        i64 n = vm.reg(argBase).i;
        i64 d = vm.reg((u16)(argBase + 1)).i;
        any->value_.o = new Fraction(r64(n, d));
    } else {
        any->value_ = vm.reg(argBase);
    }
    any->wrappedType_ = wrappedType;
    any->isObjType_ = storesObjPtr(wrappedType);
    if (any->isObjType_ && any->value_.o) any->value_.o->retain();
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
static void builtin_any_variadic(VM& vm, u16 dst, u16, u16 argBase) {
    auto* tuple = static_cast<Tuple*>(vm.reg(argBase).o);
    auto* tupleType = static_cast<TupleType*>(tuple->type_);
    auto* anyArrayType = vm.arrayType(vm.anyType());
    auto* arr = new ObjArray(anyArrayType);
    size_t n = tuple->numFields_;
    arr->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto* any = new AnyObj(vm.anyType());
        any->value_ = tuple->v[i];
        any->wrappedType_ = tupleType->fields_[i];
        any->isObjType_ = storesObjPtr(tupleType->fields_[i]);
        if (any->isObjType_ && any->value_.o) any->value_.o->retain();
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
// Phase 4g.2: tuple param arrives as a 1-Word boxed Tuple* (the codegen
// box-at-builtin-boundary applies even when the source-language tuple type
// is classified Inline). We can keep the original heap-Tuple* logic.
static void builtin_toAnyArray(VM& vm, u16 dst, u16, u16 argBase) {
    auto* tuple = static_cast<Tuple*>(vm.reg(argBase).o);
    auto* tupleType = static_cast<TupleType*>(tuple->type_);
    auto* anyArrayType = vm.arrayType(vm.anyType());
    auto* arr = new ObjArray(anyArrayType);
    size_t n = tuple->numFields_;
    arr->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        auto* any = new AnyObj(vm.anyType());
        any->value_ = tuple->v[i];
        any->wrappedType_ = tupleType->fields_[i];
        any->isObjType_ = storesObjPtr(tupleType->fields_[i]);
        if (any->isObjType_ && any->value_.o) any->value_.o->retain();
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
    registerTemplate(compiler, functions, "reverse",   resolve_reverse_a);
    registerTemplate(compiler, functions, "push",      resolve_push);
    registerTemplate(compiler, functions, "pop",       resolve_pop_a);
    registerTemplate(compiler, functions, "muss",      resolve_muss_a);
    registerTemplate(compiler, functions, "sort",      resolve_sort);
    registerTemplate(compiler, functions, "grade",     resolve_grade);
    registerTemplate(compiler, functions, "take",      resolve_take);
    registerTemplate(compiler, functions, "drop",      resolve_drop);
    registerTemplate(compiler, functions, "stride",    resolve_stride);
    registerTemplate(compiler, functions, "stutter",   resolve_stutter);
    registerTemplate(compiler, functions, "repeat",    resolve_repeat);
    registerTemplate(compiler, functions, "cat",       resolve_cat);
    registerTemplate(compiler, functions, "join",      resolve_join);
    registerTemplate(compiler, functions, "flatten",   resolve_flatten);
    registerTemplate(compiler, functions, "map",       resolve_map);
    registerTemplate(compiler, functions, "filter",    resolve_filter);
    registerTemplate(compiler, functions, "fold",      resolve_fold);
    registerTemplate(compiler, functions, "scan",      resolve_scan);
    registerTemplate(compiler, functions, "fold1",     resolve_fold1);
    registerTemplate(compiler, functions, "scan1",     resolve_scan1);
    registerTemplate(compiler, functions, "find",      resolve_find);
    registerTemplate(compiler, functions, "iter",      resolve_iter);
    registerTemplate(compiler, functions, "takeWhile", resolve_takeWhile);
    registerTemplate(compiler, functions, "dropWhile", resolve_dropWhile);
    registerTemplate(compiler, functions, "zip",       resolve_zip);
    registerTemplate(compiler, functions, "enumerate", resolve_enumerate);
    registerTemplate(compiler, functions, "cyc",       resolve_cyc);
    registerTemplate(compiler, functions, "ncyc",      resolve_ncyc);
    registerTemplate(compiler, functions, "hang",      resolve_hang);
    registerTemplate(compiler, functions, "head",      resolve_head);
    registerTemplate(compiler, functions, "tail",      resolve_tail);
    registerTemplate(compiler, functions, "cons",      resolve_cons);
    registerTemplate(compiler, functions, "isNil",     resolve_isNil);
    registerTemplate(compiler, functions, "notNil",    resolve_notNil);
    registerTemplate(compiler, functions, "length",    resolve_length);
    registerTemplate(compiler, functions, "ordinal",   resolve_ordinal);
    registerTemplate(compiler, functions, "tag",       resolve_tag);

    registerTemplate(compiler, functions, "toList",    resolve_toList_array);
    registerTemplate(compiler, functions, "toList",    resolve_toList_coroutine);
    registerTemplate(compiler, functions, "codePoints", resolve_codePoints);
    registerTemplate(compiler, functions, "collect",   resolve_collect);
    registerTemplate(compiler, functions, "pick",      resolve_pick);
    registerTemplate(compiler, functions, "picks",     resolve_picks);

    // --- Map builtins ---
    registerTemplate(compiler, functions, "get",          resolve_get_map);
    registerTemplate(compiler, functions, "put",          resolve_put);
    registerTemplate(compiler, functions, "remove",       resolve_remove);
    registerTemplate(compiler, functions, "contains",     resolve_contains);
    registerTemplate(compiler, functions, "keys",         resolve_keys);
    registerTemplate(compiler, functions, "values",       resolve_values);
    registerTemplate(compiler, functions, "pairs",        resolve_pairs);
    registerTemplate(compiler, functions, "merge",        resolve_merge);

    // --- Option builtins ---
    registerTemplate(compiler, functions, "unwrap",       resolve_unwrap);
    registerTemplate(compiler, functions, "unwrapOr",     resolve_unwrapOr);
    registerTemplate(compiler, functions, "isSome",       resolve_isSome);
    registerTemplate(compiler, functions, "isNone",       resolve_isNone);

    // --- Coroutine builtins ---
    registerTemplate(compiler, functions, "next",         resolve_next);
    registerTemplate(compiler, functions, "yield",        resolve_yield);
    registerTemplate(compiler, functions, "yieldAll",     resolve_yieldAll);

    // --- Set builtins ---
    registerTemplate(compiler, functions, "add",          resolve_add_set);
    registerTemplate(compiler, functions, "union",        resolve_union);
    registerTemplate(compiler, functions, "intersection", resolve_intersection);
    registerTemplate(compiler, functions, "difference",   resolve_difference);
    registerTemplate(compiler, functions, "toArray",      resolve_toArray_set);

    // --- hash builtin ---
    // Phase 4g.6: hash resolves builtin_hash_inline for Inline-classified
    // types and reads its arg directly out of a multi-word slot.
    registerTemplate(compiler, functions, "hash",         resolve_hash, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- toString builtin ---
    // Phase 4g.6: toString reads inline composites natively via slotToString.
    registerTemplate(compiler, functions, "toString",     resolve_toString, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- fmt builtin ---
    registerTemplate(compiler, functions, "fmt",          resolve_fmt);

    // --- print/println builtins (allowed on RT for debugging) ---
    // Phase 4g.6: opted in to inline-composite arg passing (printArgs reads
    // multi-word inline slots directly via slotToString).
    registerTemplate(compiler, functions, "print",        resolve_print,   /*rtSafe=*/true, /*acceptsInlineArgs=*/true);
    registerTemplate(compiler, functions, "println",      resolve_println, /*rtSafe=*/true, /*acceptsInlineArgs=*/true);

    // --- disassemble builtin (not RT-safe: writes to stdout) ---
    registerTemplate(compiler, functions, "disassemble",  resolve_disassemble, /*rtSafe=*/false);

    // --- typeRepr builtin (Phase 0 debug helper, not RT-safe: writes to stdout) ---
    registerTemplate(compiler, functions, "typeRepr",     resolve_typeRepr, /*rtSafe=*/false);

    // --- gc builtin: drain deferred-delete queue to reclaim dead objects ---
    registerOne(compiler, functions, "gc", compiler.voidType(), {}, builtin_gc, /*pure=*/false, /*rtSafe=*/false);

    // --- Ref builtins ---
    registerTemplate(compiler, functions, "ref",          resolve_ref);
    registerTemplate(compiler, functions, "deref",        resolve_deref);
    registerTemplate(compiler, functions, "setref",       resolve_setref);
    registerTemplate(compiler, functions, "setref",       resolve_setref_rev);

    // --- Any builtins ---
    registerTemplate(compiler, functions, "any",          resolve_any_single);
    registerTemplate(compiler, functions, "any",          resolve_any_variadic);
    registerTemplate(compiler, functions, "toAnyArray",   resolve_toAnyArray);
}

} // namespace ts
