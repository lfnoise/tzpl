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
//  type_checker_calls.cpp
//  lang
//
//  Type checker -- function call inference
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

// Step 1: Post-resolution finalization shared by explicit-@ and non-@ paths.
// Sets resolvedFuncGlobalIndex, isBuiltinCall, coroutine flags, variadic packing,
// checks RT safety, and does demand-driven return type inference.
Type* TypeChecker::finalizeResolvedCall(CallExpr_* expr, FuncInfo* func,
                                         const std::string& name,
                                         const std::vector<Type*>& argTypes) {
    // RT safety check
    checkRTSafety(func, name, expr->loc);

    // Demand-driven inference: if return type not yet resolved, infer it now
    if (func->returnType == nullptr) {
        // If this is a partial-arity entry (from default args), find the canonical
        // full-arity entry -- inferFunctionReturnType needs the full param types.
        FuncInfo* inferTarget = func;
        if (func->canonicalFunc) {
            auto it2 = functions_.find(name);
            if (it2 != functions_.end()) {
                for (auto& fi : it2->second) {
                    if (!fi.canonicalFunc && fi.globalIndex == func->globalIndex) {
                        inferTarget = &fi;
                        break;
                    }
                }
            }
        }
        if (inferTarget->declNode) {
            inferFunctionReturnType(inferTarget->declNode, inferTarget);
            // Propagate inferred return type to the partial-arity entry
            if (func != inferTarget) {
                func->returnType = inferTarget->returnType;
            }
        } else {
            error(expr->loc, "Cannot call function '" + name +
                  "' whose return type has not been inferred yet");
            return compiler_.intType();
        }
    }

    // Store resolved global index on the AST node
    expr->resolvedFuncGlobalIndex = (i32)func->globalIndex;
    expr->isBuiltinCall = func->isBuiltin;

    // Mark coroutine calls
    if (func->returnType && dynamic_cast<CoroutineType*>(func->returnType)) {
        expr->isCoroCall = true;
    }

    // Mark next() on Coroutine as coroutine resume
    if (name == "next" && func->paramTypes.size() == 1 &&
        dynamic_cast<CoroutineType*>(func->paramTypes[0])) {
        expr->isCoroResume = true;
    }

    // Mark yield() as coroutine yield
    if (name == "yield" && func->isBuiltin && func->paramTypes.size() == 1) {
        if (!inCoroutineBody_) {
            error(expr->loc, "'yield' can only be used inside a 'coro fn' body");
        } else if (!argTypes.empty() && currentYieldType_ && !isAssignable(argTypes[0], currentYieldType_)) {
            error(expr->loc, "Yield type mismatch: expected '" +
                  std::string(currentYieldType_->str()) + "', got '" +
                  std::string(argTypes[0]->str()) + "'");
        }
        expr->isCoroYield = true;
    }

    // Mark yieldAll() as coroutine delegation
    if (name == "yieldAll" && func->isBuiltin && func->paramTypes.size() == 1) {
        if (!inCoroutineBody_) {
            error(expr->loc, "'yieldAll' can only be used inside a 'coro fn' body");
        } else if (!argTypes.empty()) {
            auto* ct = dynamic_cast<CoroutineType*>(argTypes[0]);
            if (ct && currentYieldType_ && !isAssignable(ct->yieldType_, currentYieldType_)) {
                error(expr->loc, "yieldAll type mismatch: inner yields '" +
                      std::string(ct->yieldType_->str()) + "' but enclosing yields '" +
                      std::string(currentYieldType_->str()) + "'");
            }
        }
        expr->isCoroYieldAll = true;
    }

    // Set variadic packing info if this is a variadic function
    if (func->isVariadic && func->fixedParamCount >= 0 && expr->variadicPackStart < 0) {
        expr->variadicPackStart = func->fixedParamCount;
        expr->variadicPackType = func->paramTypes.back();
    }

    return func->returnType;
}

// Step 2: Try implicit auto-mapping at increasing Array/List depths.
FuncInfo* TypeChecker::tryImplicitAutoMap(const std::string& name,
                                           const std::vector<Type*>& argTypes,
                                           CallExpr_* expr,
                                           bool& isAutoMapped, bool& hasListArg) {
    // Find the maximum array/list nesting depth across all arguments
    int maxNesting = 0;
    for (size_t i = 0; i < argTypes.size(); ++i) {
        int d = 0;
        Type* t = argTypes[i];
        while (true) {
            if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                d++; t = arrT->elemType_;
            } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                d++; t = listT->elemType_;
            } else break;
        }
        if (d > maxNesting) maxNesting = d;
    }

    FuncInfo* func = nullptr;

    // Try unwrapping all array/list args at depth 1, 2, 3, ... until a match is found
    for (int depth = 1; depth <= maxNesting && !func; ++depth) {
        bool anyUnwrapped = false;
        bool anyList = false;
        std::vector<Type*> unwrappedTypes;
        std::vector<AutoMapArg> autoMap(argTypes.size());

        for (size_t i = 0; i < argTypes.size(); ++i) {
            Type* t = argTypes[i];
            bool canUnwrap = true;
            bool thisList = false;
            for (int d = 0; d < depth; ++d) {
                if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                    t = arrT->elemType_;
                } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                    t = listT->elemType_;
                    thisList = true;
                } else {
                    canUnwrap = false;
                    break;
                }
            }
            bool isComposite = dynamic_cast<ArrayType*>(argTypes[i]) ||
                               dynamic_cast<ListType*>(argTypes[i]);
            if (canUnwrap && isComposite) {
                unwrappedTypes.push_back(t);
                autoMap[i] = AutoMapArg{depth, 0, thisList};
                anyUnwrapped = true;
                if (thisList) anyList = true;
            } else {
                unwrappedTypes.push_back(argTypes[i]);
            }
        }

        if (!anyUnwrapped) break;

        func = tryResolveOverload(name, unwrappedTypes);
        if (func) {
            expr->autoMapArgs = std::move(autoMap);
            isAutoMapped = true;
            hasListArg = anyList;
        }
    }

    // If still no match, try template resolution at the unwrapped types
    if (!func) {
        for (int depth = 1; depth <= maxNesting && !func; ++depth) {
            bool anyUnwrapped = false;
            bool anyList = false;
            std::vector<Type*> unwrappedTypes;
            std::vector<AutoMapArg> autoMap(argTypes.size());

            for (size_t i = 0; i < argTypes.size(); ++i) {
                Type* t = argTypes[i];
                bool canUnwrap = true;
                bool thisList = false;
                for (int d = 0; d < depth; ++d) {
                    if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                        t = arrT->elemType_;
                    } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                        t = listT->elemType_;
                        thisList = true;
                    } else {
                        canUnwrap = false;
                        break;
                    }
                }
                bool isComposite = dynamic_cast<ArrayType*>(argTypes[i]) ||
                                   dynamic_cast<ListType*>(argTypes[i]);
                if (canUnwrap && isComposite) {
                    unwrappedTypes.push_back(t);
                    autoMap[i] = AutoMapArg{depth, 0, thisList};
                    anyUnwrapped = true;
                    if (thisList) anyList = true;
                } else {
                    unwrappedTypes.push_back(argTypes[i]);
                }
            }

            if (!anyUnwrapped) break;

            func = tryResolveTemplate(name, unwrappedTypes, expr);
            if (func) {
                expr->autoMapArgs = std::move(autoMap);
                isAutoMapped = true;
                hasListArg = anyList;
            }
        }
    }

    return func;
}

