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
//  type_checker_exprs.cpp
//  lang
//
//  Type checker -- identifier, lambda, and operator inference
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

Type* TypeChecker::inferIdentifier(IdentifierExpr* expr) {
    VarInfo* var = lookupVar(expr->name);
    if (var) {
        return var->type;
    }

    // Check if it's a function name — return a FunctionType for function-as-value
    auto it = functions_.find(expr->name);
    if (it != functions_.end() && !it->second.empty()) {
        // Collect concrete (non-template) overloads
        std::vector<FuncInfo*> concrete;
        for (auto& fi : it->second) {
            if (!fi.isTemplate) concrete.push_back(&fi);
        }

        if (concrete.size() == 1) {
            FuncInfo* fi = concrete[0];
            // Demand-driven inference if return type not yet known
            if (fi->returnType == nullptr && fi->declNode) {
                inferFunctionReturnType(fi->declNode, fi);
            }
            Type* ret = fi->returnType ? fi->returnType : compiler_.voidType();
            // Build arg types vector
            TypeVec argTypes(rt::STLAllocator<Type*>(nullptr));
            for (Type* pt : fi->paramTypes) argTypes.push_back(pt);
            // Create a LambdaType with 0 free vars for wrapping at runtime
            TypeVec emptyFreeVars(rt::STLAllocator<Type*>(nullptr));
            auto* lambdaType = new LambdaType(TypeVec(argTypes), ret, std::move(emptyFreeVars));
            // Annotate the IdentifierExpr so codegen can emit op_func_ref
            expr->resolvedFuncGlobalIndex = (i32)fi->globalIndex;
            expr->funcRefLambdaType = lambdaType;
            // Return interned FunctionType for type equality
            return compiler_.functionType(argTypes, ret);
        }

        if (concrete.empty()) {
            error(expr->loc, "Template function '" + expr->name +
                  "' cannot be used as a value without type arguments");
        } else {
            error(expr->loc, "Ambiguous: function '" + expr->name +
                  "' has multiple overloads and cannot be used as a value without a call");
        }
        return compiler_.intType();
    }

    // Collect candidate names for "did you mean?" suggestion
    std::vector<std::string> candidates;
    for (auto it2 = scopes_.rbegin(); it2 != scopes_.rend(); ++it2) {
        for (auto& [name, _] : *it2) candidates.push_back(name);
    }
    for (auto& [name, _] : globalVars_) candidates.push_back(name);
    for (auto& [name, _] : functions_) candidates.push_back(name);

    std::string msg = "Undeclared identifier '" + expr->name + "'";
    std::string match = findClosestMatch(expr->name, candidates);
    // Also check module exports, matching against the bare export name
    // but suggesting the qualified form (e.g. "math_utils.square")
    for (auto& [alias, mod] : importedModules_) {
        std::vector<std::string> exportNames;
        for (auto& [exportName, _] : mod->exports) {
            exportNames.push_back(exportName);
        }
        std::string modMatch = findClosestMatch(expr->name, exportNames);
        if (!modMatch.empty()) {
            int modDist = editDistance(expr->name, modMatch);
            int curDist = match.empty() ? INT_MAX : editDistance(expr->name, match);
            if (modDist < curDist) {
                match = alias + "." + modMatch;
            }
        }
    }
    if (!match.empty()) {
        msg += "\nDid you mean '" + match + "'?";
    }
    error(expr->loc, msg);
    return compiler_.intType();
}

