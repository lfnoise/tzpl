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
//  type_checker_stmts.cpp
//  lang
//
//  Type checker -- statement and pattern checking
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

void TypeChecker::checkIfStmt(IfStmtNode* stmt) {
    Type* condType = inferExpr(static_cast<Expr*>(stmt->condition.get()));
    if (condType && condType != compiler_.boolType() && condType != compiler_.intType()) {
        error(stmt->condition->loc, "If condition must be bool or int");
    }

    checkNode(stmt->thenBranch.get());
    if (stmt->elseBranch) {
        checkNode(stmt->elseBranch.get());
    }
}

void TypeChecker::checkWhileStmt(WhileStmtNode* stmt) {
    Type* condType = inferExpr(static_cast<Expr*>(stmt->condition.get()));
    if (condType && condType != compiler_.boolType() && condType != compiler_.intType()) {
        error(stmt->condition->loc, "While condition must be bool or int");
    }

    ++loopDepth_;
    checkNode(stmt->body.get());
    --loopDepth_;
}

void TypeChecker::checkForStmt(ForStmtNode* stmt) {
    Type* iterType = inferExpr(static_cast<Expr*>(stmt->iterable.get()));
    if (!iterType) {
        error(stmt->iterable->loc, "Cannot infer type of for-loop iterable");
        return;
    }

    // Extract element type from the iterable
    Type* elemType = nullptr;
    if (auto* at = dynamic_cast<ArrayType*>(iterType)) {
        elemType = at->elemType_;
    } else if (auto* lt = dynamic_cast<ListType*>(iterType)) {
        elemType = lt->elemType_;
    } else if (auto* rt = dynamic_cast<RangeType*>(iterType)) {
        elemType = rt->elemType_;
    } else if (auto* ct = dynamic_cast<CoroutineType*>(iterType)) {
        elemType = ct->yieldType_;
    } else if (auto* pv = dynamic_cast<PersistentVectorType*>(iterType)) {
        elemType = pv->elemType_;
    } else if (dynamic_cast<PersistentMapType*>(iterType)) {
        // Like mutable Map, a persistent map is not directly for-iterable;
        // iterate `pm keys`, `pm values`, or `pm pairs` instead.
        error(stmt->iterable->loc, "Cannot iterate a persistent map directly; use keys, values, or pairs");
        return;
    } else {
        error(stmt->iterable->loc, "For-loop iterable must be an Array, List, Range, Coroutine, or persistent vector");
        return;
    }

    pushScope();
    declareVar(stmt->varName, elemType, false);
    ++loopDepth_;
    checkNode(stmt->body.get());
    --loopDepth_;
    popScope();
}

void TypeChecker::checkBreakStmt(BreakStmtNode* stmt) {
    if (loopDepth_ == 0) {
        error(stmt->loc, "'break' can only be used inside a loop");
    }
}

void TypeChecker::checkContinueStmt(ContinueStmtNode* stmt) {
    if (loopDepth_ == 0) {
        error(stmt->loc, "'continue' can only be used inside a loop");
    }
}

void TypeChecker::checkSwitchStmt(SwitchStmtNode* stmt) {
    Type* subjType = inferExpr(static_cast<Expr*>(stmt->subject.get()));

    Type* resultType = nullptr;
    for (auto& clause : stmt->cases) {
        pushScope();
        checkPattern(clause.pattern.get(), subjType, false, /*inMatch=*/true);
        checkNode(clause.body.get());

        // Unify trailing types across arms for value-producing match
        Type* armType = getNodeTrailingType(clause.body.get());
        if (armType) {
            if (!resultType) {
                resultType = armType;
            } else if (!typesEqual(resultType, armType)) {
                if (isNumeric(resultType) && isNumeric(armType)) {
                    resultType = commonNumericType(resultType, armType);
                }
            }
        }

        popScope();
    }
    stmt->resolvedType = resultType;
}

