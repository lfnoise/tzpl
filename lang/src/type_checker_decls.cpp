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
//  type_checker_decls.cpp
//  lang
//
//  Type checker -- declaration checking
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {

void TypeChecker::checkImportDecl(ImportDeclNode* decl) {
    if (!moduleCompiler_) {
        error(decl->loc, "Import statements require module compilation support");
        return;
    }

    ModuleInfo* mod = moduleCompiler_->compileModule(
        decl->modulePath, sourceFilePath_, decl->loc, errors_);
    if (!mod) return;  // errors already reported

    // Track for codegen init calls
    allImportedModules_.push_back(mod);

    switch (decl->importKind) {
        case ImportKind::Whole: {
            // import math or import std.math as m
            std::string alias = decl->alias.empty() ? decl->modulePath.back() : decl->alias;
            importedModules_[alias] = mod;
            break;
        }
        case ImportKind::Wildcard: {
            // import math.* — inject all exports into current scope
            // Also register module name for qualified access (module.func())
            {
                std::string moduleName = decl->modulePath.back();
                importedModules_[moduleName] = mod;
            }
            for (const auto& [name, entry] : mod->exports) {
                switch (entry.kind) {
                    case ExportEntry::Func:
                        for (auto fi : entry.funcOverloads) {
                            if (fi.isTemplate) fi.sourceModule = mod;
                            functions_[name].push_back(fi);
                        }
                        break;
                    case ExportEntry::Var: {
                        VarInfo vi;
                        vi.type = entry.type;
                        vi.isMutable = false;
                        vi.isGlobal = true;
                        vi.globalIndex = entry.globalIndex;
                        globalVars_[name] = vi;
                        break;
                    }
                    case ExportEntry::StructT:
                        structTypes_[name] = entry.structType;
                        break;
                    case ExportEntry::EnumT:
                        enumTypes_[name] = entry.enumType;
                        break;
                    case ExportEntry::TemplateStructT:
                        templateStructs_[name] = entry.templateStructDecl;
                        break;
                    case ExportEntry::TemplateEnumT:
                        templateEnums_[name] = entry.templateEnumDecl;
                        break;
                    case ExportEntry::TypeAlias:
                        typeAliases_[name] = entry.aliasType;
                        break;
                    case ExportEntry::TemplateTypeAlias:
                        templateTypeAliases_[name] = entry.templateTypeAliasDecl;
                        break;
                    case ExportEntry::ConstraintT:
                        constraints_[name] = entry.constraintInfo;
                        break;
                }
            }
            break;
        }
        case ImportKind::Named: {
            // import math.{sin, cos as cosine}
            for (const auto& imp : decl->names) {
                auto it = mod->exports.find(imp.name);
                if (it == mod->exports.end()) {
                    error(decl->loc, "Module '" + mod->moduleName +
                          "' does not export '" + imp.name + "'");
                    continue;
                }
                const std::string& localName = imp.alias.empty() ? imp.name : imp.alias;
                const ExportEntry& entry = it->second;
                switch (entry.kind) {
                    case ExportEntry::Func:
                        for (auto fi : entry.funcOverloads) {
                            if (fi.isTemplate) fi.sourceModule = mod;
                            functions_[localName].push_back(fi);
                        }
                        break;
                    case ExportEntry::Var: {
                        VarInfo vi;
                        vi.type = entry.type;
                        vi.isMutable = false;
                        vi.isGlobal = true;
                        vi.globalIndex = entry.globalIndex;
                        globalVars_[localName] = vi;
                        break;
                    }
                    case ExportEntry::StructT:
                        structTypes_[localName] = entry.structType;
                        break;
                    case ExportEntry::EnumT:
                        enumTypes_[localName] = entry.enumType;
                        break;
                    case ExportEntry::TemplateStructT:
                        templateStructs_[localName] = entry.templateStructDecl;
                        break;
                    case ExportEntry::TemplateEnumT:
                        templateEnums_[localName] = entry.templateEnumDecl;
                        break;
                    case ExportEntry::TypeAlias:
                        typeAliases_[localName] = entry.aliasType;
                        break;
                    case ExportEntry::TemplateTypeAlias:
                        templateTypeAliases_[localName] = entry.templateTypeAliasDecl;
                        break;
                    case ExportEntry::ConstraintT:
                        constraints_[localName] = entry.constraintInfo;
                        break;
                }
            }
            break;
        }
    }
}

void TypeChecker::checkBlock(BlockStmt* block) {
    pushScope();
    for (auto& stmt : block->stmts) {
        checkNode(stmt.get());
    }
    popScope();
}

void TypeChecker::checkLetDecl(LetDeclNode* decl) {
    Type* declaredType = decl->typeExpr ? resolveTypeExpr(decl->typeExpr.get()) : nullptr;
    Type* initType = inferExpr(static_cast<Expr*>(decl->init.get()), declaredType);

    // Pattern destructuring
    if (decl->pattern) {
        if (!initType) {
            error(decl->loc, "Cannot infer type for pattern destructuring");
            return;
        }
        decl->resolvedType = initType;
        checkPattern(decl->pattern.get(), initType, false);
        return;
    }

    // Deferred untyped lambda: store for backward inference at call site
    if (!initType && decl->init->kind == ASTNode::LambdaExpr) {
        auto* lambda = static_cast<LambdaExprNode*>(static_cast<Expr*>(decl->init.get()));
        declareVar(decl->name, nullptr, false);
        VarInfo* vi = lookupVar(decl->name);
        if (vi) { vi->deferredLambda = lambda; vi->deferredDecl = decl; }
        return;
    }

    if (declaredType && initType && !typesEqual(declaredType, initType)) {
        // Allow int-to-float promotion
        if (declaredType == compiler_.floatType() && initType == compiler_.intType()) {
            // OK: promotion
        } else {
            error(decl->loc, "Type mismatch in let declaration: expected '" +
                  std::string(declaredType->str()) + "', got '" +
                  std::string(initType->str()) + "'");
        }
    }
    // nil init with declared ListType: set init's resolvedType
    if (declaredType && !initType && dynamic_cast<ListType*>(declaredType)) {
        decl->init->resolvedType = declaredType;
    }

    Type* varType = declaredType ? declaredType : initType;
    if (!varType) {
        error(decl->loc, "Cannot infer type of let declaration");
        varType = compiler_.intType();
    }

    decl->resolvedType = varType;
    declareVar(decl->name, varType, false);
}

void TypeChecker::checkVarDecl(VarDeclNode* decl) {
    // Dynamic scope variable: var `name [Type] = expr;
    if (decl->isDynamic) {
        Type* declaredType = decl->typeExpr ? resolveTypeExpr(decl->typeExpr.get()) : nullptr;
        Type* initType = inferExpr(static_cast<Expr*>(decl->init.get()), declaredType);
        if (!initType) {
            error(decl->loc, "Cannot infer type of dynamic variable");
            initType = compiler_.intType();
        }
        Type* varType = declaredType ? declaredType : initType;
        decl->resolvedType = varType;

        // Check if already declared — reuse index
        auto* existing = compiler_.lookupDynVar(decl->name);
        if (existing) {
            if (!typesEqual(existing->type, varType)) {
                error(decl->loc, "Dynamic variable '`" + decl->name + "' redeclared with different type");
            }
        } else {
            u32 idx;
            compiler_.registerDynVar(decl->name, varType, idx);
        }
        return;
    }

    Type* declaredType = decl->typeExpr ? resolveTypeExpr(decl->typeExpr.get()) : nullptr;
    Type* initType = inferExpr(static_cast<Expr*>(decl->init.get()), declaredType);

    // Pattern destructuring
    if (decl->pattern) {
        if (!initType) {
            error(decl->loc, "Cannot infer type for pattern destructuring");
            return;
        }
        decl->resolvedType = initType;
        checkPattern(decl->pattern.get(), initType, true);
        return;
    }

    // Deferred untyped lambda: store for backward inference at call site
    if (!initType && decl->init->kind == ASTNode::LambdaExpr) {
        auto* lambda = static_cast<LambdaExprNode*>(static_cast<Expr*>(decl->init.get()));
        declareVar(decl->name, nullptr, true);
        VarInfo* vi = lookupVar(decl->name);
        if (vi) { vi->deferredLambda = lambda; vi->deferredDecl = decl; }
        return;
    }

    if (declaredType && initType && !typesEqual(declaredType, initType)) {
        if (declaredType == compiler_.floatType() && initType == compiler_.intType()) {
            // promotion OK
        } else {
            error(decl->loc, "Type mismatch in var declaration");
        }
    }
    // nil init with declared ListType: set init's resolvedType
    if (declaredType && !initType && dynamic_cast<ListType*>(declaredType)) {
        decl->init->resolvedType = declaredType;
    }

    Type* varType = declaredType ? declaredType : initType;
    if (!varType) {
        error(decl->loc, "Cannot infer type of var declaration");
        varType = compiler_.intType();
    }

    decl->resolvedType = varType;
    declareVar(decl->name, varType, true);
}

void TypeChecker::checkConstDecl(ConstDeclNode* decl) {
    Type* declaredType = decl->typeExpr ? resolveTypeExpr(decl->typeExpr.get()) : nullptr;
    Type* initType = inferExpr(static_cast<Expr*>(decl->init.get()), declaredType);

    // Pattern destructuring
    if (decl->pattern) {
        if (!initType) {
            error(decl->loc, "Cannot infer type for pattern destructuring");
            return;
        }
        decl->resolvedType = initType;
        checkPattern(decl->pattern.get(), initType, false);
        return;
    }

    // Deferred untyped lambda: store for backward inference at call site
    if (!initType && decl->init->kind == ASTNode::LambdaExpr) {
        auto* lambda = static_cast<LambdaExprNode*>(static_cast<Expr*>(decl->init.get()));
        declareVar(decl->name, nullptr, false);
        VarInfo* vi = lookupVar(decl->name);
        if (vi) { vi->deferredLambda = lambda; vi->deferredDecl = decl; }
        return;
    }

    Type* varType = declaredType ? declaredType : initType;
    if (!varType) {
        error(decl->loc, "Cannot infer type of const declaration");
        varType = compiler_.intType();
    }

    decl->resolvedType = varType;
    declareVar(decl->name, varType, false);
}

Type* TypeChecker::getBlockTrailingType(ASTNode* node) {
    if (!node || node->kind != ASTNode::Block) return nullptr;
    auto* block = static_cast<BlockStmt*>(node);
    if (block->stmts.empty()) return nullptr;
    auto* last = block->stmts.back().get();
    // Trailing expression statement
    if (last->kind == ASTNode::ExprStmt) {
        auto* es = static_cast<ExprStmtNode*>(last);
        if (es->isTrailing) return es->expr->resolvedType;
    }
    // Trailing if-else (value-producing)
    if (last->kind == ASTNode::IfStmt) {
        auto* ifStmt = static_cast<IfStmtNode*>(last);
        if (ifStmt->elseBranch) {
            Type* thenType = getBlockTrailingType(ifStmt->thenBranch.get());
            if (thenType) return thenType;
        }
    }
    // Trailing match (value-producing)
    if (last->kind == ASTNode::SwitchStmt) {
        return last->resolvedType;
    }
    return nullptr;
}

Type* TypeChecker::getNodeTrailingType(ASTNode* node) {
    if (!node) return nullptr;
    if (node->kind == ASTNode::Block) {
        return getBlockTrailingType(node);
    }
    if (node->kind == ASTNode::ExprStmt) {
        return static_cast<ExprStmtNode*>(node)->expr->resolvedType;
    }
    if (node->kind == ASTNode::SwitchStmt) {
        return node->resolvedType;
    }
    if (node->kind == ASTNode::IfStmt) {
        auto* ifStmt = static_cast<IfStmtNode*>(node);
        if (ifStmt->elseBranch) {
            return getNodeTrailingType(ifStmt->thenBranch.get());
        }
    }
    return nullptr;
}

void TypeChecker::inferFunctionReturnType(FnDeclNode* fn, FuncInfo* fi, bool isLocal) {
    // Cycle detection
    if (fi->inferring) {
        error(fn->loc, "Cannot infer return type of '" + fn->name +
              "' due to recursive dependency");
        fi->returnType = compiler_.intType();
        fi->bodyChecked = true;
        fn->resolvedType = fi->returnType;
        return;
    }
    fi->inferring = true;

    // Save state for pointer-stability guard.
    // Body may contain a local fn with the same name, whose push_back on
    // functions_[name] can reallocate the vector and invalidate fi.
    // We only re-lookup if fi actually pointed into that vector.
    u32 fiGlobalIndex = fi->globalIndex;
    auto& overloads = functions_[fn->name];
    FuncInfo* oldVecBegin = overloads.data();
    size_t oldVecSize = overloads.size();
    bool fiInVector = (fi >= oldVecBegin && fi < oldVecBegin + (ptrdiff_t)oldVecSize);

    // Copy param types before body check (fi may be invalidated)
    std::vector<Type*> paramTypes = fi->paramTypes;

    // Set up capture tracking for local functions
    int savedBoundary = lambdaBoundary_;
    auto* savedCaptures = currentCaptures_;
    if (isLocal) {
        currentCaptures_ = &fn->captures;
    }

    // Set up scope with parameters, type-checking default expressions
    pushScope();

    if (isLocal) {
        lambdaBoundary_ = (int)scopes_.size() - 1;
    }

    for (size_t i = 0; i < fn->params.size(); ++i) {
        if (fn->params[i].defaultExpr) {
            Type* defType = inferExpr(static_cast<Expr*>(fn->params[i].defaultExpr.get()));
            if (defType && !isAssignable(defType, paramTypes[i])) {
                error(fn->params[i].loc, "Default value type '" + std::string(defType->str()) +
                      "' is not assignable to parameter type '" + std::string(paramTypes[i]->str()) + "'");
            }
        }
        declareVar(fn->params[i].name, paramTypes[i], false);
    }

    // Enter inference mode
    Type* savedReturnType = currentReturnType_;
    bool savedInferring = inferringReturnType_;
    Type* savedInferred = inferredReturnType_;

    inferringReturnType_ = true;
    currentReturnType_ = nullptr;
    inferredReturnType_ = nullptr;

    // Check the body
    checkNode(fn->body.get());

    // Re-lookup fi if the overloads vector reallocated and fi was in it
    if (fiInVector && overloads.data() != oldVecBegin) {
        for (auto& candidate : overloads) {
            if (candidate.globalIndex == fiGlobalIndex) {
                fi = &candidate;
                break;
            }
        }
    }

    // Extract trailing expression type from block
    Type* trailingType = getBlockTrailingType(fn->body.get());

    // Reconcile trailing type with return statements
    Type* resultType = nullptr;
    if (trailingType && inferredReturnType_) {
        // Both trailing expr and explicit returns
        if (typesEqual(trailingType, inferredReturnType_)) {
            resultType = trailingType;
        } else if (isNumeric(trailingType) && isNumeric(inferredReturnType_)) {
            resultType = commonNumericType(trailingType, inferredReturnType_);
        } else {
            error(fn->loc, "Inconsistent return types in inferred function '" + fn->name + "'");
            resultType = trailingType;
        }
    } else if (trailingType) {
        resultType = trailingType;
    } else if (inferredReturnType_) {
        resultType = inferredReturnType_;
    } else {
        resultType = compiler_.voidType();
    }

    // Validate return type against constraint if present
    if (fn->returnTypeConstraint && resultType) {
        auto savedBindings2 = typeParamBindings_;
        // Ensure monoBindings are active for template functions
        if (fi->monoBindings.size() > 0) {
            typeParamBindings_ = fi->monoBindings;
        }
        auto pattern = buildConstraintPattern(fn->returnTypeConstraint.get());
        if (!matchConstraintPattern(resultType, pattern)) {
            error(fn->loc, "Return type '" + std::string(resultType->str()) +
                  "' does not satisfy return type constraint in function '" + fn->name + "'");
        }
        typeParamBindings_ = savedBindings2;
    }

    // Update the FuncInfo and AST node
    fi->returnType = resultType;
    fi->bodyChecked = true;
    fi->inferring = false;
    fn->resolvedType = resultType;

    // Restore state
    inferringReturnType_ = savedInferring;
    inferredReturnType_ = savedInferred;
    currentReturnType_ = savedReturnType;
    popScope();
    lambdaBoundary_ = savedBoundary;
    currentCaptures_ = savedCaptures;
}

void TypeChecker::checkFnDecl(FnDeclNode* decl) {
    // Desugar constraint-as-param-type for local functions (top-level already done)
    if (decl->resolvedFuncGlobalIndex == -1) {
        desugarConstraintParams(decl);
    }

    // Skip template function declarations; they are type-checked per monomorphization
    if (!decl->typeParams.empty()) return;
    // Skip untyped variadic functions — registered as templates, checked per monomorphization
    if (!decl->params.empty() && decl->params.back().isVariadic && !decl->params.back().typeExpr) return;

    // Local function: register on-the-fly if not registered during first pass
    bool isLocal = (decl->resolvedFuncGlobalIndex == -1);
    if (isLocal) {
        std::vector<Type*> paramTypes = resolveAllParamTypes(decl->params);
        Type* retType = decl->returnType ? resolveTypeExpr(decl->returnType.get()) : nullptr;

        u32 globalIdx = compiler_.addGlobal(false);
        FuncInfo info{};
        info.returnType = retType;
        info.paramTypes = paramTypes;
        info.globalIndex = globalIdx;
        if (retType == nullptr) {
            info.declNode = decl;
        }
        functions_[decl->name].push_back(info);
        decl->resolvedFuncGlobalIndex = (i32)globalIdx;
    }

    auto it = functions_.find(decl->name);
    if (it == functions_.end()) {
        error(decl->loc, "Function not registered (internal error)");
        return;
    }

    // Find the matching FuncInfo by globalIndex
    FuncInfo* funcPtr = nullptr;
    for (auto& fi : it->second) {
        if (fi.canonicalFunc) continue;  // skip partial-arity entries
        if ((i32)fi.globalIndex == decl->resolvedFuncGlobalIndex) {
            funcPtr = &fi;
            break;
        }
    }
    if (!funcPtr) {
        error(decl->loc, "Function overload not registered (internal error)");
        return;
    }

    FuncInfo& func = *funcPtr;

    // Save param/return types before body check.  Body may contain a local fn
    // with the same name, whose push_back on functions_[name] can reallocate
    // the vector and invalidate funcPtr/func.
    std::vector<Type*> paramTypes = func.paramTypes;
    Type* returnType = func.returnType;

    // Demand-driven inference: if return type is still nullptr, infer it now
    if (returnType == nullptr) {
        inferFunctionReturnType(decl, funcPtr, isLocal);
        // funcPtr may be invalidated; read return type from AST node (set by inferFunctionReturnType)
        if (isLocal) {
            Type* ret = decl->resolvedType ? decl->resolvedType : compiler_.voidType();
            TypeVec argTV(rt::STLAllocator<Type*>(nullptr));
            for (Type* pt : paramTypes) argTV.push_back(pt);
            TypeVec freeVarTypes(rt::STLAllocator<Type*>(nullptr));
            for (auto& cap : decl->captures) {
                freeVarTypes.push_back(cap.type);
            }
            decl->localLambdaType = new LambdaType(TypeVec(argTV), ret, std::move(freeVarTypes));
            declareVar(decl->name, compiler_.functionType(argTV, ret), false);
        }
        return;  // inferFunctionReturnType sets bodyChecked and resolvedType
    }

    // Skip body re-checking for already-inferred functions
    if (func.bodyChecked) {
        decl->resolvedType = returnType;
        if (isLocal) {
            Type* ret = returnType ? returnType : compiler_.voidType();
            TypeVec argTV(rt::STLAllocator<Type*>(nullptr));
            for (Type* pt : paramTypes) argTV.push_back(pt);
            TypeVec emptyFV(rt::STLAllocator<Type*>(nullptr));
            decl->localLambdaType = new LambdaType(TypeVec(argTV), ret, std::move(emptyFV));
            declareVar(decl->name, compiler_.functionType(argTV, ret), false);
        }
        return;
    }

    // Set up capture tracking for local functions (like lambdas)
    int savedBoundary = lambdaBoundary_;
    auto* savedCaptures = currentCaptures_;
    if (isLocal) {
        currentCaptures_ = &decl->captures;
    }

    // Set up scope with parameters, type-checking default expressions
    pushScope();

    if (isLocal) {
        lambdaBoundary_ = (int)scopes_.size() - 1;
    }

    for (size_t i = 0; i < decl->params.size(); ++i) {
        // Type-check default expression before declaring param (preceding params in scope)
        if (decl->params[i].defaultExpr) {
            Type* defType = inferExpr(static_cast<Expr*>(decl->params[i].defaultExpr.get()));
            if (defType && !isAssignable(defType, paramTypes[i])) {
                error(decl->params[i].loc, "Default value type '" + std::string(defType->str()) +
                      "' is not assignable to parameter type '" + std::string(paramTypes[i]->str()) + "'");
            }
        }
        declareVar(decl->params[i].name, paramTypes[i], false);
    }

    // Save coroutine state
    bool savedInCoro = inCoroutineBody_;
    Type* savedYieldType = currentYieldType_;

    // Set current return type
    Type* savedReturnType = currentReturnType_;

    if (decl->isCoroutine) {
        // For coro fn, the returnType stored in FuncInfo is Coroutine<T>
        // Extract yield type for checking yield statements
        auto* coroType = dynamic_cast<CoroutineType*>(returnType);
        if (coroType) {
            inCoroutineBody_ = true;
            currentYieldType_ = coroType->yieldType_;
            // The body's return type is void (coro fn bodies don't return values,
            // they yield them; the body ending is handled by op_coro_done)
            currentReturnType_ = compiler_.voidType();
        }
    } else {
        currentReturnType_ = returnType;
    }

    // Annotate the function node
    decl->resolvedType = returnType;

    // Check body (may invalidate funcPtr/func via same-name local fn push_back)
    checkNode(decl->body.get());

    currentReturnType_ = savedReturnType;
    inCoroutineBody_ = savedInCoro;
    currentYieldType_ = savedYieldType;
    popScope();

    // Restore capture tracking state
    lambdaBoundary_ = savedBoundary;
    currentCaptures_ = savedCaptures;

    // For local functions, declare a local variable with FunctionType
    // so calls in the enclosing scope resolve via lookupVar (shadowing outer functions)
    // Use saved paramTypes/returnType since funcPtr may be invalidated
    if (isLocal) {
        Type* ret = returnType ? returnType : compiler_.voidType();
        TypeVec argTV(rt::STLAllocator<Type*>(nullptr));
        for (Type* pt : paramTypes) argTV.push_back(pt);
        TypeVec freeVarTypes(rt::STLAllocator<Type*>(nullptr));
        for (auto& cap : decl->captures) {
            freeVarTypes.push_back(cap.type);
        }
        decl->localLambdaType = new LambdaType(TypeVec(argTV), ret, std::move(freeVarTypes));
        declareVar(decl->name, compiler_.functionType(argTV, ret), false);
    }
}

void TypeChecker::checkStructDecl(StructDeclNode* decl) {
    // Template struct declarations are handled via monomorphization - skip
    // Non-template structs are already registered in the first pass
}

void TypeChecker::checkUnionDecl(UnionDeclNode* decl) {
    // Template enum declarations are handled via monomorphization - skip
    // Non-template enums are already registered in the first pass
}

} // namespace ts