Type* TypeChecker::inferLambdaExpr(LambdaExprNode* expr) {
    // Template lambda: has type params — handle separately
    if (!expr->typeParams.empty()) {
        return inferTemplateLambdaExpr(expr);
    }

    // Check if this lambda has untyped params with no resolved types — defer inference
    bool hasUntypedParam = false;
    for (auto& param : expr->params) {
        if (!param.typeExpr && !param.resolvedType) {
            hasUntypedParam = true;
            break;
        }
    }
    if (hasUntypedParam) {
        // Deferred inference — types will be resolved when used in a call context
        return nullptr;
    }

    // Resolve param types (use pre-resolved type from backward inference if available)
    std::vector<Type*> paramTypes;
    for (auto& param : expr->params) {
        Type* t = param.resolvedType ? param.resolvedType : resolveTypeExpr(param.typeExpr.get());
        if (!t) {
            error(expr->loc, "Cannot infer type for lambda parameter '" + param.name + "'");
            t = compiler_.intType();
        }
        paramTypes.push_back(t);
    }

    // Resolve return type (omitted = infer from body)
    Type* retType;
    if (!expr->returnType) {
        retType = nullptr;  // infer from body
    } else {
        retType = resolveTypeExpr(expr->returnType.get());
    }
    bool inferLambdaReturn = (retType == nullptr);

    // Save state
    int savedBoundary = lambdaBoundary_;
    auto* savedCaptures = currentCaptures_;
    Type* savedReturnType = currentReturnType_;
    bool savedInferring = inferringReturnType_;
    Type* savedInferred = inferredReturnType_;
    bool savedInCoro = inCoroutineBody_;
    Type* savedYieldType = currentYieldType_;

    // Set up for capture detection
    currentCaptures_ = &expr->captures;

    // Handle coroutine lambdas: declared return type is yield type,
    // wrapped in Coroutine<T> (same convention as coro fn declarations)
    if (expr->isCoroutine && retType) {
        inCoroutineBody_ = true;
        currentYieldType_ = retType;
        retType = compiler_.coroutineType(retType);
        currentReturnType_ = compiler_.voidType();
    } else if (inferLambdaReturn) {
        inferringReturnType_ = true;
        currentReturnType_ = nullptr;
        inferredReturnType_ = nullptr;
    } else {
        currentReturnType_ = retType;
    }

    // Push scope and set boundary
    pushScope();
    lambdaBoundary_ = (int)scopes_.size() - 1;

    // Declare params
    for (size_t i = 0; i < expr->params.size(); ++i) {
        declareVar(expr->params[i].name, paramTypes[i], false);
    }

    // Check body
    checkNode(expr->body.get());

    // If inferring, extract the return type
    if (inferLambdaReturn) {
        Type* trailingType = getBlockTrailingType(expr->body.get());
        if (trailingType && inferredReturnType_) {
            if (typesEqual(trailingType, inferredReturnType_)) {
                retType = trailingType;
            } else if (isNumeric(trailingType) && isNumeric(inferredReturnType_)) {
                retType = commonNumericType(trailingType, inferredReturnType_);
            } else {
                error(expr->loc, "Inconsistent return types in inferred lambda");
                retType = trailingType;
            }
        } else if (trailingType) {
            retType = trailingType;
        } else if (inferredReturnType_) {
            retType = inferredReturnType_;
        } else {
            retType = compiler_.voidType();
        }
    }

    // Pop scope and restore state
    popScope();
    lambdaBoundary_ = savedBoundary;
    currentCaptures_ = savedCaptures;
    currentReturnType_ = savedReturnType;
    inferringReturnType_ = savedInferring;
    inferredReturnType_ = savedInferred;
    inCoroutineBody_ = savedInCoro;
    currentYieldType_ = savedYieldType;

    // Propagate captures upward: if we're inside a parent lambda, any variable
    // that the nested lambda captured from beyond the parent's boundary must also
    // be captured by the parent (so it's available at the nested lambda's definition site).
    if (lambdaBoundary_ >= 0 && currentCaptures_) {
        for (auto& cap : expr->captures) {
            for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
                auto it = scopes_[i].find(cap.name);
                if (it != scopes_[i].end()) {
                    if (i < lambdaBoundary_) {
                        // Variable is from beyond parent's boundary — parent must capture it too
                        bool found = false;
                        for (auto& pcap : *currentCaptures_) {
                            if (pcap.name == cap.name) { found = true; break; }
                        }
                        if (!found) {
                            currentCaptures_->push_back({cap.name, cap.type});
                        }
                    }
                    break;
                }
            }
        }
    }

    // Build LambdaType (runtime type with free variable info for GC)
    TypeVec argTypes(rt::STLAllocator<Type*>(nullptr));
    for (Type* t : paramTypes) argTypes.push_back(t);

    TypeVec freeVarTypes(rt::STLAllocator<Type*>(nullptr));
    for (auto& cap : expr->captures) {
        freeVarTypes.push_back(cap.type);
    }

    auto* lambdaType = new LambdaType(std::move(argTypes), retType, std::move(freeVarTypes));
    expr->lambdaType = lambdaType;

    // Return interned FunctionType for type equality (signature-based interning)
    return compiler_.functionType(lambdaType->argTypes_, retType);
}

// Discover captures for a template lambda without full type-checking the body.
// Walks the AST looking for identifier references to variables from outer scopes.
void TypeChecker::discoverCaptures(LambdaExprNode* expr) {
    // Save state
    int savedBoundary = lambdaBoundary_;
    auto* savedCaptures = currentCaptures_;

    currentCaptures_ = &expr->captures;
    expr->captures.clear();

    // Push scope and set boundary
    pushScope();
    lambdaBoundary_ = (int)scopes_.size() - 1;

    // Declare params with placeholder types so they don't get captured
    for (auto& param : expr->params) {
        declareVar(param.name, compiler_.intType(), false);  // placeholder type
    }

    // Walk the body to discover captures using a recursive lambda
    std::function<void(ASTNode*)> walk = [&](ASTNode* node) {
        if (!node) return;

        // If it's an identifier, try to look it up — the lambda boundary mechanism
        // in lookupVar will detect cross-boundary references and add captures
        if (node->kind == ASTNode::Identifier) {
            auto* ident = static_cast<IdentifierExpr*>(node);
            lookupVar(ident->name);
            return;
        }

        // Recursively walk child nodes based on kind
        switch (node->kind) {
            case ASTNode::Block: {
                auto* block = static_cast<BlockStmt*>(node);
                for (auto& stmt : block->stmts) walk(stmt.get());
                break;
            }
            case ASTNode::ExprStmt: {
                auto* es = static_cast<ExprStmtNode*>(node);
                walk(es->expr.get());
                break;
            }
            case ASTNode::CallExpr: {
                auto* call = static_cast<CallExpr_*>(node);
                walk(call->callee.get());
                for (auto& arg : call->args) walk(arg.get());
                break;
            }
            case ASTNode::BinaryOp: {
                auto* binop = static_cast<BinaryOpExpr*>(node);
                walk(binop->left.get());
                walk(binop->right.get());
                break;
            }
            case ASTNode::UnaryOp: {
                auto* unop = static_cast<UnaryOpExpr*>(node);
                walk(unop->operand.get());
                break;
            }
            case ASTNode::IfExpr: {
                auto* ife = static_cast<IfExprNode*>(node);
                walk(ife->condition.get());
                walk(ife->thenBranch.get());
                walk(ife->elseBranch.get());
                break;
            }
            case ASTNode::BlockExpr: {
                auto* be = static_cast<BlockExprNode*>(node);
                walk(be->body.get());
                break;
            }
            case ASTNode::IfStmt: {
                auto* ifs = static_cast<IfStmtNode*>(node);
                walk(ifs->condition.get());
                walk(ifs->thenBranch.get());
                walk(ifs->elseBranch.get());
                break;
            }
            case ASTNode::LambdaExpr: {
                auto* lambda = static_cast<LambdaExprNode*>(node);
                // Don't descend into nested lambdas for capture discovery —
                // they'll do their own capture discovery
                // But we do need to check the body for references to our scope
                // Actually, for correctness we should walk nested lambda bodies too
                walk(lambda->body.get());
                break;
            }
            case ASTNode::LetDecl: {
                auto* let = static_cast<LetDeclNode*>(node);
                walk(let->init.get());
                break;
            }
            case ASTNode::VarDecl: {
                auto* var = static_cast<VarDeclNode*>(node);
                walk(var->init.get());
                break;
            }
            case ASTNode::ReturnStmt: {
                auto* ret = static_cast<ReturnStmtNode*>(node);
                walk(ret->value.get());
                break;
            }
            case ASTNode::WhileStmt: {
                auto* ws = static_cast<WhileStmtNode*>(node);
                walk(ws->condition.get());
                walk(ws->body.get());
                break;
            }
            case ASTNode::ForStmt: {
                auto* fs = static_cast<ForStmtNode*>(node);
                walk(fs->iterable.get());
                walk(fs->body.get());
                break;
            }
            case ASTNode::IndexExpr: {
                auto* idx = static_cast<IndexExpr_*>(node);
                walk(idx->object.get());
                walk(idx->index.get());
                break;
            }
            case ASTNode::FieldExpr: {
                auto* fa = static_cast<FieldExpr_*>(node);
                walk(fa->object.get());
                break;
            }
            case ASTNode::TupleLiteral: {
                auto* tl = static_cast<TupleLiteralExpr*>(node);
                for (auto& elem : tl->elements) walk(elem.get());
                break;
            }
            case ASTNode::ArrayLiteral: {
                auto* al = static_cast<ArrayLiteralExpr*>(node);
                for (auto& elem : al->elements) walk(elem.get());
                break;
            }
            case ASTNode::AssignStmt: {
                auto* as = static_cast<AssignStmtNode*>(node);
                // target is a string name — trigger capture lookup
                lookupVar(as->target);
                walk(as->value.get());
                break;
            }
            case ASTNode::AutoMap: {
                auto* am = static_cast<AutoMapExpr*>(node);
                walk(am->inner.get());
                break;
            }
            case ASTNode::AsTypeExpr: {
                auto* ate = static_cast<AsTypeExprNode*>(node);
                walk(ate->subject.get());
                break;
            }
            default:
                break;  // Other node kinds don't contain identifiers we need
        }
    };

    walk(expr->body.get());

    // Pop scope and restore
    popScope();
    lambdaBoundary_ = savedBoundary;
    currentCaptures_ = savedCaptures;
}