void TypeChecker::checkPattern(Pattern* pat, Type* subjType, bool isMutable, bool inMatch) {
    switch (pat->kind) {
        case Pattern::LiteralPat: {
            auto* lit = static_cast<LiteralPattern*>(pat);
            // Handle nil pattern: validate subject is ListType
            if (lit->literal->kind == ASTNode::NilLiteral) {
                if (subjType && !dynamic_cast<ListType*>(subjType)) {
                    error(pat->loc, "nil pattern requires a List type");
                }
                pat->resolvedType = subjType;
                break;
            }
            Type* litType = inferExpr(static_cast<Expr*>(lit->literal.get()));
            if (litType && subjType && !typesEqual(litType, subjType)) {
                if (isNumeric(litType) && isNumeric(subjType)) {
                    // Allow numeric promotion in pattern
                } else {
                    error(pat->loc, "Literal pattern type doesn't match subject type");
                }
            }
            pat->resolvedType = subjType;
            break;
        }
        case Pattern::WildcardPat:
            pat->resolvedType = subjType;
            break;
        case Pattern::BindingPat: {
            auto* bp = static_cast<BindingPattern*>(pat);
            // In match context, check if this is an unqualified no-data enum case
            if (inMatch) {
                if (auto* etype = dynamic_cast<EnumType*>(subjType)) {
                    for (size_t i = 0; i < etype->cases_.size(); ++i) {
                        if (etype->cases_[i].name->str() == bp->name) {
                            if (etype->cases_[i].type != compiler_.voidType()) {
                                error(pat->loc, "Enum case '" + bp->name + "' requires a value");
                            }
                            pat->enumCaseIndex = (int)i;
                            pat->resolvedType = etype;
                            return;
                        }
                    }
                }
            }
            pat->resolvedType = subjType;
            declareVar(bp->name, subjType, isMutable);
            break;
        }
        case Pattern::EnumPat: {
            auto* ep = static_cast<EnumPattern*>(pat);
            EnumType* etype = nullptr;

            // Try direct lookup
            auto it = enumTypes_.find(ep->enumName);
            if (it != enumTypes_.end()) {
                etype = it->second;
            }

            // If not found and subject is an EnumType, check if its base name matches
            if (!etype && subjType) {
                if (auto* subjEnum = dynamic_cast<EnumType*>(subjType)) {
                    std::string subjName(subjEnum->name_->str());
                    // Check if subjName starts with ep->enumName + "<"
                    if (subjName.substr(0, ep->enumName.size()) == ep->enumName &&
                        (subjName.size() == ep->enumName.size() ||
                         subjName[ep->enumName.size()] == '<')) {
                        etype = subjEnum;
                    }
                }
            }

            // If still not found, check if it's a template enum (error)
            if (!etype) {
                auto tmplIt = templateEnums_.find(ep->enumName);
                if (tmplIt != templateEnums_.end()) {
                    // Use subject type if available
                    if (subjType && dynamic_cast<EnumType*>(subjType)) {
                        etype = static_cast<EnumType*>(subjType);
                    } else {
                        error(pat->loc, "Cannot determine concrete type for template enum '" +
                              ep->enumName + "' in pattern");
                        return;
                    }
                } else {
                    error(pat->loc, "Unknown enum type '" + ep->enumName + "' in pattern");
                    return;
                }
            }

            if (subjType && subjType != etype) {
                error(pat->loc, "Pattern enum type '" + ep->enumName +
                      "' doesn't match subject type");
            }

            bool found = false;
            for (size_t i = 0; i < etype->cases_.size(); ++i) {
                if (etype->cases_[i].name->str() == ep->caseName) {
                    found = true;
                    Type* caseType = etype->cases_[i].type;
                    if (ep->innerPattern) {
                        if (caseType == compiler_.voidType()) {
                            error(pat->loc, "Enum case '" + ep->caseName + "' has no data");
                        } else {
                            checkPattern(ep->innerPattern.get(), caseType, isMutable, inMatch);
                        }
                    }
                    break;
                }
            }
            if (!found) {
                error(pat->loc, "Unknown case '" + ep->caseName +
                      "' in enum '" + ep->enumName + "'");
            }
            pat->resolvedType = etype;
            break;
        }
        case Pattern::StructPat: {
            auto* sp = static_cast<StructPattern*>(pat);
            StructType* stype = nullptr;

            // Try direct lookup
            auto it = structTypes_.find(sp->structName);
            if (it != structTypes_.end()) {
                stype = it->second;
            }

            // If not found and subject is a StructType, check if its base name matches
            if (!stype && subjType) {
                if (auto* subjStruct = dynamic_cast<StructType*>(subjType)) {
                    std::string subjName(subjStruct->name_->str());
                    if (subjName.substr(0, sp->structName.size()) == sp->structName &&
                        (subjName.size() == sp->structName.size() ||
                         subjName[sp->structName.size()] == '<')) {
                        stype = subjStruct;
                    }
                }
            }

            // If still not found, check if it's a template struct
            if (!stype) {
                auto tmplIt = templateStructs_.find(sp->structName);
                if (tmplIt != templateStructs_.end()) {
                    if (subjType && dynamic_cast<StructType*>(subjType)) {
                        stype = static_cast<StructType*>(subjType);
                    } else {
                        error(pat->loc, "Cannot determine concrete type for template struct '" +
                              sp->structName + "' in pattern");
                        return;
                    }
                } else {
                    error(pat->loc, "Unknown struct type '" + sp->structName + "' in pattern");
                    return;
                }
            }

            if (subjType && subjType != stype) {
                error(pat->loc, "Pattern struct type '" + sp->structName +
                      "' doesn't match subject type");
            }

            for (auto& field : sp->fields) {
                bool found = false;
                for (size_t j = 0; j < stype->fields_.size(); ++j) {
                    if (stype->fields_[j].name->str() == field.name) {
                        found = true;
                        checkPattern(field.pattern.get(), stype->fields_[j].type, isMutable, inMatch);
                        break;
                    }
                }
                if (!found) {
                    error(field.loc, "Unknown field '" + field.name +
                          "' in struct '" + sp->structName + "'");
                }
            }
            pat->resolvedType = stype;
            break;
        }
        case Pattern::TuplePat: {
            auto* tp = static_cast<TuplePattern*>(pat);

            // Tuple struct pattern: Name(pat, pat, ...)
            if (!tp->structName.empty()) {
                auto* stype = dynamic_cast<StructType*>(subjType);
                if (!stype) {
                    // Try looking up the struct type by name
                    auto sIt = structTypes_.find(tp->structName);
                    if (sIt != structTypes_.end()) {
                        stype = sIt->second;
                        // Verify the subject type matches
                        if (subjType != stype) {
                            error(pat->loc, "Tuple struct pattern '" + tp->structName +
                                  "' does not match subject type");
                            return;
                        }
                    } else if (inMatch) {
                        // Try as unqualified enum case with data
                        if (auto* etype = dynamic_cast<EnumType*>(subjType)) {
                            for (size_t i = 0; i < etype->cases_.size(); ++i) {
                                if (etype->cases_[i].name->str() == tp->structName) {
                                    Type* caseType = etype->cases_[i].type;
                                    pat->enumCaseIndex = (int)i;
                                    pat->enumCaseDataType = caseType;
                                    pat->resolvedType = etype;
                                    if (caseType == compiler_.voidType()) {
                                        if (!tp->elements.empty()) {
                                            error(pat->loc, "Enum case '" + tp->structName + "' has no data");
                                        }
                                    } else if (tp->elements.size() == 1) {
                                        checkPattern(tp->elements[0].get(), caseType, isMutable, inMatch);
                                    } else {
                                        auto* ttype = dynamic_cast<TupleType*>(caseType);
                                        if (!ttype) {
                                            error(pat->loc, "Cannot destructure non-tuple enum case data with multiple patterns");
                                        } else if (tp->elements.size() != ttype->fields_.size()) {
                                            error(pat->loc, "Enum case pattern has " + std::to_string(tp->elements.size()) +
                                                  " elements but case data has " + std::to_string(ttype->fields_.size()) + " fields");
                                        } else {
                                            for (size_t j = 0; j < tp->elements.size(); ++j) {
                                                checkPattern(tp->elements[j].get(), ttype->fields_[j], isMutable, inMatch);
                                            }
                                        }
                                    }
                                    return;
                                }
                            }
                        }
                        error(pat->loc, "Unknown tuple struct '" + tp->structName + "'");
                        return;
                    } else {
                        error(pat->loc, "Unknown tuple struct '" + tp->structName + "'");
                        return;
                    }
                }
                if (!stype->isTupleStruct_) {
                    error(pat->loc, "Struct '" + tp->structName + "' is not a tuple struct");
                    return;
                }
                if (tp->hasRest) {
                    if (tp->elements.size() > stype->fields_.size()) {
                        error(pat->loc, "Tuple struct pattern has " + std::to_string(tp->elements.size()) +
                              " fixed elements but struct has " + std::to_string(stype->fields_.size()) + " fields");
                    }
                } else {
                    if (tp->elements.size() != stype->fields_.size()) {
                        error(pat->loc, "Tuple struct pattern has " + std::to_string(tp->elements.size()) +
                              " elements but struct has " + std::to_string(stype->fields_.size()) + " fields");
                    }
                }
                for (size_t i = 0; i < tp->elements.size() && i < stype->fields_.size(); ++i) {
                    checkPattern(tp->elements[i].get(), stype->fields_[i].type, isMutable, inMatch);
                }
                if (tp->hasRest && !tp->restName.empty()) {
                    Vec<Type*> restFields(rt::STLAllocator<Type*>(nullptr));
                    for (size_t i = tp->elements.size(); i < stype->fields_.size(); ++i) {
                        restFields.push_back(stype->fields_[i].type);
                    }
                    TupleType* restType = compiler_.tupleType(restFields);
                    declareVar(tp->restName, restType, isMutable);
                }
                pat->resolvedType = stype;
                break;
            }

            auto* ttype = dynamic_cast<TupleType*>(subjType);
            if (!ttype) {
                error(pat->loc, "Tuple pattern used on non-tuple type");
                return;
            }
            if (tp->hasRest) {
                if (tp->elements.size() > ttype->fields_.size()) {
                    error(pat->loc, "Tuple pattern has " + std::to_string(tp->elements.size()) +
                          " fixed elements but subject has only " + std::to_string(ttype->fields_.size()));
                }
            } else {
                if (tp->elements.size() != ttype->fields_.size()) {
                    error(pat->loc, "Tuple pattern has " + std::to_string(tp->elements.size()) +
                          " elements but subject has " + std::to_string(ttype->fields_.size()));
                }
            }
            for (size_t i = 0; i < tp->elements.size() && i < ttype->fields_.size(); ++i) {
                checkPattern(tp->elements[i].get(), ttype->fields_[i], isMutable, inMatch);
            }
            // Rest binding gets a tuple of the remaining fields
            if (tp->hasRest && !tp->restName.empty()) {
                Vec<Type*> restFields(rt::STLAllocator<Type*>(nullptr));
                for (size_t i = tp->elements.size(); i < ttype->fields_.size(); ++i) {
                    restFields.push_back(ttype->fields_[i]);
                }
                TupleType* restType = compiler_.tupleType(restFields);
                declareVar(tp->restName, restType, isMutable);
            }
            pat->resolvedType = ttype;
            break;
        }
        case Pattern::ArrayPat: {
            auto* ap = static_cast<ArrayPattern*>(pat);
            auto* atype = dynamic_cast<ArrayType*>(subjType);
            if (!atype) {
                error(pat->loc, "Array pattern used on non-array type");
                return;
            }
            Type* elemType = atype->elemType_;
            // Check each fixed element pattern against the array's element type
            for (size_t i = 0; i < ap->elements.size(); ++i) {
                checkPattern(ap->elements[i].get(), elemType, isMutable, inMatch);
            }
            // Rest binding gets the same array type
            if (ap->hasRest && !ap->restName.empty()) {
                declareVar(ap->restName, subjType, isMutable);
            }
            pat->resolvedType = atype;
            break;
        }
        case Pattern::GuardedPat: {
            auto* gp = static_cast<GuardedPattern*>(pat);
            checkPattern(gp->pattern.get(), subjType, isMutable, inMatch);
            Type* guardType = inferExpr(static_cast<Expr*>(gp->guard.get()));
            if (guardType && guardType != compiler_.boolType() && guardType != compiler_.intType()) {
                error(gp->guard->loc, "Guard expression must be bool");
            }
            pat->resolvedType = subjType;
            break;
        }
        case Pattern::ConsPat: {
            auto* cp = static_cast<ConsPattern*>(pat);
            auto* ltype = dynamic_cast<ListType*>(subjType);
            if (!ltype) {
                error(pat->loc, "Cons pattern used on non-list type");
                return;
            }
            // Head pattern matches the element type
            checkPattern(cp->head.get(), ltype->elemType_, isMutable, inMatch);
            // Tail pattern matches the list type
            checkPattern(cp->tail.get(), subjType, isMutable, inMatch);
            pat->resolvedType = subjType;
            break;
        }
        case Pattern::TypeTestPat: {
            auto* tp = static_cast<TypeTestPattern*>(pat);
            if (subjType && !dynamic_cast<AnyType*>(subjType)) {
                error(pat->loc, "Type-test pattern can only match on values of type Any");
            }
            // Resolve the target type from the stored type expression
            Type* targetType = resolveTypeExpr(tp->typeExpr.get());
            tp->resolvedTargetType = targetType;
            tp->resolvedType = targetType;
            // Declare the binding with the unwrapped type
            declareVar(tp->bindingName, targetType, isMutable);
            break;
        }
    }
}