// Step 3: Try implicit auto-mapping of non-@ args within an explicit-@ call.
FuncInfo* TypeChecker::tryImplicitAutoMapInner(const std::string& name,
                                                const std::vector<Type*>& unwrappedTypes,
                                                const std::vector<AutoMapArg>& explicitAutoMap,
                                                CallExpr_* expr) {
    // Find max nesting depth of non-@ args
    int maxNesting = 0;
    for (size_t i = 0; i < unwrappedTypes.size(); ++i) {
        if (explicitAutoMap[i].depth > 0) continue;
        int d = 0;
        Type* t = unwrappedTypes[i];
        while (true) {
            if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                d++; t = arrT->elemType_;
            } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                d++; t = listT->elemType_;
            } else break;
        }
        if (d > maxNesting) maxNesting = d;
    }

    FuncInfo* func = nullptr;

    // Try unwrapping non-@ args at depth 1, 2, ...
    for (int depth = 1; depth <= maxNesting && !func; ++depth) {
        std::vector<Type*> furtherUnwrapped = unwrappedTypes;
        std::vector<AutoMapArg> innerAutoMap(unwrappedTypes.size());
        bool anyImplicit = false;

        for (size_t i = 0; i < unwrappedTypes.size(); ++i) {
            if (explicitAutoMap[i].depth > 0) continue;
            Type* t = unwrappedTypes[i];
            bool canUnwrap = true;
            bool thisList = false;
            for (int d = 0; d < depth; ++d) {
                if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                    t = arrT->elemType_;
                } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                    t = listT->elemType_;
                    thisList = true;
                } else {
                    canUnwrap = false;
                    break;
                }
            }
            bool isComposite = dynamic_cast<ArrayType*>(unwrappedTypes[i]) ||
                               dynamic_cast<ListType*>(unwrappedTypes[i]);
            if (canUnwrap && isComposite) {
                furtherUnwrapped[i] = t;
                innerAutoMap[i] = AutoMapArg{depth, 0, thisList};
                anyImplicit = true;
            }
        }

        if (!anyImplicit) break;

        // Exact match -> template -> promotion
        {
            auto it2 = functions_.find(name);
            if (it2 != functions_.end()) {
                for (auto& fi2 : it2->second) {
                    if (fi2.isTemplate || fi2.paramTypes.size() != furtherUnwrapped.size()) continue;
                    bool match = true;
                    for (size_t j = 0; j < furtherUnwrapped.size(); ++j) {
                        if (fi2.paramTypes[j] != furtherUnwrapped[j]) { match = false; break; }
                    }
                    if (match) { func = &fi2; break; }
                }
            }
        }
        if (!func) func = tryResolveTemplate(name, furtherUnwrapped, expr);
        if (!func) func = tryResolveOverload(name, furtherUnwrapped);
        if (func) {
            expr->innerAutoMapArgs = std::move(innerAutoMap);
        }
    }

    return func;
}

// Step 4: Compute auto-mapped return type by wrapping scalar return in Array/List.
Type* TypeChecker::computeAutoMapReturnType(Type* scalarReturn,
                                             const std::vector<AutoMapArg>& autoMapArgs,
                                             const std::vector<AutoMapArg>& innerAutoMapArgs,
                                             bool hasCartesian, int maxCartesianIndex,
                                             bool anyListArg) {
    if (scalarReturn == compiler_.voidType()) return scalarReturn;

    Type* retType = scalarReturn;

    // First wrap for inner (implicit) auto-mapping
    if (!innerAutoMapArgs.empty()) {
        int innerDepth = 0;
        bool innerList = false;
        for (auto& iam : innerAutoMapArgs) {
            if (iam.depth > innerDepth) innerDepth = iam.depth;
            if (iam.isList) innerList = true;
        }
        for (int level = 0; level < innerDepth; ++level) {
            retType = innerList ? static_cast<Type*>(compiler_.listType(retType))
                                : static_cast<Type*>(compiler_.arrayType(retType));
        }
    }

    // Then wrap for outer auto-mapping
    if (hasCartesian) {
        for (int level = maxCartesianIndex; level >= 1; --level) {
            retType = anyListArg ? static_cast<Type*>(compiler_.listType(retType))
                                 : static_cast<Type*>(compiler_.arrayType(retType));
        }
    } else {
        int maxDepth = 0;
        for (auto& am : autoMapArgs) {
            if (am.depth > maxDepth) maxDepth = am.depth;
        }
        for (int level = 0; level < maxDepth; ++level) {
            retType = anyListArg ? static_cast<Type*>(compiler_.listType(retType))
                                 : static_cast<Type*>(compiler_.arrayType(retType));
        }
    }

    return retType;
}

// Step 5: Try to infer an enum data case construction: EnumName.caseName(value)
Type* TypeChecker::tryInferEnumConstruct(CallExpr_* expr, FieldExpr_* fe, IdentifierExpr* ident) {
    auto enumIt = enumTypes_.find(ident->name);
    if (enumIt != enumTypes_.end()) {
        EnumType* etype = enumIt->second;
        // Find the case
        bool found = false;
        for (size_t i = 0; i < etype->cases_.size(); ++i) {
            if (etype->cases_[i].name->str() == fe->field) {
                found = true;
                Type* caseType = etype->cases_[i].type;
                if (caseType == compiler_.voidType()) {
                    error(expr->loc, "Enum case '" + fe->field + "' takes no data");
                } else if (expr->args.size() == 1) {
                    Type* argType = inferExpr(static_cast<Expr*>(expr->args[0].get()));
                    if (argType && !typesEqual(argType, caseType)) {
                        if (isAssignable(argType, caseType)) {
                            expr->args[0]->resolvedType = caseType;
                        } else {
                            error(expr->args[0]->loc, "Enum case '" + fe->field +
                                  "' expects type '" + std::string(caseType->str().data(), caseType->str().size()) + "'");
                        }
                    }
                } else if (auto* tupleType = dynamic_cast<TupleType*>(caseType);
                           tupleType && expr->args.size() == tupleType->fields_.size()) {
                    // Multi-arg construction for tuple-typed case
                    ExprList tupleElems;
                    for (size_t j = 0; j < expr->args.size(); ++j) {
                        tupleElems.push_back(std::move(expr->args[j]));
                    }
                    auto tupleLit = std::make_unique<TupleLiteralExpr>(expr->loc, std::move(tupleElems));
                    expr->args.clear();
                    expr->args.push_back(std::move(tupleLit));
                    Type* argType = inferExpr(static_cast<Expr*>(expr->args[0].get()));
                    if (argType && !typesEqual(argType, caseType)) {
                        if (isAssignable(argType, caseType)) {
                            expr->args[0]->resolvedType = caseType;
                        } else {
                            error(expr->args[0]->loc, "Enum case '" + fe->field +
                                  "' expects type '" + std::string(caseType->str().data(), caseType->str().size()) + "'");
                        }
                    }
                } else {
                    auto* tupleType2 = dynamic_cast<TupleType*>(caseType);
                    if (tupleType2) {
                        error(expr->loc, "Enum case '" + fe->field +
                              "' expects " + std::to_string(tupleType2->fields_.size()) + " arguments");
                    } else {
                        error(expr->loc, "Enum case '" + fe->field +
                              "' expects exactly 1 argument");
                    }
                }
                break;
            }
        }
        if (!found) {
            error(expr->loc, "Unknown case '" + fe->field +
                  "' in enum '" + ident->name + "'");
        }
        // Re-tag this node as EnumConstructor for codegen
        expr->kind = ASTNode::EnumConstructor;
        expr->resolvedType = etype;
        return etype;
    }

    // Check for template enum data case: TemplateName.caseName(value)
    auto tmplIt = templateEnums_.find(ident->name);
    if (tmplIt != templateEnums_.end()) {
        UnionDeclNode* decl = tmplIt->second;
        // Multi-arg: synthesize TupleLiteralExpr if case type is a tuple with matching count
        if (expr->args.size() > 1) {
            for (auto& ucase : decl->cases) {
                if (ucase.name == fe->field && ucase.typeExpr &&
                    ucase.typeExpr->kind == TypeExpr::TupleType) {
                    auto* ttn = static_cast<TupleTypeNode*>(ucase.typeExpr.get());
                    if (ttn->elemTypes.size() == expr->args.size()) {
                        ExprList tupleElems;
                        for (auto& a : expr->args) {
                            tupleElems.push_back(std::move(a));
                        }
                        auto tupleLit = std::make_unique<TupleLiteralExpr>(expr->loc, std::move(tupleElems));
                        expr->args.clear();
                        expr->args.push_back(std::move(tupleLit));
                    }
                    break;
                }
            }
        }
        // Infer type args from the argument
        if (expr->args.size() == 1) {
            Type* argType = inferExpr(static_cast<Expr*>(expr->args[0].get()));
            for (auto& ucase : decl->cases) {
                if (ucase.name == fe->field && ucase.typeExpr) {
                    std::unordered_map<std::string, Type*> bindings;
                    if (argType && unifyTypeExpr(ucase.typeExpr.get(), argType,
                                                  decl->typeParams, bindings)) {
                        std::vector<Type*> typeArgs;
                        bool allBound = true;
                        for (auto& tp : decl->typeParams) {
                            auto bIt = bindings.find(tp);
                            if (bIt == bindings.end()) {
                                allBound = false;
                                break;
                            }
                            typeArgs.push_back(bIt->second);
                        }
                        if (allBound) {
                            EnumType* etype2 = monomorphizeEnum(ident->name, typeArgs, expr->loc);
                            if (etype2) {
                                expr->kind = ASTNode::EnumConstructor;
                                expr->resolvedType = etype2;
                                return etype2;
                            }
                        }
                    }
                    break;
                }
            }
        }
        error(expr->loc, "Cannot infer type arguments for template enum '" +
              ident->name + "'");
        return compiler_.intType();
    }

    return nullptr;  // not an enum
}