Type* TypeChecker::inferTemplateLambdaExpr(LambdaExprNode* expr) {
    // Discover captures by walking the AST (doesn't type-check the body)
    discoverCaptures(expr);

    // Build free variable types from captures
    TypeVec freeVarTypes(rt::STLAllocator<Type*>(nullptr));
    for (auto& cap : expr->captures) {
        freeVarTypes.push_back(cap.type);
    }

    // Convert WhereConstraints to ConstraintEntry
    std::vector<TemplateLambdaType::ConstraintEntry> constraints;
    for (auto& wc : expr->whereConstraints) {
        constraints.push_back({wc.typeParam, wc.constraintName});
    }

    // Create TemplateLambdaType
    auto* tmplType = new TemplateLambdaType(
        expr, std::move(freeVarTypes),
        expr->typeParams, std::move(constraints));
    expr->templateLambdaType = tmplType;

    return tmplType;
}

LambdaType* TypeChecker::monomorphizeTemplateLambda(TemplateLambdaType* tmplType,
                                                     const std::vector<Type*>& argTypes,
                                                     SourceRange loc) {
    LambdaExprNode* expr = tmplType->astNode_;

    // Unify arg types against param type expressions to bind type params
    std::unordered_map<std::string, Type*> bindings;
    if (argTypes.size() != expr->params.size()) {
        error(loc, "Template lambda expects " + std::to_string(expr->params.size()) +
              " arguments, got " + std::to_string(argTypes.size()));
        return nullptr;
    }

    for (size_t i = 0; i < expr->params.size(); ++i) {
        if (expr->params[i].typeExpr) {
            if (!unifyTypeExpr(expr->params[i].typeExpr.get(), argTypes[i],
                               tmplType->typeParams_, bindings)) {
                error(loc, "Cannot unify argument " + std::to_string(i + 1) +
                      " type '" + std::string(argTypes[i]->str().data(), argTypes[i]->str().size()) + "' with template parameter");
                return nullptr;
            }
        } else {
            // No type expr on param — just bind directly if it's a type param name
            // For now, this means the param type is exactly the arg type
            // (untyped params in a template lambda are treated as concrete)
        }
    }

    // Verify all type params bound
    for (auto& tp : tmplType->typeParams_) {
        if (bindings.find(tp) == bindings.end()) {
            error(loc, "Cannot infer type parameter '" + tp + "' from arguments");
            return nullptr;
        }
    }

    // Build type args vector for cache lookup
    std::vector<Type*> typeArgs;
    for (auto& tp : tmplType->typeParams_) {
        typeArgs.push_back(bindings[tp]);
    }

    // Check mono cache
    if (auto* cached = tmplType->findMono(typeArgs)) {
        return cached;
    }

    // Check where constraints
    if (!expr->whereConstraints.empty()) {
        if (!checkWhereConstraints(expr->whereConstraints, bindings, "template lambda", true)) {
            return nullptr;
        }
    }

    // Save state
    auto savedBindings = typeParamBindings_;
    typeParamBindings_ = bindings;

    // Resolve param types using bindings
    std::vector<Type*> paramTypes;
    for (auto& param : expr->params) {
        if (param.typeExpr) {
            Type* t = resolveTypeExpr(param.typeExpr.get());
            paramTypes.push_back(t);
        } else {
            // Param without type expr in template — shouldn't happen if correctly parsed
            paramTypes.push_back(compiler_.intType());
        }
    }

    // Resolve return type
    Type* retType = nullptr;
    if (expr->returnType) {
        retType = resolveTypeExpr(expr->returnType.get());
    }
    bool inferReturn = (retType == nullptr);

    // Save more state
    int savedBoundary = lambdaBoundary_;
    auto* savedCaptures = currentCaptures_;
    Type* savedReturnType = currentReturnType_;
    bool savedInferring = inferringReturnType_;
    Type* savedInferred = inferredReturnType_;

    // We don't want to modify the original captures — they were already discovered.
    // But we need to set up for type-checking the body.
    // Create a temporary captures vector for this instantiation (captures are same across instances).
    std::vector<LambdaExprNode::CapturedVar> tmpCaptures = expr->captures;
    currentCaptures_ = &tmpCaptures;

    if (inferReturn) {
        inferringReturnType_ = true;
        currentReturnType_ = nullptr;
        inferredReturnType_ = nullptr;
    } else {
        currentReturnType_ = retType;
    }

    // Push scope and set boundary
    pushScope();
    lambdaBoundary_ = (int)scopes_.size() - 1;

    // Declare params with concrete types
    for (size_t i = 0; i < expr->params.size(); ++i) {
        declareVar(expr->params[i].name, paramTypes[i], false);
    }

    // Declare captured variables so they're accessible during type-checking
    // (the outer scope where they originated may no longer be active)
    for (auto& cap : expr->captures) {
        declareVar(cap.name, cap.type, false);
    }

    // Check body
    checkNode(expr->body.get());

    // Extract return type if inferring
    if (inferReturn) {
        Type* trailingType = getBlockTrailingType(expr->body.get());
        if (trailingType && inferredReturnType_) {
            if (typesEqual(trailingType, inferredReturnType_)) {
                retType = trailingType;
            } else if (isNumeric(trailingType) && isNumeric(inferredReturnType_)) {
                retType = commonNumericType(trailingType, inferredReturnType_);
            } else {
                error(loc, "Inconsistent return types in template lambda instantiation");
                retType = trailingType;
            }
        } else if (trailingType) {
            retType = trailingType;
        } else if (inferredReturnType_) {
            retType = inferredReturnType_;
        } else {
            retType = compiler_.voidType();
        }
    }

    // Pop scope and restore state
    popScope();
    lambdaBoundary_ = savedBoundary;
    currentCaptures_ = savedCaptures;
    currentReturnType_ = savedReturnType;
    inferringReturnType_ = savedInferring;
    inferredReturnType_ = savedInferred;
    typeParamBindings_ = savedBindings;

    // Build the concrete LambdaType
    TypeVec argTypesVec(rt::STLAllocator<Type*>(nullptr));
    for (Type* t : paramTypes) argTypesVec.push_back(t);

    // Use the same captures as the template lambda (same free var types)
    TypeVec freeVarTypes(rt::STLAllocator<Type*>(nullptr));
    for (auto& cap : expr->captures) {
        freeVarTypes.push_back(cap.type);
    }

    auto* lambdaType = new LambdaType(std::move(argTypesVec), retType, std::move(freeVarTypes));

    // Cache the monomorphization
    tmplType->addMono(typeArgs, lambdaType);

    return lambdaType;
}