void TypeChecker::checkReturnStmt(ReturnStmtNode* stmt) {
    // When inferring return type, collect types instead of validating
    if (inferringReturnType_) {
        if (stmt->value) {
            Type* valType = inferExpr(static_cast<Expr*>(stmt->value.get()));
            if (inferredReturnType_ && valType && !typesEqual(inferredReturnType_, valType)) {
                if (isNumeric(inferredReturnType_) && isNumeric(valType)) {
                    inferredReturnType_ = commonNumericType(inferredReturnType_, valType);
                } else {
                    error(stmt->loc, "Inconsistent return types in inferred function");
                }
            } else if (valType) {
                inferredReturnType_ = valType;
            }
        }
        // void return in inferred function — inferredReturnType_ stays as-is
        return;
    }

    if (stmt->value) {
        Type* valType = inferExpr(static_cast<Expr*>(stmt->value.get()), currentReturnType_);
        if (currentReturnType_ && valType && !typesEqual(currentReturnType_, valType)) {
            if (currentReturnType_ == compiler_.floatType() && valType == compiler_.intType()) {
                // promotion OK
            } else {
                error(stmt->loc, "Return type mismatch");
            }
        }
    } else {
        if (currentReturnType_ && currentReturnType_ != compiler_.voidType()) {
            error(stmt->loc, "Non-void function must return a value");
        }
    }
}

