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
//  type_checker_overload.cpp
//  lang
//
//  Type checker -- type utilities, overload resolution, and template instantiation
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

// --- Type utilities ---

bool TypeChecker::isNumeric(Type* t) const {
    if (t == compiler_.intType() || t == compiler_.floatType() || t == compiler_.boolType()
        || t == compiler_.fractionType() || t == compiler_.complexType())
        return true;
    if (auto* at = dynamic_cast<ArrayType*>(t))
        return isNumeric(at->elemType_);
    if (auto* lt = dynamic_cast<ListType*>(t))
        return isNumeric(lt->elemType_);
    if (auto* pv = dynamic_cast<PersistentVectorType*>(t))
        return isNumeric(pv->elemType_);
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        for (Type* f : tt->fields_)
            if (!isNumeric(f)) return false;
        return !tt->fields_.empty();
    }
    return false;
}

bool TypeChecker::containsComplex(Type* t) const {
    if (!t) return false;
    if (t == compiler_.complexType()) return true;
    if (auto* at = dynamic_cast<ArrayType*>(t))
        return containsComplex(at->elemType_);
    if (auto* lt = dynamic_cast<ListType*>(t))
        return containsComplex(lt->elemType_);
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        for (Type* f : tt->fields_)
            if (containsComplex(f)) return true;
        return false;
    }
    return false;
}

bool TypeChecker::isBoolComposite(Type* t) const {
    if (t == compiler_.boolType()) return true;
    if (auto* at = dynamic_cast<ArrayType*>(t))
        return isBoolComposite(at->elemType_);
    if (auto* lt = dynamic_cast<ListType*>(t))
        return isBoolComposite(lt->elemType_);
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        for (Type* f : tt->fields_)
            if (!isBoolComposite(f)) return false;
        return !tt->fields_.empty();
    }
    return false;
}

bool TypeChecker::isIntComposite(Type* t) const {
    if (t == compiler_.intType()) return true;
    if (auto* at = dynamic_cast<ArrayType*>(t))
        return isIntComposite(at->elemType_);
    if (auto* lt = dynamic_cast<ListType*>(t))
        return isIntComposite(lt->elemType_);
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        for (Type* f : tt->fields_)
            if (!isIntComposite(f)) return false;
        return !tt->fields_.empty();
    }
    return false;
}

bool TypeChecker::typesEqual(Type* a, Type* b) const {
    return a == b;  // Types are interned, pointer comparison works
}

bool TypeChecker::typesNominallyEqual(Type* a, Type* b) const {
    if (a == b) return true;
    if (!a || !b) return false;
    // Type::str() encodes the structural composition of composite types and
    // returns the interned name for user-defined struct/enum. So two types from
    // different TypeChecker instances representing the same logical type compare
    // equal here even though their pointers differ.
    return std::string(a->str()) == std::string(b->str());
}

int TypeChecker::numericRank(Type* t) const {
    if (t == compiler_.boolType()) return 0;
    if (t == compiler_.intType()) return 1;
    if (t == compiler_.fractionType()) return 2;
    if (t == compiler_.floatType()) return 3;
    if (t == compiler_.complexType()) return 4;
    if (dynamic_cast<ArrayType*>(t)) return 5;
    if (dynamic_cast<PersistentVectorType*>(t)) return 5;
    if (dynamic_cast<TupleType*>(t)) return 6;
    if (dynamic_cast<ListType*>(t)) return 7;
    return -1;
}

Type* TypeChecker::commonNumericType(Type* a, Type* b, bool isDiv) const {
    int ra = numericRank(a);
    int rb = numericRank(b);

    // Both scalars (ranks 0-4: Bool, Int, Fraction, Float, Complex)
    if (ra <= 4 && rb <= 4) {
        // Division special case: Int/Int or Bool/Bool -> Fraction
        if (isDiv && ra <= 1 && rb <= 1)
            return compiler_.fractionType();
        int maxRank = (ra > rb) ? ra : rb;
        switch (maxRank) {
            case 0: return compiler_.boolType();
            case 1: return compiler_.intType();
            case 2: return compiler_.fractionType();
            case 3: return compiler_.floatType();
            case 4: return compiler_.complexType();
            default: return compiler_.intType();
        }
    }

    // List is highest rank (6) — handle first
    auto* listA = dynamic_cast<ListType*>(a);
    auto* listB = dynamic_cast<ListType*>(b);

    // List + List → zip element types
    if (listA && listB)
        return compiler_.listType(commonNumericType(listA->elemType_, listB->elemType_, isDiv));
    // List + anything → map over list, broadcast other
    if (listA)
        return compiler_.listType(commonNumericType(listA->elemType_, b, isDiv));
    // anything + List → map over list, broadcast other
    if (listB)
        return compiler_.listType(commonNumericType(a, listB->elemType_, isDiv));

    // Persistent vector broadcasts elementwise like Array, producing #[...].
    auto* pvA = dynamic_cast<PersistentVectorType*>(a);
    auto* pvB = dynamic_cast<PersistentVectorType*>(b);
    if (pvA && pvB)
        return compiler_.persistentVectorType(commonNumericType(pvA->elemType_, pvB->elemType_, isDiv));
    if (pvA && !dynamic_cast<ArrayType*>(b) && !dynamic_cast<TupleType*>(b))
        return compiler_.persistentVectorType(commonNumericType(pvA->elemType_, b, isDiv));
    if (pvB && !dynamic_cast<ArrayType*>(a) && !dynamic_cast<TupleType*>(a))
        return compiler_.persistentVectorType(commonNumericType(a, pvB->elemType_, isDiv));

    auto* arrA = dynamic_cast<ArrayType*>(a);
    auto* arrB = dynamic_cast<ArrayType*>(b);
    auto* tupA = dynamic_cast<TupleType*>(a);
    auto* tupB = dynamic_cast<TupleType*>(b);
    bool scalarA = !arrA && !tupA;
    bool scalarB = !arrB && !tupB;

    // Array + Array
    if (arrA && arrB)
        return compiler_.arrayType(commonNumericType(arrA->elemType_, arrB->elemType_, isDiv));

    // Array + Scalar
    if (arrA && scalarB)
        return compiler_.arrayType(commonNumericType(arrA->elemType_, b, isDiv));

    // Scalar + Array
    if (scalarA && arrB)
        return compiler_.arrayType(commonNumericType(a, arrB->elemType_, isDiv));

    // Tuple + Tuple (same arity)
    if (tupA && tupB) {
        if (tupA->fields_.size() != tupB->fields_.size()) {
            // Error handled by caller
            return compiler_.intType();
        }
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (size_t i = 0; i < tupA->fields_.size(); ++i)
            fields.push_back(commonNumericType(tupA->fields_[i], tupB->fields_[i], isDiv));
        return compiler_.tupleType(fields);
    }

    // Tuple + Scalar
    if (tupA && scalarB) {
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (Type* f : tupA->fields_)
            fields.push_back(commonNumericType(f, b, isDiv));
        return compiler_.tupleType(fields);
    }

    // Scalar + Tuple
    if (scalarA && tupB) {
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (Type* f : tupB->fields_)
            fields.push_back(commonNumericType(a, f, isDiv));
        return compiler_.tupleType(fields);
    }

    // Tuple + Array: array broadcasts to each tuple field
    if (tupA && arrB) {
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (Type* f : tupA->fields_)
            fields.push_back(commonNumericType(f, b, isDiv));
        return compiler_.tupleType(fields);
    }

    // Array + Tuple: array broadcasts to each tuple field
    if (arrA && tupB) {
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (Type* f : tupB->fields_)
            fields.push_back(commonNumericType(a, f, isDiv));
        return compiler_.tupleType(fields);
    }

    return compiler_.intType();
}