// Map BinaryOpExpr::Op to operator function name for overload lookup
static const char* opToFuncName(BinaryOpExpr::Op op) {
    switch (op) {
        case BinaryOpExpr::Add:    return "+";
        case BinaryOpExpr::Sub:    return "-";
        case BinaryOpExpr::Mul:    return "*";
        case BinaryOpExpr::Div:    return "/";
        case BinaryOpExpr::Mod:    return "%";
        case BinaryOpExpr::Eq:     return "==";
        case BinaryOpExpr::Ne:     return "!=";
        case BinaryOpExpr::Lt:     return "<";
        case BinaryOpExpr::Le:     return "<=";
        case BinaryOpExpr::Gt:     return ">";
        case BinaryOpExpr::Ge:     return ">=";
        case BinaryOpExpr::BitAnd: return "&";
        case BinaryOpExpr::BitOr:  return "|";
        case BinaryOpExpr::BitXor: return "^";
        case BinaryOpExpr::ShiftL: return "<<";
        case BinaryOpExpr::ShiftR: return ">>";
        case BinaryOpExpr::UShiftR: return ">>>";
        case BinaryOpExpr::Concat: return "$";
        case BinaryOpExpr::Cons:      return "::";
        case BinaryOpExpr::LeftArrow:  return "<-";
        case BinaryOpExpr::RightArrow: return "->";
        default: return nullptr;
    }
}

// Map UnaryOpExpr::Op to operator function name for overload lookup
static const char* unaryOpToFuncName(UnaryOpExpr::Op op) {
    switch (op) {
        case UnaryOpExpr::Neg:    return "-";
        case UnaryOpExpr::Not:    return "!";
        case UnaryOpExpr::BitNot: return "~";
        default: return nullptr;
    }
}