// Step 6: Try to infer a tuple struct construction: StructName(arg1, arg2, ...)
Type* TypeChecker::tryInferTupleStructConstruct(CallExpr_* expr, IdentifierExpr* ident) {
    {
        auto sIt = structTypes_.find(ident->name);
        if (sIt != structTypes_.end() && sIt->second->isTupleStruct_) {
            StructType* stype = sIt->second;
            if (expr->args.size() != stype->fields_.size()) {
                error(expr->loc, "Tuple struct '" + ident->name + "' expects " +
                      std::to_string(stype->fields_.size()) + " arguments, got " +
                      std::to_string(expr->args.size()));
            }

            // Infer all arg types, detecting explicit @ annotations
            std::vector<Type*> argTypes;
            std::vector<AutoMapArg> autoMap(expr->args.size());

            for (size_t i = 0; i < expr->args.size(); ++i) {
                Expr* arg = static_cast<Expr*>(expr->args[i].get());
                if (arg->kind == ASTNode::AutoMap) {
                    auto* am = static_cast<AutoMapExpr*>(arg);
                    autoMap[i].depth = am->depth;
                    autoMap[i].cartesianIndex = am->cartesianIndex;
                }
                argTypes.push_back(inferExpr(arg));
            }

            // Check each arg against its field type, detecting auto-mapping
            bool anyAutoMap = false;
            for (size_t i = 0; i < expr->args.size() && i < stype->fields_.size(); ++i) {
                Type* fieldType = stype->fields_[i].type;

                if (autoMap[i].depth > 0) {
                    // Explicit @ on this arg
                    auto* arrT = dynamic_cast<ArrayType*>(argTypes[i]);
                    auto* listT = dynamic_cast<ListType*>(argTypes[i]);
                    if (!arrT && !listT) {
                        error(expr->args[i]->loc, "Explicit '@' on tuple struct arg requires Array or List type");
                    } else {
                        bool isList = false;
                        Type* innerType = unwrapAutoMapLayers(argTypes[i], autoMap[i].depth, isList, expr->args[i]->loc);
                        autoMap[i].isList = isList;
                        if (!typesEqual(innerType, fieldType) && !isAssignable(innerType, fieldType)) {
                            error(expr->args[i]->loc, "Tuple struct field type mismatch: expected '" +
                                  std::string(fieldType->str().data(), fieldType->str().size()) +
                                  "', got '" + std::string(innerType->str().data(), innerType->str().size()) + "'");
                        }
                    }
                    anyAutoMap = true;
                } else if (argTypes[i] && !typesEqual(argTypes[i], fieldType)) {
                    if (isAssignable(argTypes[i], fieldType)) {
                        // promotion OK
                    } else if (auto* arrT = dynamic_cast<ArrayType*>(argTypes[i])) {
                        if (typesEqual(arrT->elemType_, fieldType) ||
                            isAssignable(arrT->elemType_, fieldType)) {
                            autoMap[i] = AutoMapArg{1, 0, false};
                            anyAutoMap = true;
                        } else {
                            error(expr->args[i]->loc, "Tuple struct field type mismatch: expected '" +
                                  std::string(fieldType->str().data(), fieldType->str().size()) +
                                  "', got '" + std::string(argTypes[i]->str().data(), argTypes[i]->str().size()) + "'");
                        }
                    } else if (auto* listT = dynamic_cast<ListType*>(argTypes[i])) {
                        if (typesEqual(listT->elemType_, fieldType) ||
                            isAssignable(listT->elemType_, fieldType)) {
                            autoMap[i] = AutoMapArg{1, 0, true};
                            anyAutoMap = true;
                        } else {
                            error(expr->args[i]->loc, "Tuple struct field type mismatch: expected '" +
                                  std::string(fieldType->str().data(), fieldType->str().size()) +
                                  "', got '" + std::string(argTypes[i]->str().data(), argTypes[i]->str().size()) + "'");
                        }
                    } else {
                        error(expr->args[i]->loc, "Tuple struct field type mismatch: expected '" +
                              std::string(fieldType->str().data(), fieldType->str().size()) +
                              "', got '" + std::string(argTypes[i]->str().data(), argTypes[i]->str().size()) + "'");
                    }
                }
            }

            expr->resolvedFuncGlobalIndex = -3;  // sentinel for tuple struct construction
            if (anyAutoMap) {
                expr->autoMapArgs = std::move(autoMap);
                // Determine if any auto-mapped arg is a List
                bool anyList = false;
                for (auto& am : expr->autoMapArgs) {
                    if (am.isList) { anyList = true; break; }
                }
                // Compute cartesian nesting depth
                int maxCartesian = 0;
                for (auto& am : expr->autoMapArgs) {
                    if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                }
                int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                Type* wrapped = static_cast<Type*>(stype);
                for (int level = 0; level < wrapLevels; ++level) {
                    if (anyList) wrapped = compiler_.listType(wrapped);
                    else         wrapped = compiler_.arrayType(wrapped);
                }
                expr->resolvedType = wrapped;
                return wrapped;
            } else {
                expr->resolvedType = stype;
                return stype;
            }
        }

        // Check template tuple structs
        auto tmplIt = templateStructs_.find(ident->name);
        if (tmplIt != templateStructs_.end() && tmplIt->second->isTupleStruct) {
            StructDeclNode* decl = tmplIt->second;
            // Infer types from args, detecting explicit @ annotations
            std::vector<Type*> argTypes;
            std::vector<AutoMapArg> autoMap(expr->args.size());

            for (size_t i = 0; i < expr->args.size(); ++i) {
                Expr* arg = static_cast<Expr*>(expr->args[i].get());
                if (arg->kind == ASTNode::AutoMap) {
                    auto* am = static_cast<AutoMapExpr*>(arg);
                    autoMap[i].depth = am->depth;
                    autoMap[i].cartesianIndex = am->cartesianIndex;
                }
                argTypes.push_back(inferExpr(arg));
            }

            if (argTypes.size() != decl->fields.size()) {
                error(expr->loc, "Tuple struct '" + ident->name + "' expects " +
                      std::to_string(decl->fields.size()) + " arguments, got " +
                      std::to_string(argTypes.size()));
                return compiler_.intType();
            }

            // Check if any arg has explicit @ annotation
            bool hasExplicitAt = false;
            for (auto& am : autoMap) {
                if (am.depth > 0) { hasExplicitAt = true; break; }
            }

            // Try direct unification first (unless explicit @ forces auto-mapping)
            std::unordered_map<std::string, Type*> bindings;
            bool ok = !hasExplicitAt;
            if (!hasExplicitAt) {
                for (size_t i = 0; i < decl->fields.size(); ++i) {
                    if (argTypes[i] && !unifyTypeExpr(decl->fields[i].typeExpr.get(), argTypes[i],
                                                       decl->typeParams, bindings)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    std::vector<Type*> typeArgs;
                    for (auto& tp : decl->typeParams) {
                        auto bIt = bindings.find(tp);
                        if (bIt == bindings.end()) { ok = false; break; }
                        typeArgs.push_back(bIt->second);
                    }
                    if (ok) {
                        StructType* stype2 = monomorphizeStruct(ident->name, typeArgs, expr->loc);
                        if (stype2) {
                            expr->resolvedFuncGlobalIndex = -3;
                            expr->resolvedType = stype2;
                            return stype2;
                        }
                    }
                }
            }

            // Direct unification failed or explicit @ present -- try auto-mapping
            bool anyAutoMap = false;
            bool anyList = false;
            std::vector<Type*> unwrappedTypes;

            for (size_t i = 0; i < argTypes.size(); ++i) {
                if (autoMap[i].depth > 0) {
                    // Explicit @
                    bool isList = false;
                    Type* inner = unwrapAutoMapLayers(argTypes[i], autoMap[i].depth, isList, expr->args[i]->loc);
                    autoMap[i].isList = isList;
                    if (isList) anyList = true;
                    unwrappedTypes.push_back(inner);
                    anyAutoMap = true;
                } else if (auto* arrT = dynamic_cast<ArrayType*>(argTypes[i])) {
                    unwrappedTypes.push_back(arrT->elemType_);
                    autoMap[i] = AutoMapArg{1, 0, false};
                    anyAutoMap = true;
                } else if (auto* listT = dynamic_cast<ListType*>(argTypes[i])) {
                    unwrappedTypes.push_back(listT->elemType_);
                    autoMap[i] = AutoMapArg{1, 0, true};
                    anyAutoMap = true;
                    anyList = true;
                } else {
                    unwrappedTypes.push_back(argTypes[i]);
                }
            }

            if (anyAutoMap) {
                bindings.clear();
                ok = true;
                for (size_t i = 0; i < decl->fields.size(); ++i) {
                    if (unwrappedTypes[i] && !unifyTypeExpr(decl->fields[i].typeExpr.get(), unwrappedTypes[i],
                                                             decl->typeParams, bindings)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    std::vector<Type*> typeArgs;
                    for (auto& tp : decl->typeParams) {
                        auto bIt = bindings.find(tp);
                        if (bIt == bindings.end()) { ok = false; break; }
                        typeArgs.push_back(bIt->second);
                    }
                    if (ok) {
                        StructType* stype2 = monomorphizeStruct(ident->name, typeArgs, expr->loc);
                        if (stype2) {
                            expr->autoMapArgs = std::move(autoMap);
                            expr->resolvedFuncGlobalIndex = -3;
                            // Compute cartesian nesting depth
                            int maxCartesian = 0;
                            for (auto& am : expr->autoMapArgs) {
                                if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                            }
                            int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                            Type* wrapped = static_cast<Type*>(stype2);
                            for (int level = 0; level < wrapLevels; ++level) {
                                if (anyList) wrapped = compiler_.listType(wrapped);
                                else         wrapped = compiler_.arrayType(wrapped);
                            }
                            expr->resolvedType = wrapped;
                            return wrapped;
                        }
                    }
                }
            }

            error(expr->loc, "Cannot infer type arguments for template tuple struct '" +
                  ident->name + "'");
            return compiler_.intType();
        }
    }

    return nullptr;  // not a tuple struct
}

// Step 7a: Handle non-identifier callees (FunctionType direct call, callable-object rewrite).
Type* TypeChecker::inferIndirectCall(CallExpr_* expr) {
    Type* calleeType = inferExpr(static_cast<Expr*>(expr->callee.get()));
    auto* funcType = dynamic_cast<FunctionType*>(calleeType);
    if (funcType) {
        // Type-check argument count and types
        if (expr->args.size() != funcType->argTypes_.size()) {
            error(expr->loc, "Expected " + std::to_string(funcType->argTypes_.size()) +
                  " arguments, got " + std::to_string(expr->args.size()));
        }
        for (size_t i = 0; i < expr->args.size() && i < funcType->argTypes_.size(); ++i) {
            Type* argType = inferExpr(static_cast<Expr*>(expr->args[i].get()));
            if (argType && !typesEqual(argType, funcType->argTypes_[i])) {
                if (funcType->argTypes_[i] == compiler_.floatType() && argType == compiler_.intType()) {
                    // promotion OK
                } else {
                    error(expr->args[i]->loc, "Argument type mismatch");
                }
            }
        }
        // Mark coroutine calls on expression callees
        if (funcType->returnType_ && dynamic_cast<CoroutineType*>(funcType->returnType_)) {
            expr->isCoroCall = true;
        }
        return funcType->returnType_;
    }
    // Template lambda: monomorphize based on argument types
    auto* tmplLambda = dynamic_cast<TemplateLambdaType*>(calleeType);
    if (tmplLambda) {
        std::vector<Type*> callArgTypes;
        for (auto& arg : expr->args)
            callArgTypes.push_back(inferExpr(static_cast<Expr*>(arg.get())));
        LambdaType* concreteLT = monomorphizeTemplateLambda(tmplLambda, callArgTypes, expr->loc);
        if (!concreteLT) return compiler_.intType();
        expr->resolvedTemplateLambdaType = concreteLT;
        return concreteLT->returnType_;
    }
    // Not a function type -- try callable object via `call` function
    if (functions_.find("call") == functions_.end()) {
        error(expr->callee->loc, "Expression is not callable");
        return compiler_.intType();
    }
    // Rewrite: expr(args...) -> call(expr, args...)
    expr->args.insert(expr->args.begin(), std::move(expr->callee));
    expr->callee = std::make_unique<IdentifierExpr>(expr->loc, "call");
    // Fall through -- caller will handle identifier-based resolution
    return nullptr;
}

// Step 7b: Handle variable call (deferred lambda, template lambda, FunctionType, callable-object).
Type* TypeChecker::tryInferVariableCall(CallExpr_* expr, IdentifierExpr* ident) {
    VarInfo* calleeVar = lookupVar(ident->name);
    if (!calleeVar) return nullptr;

    // Deferred lambda called directly: infer param types from call arguments
    if (calleeVar->deferredLambda) {
        LambdaExprNode* lambda = calleeVar->deferredLambda;
        std::vector<Type*> callArgTypes;
        for (auto& arg : expr->args) {
            callArgTypes.push_back(inferExpr(static_cast<Expr*>(arg.get())));
        }
        for (size_t j = 0; j < lambda->params.size() && j < callArgTypes.size(); ++j) {
            if (!lambda->params[j].typeExpr && !lambda->params[j].resolvedType)
                lambda->params[j].resolvedType = callArgTypes[j];
        }
        Type* resolvedType = inferLambdaExpr(lambda);
        lambda->resolvedType = resolvedType;
        calleeVar->type = resolvedType;
        if (calleeVar->deferredDecl)
            calleeVar->deferredDecl->resolvedType = resolvedType;
        calleeVar->deferredLambda = nullptr;
        calleeVar->deferredDecl = nullptr;

        auto* funcType = dynamic_cast<FunctionType*>(resolvedType);
        if (funcType) {
            if (expr->args.size() != funcType->argTypes_.size()) {
                error(expr->loc, "Lambda expects " +
                      std::to_string(funcType->argTypes_.size()) +
                      " arguments, got " + std::to_string(expr->args.size()));
            }
            return funcType->returnType_;
        }
        return compiler_.intType();
    }

    // Template lambda called directly: monomorphize based on argument types
    auto* tmplLambda = dynamic_cast<TemplateLambdaType*>(calleeVar->type);
    if (tmplLambda) {
        std::vector<Type*> callArgTypes;
        for (auto& arg : expr->args) {
            callArgTypes.push_back(inferExpr(static_cast<Expr*>(arg.get())));
        }
        LambdaType* concreteLT = monomorphizeTemplateLambda(tmplLambda, callArgTypes, expr->loc);
        if (!concreteLT) return compiler_.intType();
        expr->resolvedTemplateLambdaType = concreteLT;
        return concreteLT->returnType_;
    }

    auto* funcType = dynamic_cast<FunctionType*>(calleeVar->type);
    if (funcType) {
        // Type-check argument count
        if (expr->args.size() != funcType->argTypes_.size()) {
            error(expr->loc, "Lambda expects " +
                  std::to_string(funcType->argTypes_.size()) +
                  " arguments, got " + std::to_string(expr->args.size()));
        }

        // Infer argument types and check for explicit @ auto-mapping
        std::vector<Type*> argTypes;
        bool hasExplicitAutoMap = false;
        std::vector<AutoMapArg> explicitAutoMap(expr->args.size());
        for (size_t i = 0; i < expr->args.size(); ++i) {
            Expr* arg = static_cast<Expr*>(expr->args[i].get());
            if (arg->kind == ASTNode::AutoMap) {
                auto* am = static_cast<AutoMapExpr*>(arg);
                explicitAutoMap[i].depth = am->depth;
                explicitAutoMap[i].cartesianIndex = am->cartesianIndex;
                hasExplicitAutoMap = true;
            }
            argTypes.push_back(inferExpr(arg));
        }

        // Handle explicit @ auto-mapping on lambda calls
        if (hasExplicitAutoMap) {
            bool anyListArg = false;
            std::vector<Type*> unwrappedTypes;
            for (size_t i = 0; i < argTypes.size(); ++i) {
                if (explicitAutoMap[i].depth > 0) {
                    Type* t = argTypes[i];
                    int d = explicitAutoMap[i].depth;
                    for (int level = 0; level < d; ++level) {
                        if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                            t = arrT->elemType_;
                        } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                            t = listT->elemType_;
                            explicitAutoMap[i].isList = true;
                            anyListArg = true;
                        } else {
                            error(expr->args[i]->loc,
                                  "Explicit '@' requires Array or List type");
                            t = nullptr;
                            break;
                        }
                    }
                    unwrappedTypes.push_back(t ? t : argTypes[i]);
                } else {
                    unwrappedTypes.push_back(argTypes[i]);
                }
            }
            // Check unwrapped types against param types
            for (size_t i = 0; i < unwrappedTypes.size() && i < funcType->argTypes_.size(); ++i) {
                if (unwrappedTypes[i] && !typesEqual(unwrappedTypes[i], funcType->argTypes_[i])) {
                    if (!(funcType->argTypes_[i] == compiler_.floatType() && unwrappedTypes[i] == compiler_.intType())) {
                        error(expr->args[i]->loc, "Argument type mismatch");
                    }
                }
            }
            expr->autoMapArgs = std::move(explicitAutoMap);
            Type* retType = funcType->returnType_;
            if (retType != compiler_.voidType()) {
                int maxDepth = 0;
                for (auto& am : expr->autoMapArgs) {
                    if (am.depth > maxDepth) maxDepth = am.depth;
                }
                for (int d = 0; d < maxDepth; ++d) {
                    retType = anyListArg ? static_cast<Type*>(compiler_.listType(retType))
                                         : static_cast<Type*>(compiler_.arrayType(retType));
                }
            }
            return retType;
        }

        // Direct type check
        bool allMatch = true;
        for (size_t i = 0; i < argTypes.size() && i < funcType->argTypes_.size(); ++i) {
            if (argTypes[i] && !typesEqual(argTypes[i], funcType->argTypes_[i])) {
                if (funcType->argTypes_[i] == compiler_.floatType() && argTypes[i] == compiler_.intType()) {
                    // promotion OK
                } else {
                    allMatch = false;
                }
            }
        }

        if (allMatch) {
            if (funcType->returnType_ && dynamic_cast<CoroutineType*>(funcType->returnType_)) {
                expr->isCoroCall = true;
            }
            return funcType->returnType_;
        }

        // Try implicit auto-mapping: unwrap Array/List args at increasing depths
        int maxNesting = 0;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            int d = 0;
            Type* t = argTypes[i];
            while (true) {
                if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                    d++; t = arrT->elemType_;
                } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                    d++; t = listT->elemType_;
                } else break;
            }
            if (d > maxNesting) maxNesting = d;
        }

        for (int depth = 1; depth <= maxNesting; ++depth) {
            bool anyUnwrapped = false;
            bool anyList = false;
            std::vector<Type*> unwrappedTypes;
            std::vector<AutoMapArg> autoMap(argTypes.size());

            for (size_t i = 0; i < argTypes.size(); ++i) {
                Type* t = argTypes[i];
                bool canUnwrap = true;
                bool thisList = false;
                for (int d = 0; d < depth; ++d) {
                    if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                        t = arrT->elemType_;
                    } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                        t = listT->elemType_;
                        thisList = true;
                    } else {
                        canUnwrap = false;
                        break;
                    }
                }
                bool isComposite = dynamic_cast<ArrayType*>(argTypes[i]) ||
                                   dynamic_cast<ListType*>(argTypes[i]);
                if (canUnwrap && isComposite) {
                    unwrappedTypes.push_back(t);
                    autoMap[i] = AutoMapArg{depth, 0, thisList};
                    anyUnwrapped = true;
                    if (thisList) anyList = true;
                } else {
                    unwrappedTypes.push_back(argTypes[i]);
                }
            }

            if (!anyUnwrapped) break;

            // Check if unwrapped types match the function's param types
            bool match = true;
            for (size_t i = 0; i < unwrappedTypes.size() && i < funcType->argTypes_.size(); ++i) {
                if (unwrappedTypes[i] && !typesEqual(unwrappedTypes[i], funcType->argTypes_[i])) {
                    if (funcType->argTypes_[i] == compiler_.floatType() && unwrappedTypes[i] == compiler_.intType()) {
                        // promotion OK
                    } else {
                        match = false;
                        break;
                    }
                }
            }

            if (match) {
                expr->autoMapArgs = std::move(autoMap);
                Type* retType = funcType->returnType_;
                if (retType != compiler_.voidType()) {
                    for (int d = 0; d < depth; ++d) {
                        retType = anyList ? static_cast<Type*>(compiler_.listType(retType))
                                          : static_cast<Type*>(compiler_.arrayType(retType));
                    }
                }
                return retType;
            }
        }

        // No auto-map match either -- report type errors
        for (size_t i = 0; i < argTypes.size() && i < funcType->argTypes_.size(); ++i) {
            if (argTypes[i] && !typesEqual(argTypes[i], funcType->argTypes_[i])) {
                if (!(funcType->argTypes_[i] == compiler_.floatType() && argTypes[i] == compiler_.intType())) {
                    error(expr->args[i]->loc, "Argument type mismatch");
                }
            }
        }
        return funcType->returnType_;
    }

    // Callable object: variable of non-function type with a `call` function.
    // Rewrite: myValue(args...) -> call(myValue, args...)
    if (calleeVar->type && functions_.find("call") != functions_.end()) {
        auto calleeArg = std::make_unique<IdentifierExpr>(ident->loc, ident->name);
        expr->args.insert(expr->args.begin(), std::move(calleeArg));
        ident->name = "call";
        return nullptr;  // fall through to standard function resolution
    }

    return nullptr;
}