Type* TypeChecker::comparisonResultType(Type* a, Type* b) const {
    // Mirrors commonNumericType structure but returns Bool at scalar leaves
    auto* listA = dynamic_cast<ListType*>(a);
    auto* listB = dynamic_cast<ListType*>(b);
    if (listA && listB) return compiler_.listType(comparisonResultType(listA->elemType_, listB->elemType_));
    if (listA) return compiler_.listType(comparisonResultType(listA->elemType_, b));
    if (listB) return compiler_.listType(comparisonResultType(a, listB->elemType_));

    auto* arrA = dynamic_cast<ArrayType*>(a);
    auto* arrB = dynamic_cast<ArrayType*>(b);
    auto* tupA = dynamic_cast<TupleType*>(a);
    auto* tupB = dynamic_cast<TupleType*>(b);
    bool scalarA = !arrA && !tupA;
    bool scalarB = !arrB && !tupB;

    if (arrA && arrB) return compiler_.arrayType(comparisonResultType(arrA->elemType_, arrB->elemType_));
    if (arrA && scalarB) return compiler_.arrayType(comparisonResultType(arrA->elemType_, b));
    if (scalarA && arrB) return compiler_.arrayType(comparisonResultType(a, arrB->elemType_));

    if (tupA && tupB && tupA->fields_.size() == tupB->fields_.size()) {
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (size_t i = 0; i < tupA->fields_.size(); ++i)
            fields.push_back(comparisonResultType(tupA->fields_[i], tupB->fields_[i]));
        return compiler_.tupleType(fields);
    }
    if (tupA && scalarB) {
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (Type* f : tupA->fields_) fields.push_back(comparisonResultType(f, b));
        return compiler_.tupleType(fields);
    }
    if (scalarA && tupB) {
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (Type* f : tupB->fields_) fields.push_back(comparisonResultType(a, f));
        return compiler_.tupleType(fields);
    }

    return compiler_.boolType();
}

// --- Auto-map helpers ---

AutoMapArg TypeChecker::extractAutoMapAnnotation(Expr* expr) const {
    AutoMapArg result;
    if (expr->kind == ASTNode::AutoMap) {
        auto* am = static_cast<AutoMapExpr*>(expr);
        result.depth = am->depth;
        result.cartesianIndex = am->cartesianIndex;
    }
    return result;
}

Type* TypeChecker::unwrapAutoMapLayers(Type* type, int depth, bool& isList, SourceRange loc,
                                       bool* isPVec) {
    Type* t = type;
    for (int level = 0; level < depth; ++level) {
        if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
            t = arrT->elemType_;
        } else if (auto* listT = dynamic_cast<ListType*>(t)) {
            t = listT->elemType_;
            isList = true;
        } else if (auto* pvT = dynamic_cast<PersistentVectorType*>(t)) {
            t = pvT->elemType_;
            if (isPVec) *isPVec = true;
        } else {
            error(loc, "Explicit '@' requires Array, List, or persistent vector type (need " +
                  std::to_string(depth) + " levels, found " +
                  std::to_string(level) + ")");
            return type;
        }
    }
    return t;
}

Type* TypeChecker::wrapAutoMapResult(Type* scalarResult, const AutoMapArg& leftAM,
                                     const AutoMapArg& rightAM, bool anyList, bool anyPVec) {
    // Container precedence when operands mix kinds: List > PVec > Array.
    auto wrap = [&](Type* t) -> Type* {
        if (anyList) return compiler_.listType(t);
        if (anyPVec) return compiler_.persistentVectorType(t);
        return compiler_.arrayType(t);
    };
    Type* retType = scalarResult;
    bool hasCartesian = (leftAM.cartesianIndex > 0) || (rightAM.cartesianIndex > 0);
    if (hasCartesian) {
        int maxIdx = std::max(leftAM.cartesianIndex, rightAM.cartesianIndex);
        for (int level = maxIdx; level >= 1; --level) retType = wrap(retType);
    } else {
        int maxDepth = std::max(leftAM.depth, rightAM.depth);
        for (int level = 0; level < maxDepth; ++level) retType = wrap(retType);
    }
    return retType;
}

// --- Overload resolution ---