void TypeChecker::checkAssignStmt(AssignStmtNode* stmt) {
    // Dynamic scope variable assignment: `name = expr;
    if (stmt->isDynamic) {
        auto* dvi = compiler_.lookupDynVar(stmt->target);
        if (!dvi) {
            error(stmt->loc, "Undeclared dynamic variable '`" + stmt->target + "'");
            return;
        }
        Type* valType = inferExpr(static_cast<Expr*>(stmt->value.get()), dvi->type);
        if (dvi->type && valType && !typesEqual(dvi->type, valType)) {
            if (dvi->type == compiler_.floatType() && valType == compiler_.intType()) {
                // promotion OK
            } else {
                error(stmt->loc, "Type mismatch in assignment to dynamic variable '`" + stmt->target + "'");
            }
        }
        return;
    }

    VarInfo* var = lookupVar(stmt->target);
    if (!var) {
        error(stmt->loc, "Undeclared variable '" + stmt->target + "'");
        return;
    }
    if (!var->isMutable) {
        error(stmt->loc, "Cannot assign to immutable variable '" + stmt->target + "'");
        return;
    }

    Type* valType = inferExpr(static_cast<Expr*>(stmt->value.get()), var->type);
    if (var->type && valType && !typesEqual(var->type, valType)) {
        if (var->type == compiler_.floatType() && valType == compiler_.intType()) {
            // promotion OK
        } else {
            error(stmt->loc, "Type mismatch in assignment to '" + stmt->target + "'");
        }
    }
}