Type* TypeChecker::inferBinaryOp(BinaryOpExpr* expr) {
    // Check for explicit @ on operands
    AutoMapArg leftAM = extractAutoMapAnnotation(static_cast<Expr*>(expr->left.get()));
    AutoMapArg rightAM = extractAutoMapAnnotation(static_cast<Expr*>(expr->right.get()));

    if (leftAM || rightAM) {
        // Infer full types (including array/list wrappers)
        Type* leftType = inferExpr(static_cast<Expr*>(expr->left.get()));
        Type* rightType = inferExpr(static_cast<Expr*>(expr->right.get()));
        if (!leftType || !rightType) return compiler_.intType();

        // Unwrap @-tagged operand types
        bool anyList = false;
        Type* unwrappedLeft = leftType;
        Type* unwrappedRight = rightType;
        if (leftAM) {
            unwrappedLeft = unwrapAutoMapLayers(leftType, leftAM.depth, leftAM.isList, expr->left->loc);
            if (leftAM.isList) anyList = true;
        }
        if (rightAM) {
            unwrappedRight = unwrapAutoMapLayers(rightType, rightAM.depth, rightAM.isList, expr->right->loc);
            if (rightAM.isList) anyList = true;
        }

        // Store annotations on the AST node
        expr->leftAutoMap = leftAM;
        expr->rightAutoMap = rightAM;

        // Try operator overload resolution with unwrapped types
        const char* opName = opToFuncName(expr->op);
        if (opName) {
            std::vector<Type*> argTypes = {unwrappedLeft, unwrappedRight};
            auto it = functions_.find(opName);
            if (it != functions_.end()) {
                FuncInfo* func = tryResolveOverload(opName, argTypes);
                if (func) {
                    checkRTSafety(func, opName, expr->loc);
                    if (func->returnType == nullptr && func->declNode) {
                        inferFunctionReturnType(func->declNode, func);
                    }
                    expr->resolvedFuncGlobalIndex = (i32)func->globalIndex;
                    expr->isBuiltinCall = func->isBuiltin; expr->builtinAcceptsInlineArgs = func->acceptsInlineArgs;
                    Type* scalarResult = func->returnType ? func->returnType : compiler_.intType();
                    return wrapAutoMapResult(scalarResult, leftAM, rightAM, anyList);
                }
            }
        }

        // Try built-in numeric ops with unwrapped types
        bool isDiv = (expr->op == BinaryOpExpr::Div);
        if (isNumeric(unwrappedLeft) && isNumeric(unwrappedRight)) {
            Type* scalarResult;
            switch (expr->op) {
                case BinaryOpExpr::Add:
                case BinaryOpExpr::Sub:
                case BinaryOpExpr::Mul:
                case BinaryOpExpr::Div:
                    scalarResult = commonNumericType(unwrappedLeft, unwrappedRight, isDiv);
                    break;
                case BinaryOpExpr::Mod:
                case BinaryOpExpr::IntDiv:
                    scalarResult = compiler_.intType();
                    break;
                case BinaryOpExpr::Eq:
                case BinaryOpExpr::Ne:
                case BinaryOpExpr::Lt:
                case BinaryOpExpr::Le:
                case BinaryOpExpr::Gt:
                case BinaryOpExpr::Ge:
                    scalarResult = compiler_.boolType();
                    break;
                case BinaryOpExpr::BitAnd:
                case BinaryOpExpr::BitOr:
                case BinaryOpExpr::BitXor:
                case BinaryOpExpr::ShiftL:
                case BinaryOpExpr::ShiftR:
                    scalarResult = compiler_.intType();
                    break;
                default:
                    scalarResult = commonNumericType(unwrappedLeft, unwrappedRight, isDiv);
                    break;
            }
            return wrapAutoMapResult(scalarResult, leftAM, rightAM, anyList);
        }

        // Non-numeric unwrapped types with @ — try generic binary op rules
        // (e.g. String $ String @, etc.)
        // For now, compute scalar result via the normal path
        // Fall through to the normal path but with unwrapped types isn't clean,
        // so just handle string concat specially
        if (expr->op == BinaryOpExpr::Concat) {
            if (unwrappedLeft == compiler_.stringType() && unwrappedRight == compiler_.stringType()) {
                return wrapAutoMapResult(compiler_.stringType(), leftAM, rightAM, anyList);
            }
        }

        error(expr->loc, "Cannot apply '@' auto-map to this operator with these types");
        return compiler_.intType();
    }

    Type* leftType = inferExpr(static_cast<Expr*>(expr->left.get()));
    Type* rightType = inferExpr(static_cast<Expr*>(expr->right.get()));

    if (!leftType && !rightType) return compiler_.intType();

    // Cons operator: head :: tail
    if (expr->op == BinaryOpExpr::Cons) {
        if (!leftType) {
            error(expr->loc, "Cannot infer type of cons head");
            return compiler_.intType();
        }
        // Right side is List[T] or nil (nullptr)
        if (!rightType) {
            // nil on right: list type comes from left
            return compiler_.listType(leftType);
        }
        auto* listT = dynamic_cast<ListType*>(rightType);
        if (!listT) {
            error(expr->right->loc, "Right side of '::' must be a List or nil");
            return compiler_.listType(leftType);
        }
        // Check left type matches list element type
        if (!typesEqual(leftType, listT->elemType_)) {
            if (isNumeric(leftType) && isNumeric(listT->elemType_)) {
                // Allow numeric promotion
            } else {
                error(expr->loc, "Cons head type doesn't match list element type");
            }
        }
        return rightType;
    }

    // Concat with nil: nil acts as empty list
    if (expr->op == BinaryOpExpr::Concat && (!leftType || !rightType)) {
        Type* knownType = leftType ? leftType : rightType;
        if (auto* listT = dynamic_cast<ListType*>(knownType)) {
            return listT;
        }
        // nil $ nil or non-list $ nil — not meaningful
        error(expr->loc, "'$' with nil requires a List on the other side");
        return compiler_.intType();
    }

    if (!leftType || !rightType) return compiler_.intType();

    // Clear stale resolution from previous template monomorphizations on this
    // shared AST node.  When the built-in numeric path matches, we must NOT let
    // a previous overload resolution (e.g. signal-domain operator) persist —
    // codegen checks resolvedFuncGlobalIndex to decide call vs direct opcode.
    expr->resolvedFuncGlobalIndex = -1;
    expr->isBuiltinCall = false;
    expr->leftAutoMap = {};
    expr->rightAutoMap = {};

    // Try built-in rules first
    switch (expr->op) {
        case BinaryOpExpr::Add:
            if (isNumeric(leftType) && isNumeric(rightType)) {
                return commonNumericType(leftType, rightType);
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::Concat:
            if (leftType == compiler_.stringType() && rightType == compiler_.stringType()) {
                return compiler_.stringType();
            }
            if (auto* arrL = dynamic_cast<ArrayType*>(leftType)) {
                if (auto* arrR = dynamic_cast<ArrayType*>(rightType)) {
                    if (typesEqual(arrL->elemType_, arrR->elemType_)) {
                        return leftType;
                    }
                    error(expr->loc, "'$' requires arrays with the same element type");
                    return leftType;
                }
            }
            if (auto* listL = dynamic_cast<ListType*>(leftType)) {
                if (auto* listR = dynamic_cast<ListType*>(rightType)) {
                    if (typesEqual(listL->elemType_, listR->elemType_)) {
                        return leftType;
                    }
                    error(expr->loc, "'$' requires lists with the same element type");
                    return leftType;
                }
            }
            if (auto* tupL = dynamic_cast<TupleType*>(leftType)) {
                if (auto* tupR = dynamic_cast<TupleType*>(rightType)) {
                    Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
                    for (Type* f : tupL->fields_) fields.push_back(f);
                    for (Type* f : tupR->fields_) fields.push_back(f);
                    return compiler_.tupleType(fields);
                }
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::Sub:
        case BinaryOpExpr::Mul:
            if (isNumeric(leftType) && isNumeric(rightType)) {
                return commonNumericType(leftType, rightType);
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::Div:
            if (isNumeric(leftType) && isNumeric(rightType)) {
                return commonNumericType(leftType, rightType, /*isDiv=*/true);
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::Mod:
            if ((leftType == compiler_.intType() || leftType == compiler_.boolType()) &&
                (rightType == compiler_.intType() || rightType == compiler_.boolType())) {
                return compiler_.intType();
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::IntDiv:
            if ((leftType == compiler_.intType() || leftType == compiler_.boolType()) &&
                (rightType == compiler_.intType() || rightType == compiler_.boolType())) {
                return compiler_.intType();
            }
            error(expr->loc, "'//' requires integer operands");
            return compiler_.intType();

        case BinaryOpExpr::Eq:
        case BinaryOpExpr::Ne:
            if (leftType == compiler_.stringType() && rightType == compiler_.stringType()) {
                return compiler_.boolType();
            }
            if (isNumeric(leftType) && isNumeric(rightType)) {
                // Check for implicit auto-mapping with Array/List
                auto* leftArr = dynamic_cast<ArrayType*>(leftType);
                auto* leftList = dynamic_cast<ListType*>(leftType);
                auto* rightArr = dynamic_cast<ArrayType*>(rightType);
                auto* rightList = dynamic_cast<ListType*>(rightType);
                // Auto-map when at least one is Array/List and types differ
                // (same-type arrays use structural equality via op_cmp_eq_obj)
                if ((leftArr || leftList || rightArr || rightList) && leftType != rightType) {
                    bool anyList = false;
                    if (leftArr) expr->leftAutoMap = AutoMapArg{1, 0, false};
                    else if (leftList) { expr->leftAutoMap = AutoMapArg{1, 0, true}; anyList = true; }
                    if (rightArr) expr->rightAutoMap = AutoMapArg{1, 0, false};
                    else if (rightList) { expr->rightAutoMap = AutoMapArg{1, 0, true}; anyList = true; }
                    return wrapAutoMapResult(compiler_.boolType(), expr->leftAutoMap, expr->rightAutoMap, anyList);
                }
                // Composite comparison for tuples (element-wise, different types)
                auto* leftTup = dynamic_cast<TupleType*>(leftType);
                auto* rightTup = dynamic_cast<TupleType*>(rightType);
                if ((leftTup || rightTup) && leftType != rightType) {
                    return comparisonResultType(leftType, rightType);
                }
                return compiler_.boolType();
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::Lt:
        case BinaryOpExpr::Le:
        case BinaryOpExpr::Gt:
        case BinaryOpExpr::Ge:
            if (leftType == compiler_.stringType() && rightType == compiler_.stringType()) {
                return compiler_.boolType();
            }
            if (isNumeric(leftType) && isNumeric(rightType)) {
                // Complex numbers have no total order. Reject ordering ops on
                // any operand whose type contains Complex (scalar Complex,
                // Array[Complex], List[Complex], Tuple containing Complex,
                // etc.). Has to fire before the auto-map / tuple branches,
                // since those would otherwise wrap Array/Tuple<Complex> and
                // return a composite Bool result, hiding the error.
                if (containsComplex(leftType) || containsComplex(rightType)) {
                    error(expr->loc, "Complex numbers are not ordered");
                    return compiler_.boolType();
                }
                // Check for implicit auto-mapping with Array/List
                auto* leftArr = dynamic_cast<ArrayType*>(leftType);
                auto* leftList = dynamic_cast<ListType*>(leftType);
                auto* rightArr = dynamic_cast<ArrayType*>(rightType);
                auto* rightList = dynamic_cast<ListType*>(rightType);
                if (leftArr || leftList || rightArr || rightList) {
                    bool anyList = false;
                    if (leftArr) expr->leftAutoMap = AutoMapArg{1, 0, false};
                    else if (leftList) { expr->leftAutoMap = AutoMapArg{1, 0, true}; anyList = true; }
                    if (rightArr) expr->rightAutoMap = AutoMapArg{1, 0, false};
                    else if (rightList) { expr->rightAutoMap = AutoMapArg{1, 0, true}; anyList = true; }
                    return wrapAutoMapResult(compiler_.boolType(), expr->leftAutoMap, expr->rightAutoMap, anyList);
                }
                // Composite comparison for tuples (element-wise)
                auto* leftTup = dynamic_cast<TupleType*>(leftType);
                auto* rightTup = dynamic_cast<TupleType*>(rightType);
                if ((leftTup || rightTup) && leftType != rightType) {
                    return comparisonResultType(leftType, rightType);
                }
                return compiler_.boolType();
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::And:
        case BinaryOpExpr::Or:
            return compiler_.boolType();

        case BinaryOpExpr::BitAnd:
        case BinaryOpExpr::BitOr:
        case BinaryOpExpr::BitXor:
        case BinaryOpExpr::ShiftL:
        case BinaryOpExpr::ShiftR:
        case BinaryOpExpr::UShiftR:
            if (leftType == compiler_.intType() && rightType == compiler_.intType()) {
                return compiler_.intType();
            }
            break;  // Fall through to operator overload lookup

        case BinaryOpExpr::Pipeline:
            // Already desugared by parser
            return rightType;

        case BinaryOpExpr::LeftArrow: {
            // Built-in: Ref<T> <- T sets the ref and returns the value
            auto* refType = dynamic_cast<RefType*>(leftType);
            if (refType) {
                if (!typesEqual(refType->elemType_, rightType)) {
                    if (!isAssignable(rightType, refType->elemType_)) {
                        error(expr->loc, "Type mismatch in '<-': Ref holds '" +
                              std::string(refType->elemType_->str().data(), refType->elemType_->str().size()) +
                              "' but assigned '" +
                              std::string(rightType->str().data(), rightType->str().size()) + "'");
                    }
                }
                return refType->elemType_;
            }
            break;  // Fall through to operator overload lookup
        }

        case BinaryOpExpr::RightArrow: {
            // Built-in: T -> Ref<T> sets the ref and returns the value
            auto* refType = dynamic_cast<RefType*>(rightType);
            if (refType) {
                if (!typesEqual(refType->elemType_, leftType)) {
                    if (!isAssignable(leftType, refType->elemType_)) {
                        error(expr->loc, "Type mismatch in '->': Ref holds '" +
                              std::string(refType->elemType_->str().data(), refType->elemType_->str().size()) +
                              "' but assigned '" +
                              std::string(leftType->str().data(), leftType->str().size()) + "'");
                    }
                }
                return refType->elemType_;
            }
            break;  // Fall through to operator overload lookup
        }

        default:
            return compiler_.intType();
    }

    // Built-in rules didn't match — try operator overload
    const char* opName = opToFuncName(expr->op);
    if (opName) {
        std::vector<Type*> argTypes = {leftType, rightType};
        auto it = functions_.find(opName);
        if (it != functions_.end()) {
            // Resolution order: (1) exact match, (2) template, (3) promotion
            FuncInfo* func = nullptr;
            for (auto& fi : it->second) {
                if (fi.isTemplate || fi.paramTypes.size() != argTypes.size()) continue;
                bool match = true;
                for (size_t j = 0; j < argTypes.size(); ++j) {
                    if (fi.paramTypes[j] != argTypes[j]) { match = false; break; }
                }
                if (match) { func = &fi; break; }
            }
            if (!func) {
                func = tryResolveTemplate(opName, argTypes, nullptr);
            }
            if (!func) {
                func = tryResolveOverload(opName, argTypes);
            }
            if (func) {
                checkRTSafety(func, opName, expr->loc);
                if (func->returnType == nullptr) {
                    if (func->declNode) {
                        inferFunctionReturnType(func->declNode, func);
                    } else {
                        error(expr->loc, "Cannot call operator '" + std::string(opName) +
                              "' whose return type has not been inferred yet");
                        return compiler_.intType();
                    }
                }
                expr->resolvedFuncGlobalIndex = (i32)func->globalIndex;
                expr->isBuiltinCall = func->isBuiltin; expr->builtinAcceptsInlineArgs = func->acceptsInlineArgs;
                return func->returnType;
            }
            // No overload matched — fall through to structural equality check
        }
    }

    // Built-in structural equality for all same-type values
    if ((expr->op == BinaryOpExpr::Eq || expr->op == BinaryOpExpr::Ne)
        && leftType == rightType) {
        return compiler_.boolType();
    }

    // Implicit auto-mapping for operator overloads: if either operand is
    // Array or List, unwrap one level and retry the overload resolution.
    {
        auto* leftArr  = dynamic_cast<ArrayType*>(leftType);
        auto* leftList = dynamic_cast<ListType*>(leftType);
        auto* rightArr  = dynamic_cast<ArrayType*>(rightType);
        auto* rightList = dynamic_cast<ListType*>(rightType);
        if (leftArr || leftList || rightArr || rightList) {
            bool anyList = false;
            Type* unwrappedLeft = leftType;
            Type* unwrappedRight = rightType;
            AutoMapArg leftAM{}, rightAM{};
            if (leftArr) { unwrappedLeft = leftArr->elemType_; leftAM = {1, 0, false}; }
            else if (leftList) { unwrappedLeft = leftList->elemType_; leftAM = {1, 0, true}; anyList = true; }
            if (rightArr) { unwrappedRight = rightArr->elemType_; rightAM = {1, 0, false}; }
            else if (rightList) { unwrappedRight = rightList->elemType_; rightAM = {1, 0, true}; anyList = true; }

            const char* opN = opToFuncName(expr->op);
            if (opN) {
                std::vector<Type*> unwrappedArgs = {unwrappedLeft, unwrappedRight};
                // Try exact match
                FuncInfo* func = nullptr;
                auto it2 = functions_.find(opN);
                if (it2 != functions_.end()) {
                    for (auto& fi : it2->second) {
                        if (fi.isTemplate || fi.paramTypes.size() != unwrappedArgs.size()) continue;
                        bool match = true;
                        for (size_t j = 0; j < unwrappedArgs.size(); ++j) {
                            if (fi.paramTypes[j] != unwrappedArgs[j]) { match = false; break; }
                        }
                        if (match) { func = &fi; break; }
                    }
                }
                if (!func) func = tryResolveTemplate(opN, unwrappedArgs, nullptr);
                if (!func) func = tryResolveOverload(opN, unwrappedArgs);

                // Also try built-in numeric ops on unwrapped types
                if (!func && isNumeric(unwrappedLeft) && isNumeric(unwrappedRight)) {
                    expr->leftAutoMap = leftAM;
                    expr->rightAutoMap = rightAM;
                    Type* scalarResult;
                    bool isDiv = (expr->op == BinaryOpExpr::Div);
                    scalarResult = commonNumericType(unwrappedLeft, unwrappedRight, isDiv);
                    return wrapAutoMapResult(scalarResult, leftAM, rightAM, anyList);
                }

                if (func) {
                    checkRTSafety(func, opN, expr->loc);
                    if (func->returnType == nullptr && func->declNode) {
                        inferFunctionReturnType(func->declNode, func);
                    }
                    expr->resolvedFuncGlobalIndex = (i32)func->globalIndex;
                    expr->isBuiltinCall = func->isBuiltin; expr->builtinAcceptsInlineArgs = func->acceptsInlineArgs;
                    expr->leftAutoMap = leftAM;
                    expr->rightAutoMap = rightAM;
                    Type* scalarResult = func->returnType ? func->returnType : compiler_.intType();
                    return wrapAutoMapResult(scalarResult, leftAM, rightAM, anyList);
                }
            }
        }
    }

    // No overload found — report built-in error
    switch (expr->op) {
        case BinaryOpExpr::Add:
            error(expr->loc, "'+' requires numeric operands");
            return compiler_.intType();
        case BinaryOpExpr::Concat:
            error(expr->loc, "'$' requires string, array, or tuple operands");
            return compiler_.intType();
        case BinaryOpExpr::Sub:
        case BinaryOpExpr::Mul:
            error(expr->loc, "Arithmetic operators require numeric operands");
            return compiler_.intType();
        case BinaryOpExpr::Div:
            error(expr->loc, "'/' requires numeric operands");
            return compiler_.intType();
        case BinaryOpExpr::Mod:
            error(expr->loc, "'%' requires integer operands");
            return compiler_.intType();
        case BinaryOpExpr::Eq:
        case BinaryOpExpr::Ne:
            error(expr->loc, "Equality operators require matching operand types");
            return compiler_.boolType();
        case BinaryOpExpr::Lt:
        case BinaryOpExpr::Le:
        case BinaryOpExpr::Gt:
        case BinaryOpExpr::Ge:
            error(expr->loc, "Comparison operators require numeric operands");
            return compiler_.boolType();
        case BinaryOpExpr::BitAnd:
        case BinaryOpExpr::BitOr:
        case BinaryOpExpr::BitXor:
        case BinaryOpExpr::ShiftL:
        case BinaryOpExpr::ShiftR:
        case BinaryOpExpr::UShiftR:
            error(expr->loc, "Bitwise operators require integer operands");
            return compiler_.intType();
        case BinaryOpExpr::LeftArrow:
            error(expr->loc, "'<-' requires a Ref on the left side");
            return compiler_.intType();
        case BinaryOpExpr::RightArrow:
            error(expr->loc, "'->' requires a Ref on the right side");
            return compiler_.intType();
        default:
            return compiler_.intType();
    }
}

Type* TypeChecker::inferUnaryOp(UnaryOpExpr* expr) {
    Type* operandType = inferExpr(static_cast<Expr*>(expr->operand.get()));
    if (!operandType) return compiler_.intType();

    switch (expr->op) {
        case UnaryOpExpr::Neg:
            if (isNumeric(operandType)) {
                // Bool promotes to Int for negation
                if (operandType == compiler_.boolType()) return compiler_.intType();
                // Arrays and tuples preserve their type under negation
                return operandType;
            }
            break;  // fall through to overload lookup

        case UnaryOpExpr::Not:
            if (isBoolComposite(operandType)) return operandType;
            break;  // fall through to overload lookup

        case UnaryOpExpr::BitNot:
            if (isIntComposite(operandType)) return operandType;
            break;  // fall through to overload lookup

        case UnaryOpExpr::Ref:
            // &expr creates a Ref<T> where T is the type of expr
            return compiler_.refType(operandType);

        case UnaryOpExpr::Deref: {
            // *expr dereferences a Ref<T> to get T
            auto* refType = dynamic_cast<RefType*>(operandType);
            if (!refType) {
                error(expr->loc, "Dereference (*) requires a Ref operand, got '" +
                      std::string(operandType->str().data(), operandType->str().size()) + "'");
                return compiler_.intType();
            }
            return refType->elemType_;
        }
    }

    // Built-in rules didn't match — try unary operator overload
    const char* opName = unaryOpToFuncName(expr->op);
    if (opName) {
        std::vector<Type*> argTypes = {operandType};
        auto it = functions_.find(opName);
        if (it != functions_.end()) {
            // Resolution order: (1) exact match, (2) template, (3) promotion
            FuncInfo* func = nullptr;
            for (auto& fi : it->second) {
                if (fi.isTemplate || fi.paramTypes.size() != argTypes.size()) continue;
                bool match = true;
                for (size_t j = 0; j < argTypes.size(); ++j) {
                    if (fi.paramTypes[j] != argTypes[j]) { match = false; break; }
                }
                if (match) { func = &fi; break; }
            }
            if (!func) {
                func = tryResolveTemplate(opName, argTypes, nullptr);
            }
            if (!func) {
                func = tryResolveOverload(opName, argTypes);
            }
            if (func) {
                checkRTSafety(func, opName, expr->loc);
                if (func->returnType == nullptr) {
                    if (func->declNode) {
                        inferFunctionReturnType(func->declNode, func);
                    } else {
                        error(expr->loc, "Cannot call operator '" + std::string(opName) +
                              "' whose return type has not been inferred yet");
                        return compiler_.intType();
                    }
                }
                expr->resolvedFuncGlobalIndex = (i32)func->globalIndex;
                expr->isBuiltinCall = func->isBuiltin; expr->builtinAcceptsInlineArgs = func->acceptsInlineArgs;
                return func->returnType;
            }
        }
    }

    // No overload found — report built-in error
    switch (expr->op) {
        case UnaryOpExpr::Neg:
            error(expr->loc, "Negation requires numeric operand");
            break;
        case UnaryOpExpr::Not:
            error(expr->loc, "Logical not requires boolean operand");
            break;
        case UnaryOpExpr::BitNot:
            error(expr->loc, "Bitwise not requires integer operand");
            break;
        default:
            break;
    }
    return compiler_.intType();
}

} // namespace ts