// ============================================================================
// Main inferCall -- now delegates to extracted helpers
// ============================================================================

Type* TypeChecker::inferCall(CallExpr_* expr) {
    // Clear stale autoMapArgs/innerAutoMapArgs from previous template instantiation
    // of the same AST node (template bodies share AST across monomorphizations)
    expr->autoMapArgs.clear();
    expr->innerAutoMapArgs.clear();

    // Check for std-qualified or module-qualified function call: std.func(args) or module.func(args)
    if (expr->callee->kind == ASTNode::FieldExpr) {
        auto* fe = static_cast<FieldExpr_*>(expr->callee.get());
        if (fe->object->kind == ASTNode::Identifier) {
            auto* ident = static_cast<IdentifierExpr*>(fe->object.get());

            auto modIt = importedModules_.find(ident->name);
            if (modIt != importedModules_.end()) {
                ModuleInfo* mod = modIt->second;
                auto expIt = mod->exports.find(fe->field);
                if (expIt == mod->exports.end()) {
                    error(expr->loc, "Module '" + ident->name +
                          "' does not export '" + fe->field + "'");
                    return compiler_.intType();
                }
                const ExportEntry& entry = expIt->second;
                if (entry.kind == ExportEntry::Func) {
                    // Infer argument types
                    std::vector<Type*> argTypes;
                    for (auto& arg : expr->args) {
                        argTypes.push_back(inferExpr(static_cast<Expr*>(arg.get())));
                    }
                    // First try concrete (non-template) overload match
                    FuncInfo* resolved = nullptr;
                    for (auto& fi : const_cast<std::deque<FuncInfo>&>(entry.funcOverloads)) {
                        if (fi.isTemplate) continue;
                        if (fi.paramTypes.size() == argTypes.size()) {
                            bool match = true;
                            for (size_t i = 0; i < argTypes.size(); ++i) {
                                if (!isAssignable(argTypes[i], fi.paramTypes[i])) {
                                    match = false;
                                    break;
                                }
                            }
                            if (match) {
                                resolved = &fi;
                                break;
                            }
                        }
                    }
                    // If no concrete match, try template resolution
                    if (!resolved) {
                        resolved = tryResolveModuleTemplate(
                            fe->field, entry.funcOverloads, argTypes, expr);
                    }
                    if (!resolved && !entry.funcOverloads.empty()) {
                        // Fall back to first non-template overload if single
                        size_t concreteCount = 0;
                        FuncInfo* singleConcrete = nullptr;
                        for (auto& fi : const_cast<std::deque<FuncInfo>&>(entry.funcOverloads)) {
                            if (!fi.isTemplate) {
                                concreteCount++;
                                singleConcrete = &fi;
                            }
                        }
                        if (concreteCount == 1) {
                            resolved = singleConcrete;
                        } else {
                            error(expr->loc, "No matching overload for '" + fe->field +
                                  "' in module '" + ident->name + "'");
                            return compiler_.intType();
                        }
                    }
                    if (resolved) {
                        expr->resolvedFuncGlobalIndex = (i32)resolved->globalIndex;
                        expr->isBuiltinCall = resolved->isBuiltin;
                        // Mark coroutine calls
                        if (resolved->returnType && dynamic_cast<CoroutineType*>(resolved->returnType)) {
                            expr->isCoroCall = true;
                        }
                        // Demand-driven inference if needed
                        if (resolved->returnType == nullptr && resolved->declNode) {
                            inferFunctionReturnType(resolved->declNode, resolved);
                        }
                        return resolved->returnType ? resolved->returnType : compiler_.voidType();
                    }
                    return compiler_.intType();
                } else if (entry.kind == ExportEntry::EnumT) {
                    // Could be enum construction through module: mod.Enum.Case(val)
                    // Fall through to normal enum handling below
                } else {
                    error(expr->loc, "'" + fe->field + "' in module '" +
                          ident->name + "' is not callable");
                    return compiler_.intType();
                }
            }
        }
    }

    // Check for enum data case construction: EnumName.caseName(value)
    if (expr->callee->kind == ASTNode::FieldExpr) {
        auto* fe = static_cast<FieldExpr_*>(expr->callee.get());
        if (fe->object->kind == ASTNode::Identifier) {
            auto* ident = static_cast<IdentifierExpr*>(fe->object.get());
            if (Type* t = tryInferEnumConstruct(expr, fe, ident)) return t;
        }
    }

    // Handle non-identifier callees (e.g., a[i](x, y))
    if (expr->callee->kind != ASTNode::Identifier) {
        Type* result = inferIndirectCall(expr);
        if (result) return result;
        // If nullptr, callee was rewritten to "call" identifier -- fall through
    }

    auto* ident = static_cast<IdentifierExpr*>(expr->callee.get());

    if (ident->name == "Complex") {
        // Complex(real, imag) constructor
        if (expr->args.size() != 2) {
            error(expr->loc, "Complex() requires exactly 2 arguments");
            return compiler_.complexType();
        }
        for (auto& arg : expr->args) {
            Type* argType = inferExpr(static_cast<Expr*>(arg.get()));
            if (argType && !isNumeric(argType)) {
                error(arg->loc, "Complex() arguments must be numeric");
            }
        }
        return compiler_.complexType();
    }

    if (ident->name == "getListPrintLimit") {
        if (expr->args.size() != 0) {
            error(expr->loc, "getListPrintLimit() takes no arguments");
        }
        return compiler_.intType();
    }

    if (ident->name == "setListPrintLimit") {
        if (expr->args.size() != 1) {
            error(expr->loc, "setListPrintLimit() requires exactly 1 argument");
            return compiler_.intType();
        }
        Type* argType = inferExpr(static_cast<Expr*>(expr->args[0].get()));
        if (argType && argType != compiler_.intType()) {
            error(expr->args[0]->loc, "setListPrintLimit() argument must be Int");
        }
        return compiler_.intType();
    }

    // Check for tuple struct construction: StructName(arg1, arg2, ...)
    if (Type* t = tryInferTupleStructConstruct(expr, ident)) return t;

    // Check if callee is a variable holding a lambda/function type
    {
        Type* result = tryInferVariableCall(expr, ident);
        if (result) return result;
        // If nullptr, either not a variable call, or was rewritten to "call" -- fall through
    }

    // Infer argument types first.
    // Also detect explicit @-annotated arguments and extract info.
    std::vector<Type*> argTypes;
    bool hasExplicitAutoMap = false;
    bool hasCartesian = false;
    int maxCartesianIndex = 0;
    std::vector<AutoMapArg> explicitAutoMap(expr->args.size());

    bool hasDeferredLambda = false;
    for (size_t i = 0; i < expr->args.size(); ++i) {
        Expr* arg = static_cast<Expr*>(expr->args[i].get());
        if (arg->kind == ASTNode::AutoMap) {
            auto* am = static_cast<AutoMapExpr*>(arg);
            explicitAutoMap[i].depth = am->depth;
            explicitAutoMap[i].cartesianIndex = am->cartesianIndex;
            hasExplicitAutoMap = true;
            if (am->cartesianIndex > 0) {
                hasCartesian = true;
                if (am->cartesianIndex > maxCartesianIndex)
                    maxCartesianIndex = am->cartesianIndex;
            }
        }
        // Check if this is a lambda with untyped parameters -- defer inference
        bool isUntypedLambda = false;
        if (arg->kind == ASTNode::LambdaExpr) {
            auto* lambda = static_cast<LambdaExprNode*>(arg);
            for (auto& param : lambda->params) {
                if (!param.typeExpr && !param.resolvedType) {
                    isUntypedLambda = true;
                    break;
                }
            }
        }
        // Also check if this is an identifier referencing a deferred lambda variable
        if (!isUntypedLambda && arg->kind == ASTNode::Identifier) {
            auto* argIdent = static_cast<IdentifierExpr*>(arg);
            VarInfo* vi = lookupVar(argIdent->name);
            if (vi && vi->deferredLambda) {
                isUntypedLambda = true;
            }
        }
        // Check if this is a template-only function reference (no concrete overloads)
        bool isTemplateFuncRef = false;
        if (!isUntypedLambda && arg->kind == ASTNode::Identifier) {
            auto* argIdent = static_cast<IdentifierExpr*>(arg);
            if (!lookupVar(argIdent->name)) {
                auto funcIt = functions_.find(argIdent->name);
                if (funcIt != functions_.end()) {
                    bool hasConcreteOverload = false;
                    for (auto& fi : funcIt->second) {
                        if (!fi.isTemplate) { hasConcreteOverload = true; break; }
                    }
                    if (!hasConcreteOverload) isTemplateFuncRef = true;
                }
            }
        }
        // Check if this is a template lambda variable reference
        bool isTemplateLambdaRef = false;
        if (!isUntypedLambda && !isTemplateFuncRef && arg->kind == ASTNode::Identifier) {
            auto* argIdent = static_cast<IdentifierExpr*>(arg);
            VarInfo* vi = lookupVar(argIdent->name);
            if (vi && vi->type && dynamic_cast<TemplateLambdaType*>(vi->type)) {
                isTemplateLambdaRef = true;
            }
        }
        // Also check inline template lambdas
        if (!isUntypedLambda && !isTemplateFuncRef && !isTemplateLambdaRef &&
            arg->kind == ASTNode::LambdaExpr) {
            auto* lambda = static_cast<LambdaExprNode*>(arg);
            if (!lambda->typeParams.empty()) {
                isTemplateLambdaRef = true;
            }
        }
        if (isUntypedLambda || isTemplateFuncRef || isTemplateLambdaRef) {
            argTypes.push_back(nullptr);  // defer -- will resolve after backward inference
            hasDeferredLambda = true;
        } else {
            argTypes.push_back(inferExpr(arg));
        }
    }

    // Backward inference: deduce types for untyped lambda parameters from other args
    if (hasDeferredLambda) {
        // Collect element types from collection args and scalar types from non-lambda args
        std::vector<Type*> collectionElemTypes;
        std::vector<Type*> scalarTypes;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (!argTypes[i]) continue;  // skip deferred lambdas
            Type* t = argTypes[i];
            if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                collectionElemTypes.push_back(arrT->elemType_);
            } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                collectionElemTypes.push_back(listT->elemType_);
            } else {
                scalarTypes.push_back(t);
            }
        }

        // For each deferred lambda, deduce param types and then infer
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (argTypes[i]) continue;

            // Get the lambda node -- either inline or via deferred variable
            LambdaExprNode* lambda = nullptr;
            VarInfo* deferredVarInfo = nullptr;
            Expr* arg = static_cast<Expr*>(expr->args[i].get());
            if (arg->kind == ASTNode::LambdaExpr) {
                lambda = static_cast<LambdaExprNode*>(arg);
            } else if (arg->kind == ASTNode::Identifier) {
                auto* argIdent = static_cast<IdentifierExpr*>(arg);
                VarInfo* vi = lookupVar(argIdent->name);
                if (vi && vi->deferredLambda) {
                    lambda = vi->deferredLambda;
                    deferredVarInfo = vi;
                }
            }
            // Check for template lambda (inline or via variable)
            TemplateLambdaType* tmplLambdaType = nullptr;
            if (lambda && !lambda->typeParams.empty()) {
                // Inline template lambda -- infer it first to create the TemplateLambdaType
                Type* tmplType = inferExpr(arg);
                tmplLambdaType = dynamic_cast<TemplateLambdaType*>(tmplType);
                lambda = nullptr;  // not a regular lambda
            } else if (!lambda && arg->kind == ASTNode::Identifier) {
                auto* argIdent = static_cast<IdentifierExpr*>(arg);
                VarInfo* vi = lookupVar(argIdent->name);
                if (vi && vi->type) {
                    tmplLambdaType = dynamic_cast<TemplateLambdaType*>(vi->type);
                }
            }
            if (tmplLambdaType) {
                // Build guessed arg types from context
                size_t nParams = tmplLambdaType->astNode_->params.size();
                std::vector<Type*> guessArgs;

                if (collectionElemTypes.size() >= nParams) {
                    for (size_t j = 0; j < nParams; ++j)
                        guessArgs.push_back(collectionElemTypes[j]);
                } else if (!scalarTypes.empty() && !collectionElemTypes.empty()) {
                    guessArgs.push_back(scalarTypes[0]);
                    for (size_t j = 1; j < nParams && (j-1) < collectionElemTypes.size(); ++j)
                        guessArgs.push_back(collectionElemTypes[j-1]);
                } else if (!collectionElemTypes.empty()) {
                    for (size_t j = 0; j < nParams; ++j)
                        guessArgs.push_back(collectionElemTypes[0]);
                } else if (!scalarTypes.empty()) {
                    for (size_t j = 0; j < nParams; ++j)
                        guessArgs.push_back(scalarTypes[0]);
                }

                if (guessArgs.size() == nParams) {
                    LambdaType* concreteLT = monomorphizeTemplateLambda(tmplLambdaType, guessArgs, arg->loc);
                    if (concreteLT) {
                        argTypes[i] = compiler_.functionType(concreteLT->argTypes_, concreteLT->returnType_);
                        // Store specialization info for codegen
                        if (arg->kind == ASTNode::Identifier) {
                            static_cast<IdentifierExpr*>(arg)->templateLambdaSpecType = concreteLT;
                        } else if (arg->kind == ASTNode::LambdaExpr) {
                            // Inline template lambda: convert to concrete lambda for codegen
                            auto* inlineLambda = static_cast<LambdaExprNode*>(arg);
                            inlineLambda->lambdaType = concreteLT;
                            inlineLambda->templateLambdaType = nullptr;
                        }
                    }
                }
                continue;
            }

            if (!lambda) {
                // Not a lambda -- check if it's a template function reference
                if (arg->kind == ASTNode::Identifier) {
                    auto* argIdent = static_cast<IdentifierExpr*>(arg);
                    auto funcIt = functions_.find(argIdent->name);
                    if (funcIt == functions_.end()) continue;

                    // Find a template overload with a decl node
                    FuncInfo* templateFI = nullptr;
                    for (auto& fi : funcIt->second) {
                        if (fi.isTemplate && fi.declNode) { templateFI = &fi; break; }
                    }
                    if (!templateFI) continue;

                    // Build guessed arg types from context (same heuristic as lambdas)
                    size_t nParams = templateFI->declNode->params.size();
                    std::vector<Type*> guessArgs;

                    if (collectionElemTypes.size() >= nParams) {
                        for (size_t j = 0; j < nParams; ++j)
                            guessArgs.push_back(collectionElemTypes[j]);
                    } else if (!scalarTypes.empty() && !collectionElemTypes.empty()) {
                        guessArgs.push_back(scalarTypes[0]);
                        for (size_t j = 1; j < nParams && (j-1) < collectionElemTypes.size(); ++j)
                            guessArgs.push_back(collectionElemTypes[j-1]);
                    } else if (!collectionElemTypes.empty()) {
                        for (size_t j = 0; j < nParams; ++j)
                            guessArgs.push_back(collectionElemTypes[0]);
                    } else if (!scalarTypes.empty()) {
                        for (size_t j = 0; j < nParams; ++j)
                            guessArgs.push_back(scalarTypes[0]);
                    }

                    if (guessArgs.size() != nParams) continue;

                    // Try to instantiate the template
                    FuncInfo* resolved = tryResolveTemplate(argIdent->name, guessArgs, nullptr);
                    if (!resolved) continue;

                    // Ensure return type is inferred
                    if (resolved->returnType == nullptr && resolved->declNode)
                        inferFunctionReturnType(resolved->declNode, resolved);
                    Type* ret = resolved->returnType ? resolved->returnType : compiler_.voidType();
                    TypeVec paramTV(rt::STLAllocator<Type*>(nullptr));
                    for (Type* pt : resolved->paramTypes) paramTV.push_back(pt);
                    TypeVec emptyFV(rt::STLAllocator<Type*>(nullptr));
                    auto* lt = new LambdaType(TypeVec(paramTV), ret, std::move(emptyFV));

                    argIdent->resolvedFuncGlobalIndex = (i32)resolved->globalIndex;
                    argIdent->funcRefLambdaType = lt;
                    argTypes[i] = compiler_.functionType(paramTV, ret);
                }
                continue;
            }

            size_t nParams = lambda->params.size();

            if (collectionElemTypes.size() >= nParams) {
                // map, filter, zip, etc.: each param gets a collection elem type
                for (size_t j = 0; j < nParams; ++j) {
                    if (!lambda->params[j].typeExpr && !lambda->params[j].resolvedType)
                        lambda->params[j].resolvedType = collectionElemTypes[j];
                }
            } else if (!scalarTypes.empty() && !collectionElemTypes.empty()) {
                // fold, scan: first param = scalar (accumulator), rest = collection elems
                if (!lambda->params[0].typeExpr && !lambda->params[0].resolvedType)
                    lambda->params[0].resolvedType = scalarTypes[0];
                for (size_t j = 1; j < nParams && (j - 1) < collectionElemTypes.size(); ++j) {
                    if (!lambda->params[j].typeExpr && !lambda->params[j].resolvedType)
                        lambda->params[j].resolvedType = collectionElemTypes[j - 1];
                }
            } else if (!collectionElemTypes.empty()) {
                // fold1, scan1: all params get first collection's elem type
                for (size_t j = 0; j < nParams; ++j) {
                    if (!lambda->params[j].typeExpr && !lambda->params[j].resolvedType)
                        lambda->params[j].resolvedType = collectionElemTypes[0];
                }
            }
            // If no context available, params stay null -- inferLambdaExpr will error

            if (deferredVarInfo) {
                Type* resolvedType = inferLambdaExpr(lambda);
                lambda->resolvedType = resolvedType;
                deferredVarInfo->type = resolvedType;
                if (deferredVarInfo->deferredDecl)
                    deferredVarInfo->deferredDecl->resolvedType = resolvedType;
                deferredVarInfo->deferredLambda = nullptr;
                deferredVarInfo->deferredDecl = nullptr;
                argTypes[i] = resolvedType;
            } else {
                argTypes[i] = inferExpr(arg);
            }
        }
    }

    // Error on still-deferred lambda arguments that couldn't be resolved
    if (hasDeferredLambda) {
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (argTypes[i]) continue;
            Expr* arg = static_cast<Expr*>(expr->args[i].get());
            if (arg->kind == ASTNode::LambdaExpr) {
                auto* lambda = static_cast<LambdaExprNode*>(arg);
                if (!lambda->typeParams.empty()) {
                    error(arg->loc, "Cannot infer type parameters for lambda"
                          " -- add type annotations or provide context");
                } else {
                    for (auto& param : lambda->params) {
                        if (!param.typeExpr && !param.resolvedType) {
                            error(arg->loc, "Cannot infer type for lambda parameter '" +
                                  param.name + "' -- add a type annotation");
                        }
                    }
                }
                argTypes[i] = compiler_.intType();
            }
        }
    }

    // If explicit auto-map is used, unwrap array types at the appropriate depth
    // for overload resolution, then set the autoMapArgs.
    if (hasExplicitAutoMap) {
        bool anyListArg = false;
        std::vector<Type*> unwrappedTypes;
        for (size_t i = 0; i < argTypes.size(); ++i) {
            if (explicitAutoMap[i].depth > 0) {
                // Unwrap 'depth' levels of Array or List
                Type* t = argTypes[i];
                int d = explicitAutoMap[i].depth;
                for (int level = 0; level < d; ++level) {
                    if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                        t = arrT->elemType_;
                    } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                        t = listT->elemType_;
                        explicitAutoMap[i].isList = true;
                        anyListArg = true;
                    } else {
                        error(expr->args[i]->loc,
                              "Explicit '@' requires Array or List type (need " +
                              std::to_string(d) + " levels, found " +
                              std::to_string(level) + ")");
                        t = nullptr;
                        break;
                    }
                }
                unwrappedTypes.push_back(t ? t : argTypes[i]);
            } else {
                unwrappedTypes.push_back(argTypes[i]);
            }
        }

        // Exact match -> template -> promotion (same order as non-@ path)
        FuncInfo* func = nullptr;
        {
            auto it2 = functions_.find(ident->name);
            if (it2 != functions_.end()) {
                for (auto& fi2 : it2->second) {
                    if (fi2.isTemplate || fi2.paramTypes.size() != unwrappedTypes.size()) continue;
                    bool match = true;
                    for (size_t j = 0; j < unwrappedTypes.size(); ++j) {
                        if (fi2.paramTypes[j] != unwrappedTypes[j]) { match = false; break; }
                    }
                    if (match) { func = &fi2; break; }
                }
            }
        }
        if (!func) func = tryResolveTemplate(ident->name, unwrappedTypes, expr);
        if (!func) func = tryResolveOverload(ident->name, unwrappedTypes);

        // Recursive auto-mapping: try also unwrapping non-@ Array/List args
        if (!func) {
            func = tryImplicitAutoMapInner(ident->name, unwrappedTypes, explicitAutoMap, expr);
        }

        if (!func) {
            func = resolveOverload(ident->name, unwrappedTypes, expr->loc);
            if (!func) return compiler_.intType();
        }

        expr->autoMapArgs = std::move(explicitAutoMap);
        Type* retType = finalizeResolvedCall(expr, func, ident->name, unwrappedTypes);

        // Compute auto-mapped result type
        return computeAutoMapReturnType(retType, expr->autoMapArgs, expr->innerAutoMapArgs,
                                         hasCartesian, maxCartesianIndex, anyListArg);
    }

    // Resolution order: (1) exact overload match, (2) template resolution,
    // (3) promotion-based overload. This ensures concrete overloads are preferred.
    FuncInfo* func = nullptr;
    {
        auto it2 = functions_.find(ident->name);
        if (it2 != functions_.end()) {
            for (auto& fi2 : it2->second) {
                if (fi2.isTemplate || fi2.builtinVariadicPacked ||
                    fi2.paramTypes.size() != argTypes.size()) continue;
                bool match = true;
                for (size_t j = 0; j < argTypes.size(); ++j) {
                    if (fi2.paramTypes[j] != argTypes[j]) { match = false; break; }
                }
                if (match) { func = &fi2; break; }
            }
        }
    }
    if (!func) {
        func = tryResolveTemplate(ident->name, argTypes, expr);
    }
    if (!func) {
        func = tryResolveOverload(ident->name, argTypes);
    }

    // If no direct match, try implicit auto-mapping at increasing depths.
    bool isAutoMapped = false;
    bool hasListArg = false;
    if (!func) {
        func = tryImplicitAutoMap(ident->name, argTypes, expr, isAutoMapped, hasListArg);

        // If still no match, use the error-reporting version for diagnostics
        if (!func) {
            func = resolveOverload(ident->name, argTypes, expr->loc);
            if (!func) {
                return compiler_.intType();
            }
        }
    }

    Type* retType = finalizeResolvedCall(expr, func, ident->name, argTypes);

    // If auto-mapped, wrap return type in Array or List for each depth level
    if (isAutoMapped) {
        if (retType == compiler_.voidType()) {
            return retType;
        }
        int maxDepth = 0;
        for (auto& am : expr->autoMapArgs) {
            if (am.depth > maxDepth) maxDepth = am.depth;
        }
        for (int d = 0; d < maxDepth; ++d) {
            retType = hasListArg ? static_cast<Type*>(compiler_.listType(retType)) : static_cast<Type*>(compiler_.arrayType(retType));
        }
        return retType;
    }

    return retType;
}

} // namespace ts