bool TypeChecker::isAssignable(Type* from, Type* to) const {
    if (from == to) return true;
    // nil (nullptr) is assignable to List types
    if (!from && dynamic_cast<ListType*>(to)) return true;
    // Existential packing: a concrete type that satisfies C is assignable to
    // `some C`. (checkConstraint mutates internal scratch state, hence the cast;
    // it is re-entrancy-safe via its own recursion guard.)
    if (from) {
        if (auto* ex = dynamic_cast<ExistentialType*>(to)) {
            return const_cast<TypeChecker*>(this)->checkConstraint(
                from, ex->constraintName_, "", "", {}, false);
        }
    }
    // Numeric promotions: bool -> int -> fraction -> float -> complex
    int fromRank = numericRank(from);
    int toRank = numericRank(to);
    if (fromRank >= 0 && fromRank <= 4 && toRank >= 0 && toRank <= 4) {
        return fromRank <= toRank;
    }
    // Simple enum -> Int coercion (enums with only nullary cases)
    if (to == compiler_.intType()) {
        if (auto* enumType = dynamic_cast<EnumType*>(from)) {
            bool allNullary = true;
            for (auto const& c : enumType->cases_) {
                if (c.type != compiler_.voidType()) { allNullary = false; break; }
            }
            if (allNullary) return true;
        }
    }
    // Tuple element-wise assignability
    auto* fromTuple = dynamic_cast<TupleType*>(from);
    auto* toTuple = dynamic_cast<TupleType*>(to);
    if (fromTuple && toTuple && fromTuple->fields_.size() == toTuple->fields_.size()) {
        for (size_t i = 0; i < fromTuple->fields_.size(); ++i) {
            if (!isAssignable(fromTuple->fields_[i], toTuple->fields_[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

FuncInfo* TypeChecker::tryResolveOverload(const std::string& name,
                                          const std::vector<Type*>& argTypes) {
    auto it = functions_.find(name);
    if (it == functions_.end()) return nullptr;

    auto& overloads = it->second;

    // Filter by arity, skipping template entries
    std::vector<FuncInfo*> candidates;
    for (auto& fi : overloads) {
        if (fi.isTemplate) continue;
        if (fi.paramTypes.size() == argTypes.size()) {
            candidates.push_back(&fi);
        }
    }

    // Try non-variadic candidates first (exact match preferred)
    if (!candidates.empty()) {
        // Try exact match
        for (auto* fi : candidates) {
            bool match = true;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                if (fi->paramTypes[i] != argTypes[i]) { match = false; break; }
            }
            if (match) return fi;
        }

        // Try with numeric promotions
        std::vector<FuncInfo*> compatible;
        for (auto* fi : candidates) {
            bool match = true;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                if (!isAssignable(argTypes[i], fi->paramTypes[i])) { match = false; break; }
            }
            if (match) compatible.push_back(fi);
        }
        if (compatible.size() >= 1) return compatible[0];
    }

    // Try variadic candidates: accept if argTypes.size() >= fixedParamCount
    for (auto& fi : overloads) {
        if (fi.isTemplate || !fi.isVariadic || fi.fixedParamCount < 0) continue;
        if ((int)argTypes.size() < fi.fixedParamCount) continue;

        // Check fixed params match
        bool match = true;
        for (int i = 0; i < fi.fixedParamCount; ++i) {
            if (!isAssignable(argTypes[i], fi.paramTypes[i])) { match = false; break; }
        }
        if (!match) continue;

        // For typed variadic: last paramType is [ElemType], verify excess args
        auto* arrType = dynamic_cast<ArrayType*>(fi.paramTypes.back());
        if (arrType) {
            for (size_t i = fi.fixedParamCount; i < argTypes.size(); ++i) {
                if (!isAssignable(argTypes[i], arrType->elemType_)) { match = false; break; }
            }
        }
        if (match) return &fi;
    }

    return nullptr;
}

FuncInfo* TypeChecker::resolveOverload(const std::string& name,
                                        const std::vector<Type*>& argTypes,
                                        SourceRange loc) {
    auto it = functions_.find(name);
    if (it == functions_.end()) {
        std::vector<std::string> candidates;
        for (auto& [fname, _] : functions_) candidates.push_back(fname);
        std::string msg = "Undeclared function '" + name + "'";
        std::string match = findClosestMatch(name, candidates);
        // Also check module function exports, matching against bare name
        for (auto& [alias, mod] : importedModules_) {
            std::vector<std::string> exportNames;
            for (auto& [exportName, entry] : mod->exports) {
                if (entry.kind == ExportEntry::Func) {
                    exportNames.push_back(exportName);
                }
            }
            std::string modMatch = findClosestMatch(name, exportNames);
            if (!modMatch.empty()) {
                int modDist = editDistance(name, modMatch);
                int curDist = match.empty() ? INT_MAX : editDistance(name, match);
                if (modDist < curDist) {
                    match = alias + "." + modMatch;
                }
            }
        }
        if (!match.empty()) {
            msg += "\nDid you mean '" + match + "'?";
        }
        error(loc, msg);
        return nullptr;
    }

    auto& overloads = it->second;

    // 1. Filter by arity, skipping template entries
    std::vector<FuncInfo*> candidates;
    for (auto& fi : overloads) {
        if (fi.isTemplate) continue;
        if (fi.paramTypes.size() == argTypes.size()) {
            candidates.push_back(&fi);
        }
    }

    // 2. Try exact match (non-variadic)
    if (!candidates.empty()) {
        std::vector<FuncInfo*> exact;
        for (auto* fi : candidates) {
            bool match = true;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                if (fi->paramTypes[i] != argTypes[i]) { match = false; break; }
            }
            if (match) exact.push_back(fi);
        }
        if (exact.size() == 1) {
            checkRTSafety(exact[0], name, loc);
            return exact[0];
        }

        // 3. Try with numeric promotions (non-variadic)
        std::vector<FuncInfo*> compatible;
        for (auto* fi : candidates) {
            bool match = true;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                if (!isAssignable(argTypes[i], fi->paramTypes[i])) { match = false; break; }
            }
            if (match) compatible.push_back(fi);
        }
        if (compatible.size() == 1) {
            checkRTSafety(compatible[0], name, loc);
            return compatible[0];
        }
        if (compatible.size() > 1) {
            error(loc, "Ambiguous overload for '" + name + "'");
            return compatible[0];
        }
    }

    // 3b. Try variadic candidates
    for (auto& fi : overloads) {
        if (fi.isTemplate || !fi.isVariadic || fi.fixedParamCount < 0) continue;
        if ((int)argTypes.size() < fi.fixedParamCount) continue;

        bool match = true;
        for (int i = 0; i < fi.fixedParamCount; ++i) {
            if (!isAssignable(argTypes[i], fi.paramTypes[i])) { match = false; break; }
        }
        if (!match) continue;

        auto* arrType = dynamic_cast<ArrayType*>(fi.paramTypes.back());
        if (arrType) {
            for (size_t i = fi.fixedParamCount; i < argTypes.size(); ++i) {
                if (!isAssignable(argTypes[i], arrType->elemType_)) { match = false; break; }
            }
        }
        if (match) {
            checkRTSafety(&fi, name, loc);
            return &fi;
        }
    }

    // 4. No match — build informative error message
    std::string msg = "No matching overload for '" + name + "'\n  Supplied types: (";
    for (size_t i = 0; i < argTypes.size(); ++i) {
        if (i > 0) msg += ", ";
        msg += argTypes[i] ? argTypes[i]->str() : "?";
    }
    msg += ")\n  Available overloads:";
    for (auto& fi : overloads) {
        if (fi.isTemplate) {
            if (fi.builtinTemplate) {
                msg += "\n    " + name + "(<builtin template>)";
            } else if (fi.declNode) {
                msg += "\n    " + name + "<";
                for (size_t i = 0; i < fi.typeParams.size(); ++i) {
                    if (i > 0) msg += ",";
                    msg += fi.typeParams[i];
                }
                msg += ">(";
                for (size_t i = 0; i < fi.declNode->params.size(); ++i) {
                    if (i > 0) msg += ", ";
                    msg += fi.declNode->params[i].name;
                    if (fi.declNode->params[i].typeExpr)
                        msg += " : <type>";
                }
                msg += ")";
            }
        } else {
            msg += "\n    " + name + "(";
            for (size_t i = 0; i < fi.paramTypes.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += fi.paramTypes[i] ? fi.paramTypes[i]->str() : "?";
            }
            msg += ") -> " + (fi.returnType ? fi.returnType->str() : "?");
        }
    }
    error(loc, msg);
    return nullptr;
}

// --- Template function support ---

bool TypeChecker::unifyTypeExpr(TypeExpr* texpr, Type* concrete,
                                const std::vector<std::string>& typeParams,
                                std::unordered_map<std::string, Type*>& bindings) {
    if (!texpr || !concrete) return false;

    if (texpr->kind == ASTNode::NamedType) {
        auto* named = static_cast<NamedTypeNode*>(texpr);
        // Check if this is a type parameter
        for (auto& tp : typeParams) {
            if (named->name == tp) {
                auto it = bindings.find(tp);
                if (it != bindings.end()) {
                    // Already bound: check consistency
                    return it->second == concrete;
                }
                bindings[tp] = concrete;
                return true;
            }
        }
        // Concrete type name: resolve and compare
        // Save/restore typeParamBindings_ to avoid interference
        auto saved = typeParamBindings_;
        typeParamBindings_ = bindings;
        Type* resolved = resolveTypeExpr(texpr);
        typeParamBindings_ = saved;
        return resolved == concrete;
    }

    if (texpr->kind == ASTNode::ArrayType) {
        auto* arrNode = static_cast<ArrayTypeNode*>(texpr);
        auto* arrType = dynamic_cast<ArrayType*>(concrete);
        if (!arrType) return false;
        return unifyTypeExpr(arrNode->elemType.get(), arrType->elemType_, typeParams, bindings);
    }

    if (texpr->kind == ASTNode::ListType) {
        auto* listNode = static_cast<ListTypeNode*>(texpr);
        auto* listType = dynamic_cast<ListType*>(concrete);
        if (!listType) return false;
        return unifyTypeExpr(listNode->elemType.get(), listType->elemType_, typeParams, bindings);
    }

    if (texpr->kind == ASTNode::RefType) {
        auto* refNode = static_cast<RefTypeNode*>(texpr);
        auto* refType = dynamic_cast<RefType*>(concrete);
        if (!refType) return false;
        return unifyTypeExpr(refNode->elemType.get(), refType->elemType_, typeParams, bindings);
    }

    if (texpr->kind == ASTNode::TupleType) {
        auto* tupleNode = static_cast<TupleTypeNode*>(texpr);
        auto* tupleType = dynamic_cast<TupleType*>(concrete);
        if (!tupleType) return false;
        if (tupleNode->elemTypes.size() != tupleType->fields_.size()) return false;
        for (size_t i = 0; i < tupleNode->elemTypes.size(); ++i) {
            if (!unifyTypeExpr(tupleNode->elemTypes[i].get(), tupleType->fields_[i], typeParams, bindings))
                return false;
        }
        return true;
    }

    if (texpr->kind == ASTNode::FunctionType) {
        auto* fnNode = static_cast<FunctionTypeNode*>(texpr);
        auto* fnType = dynamic_cast<FunctionType*>(concrete);
        if (!fnType) return false;
        if (fnNode->paramTypes.size() != fnType->argTypes_.size()) return false;
        for (size_t i = 0; i < fnNode->paramTypes.size(); ++i) {
            if (!unifyTypeExpr(fnNode->paramTypes[i].get(), fnType->argTypes_[i], typeParams, bindings))
                return false;
        }
        return unifyTypeExpr(fnNode->returnType.get(), fnType->returnType_, typeParams, bindings);
    }

    if (texpr->kind == ASTNode::TemplateType) {
        auto* tmplNode = static_cast<TemplateTypeNode*>(texpr);

        // Built-in parameterized type: Coroutine<T>
        if (tmplNode->name == "Coroutine" && tmplNode->typeArgs.size() == 1) {
            auto* coroType = dynamic_cast<CoroutineType*>(concrete);
            if (!coroType) return false;
            return unifyTypeExpr(tmplNode->typeArgs[0].get(), coroType->yieldType_, typeParams, bindings);
        }

        // Built-in parameterized type: Future<T> (deduce T from Future<T> args,
        // e.g. gather<T>(fs [Future<T>])).
        if (tmplNode->name == "Future" && tmplNode->typeArgs.size() == 1) {
            auto* futType = dynamic_cast<FutureType*>(concrete);
            if (!futType) return false;
            return unifyTypeExpr(tmplNode->typeArgs[0].get(), futType->valueType_, typeParams, bindings);
        }

        // Check if this is a template type alias (e.g. Wrapper<T> where type Wrapper<X> = Box<X>)
        auto taIt = templateTypeAliases_.find(tmplNode->name);
        if (taIt != templateTypeAliases_.end()) {
            TypeAliasDeclNode* aliasDecl = taIt->second;
            if (tmplNode->typeArgs.size() != aliasDecl->typeParams.size()) return false;

            // Step 1: Unify alias body against concrete, with alias type params as unknowns
            std::unordered_map<std::string, Type*> aliasBindings;
            if (!unifyTypeExpr(aliasDecl->typeExpr.get(), concrete, aliasDecl->typeParams, aliasBindings))
                return false;

            // Step 2: Unify each usage type arg against the discovered alias param binding
            for (size_t i = 0; i < aliasDecl->typeParams.size(); ++i) {
                auto bIt = aliasBindings.find(aliasDecl->typeParams[i]);
                if (bIt == aliasBindings.end()) return false;
                if (!unifyTypeExpr(tmplNode->typeArgs[i].get(), bIt->second, typeParams, bindings))
                    return false;
            }
            return true;
        }

        // Try to match against a monomorphized struct or enum type
        // The concrete type's name should start with tmplNode->name + "<"
        auto* structType = dynamic_cast<StructType*>(concrete);
        auto* enumType = dynamic_cast<EnumType*>(concrete);
        std::string concreteName;
        if (structType) concreteName = std::string(structType->name_->str());
        else if (enumType) concreteName = std::string(enumType->name_->str());
        else return false;

        // Check that the base name matches
        if (concreteName.substr(0, tmplNode->name.size()) != tmplNode->name ||
            concreteName.size() <= tmplNode->name.size() ||
            concreteName[tmplNode->name.size()] != '<') {
            return false;
        }

        // Fast path: the monomorphized type records its origin template and
        // type args, so unify directly against those. This is not just an
        // optimization -- the structural fallback below walks every field/case
        // type, which never terminates for a self-recursive template
        // (e.g. enum Music<T> { note T, mseq (Music<T>, Music<T>) }: deducing
        // T from the mseq case requires unifying M<T> against M<Int> again).
        if (auto originIt = monoOrigin_.find(concrete); originIt != monoOrigin_.end()) {
            auto const& origin = originIt->second;
            if (origin.templateName == tmplNode->name &&
                origin.typeArgs.size() == tmplNode->typeArgs.size()) {
                for (size_t i = 0; i < tmplNode->typeArgs.size(); ++i) {
                    if (!unifyTypeExpr(tmplNode->typeArgs[i].get(), origin.typeArgs[i],
                                       typeParams, bindings))
                        return false;
                }
                return true;
            }
        }

        // Structural fallback (e.g. the concrete type was monomorphized by a
        // DIFFERENT TypeChecker -- a module import -- so this checker's
        // monoOrigin_ has no entry). Self-recursive templates make this walk
        // cyclic: deducing T for Music<T> vs Music<Pitch> revisits the same
        // (texpr, concrete) pair through the recursive case. Guard with an
        // in-progress stack and treat re-entry as success (coinductive: the
        // outer frame completes the bindings).
        for (auto const& fr : unifyInProgress_) {
            if (fr.first == texpr && fr.second == concrete) return true;
        }
        unifyInProgress_.push_back({texpr, concrete});
        struct UnifyGuard {
            std::vector<std::pair<TypeExpr*, Type*>>& stack;
            ~UnifyGuard() { stack.pop_back(); }
        } unifyGuard{unifyInProgress_};

        // Get the type args from the template declaration and the concrete type
        // We need to look up the template declaration to get the field/case types
        if (structType) {
            auto tmplIt = templateStructs_.find(tmplNode->name);
            if (tmplIt == templateStructs_.end()) return false;
            StructDeclNode* decl = tmplIt->second;
            if (tmplNode->typeArgs.size() != decl->typeParams.size()) return false;

            // We need to extract what type args were used for the concrete type.
            // We do this by matching the template's field type exprs against the concrete fields.
            // But a simpler approach: unify each template type arg against the
            // corresponding concrete type inferred from the monomorphized struct.
            // The monomorphized struct's fields have the concrete types.
            // Build a mapping from the template's type params to concrete types.
            std::unordered_map<std::string, Type*> innerBindings;
            for (auto& field : decl->fields) {
                // Find matching field in concrete type
                for (size_t i = 0; i < structType->fields_.size(); ++i) {
                    if (structType->fields_[i].name->str() == field.name) {
                        unifyTypeExpr(field.typeExpr.get(), structType->fields_[i].type,
                                      decl->typeParams, innerBindings);
                        break;
                    }
                }
            }

            // Now unify each type arg from the TemplateTypeNode against the resolved inner bindings
            for (size_t i = 0; i < tmplNode->typeArgs.size(); ++i) {
                auto bIt = innerBindings.find(decl->typeParams[i]);
                if (bIt == innerBindings.end()) return false;
                if (!unifyTypeExpr(tmplNode->typeArgs[i].get(), bIt->second, typeParams, bindings))
                    return false;
            }
            return true;
        }

        if (enumType) {
            auto tmplIt = templateEnums_.find(tmplNode->name);
            if (tmplIt == templateEnums_.end()) return false;
            UnionDeclNode* decl = tmplIt->second;
            if (tmplNode->typeArgs.size() != decl->typeParams.size()) return false;

            std::unordered_map<std::string, Type*> innerBindings;
            for (auto& ucase : decl->cases) {
                if (!ucase.typeExpr) continue;
                for (size_t i = 0; i < enumType->cases_.size(); ++i) {
                    if (enumType->cases_[i].name->str() == ucase.name) {
                        unifyTypeExpr(ucase.typeExpr.get(), enumType->cases_[i].type,
                                      decl->typeParams, innerBindings);
                        break;
                    }
                }
            }

            for (size_t i = 0; i < tmplNode->typeArgs.size(); ++i) {
                auto bIt = innerBindings.find(decl->typeParams[i]);
                if (bIt == innerBindings.end()) return false;
                if (!unifyTypeExpr(tmplNode->typeArgs[i].get(), bIt->second, typeParams, bindings))
                    return false;
            }
            return true;
        }

        return false;
    }

    return false;
}

bool TypeChecker::inferTypeParams(const std::vector<std::string>& typeParams,
                                  const std::vector<FnParam>& params,
                                  const std::vector<Type*>& argTypes,
                                  std::unordered_map<std::string, Type*>& bindings) {
    // Allow fewer args than params when defaults cover the difference
    if (argTypes.size() > params.size()) return false;
    if (argTypes.size() < params.size()) {
        // Verify that all missing params have default expressions
        for (size_t i = argTypes.size(); i < params.size(); ++i) {
            if (!params[i].defaultExpr) return false;
        }
    }

    // Only unify the supplied arguments (skip params with no typeExpr —
    // they have no type param to bind, e.g. untyped "rate = Rate.audio")
    for (size_t i = 0; i < argTypes.size(); ++i) {
        if (!params[i].typeExpr) continue;
        if (!unifyTypeExpr(params[i].typeExpr.get(), argTypes[i], typeParams, bindings)) {
            return false;
        }
    }

    // For unsupplied params with defaults, infer type from default expression
    // and unify to bind any remaining type params (e.g. pm AsSignal = 0)
    for (size_t i = argTypes.size(); i < params.size(); ++i) {
        if (params[i].defaultExpr && params[i].typeExpr) {
            Type* defType = inferExpr(static_cast<Expr*>(params[i].defaultExpr.get()));
            if (!defType || !unifyTypeExpr(params[i].typeExpr.get(), defType, typeParams, bindings)) {
                return false;
            }
        }
    }

    // Verify all type params are bound
    for (auto& tp : typeParams) {
        if (bindings.find(tp) == bindings.end()) return false;
    }
    return true;
}

FuncInfo* TypeChecker::tryResolveTemplate(const std::string& name,
                                          const std::vector<Type*>& argTypes,
                                          CallExpr_* callExpr) {
    auto it = functions_.find(name);
    if (it == functions_.end()) return nullptr;

    struct Match {
        FuncInfo* fi;
        std::unordered_map<std::string, Type*> bindings;
        std::vector<Type*> typeArgs;
        std::vector<Type*> inferArgTypes;  // for variadic: includes packed TupleType
    };
    std::vector<Match> matches;

    for (auto& fi : it->second) {
        if (!fi.isTemplate) continue;

        // Built-in template: use the resolver callback (no AST node needed)
        if (fi.builtinTemplate || fi.builtinTemplateEx) {
            std::vector<Type*> paramTypes;
            Type* retType = nullptr;
            CFun cfun = nullptr;
            bool resolved;
            // Ex monos instantiated with explicit type args live in a
            // MANGLED bucket ("deserialize<Int>"): the same paramTypes can
            // monomorphize differently per type-arg list, and plain calls
            // must never resolve to a type-arg'd instantiation by accident.
            std::string monoBucket = name;
            if (fi.builtinTemplateEx) {
                static const std::vector<Type*> kNoTypeArgs;
                auto const& tArgs = callExpr ? callExpr->resolvedTypeArgs : kNoTypeArgs;
                resolved = fi.builtinTemplateEx(compiler_, argTypes, tArgs,
                                                paramTypes, retType, cfun);
                if (resolved && !tArgs.empty()) {
                    callExpr->typeArgsUsed = true;
                    monoBucket += "<";
                    for (size_t i = 0; i < tArgs.size(); ++i) {
                        if (i > 0) monoBucket += ",";
                        VMString ts = tArgs[i]->str();
                        monoBucket.append(ts.data(), ts.size());
                    }
                    monoBucket += ">";
                }
            } else {
                resolved = fi.builtinTemplate(compiler_, argTypes, paramTypes, retType, cfun);
            }
            if (resolved) {
                // Detect variadic builtin: paramTypes differs from argTypes due to packing.
                // Cases: (1) resolver returns fewer params (3 args → 2 params),
                //        (2) resolver returns more params (1 arg → 2 params, zero variadic),
                //        (3) same count but last param is a packed TupleType
                bool builtinVariadic = paramTypes.size() != argTypes.size();
                if (!builtinVariadic && !paramTypes.empty()) {
                    auto* lastParamTuple = dynamic_cast<TupleType*>(paramTypes.back());
                    if (lastParamTuple && argTypes.back() != lastParamTuple) {
                        builtinVariadic = true;
                    }
                }

                // Check if a concrete overload already exists (serves as cache)
                // Use exact-match by paramTypes (not argTypes with promotion) so that
                // builtins like println that preserve exact arg types don't get
                // incorrectly matched via numeric promotion (e.g. println(Int) → println(Float))
                if (!builtinVariadic) {
                    auto bucketIt = functions_.find(monoBucket);
                    if (bucketIt != functions_.end()) {
                        for (auto& fi2 : bucketIt->second) {
                            if (fi2.isTemplate || fi2.paramTypes.size() != paramTypes.size()) continue;
                            bool match = true;
                            for (size_t j = 0; j < paramTypes.size(); ++j) {
                                if (fi2.paramTypes[j] != paramTypes[j]) { match = false; break; }
                            }
                            if (match) {
                                if (callExpr) {
                                    callExpr->resolvedFuncGlobalIndex = (i32)fi2.globalIndex;
                                    callExpr->isBuiltinCall = fi2.isBuiltin; callExpr->builtinAcceptsInlineArgs = fi2.acceptsInlineArgs;
                                }
                                return &fi2;
                            }
                        }
                    }
                }

                // For variadic builtins, check cache by paramTypes (the packed signature)
                if (builtinVariadic) {
                    if (auto* existing = tryResolveOverload(monoBucket, paramTypes)) {
                        if (callExpr) {
                            callExpr->resolvedFuncGlobalIndex = (i32)existing->globalIndex;
                            callExpr->isBuiltinCall = existing->isBuiltin; callExpr->builtinAcceptsInlineArgs = existing->acceptsInlineArgs;
                            // Set variadic packing info
                            int fixedCount = (int)paramTypes.size() - 1;
                            callExpr->variadicPackStart = fixedCount;
                            callExpr->variadicPackType = paramTypes.back();
                        }
                        return existing;
                    }
                }

                // Create monomorphized FuncInfo
                u32 globalIdx = compiler_.addCodeGlobal();
                // Store a TupleType of paramTypes in the Primitive's type_
                // so builtins like print/println can access arg types at runtime.
                // Explicit type args are appended AFTER the params (Ex
                // builtins read them back at fields_[paramCount + i]).
                TypeVec primFields(rt::STLAllocator<Type*>(nullptr));
                for (Type* pt2 : paramTypes) primFields.push_back(pt2);
                if (fi.builtinTemplateEx && callExpr) {
                    for (Type* ta : callExpr->resolvedTypeArgs) primFields.push_back(ta);
                }
                auto* primTupleType = compiler_.tupleType(primFields);
                auto* prim = new Primitive(primTupleType);
                prim->cfun_ = cfun;
                prim->rtSafe_ = fi.rtSafe;
                prim->rtOnly_ = fi.rtOnly;
                compiler_.global(globalIdx).o = prim;

                auto monoPtr = std::make_unique<FuncInfo>();
                monoPtr->returnType = retType;
                monoPtr->paramTypes = paramTypes;
                monoPtr->globalIndex = globalIdx;
                monoPtr->isBuiltin = true;
                monoPtr->bodyChecked = true;
                monoPtr->rtSafe = fi.rtSafe;
                monoPtr->rtOnly = fi.rtOnly;
                monoPtr->acceptsInlineArgs = fi.acceptsInlineArgs;
                monoPtr->builtinVariadicPacked = builtinVariadic;

                FuncInfo* result = monoPtr.get();
                monoStorage_.push_back(std::move(monoPtr));
                functions_[monoBucket].push_back(*result);

                if (callExpr) {
                    callExpr->resolvedFuncGlobalIndex = (i32)globalIdx;
                    callExpr->isBuiltinCall = true;
                    callExpr->builtinAcceptsInlineArgs = fi.acceptsInlineArgs;
                    // Set variadic packing info for builtins that pack
                    if (builtinVariadic) {
                        int fixedCount = (int)paramTypes.size() - 1;
                        callExpr->variadicPackStart = fixedCount;
                        callExpr->variadicPackType = paramTypes.back();
                    }
                }
                return result;
            }
            continue;
        }

        if (!fi.declNode) continue;

        // Arity check: allow variadic templates to accept >= fixedParamCount args,
        // and allow fewer args when default arguments cover the difference
        if (fi.isVariadic && fi.fixedParamCount >= 0) {
            if ((int)argTypes.size() < fi.fixedParamCount) continue;
        } else {
            int numParams = (int)fi.declNode->params.size();
            int minRequired = (fi.minArity >= 0) ? fi.minArity : numParams;
            if ((int)argTypes.size() < minRequired || (int)argTypes.size() > numParams) continue;
        }

        // For variadic templates, build a synthetic argTypes list for type inference
        // by constructing a TupleType from excess args (including single-arg case)
        std::vector<Type*> inferArgTypes;
        if (fi.isVariadic && fi.fixedParamCount >= 0) {
            // Fixed args as-is
            for (int i = 0; i < fi.fixedParamCount; ++i) {
                inferArgTypes.push_back(argTypes[i]);
            }
            // Build TupleType from excess args for the variadic param
            TypeVec fields(rt::STLAllocator<Type*>(nullptr));
            for (size_t i = fi.fixedParamCount; i < argTypes.size(); ++i) {
                fields.push_back(argTypes[i]);
            }
            auto* tt = compiler_.tupleType(fields);
            inferArgTypes.push_back(tt);
        } else {
            inferArgTypes = argTypes;
        }

        std::unordered_map<std::string, Type*> bindings;
        // For variadic templates with untyped variadic param, we can't use
        // inferTypeParams directly since the variadic param has no typeExpr.
        // Instead, manually unify the fixed params and bind the variadic type.
        bool inferred = false;
        if (fi.isVariadic && fi.fixedParamCount >= 0 &&
            !fi.declNode->params.back().typeExpr) {
            // Unify fixed params
            bool ok = true;
            for (int i = 0; i < fi.fixedParamCount && i < (int)fi.declNode->params.size() - 1; ++i) {
                if (!unifyTypeExpr(fi.declNode->params[i].typeExpr.get(), inferArgTypes[i],
                                   fi.typeParams, bindings)) {
                    ok = false; break;
                }
            }
            if (ok) {
                // All type params from fixed args should be bound now
                // Verify they are all bound
                bool allBound = true;
                for (auto& tp : fi.typeParams) {
                    if (bindings.find(tp) == bindings.end()) { allBound = false; break; }
                }
                inferred = allBound || fi.typeParams.empty();
            }
        } else {
            inferred = inferTypeParams(fi.typeParams, fi.declNode->params, inferArgTypes, bindings);
        }

        if (inferred) {
            // Build typeArgs vector in declaration order
            std::vector<Type*> typeArgs;
            for (auto& tp : fi.typeParams) {
                typeArgs.push_back(bindings[tp]);
            }
            // For variadic templates, include the variadic tuple type in the key
            // so different call-site signatures get different monomorphizations
            if (fi.isVariadic && fi.fixedParamCount >= 0 &&
                inferArgTypes.size() == (size_t)fi.fixedParamCount + 1) {
                typeArgs.push_back(inferArgTypes.back());  // the TupleType
            }

            // Check monomorphization cache (include declNode to disambiguate overloads)
            MonoKey key{name, typeArgs, (void*)fi.declNode};
            auto cit = monoCache_.find(key);
            if (cit != monoCache_.end()) {
                if (callExpr) {
                    callExpr->resolvedFuncGlobalIndex = (i32)cit->second->globalIndex;
                    callExpr->isBuiltinCall = false;
                    // Set variadic packing info
                    if (fi.isVariadic && fi.fixedParamCount >= 0) {
                        callExpr->variadicPackStart = fi.fixedParamCount;
                        callExpr->variadicPackType = cit->second->paramTypes.back();
                    }
                }
                return cit->second;
            }

            // Check where constraints — skip this template if constraint fails
            if (fi.declNode && !fi.declNode->whereConstraints.empty()) {
                if (!checkWhereConstraints(fi.declNode->whereConstraints, bindings, name, false)) {
                    continue;  // constraint not satisfied, skip this template
                }
            }

            Match m;
            m.fi = &fi;
            m.bindings = std::move(bindings);
            m.typeArgs = std::move(typeArgs);
            m.inferArgTypes = inferArgTypes;
            matches.push_back(std::move(m));
        }
    }

    if (matches.empty()) return nullptr;

    // Prefer the template with fewer type parameters (more specific)
    Match* best = &matches[0];
    for (size_t i = 1; i < matches.size(); ++i) {
        if (matches[i].fi->typeParams.size() < best->fi->typeParams.size()) {
            best = &matches[i];
        }
    }

    // Copy the FuncInfo fields we need BEFORE calling monomorphize, because
    // monomorphize may push_back into functions_[name], reallocating the
    // vector and invalidating the pointer best->fi.
    FuncInfo bestFI = *best->fi;

    FuncInfo* result = monomorphize(bestFI, best->bindings, best->typeArgs, callExpr);
    if (result && bestFI.isVariadic && bestFI.fixedParamCount >= 0 && callExpr) {
        callExpr->variadicPackStart = bestFI.fixedParamCount;
        callExpr->variadicPackType = result->paramTypes.back();
    }
    return result;
}

FuncInfo* TypeChecker::tryResolveModuleTemplate(
    const std::string& name,
    const std::deque<FuncInfo>& overloads,
    const std::vector<Type*>& argTypes,
    CallExpr_* callExpr) {

    struct Match {
        FuncInfo* fi;
        std::unordered_map<std::string, Type*> bindings;
        std::vector<Type*> typeArgs;
    };
    std::vector<Match> matches;

    for (auto& fi : const_cast<std::deque<FuncInfo>&>(overloads)) {
        if (!fi.isTemplate) continue;

        // Built-in template: use the resolver callback
        if (fi.builtinTemplate) {
            std::vector<Type*> paramTypes;
            Type* retType = nullptr;
            CFun cfun = nullptr;
            if (fi.builtinTemplate(compiler_, argTypes, paramTypes, retType, cfun)) {
                u32 globalIdx = compiler_.addCodeGlobal();
                auto* prim = new Primitive(compiler_.voidType());
                prim->cfun_ = cfun;
                prim->rtSafe_ = fi.rtSafe;
                prim->rtOnly_ = fi.rtOnly;
                compiler_.global(globalIdx).o = prim;

                auto monoPtr = std::make_unique<FuncInfo>();
                monoPtr->returnType = retType;
                monoPtr->paramTypes = paramTypes;
                monoPtr->globalIndex = globalIdx;
                monoPtr->isBuiltin = true;
                monoPtr->bodyChecked = true;
                monoPtr->rtSafe = fi.rtSafe;
                monoPtr->rtOnly = fi.rtOnly;
                monoPtr->acceptsInlineArgs = fi.acceptsInlineArgs;

                FuncInfo* result = monoPtr.get();
                monoStorage_.push_back(std::move(monoPtr));

                callExpr->resolvedFuncGlobalIndex = (i32)globalIdx;
                callExpr->isBuiltinCall = true;
                callExpr->builtinAcceptsInlineArgs = fi.acceptsInlineArgs;
                return result;
            }
            continue;
        }

        if (!fi.declNode) continue;
        {
            int numParams = (int)fi.declNode->params.size();
            int minRequired = (fi.minArity >= 0) ? fi.minArity : numParams;
            if ((int)argTypes.size() < minRequired || (int)argTypes.size() > numParams) continue;
        }

        std::unordered_map<std::string, Type*> bindings;
        if (inferTypeParams(fi.typeParams, fi.declNode->params, argTypes, bindings)) {
            std::vector<Type*> typeArgs;
            for (auto& tp : fi.typeParams) {
                typeArgs.push_back(bindings[tp]);
            }

            // Check monomorphization cache (include declNode to disambiguate overloads)
            MonoKey key{name, typeArgs, (void*)fi.declNode};
            auto cit = monoCache_.find(key);
            if (cit != monoCache_.end()) {
                callExpr->resolvedFuncGlobalIndex = (i32)cit->second->globalIndex;
                callExpr->isBuiltinCall = false;
                return cit->second;
            }

            // Check where constraints — skip this template if constraint fails
            if (fi.declNode && !fi.declNode->whereConstraints.empty()) {
                if (!checkWhereConstraints(fi.declNode->whereConstraints, bindings, name, false)) {
                    continue;
                }
            }

            matches.push_back({&fi, std::move(bindings), std::move(typeArgs)});
        }
    }

    if (matches.empty()) return nullptr;

    // Prefer the template with fewer type parameters (more specific)
    Match* best = &matches[0];
    for (size_t i = 1; i < matches.size(); ++i) {
        if (matches[i].fi->typeParams.size() < best->fi->typeParams.size()) {
            best = &matches[i];
        }
    }

    // Copy before monomorphize -- it may push_back into the overloads
    // vector, invalidating best->fi.
    FuncInfo bestFI = *best->fi;
    return monomorphize(bestFI, best->bindings, best->typeArgs, callExpr);
}

FuncInfo* TypeChecker::monomorphize(FuncInfo& templateFI,
                                    const std::unordered_map<std::string, Type*>& bindings,
                                    const std::vector<Type*>& typeArgs,
                                    CallExpr_* callExpr) {
    FnDeclNode* decl = templateFI.declNode;

    // For imported templates, temporarily merge the source module's scope
    std::unique_ptr<ImportedModuleScopeGuard> scopeGuard;
    if (templateFI.sourceModule) {
        scopeGuard = std::make_unique<ImportedModuleScopeGuard>(*this, templateFI.sourceModule);
    }

    // Save typeParamBindings_
    auto savedBindings = typeParamBindings_;
    typeParamBindings_ = bindings;

    // Resolve param types using bindings, with preceding params in scope for defaults
    // Check if scoping is needed (non-literal default without type annotation)
    bool needsParamScope = false;
    for (auto& param : decl->params) {
        if (param.defaultExpr && !param.typeExpr) {
            auto* expr = param.defaultExpr.get();
            switch (expr->kind) {
                case ASTNode::IntLiteral: case ASTNode::FloatLiteral:
                case ASTNode::StringLiteral: case ASTNode::BoolLiteral:
                case ASTNode::SymbolLiteral: case ASTNode::FractionLiteral:
                    break;
                default:
                    needsParamScope = true;
                    break;
            }
        }
        if (needsParamScope) break;
    }
    if (needsParamScope) pushScope();
    std::vector<Type*> paramTypes;
    for (auto& param : decl->params) {
        Type* t;
        if (param.isVariadic && !param.typeExpr) {
            // Untyped variadic: the type is the last element of typeArgs (TupleType)
            // Find it — it's the extra entry beyond the regular type params
            if (typeArgs.size() > templateFI.typeParams.size()) {
                t = typeArgs.back();
            } else {
                // Fallback: empty tuple
                TypeVec emptyFields(rt::STLAllocator<Type*>(nullptr));
                t = compiler_.tupleType(emptyFields);
            }
        } else {
            t = resolveParamType(param);
        }
        paramTypes.push_back(t);
        if (needsParamScope && t) {
            declareVar(param.name, t, false);
        }
    }
    if (needsParamScope) popScope();

    // Resolve return type (omitted = infer from body)
    Type* retType;
    if (!decl->returnType) {
        retType = nullptr;  // infer from body
    } else {
        retType = resolveTypeExpr(decl->returnType.get());
    }

    // If this is a coroutine, the declared return type is the yield type
    // and the function's external type is Coroutine<T>
    if (decl->isCoroutine && retType) {
        retType = compiler_.coroutineType(retType);
    }
    if (decl->isAsync && retType) {
        retType = compiler_.futureType(retType);
    }

    // A monomorphized user function's global holds its CodeBlock* (set by
    // genFnDecl). Code globals live in the immutable image and are never GC
    // roots, so no isObj flag is involved.
    u32 globalIdx = compiler_.addCodeGlobal();

    // Create monomorphized FuncInfo (heap-allocated for pointer stability)
    auto monoPtr = std::make_unique<FuncInfo>();
    monoPtr->returnType = retType;
    monoPtr->paramTypes = paramTypes;
    monoPtr->globalIndex = globalIdx;
    monoPtr->isTemplate = false;
    monoPtr->isBuiltin = false;
    monoPtr->isAsync = templateFI.isAsync;  // a monomorphized async fn is still async
    monoPtr->declNode = decl;
    monoPtr->monoBindings = bindings;
    monoPtr->isVariadic = templateFI.isVariadic;
    monoPtr->fixedParamCount = templateFI.fixedParamCount;
    monoPtr->numDefaults = templateFI.numDefaults;
    monoPtr->minArity = templateFI.minArity;
    monoPtr->sourceModule = templateFI.sourceModule;  // propagate for codegen recheck

    FuncInfo* fi = monoPtr.get();
    monoStorage_.push_back(std::move(monoPtr));

    // Add to overload set so codegen can find it by globalIndex
    functions_[decl->name].push_back(*fi);

    // Cache and track (using stable pointer from monoStorage_)
    // Include declNode to disambiguate overloads with same name+typeArgs
    MonoKey key{decl->name, typeArgs, (void*)decl};
    monoCache_[key] = fi;
    monoInstances_.push_back(fi);

    // Type-check the body with bindings active
    if (retType == nullptr) {
        // Infer return type
        inferFunctionReturnType(decl, fi);
    } else {
        // Check body with known return type
        pushScope();
        for (size_t i = 0; i < decl->params.size(); ++i) {
            declareVar(decl->params[i].name, paramTypes[i], false);
        }
        Type* savedReturnType = currentReturnType_;
        bool savedInCoro = inCoroutineBody_;
        Type* savedYieldType = currentYieldType_;
        bool savedInAsync = inAsyncBody_;
        Type* savedAsyncValueType = currentAsyncValueType_;

        if (decl->isCoroutine) {
            auto* coroType = dynamic_cast<CoroutineType*>(retType);
            if (coroType) {
                inCoroutineBody_ = true;
                currentYieldType_ = coroType->yieldType_;
                currentReturnType_ = compiler_.voidType();
            }
        } else if (decl->isAsync) {
            auto* ft = dynamic_cast<FutureType*>(retType);
            if (ft) {
                inAsyncBody_ = true;
                currentAsyncValueType_ = ft->valueType_;
                currentReturnType_ = ft->valueType_;
            }
        } else {
            currentReturnType_ = retType;
        }

        checkNode(decl->body.get());
        currentReturnType_ = savedReturnType;
        inCoroutineBody_ = savedInCoro;
        currentYieldType_ = savedYieldType;
        inAsyncBody_ = savedInAsync;
        currentAsyncValueType_ = savedAsyncValueType;
        popScope();
        fi->bodyChecked = true;
    }

    // Restore bindings
    typeParamBindings_ = savedBindings;

    // Set callExpr fields
    if (callExpr) {
        callExpr->resolvedFuncGlobalIndex = (i32)globalIdx;
        callExpr->isBuiltinCall = false;
    }

    return fi;
}

void TypeChecker::recheckTemplateBody(FnDeclNode* decl, FuncInfo* fi,
                                      const std::unordered_map<std::string, Type*>& bindings) {
    // For imported templates, temporarily merge the source module's scope
    std::unique_ptr<ImportedModuleScopeGuard> scopeGuard;
    if (fi->sourceModule) {
        scopeGuard = std::make_unique<ImportedModuleScopeGuard>(*this, fi->sourceModule);
    }

    auto savedBindings = typeParamBindings_;
    typeParamBindings_ = bindings;

    pushScope();
    for (size_t i = 0; i < decl->params.size(); ++i) {
        if (decl->params[i].defaultExpr) {
            inferExpr(static_cast<Expr*>(decl->params[i].defaultExpr.get()));
        }
        declareVar(decl->params[i].name, fi->paramTypes[i], false);
    }

    Type* savedReturnType = currentReturnType_;
    bool savedInferring = inferringReturnType_;
    Type* savedInferred = inferredReturnType_;

    currentReturnType_ = fi->returnType;
    inferringReturnType_ = false;
    inferredReturnType_ = nullptr;

    checkNode(decl->body.get());

    currentReturnType_ = savedReturnType;
    inferringReturnType_ = savedInferring;
    inferredReturnType_ = savedInferred;
    popScope();

    typeParamBindings_ = savedBindings;
}

void TypeChecker::recheckTemplateLambdaBody(LambdaExprNode* expr, LambdaType* lambdaType,
                                             TemplateLambdaType* tmplType) {
    // Rebuild type param bindings from the mono cache entry
    std::unordered_map<std::string, Type*> bindings;
    for (size_t i = 0; i < tmplType->typeParams_.size(); ++i) {
        // Find matching mono entry to get type args
        for (auto& entry : tmplType->monoCache_) {
            if (entry.lambdaType == lambdaType) {
                bindings[tmplType->typeParams_[i]] = entry.typeArgs[i];
                break;
            }
        }
    }

    auto savedBindings = typeParamBindings_;
    typeParamBindings_ = bindings;

    // Push scope with lambda boundary
    int savedBoundary = lambdaBoundary_;
    auto* savedCaptures = currentCaptures_;
    Type* savedReturnType = currentReturnType_;
    bool savedInferring = inferringReturnType_;
    Type* savedInferred = inferredReturnType_;

    std::vector<LambdaExprNode::CapturedVar> tmpCaptures = expr->captures;
    currentCaptures_ = &tmpCaptures;
    currentReturnType_ = lambdaType->returnType_;
    inferringReturnType_ = false;
    inferredReturnType_ = nullptr;

    pushScope();
    lambdaBoundary_ = (int)scopes_.size() - 1;

    // Declare params with concrete types
    for (size_t i = 0; i < expr->params.size(); ++i) {
        declareVar(expr->params[i].name, lambdaType->argTypes_[i], false);
    }

    // Declare captured variables so they're accessible during re-type-checking
    for (auto& cap : expr->captures) {
        declareVar(cap.name, cap.type, false);
    }

    // Re-type-check the body
    checkNode(expr->body.get());

    // Restore state
    popScope();
    lambdaBoundary_ = savedBoundary;
    currentCaptures_ = savedCaptures;
    currentReturnType_ = savedReturnType;
    inferringReturnType_ = savedInferring;
    inferredReturnType_ = savedInferred;
    typeParamBindings_ = savedBindings;
}

} // namespace ts