void TypeChecker::checkIndexAssignStmt(IndexAssignStmtNode* stmt) {
    // Already rewritten to put!(obj, idx, v) by an earlier visit: object/
    // index/value have been moved into the call, so only re-infer that.
    if (stmt->putRewrite) {
        inferExpr(static_cast<Expr*>(stmt->putRewrite.get()));
        return;
    }

    Type* objType = inferExpr(static_cast<Expr*>(stmt->object.get()));
    if (!objType) return;

    if (auto* at = dynamic_cast<ArrayType*>(objType)) {
        // Array: index must be Int (Phase 1: only scalar Int indices; auto-mapping
        // and array-of-indices writes are not supported yet for index-assign).
        Type* idxType = inferExpr(static_cast<Expr*>(stmt->index.get()), compiler_.intType());
        if (idxType && idxType != compiler_.intType()) {
            error(stmt->loc, "Array index in assignment must be Int");
        }
        Type* valType = inferExpr(static_cast<Expr*>(stmt->value.get()), at->elemType_);
        if (at->elemType_ && valType && !typesEqual(at->elemType_, valType)) {
            if (at->elemType_ == compiler_.floatType() && valType == compiler_.intType()) {
                // Int -> Float promotion OK
            } else {
                error(stmt->loc, "Type mismatch: cannot assign value to array element");
            }
        }
        stmt->containerType = at;
        return;
    }

    if (auto* mt = dynamic_cast<MapType*>(objType)) {
        Type* idxType = inferExpr(static_cast<Expr*>(stmt->index.get()), mt->keyType_);
        if (mt->keyType_ && idxType && !typesEqual(mt->keyType_, idxType)) {
            error(stmt->loc, "Type mismatch: map key in assignment");
        }
        Type* valType = inferExpr(static_cast<Expr*>(stmt->value.get()), mt->valueType_);
        if (mt->valueType_ && valType && !typesEqual(mt->valueType_, valType)) {
            if (mt->valueType_ == compiler_.floatType() && valType == compiler_.intType()) {
                // promotion OK
            } else {
                error(stmt->loc, "Type mismatch: map value in assignment");
            }
        }
        stmt->containerType = mt;
        return;
    }

    if (dynamic_cast<PersistentVectorType*>(objType) || dynamic_cast<PersistentMapType*>(objType)) {
        error(stmt->loc, "Cannot assign into an immutable persistent collection (" +
              std::string(objType->str().data(), objType->str().size()) +
              "); use put/push to produce a new collection");
        return;
    }

    // User-defined index assignment: mirror of the read-side `at` rewrite.
    // When the object type has no built-in index assignment but a user `put!`
    // function exists, rewrite obj[idx] = v into put!(obj, idx, v).
    if (hasUserFunction("put!")) {
        ExprList putArgs;
        putArgs.push_back(std::move(stmt->object));
        putArgs.push_back(std::move(stmt->index));
        putArgs.push_back(std::move(stmt->value));
        stmt->putRewrite = std::make_unique<CallExpr_>(stmt->loc,
            std::make_unique<IdentifierExpr>(stmt->loc, "put!"), std::move(putArgs));
        inferExpr(static_cast<Expr*>(stmt->putRewrite.get()));
        return;
    }

    error(stmt->loc, "Left side of indexed assignment must be an Array or Map");
}

void TypeChecker::checkExprStmt(ExprStmtNode* stmt) {
    Type* ctx = (stmt->isTrailing && currentReturnType_) ? currentReturnType_ : nullptr;
    inferExpr(static_cast<Expr*>(stmt->expr.get()), ctx);
}

} // namespace ts
