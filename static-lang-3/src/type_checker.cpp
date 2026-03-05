//
//  type_checker.cpp
//  static-lang-3
//
//  Source-to-sink type checker implementation
//

#include "type_checker.hpp"
#include "module_compiler.hpp"
#include "builtins.hpp"
#include "value.hpp"
#include "diagnostic.hpp"

namespace ts {



TypeChecker::TypeChecker(Compiler& compiler, ModuleCompiler* mc)
    : compiler_(compiler), moduleCompiler_(mc), rtRestricted_(compiler.isRTRestricted()) {}

TypeChecker::ImportedModuleScopeGuard::ImportedModuleScopeGuard(TypeChecker& tc, ModuleInfo* mod)
    : tc(tc) {
    if (!mod) return;
    // Switch source context to module's source for correct error diagnostics
    savedSourceFilePath = tc.sourceFilePath_;
    savedSourceText = tc.sourceText_;
    if (!mod->sourceFilePath.empty()) {
        tc.sourceFilePath_ = mod->sourceFilePath;
        tc.sourceText_ = mod->sourceText;
    }
    // Merge module's internal functions into type checker scope
    for (const auto& [name, overloads] : mod->allFunctions) {
        auto& existing = tc.functions_[name];
        addedFunctions.push_back({name, existing.size()});
        for (const auto& fi : overloads) {
            // Avoid duplicates: skip if already present (by globalIndex)
            bool found = false;
            for (const auto& ef : existing) {
                if (ef.globalIndex == fi.globalIndex) { found = true; break; }
            }
            if (!found) existing.push_back(fi);
        }
    }
    // Merge struct types
    for (const auto& [name, st] : mod->allStructTypes) {
        if (tc.structTypes_.find(name) == tc.structTypes_.end()) {
            tc.structTypes_[name] = st;
            addedStructs.push_back(name);
        }
    }
    // Merge enum types
    for (const auto& [name, et] : mod->allEnumTypes) {
        if (tc.enumTypes_.find(name) == tc.enumTypes_.end()) {
            tc.enumTypes_[name] = et;
            addedEnums.push_back(name);
        }
    }
    // Merge type aliases
    for (const auto& [name, at] : mod->allTypeAliases) {
        if (tc.typeAliases_.find(name) == tc.typeAliases_.end()) {
            tc.typeAliases_[name] = at;
            addedAliases.push_back(name);
        }
    }
    // Merge constraints
    for (const auto& [name, ci] : mod->allConstraints) {
        if (tc.constraints_.find(name) == tc.constraints_.end()) {
            tc.constraints_[name] = ci;
            addedConstraints.push_back(name);
        }
    }
}

TypeChecker::ImportedModuleScopeGuard::~ImportedModuleScopeGuard() {
    // Restore source context
    tc.sourceFilePath_ = savedSourceFilePath;
    tc.sourceText_ = savedSourceText;
    // Undo function additions by truncating back to original sizes
    for (auto& [name, origSize] : addedFunctions) {
        auto it = tc.functions_.find(name);
        if (it != tc.functions_.end()) {
            it->second.resize(origSize);
            if (it->second.empty()) tc.functions_.erase(it);
        }
    }
    // Remove added struct types
    for (auto& name : addedStructs) tc.structTypes_.erase(name);
    // Remove added enum types
    for (auto& name : addedEnums) tc.enumTypes_.erase(name);
    // Remove added type aliases
    for (auto& name : addedAliases) tc.typeAliases_.erase(name);
    // Remove added constraints
    for (auto& name : addedConstraints) tc.constraints_.erase(name);
}

bool TypeChecker::checkRTSafety(const FuncInfo* func, const std::string& name, SourceRange loc) {
    if (rtRestricted_ && func && !func->rtSafe) {
        error(loc, "Function '" + name + "' is not real-time safe and cannot be "
                   "called when compiling for a real-time VM");
        return false;
    }
    return true;
}

void TypeChecker::pushScope() {
    scopes_.emplace_back();
}

void TypeChecker::popScope() {
    scopes_.pop_back();
}

void TypeChecker::declareVar(const std::string& name, Type* type, bool isMutable) {
    if (scopes_.empty()) {
        // Global scope — reuse existing slot when shadowing (REPL re-declaration)
        auto it = globalVars_.find(name);
        u32 globalIdx;
        if (it != globalVars_.end()) {
            globalIdx = it->second.globalIndex;
            compiler_.setGlobalIsObj(globalIdx, type ? type->isObjType() : true);
        } else {
            globalIdx = compiler_.addGlobal(type ? type->isObjType() : true);
        }
        VarInfo info{type, isMutable, true, globalIdx};
        globalVars_[name] = info;
    } else {
        VarInfo info{type, isMutable, false, 0};
        scopes_.back()[name] = info;
    }
}

VarInfo* TypeChecker::lookupVar(const std::string& name) {
    // Search scopes from innermost to outermost
    for (int i = (int)scopes_.size() - 1; i >= 0; --i) {
        auto it = scopes_[i].find(name);
        if (it != scopes_[i].end()) {
            // Detect cross-lambda-boundary capture
            if (lambdaBoundary_ >= 0 && i < lambdaBoundary_ && currentCaptures_) {
                // Deduplicate by name
                bool found = false;
                for (auto& cap : *currentCaptures_) {
                    if (cap.name == name) { found = true; break; }
                }
                if (!found) {
                    currentCaptures_->push_back({name, it->second.type});
                }
            }
            return &it->second;
        }
    }
    // Check globals (NOT captured - accessed via op_load_global directly)
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        return &it->second;
    }
    return nullptr;
}

void TypeChecker::error(SourceRange loc, const std::string& msg) {
    errors_.push_back(CompileError(CompileError::TypeError, loc, msg,
                                   sourceFilePath_, sourceText_));
}

// --- Check program ---

void TypeChecker::registerBuiltins() {
    registerBuiltinFunctions(compiler_, functions_);

    // Register host-provided foreign functions
    for (auto& entry : compiler_.foreignFunctions()) {
        u32 idx = compiler_.addGlobal(true);
        auto* prim = new Primitive(compiler_.voidType());
        prim->cfun_ = entry.cfun;
        prim->pure_ = entry.pure;
        prim->rtSafe_ = entry.rtSafe;
        prim->ffiData_ = entry.ffiData;
        compiler_.global(idx).o = prim;

        FuncInfo info;
        info.returnType = entry.returnType;
        info.paramTypes = entry.paramTypes;
        info.globalIndex = idx;
        info.bodyChecked = true;
        info.isBuiltin = true;
        info.rtSafe = entry.rtSafe;
        functions_[entry.name].push_back(info);
    }

    // Register built-in Option<T> template enum: enum Option<T> { some T, none }
    if (!syntheticOptionDecl_) {
        SourceRange noLoc{};
        std::vector<UnionCase> cases;
        cases.push_back(UnionCase{"some", std::make_unique<NamedTypeNode>(noLoc, "T"), noLoc});
        cases.push_back(UnionCase{"none", nullptr, noLoc});
        syntheticOptionDecl_ = std::make_unique<UnionDeclNode>(
            noLoc, "Option", std::vector<std::string>{"T"}, std::move(cases));
        templateEnums_["Option"] = syntheticOptionDecl_.get();
    }
}

void TypeChecker::check(Program& program) {
    // Register built-in math functions before user declarations
    registerBuiltins();

    // Phase 0: Process import declarations (before any other registration)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::ImportDecl) {
            checkImportDecl(static_cast<ImportDeclNode*>(item.get()));
        }
    }

    // First pass: register all struct and enum type names (empty shells)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::StructDecl) {
            auto* sd = static_cast<StructDeclNode*>(item.get());
            if (!sd->typeParams.empty()) {
                templateStructs_[sd->name] = sd;
                sd->resolvedType = nullptr;
                continue;
            }
            NameTypePairVec empty(rt::STLAllocator<NameTypePair>(nullptr));
            auto* structType = new StructType(compiler_.intern(sd->name), std::move(empty), sd->isTupleStruct);
            structTypes_[sd->name] = structType;
            sd->resolvedType = structType;
        }
    }
    for (auto& item : program.items) {
        if (item->kind == ASTNode::UnionDecl) {
            auto* ud = static_cast<UnionDeclNode*>(item.get());
            if (!ud->typeParams.empty()) {
                templateEnums_[ud->name] = ud;
                ud->resolvedType = nullptr;
                continue;
            }
            NameTypePairVec empty(rt::STLAllocator<NameTypePair>(nullptr));
            auto* enumType = new EnumType(compiler_.intern(ud->name), std::move(empty));
            enumTypes_[ud->name] = enumType;
            ud->resolvedType = enumType;
        }
    }

    // Register type aliases (after struct/enum names, before resolving fields/cases)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::TypeAliasDecl) {
            auto* ta = static_cast<TypeAliasDeclNode*>(item.get());
            if (!ta->typeParams.empty()) {
                // Generic alias: store unresolved (like template structs)
                templateTypeAliases_[ta->name] = ta;
            } else {
                // Concrete alias: resolve immediately
                Type* resolved = resolveTypeExpr(ta->typeExpr.get());
                typeAliases_[ta->name] = resolved;
                ta->resolvedType = resolved;
            }
        }
    }

    // Second pass: resolve field/case types (all type names and aliases now registered)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::StructDecl) {
            auto* sd = static_cast<StructDeclNode*>(item.get());
            if (!sd->typeParams.empty()) continue;
            NameTypePairVec fields(rt::STLAllocator<NameTypePair>(nullptr));
            for (auto& field : sd->fields) {
                Type* t = resolveTypeExpr(field.typeExpr.get());
                fields.push_back(NameTypePair{compiler_.intern(field.name), t});
            }
            static_cast<StructType*>(sd->resolvedType)->setFields(std::move(fields));
        }
    }
    for (auto& item : program.items) {
        if (item->kind == ASTNode::UnionDecl) {
            auto* ud = static_cast<UnionDeclNode*>(item.get());
            if (!ud->typeParams.empty()) continue;
            NameTypePairVec cases(rt::STLAllocator<NameTypePair>(nullptr));
            for (auto& ucase : ud->cases) {
                Type* t = ucase.typeExpr ? resolveTypeExpr(ucase.typeExpr.get()) : compiler_.voidType();
                cases.push_back(NameTypePair{compiler_.intern(ucase.name), t});
            }
            static_cast<EnumType*>(ud->resolvedType)->setCases(std::move(cases));
        }
    }

    // Register constraints (after types/aliases, before functions)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::ConstraintDecl) {
            checkConstraintDecl(static_cast<ConstraintDeclNode*>(item.get()));
        }
    }

    // Desugar constraint-as-param-type sugar before function registration
    for (auto& item : program.items) {
        if (item->kind == ASTNode::FnDecl) {
            desugarConstraintParams(static_cast<FnDeclNode*>(item.get()));
        }
    }

    // Register all function declarations at global scope
    for (auto& item : program.items) {
        if (item->kind == ASTNode::FnDecl) {
            auto* fn = static_cast<FnDeclNode*>(item.get());

            // Check if the last param is variadic
            bool hasVariadic = !fn->params.empty() && fn->params.back().isVariadic;

            // Template functions: register without resolving param types
            // Untyped variadic functions are always templates (type depends on call site)
            if (!fn->typeParams.empty() || (hasVariadic && !fn->params.back().typeExpr)) {
                FuncInfo info{};
                info.isTemplate = true;
                info.typeParams = fn->typeParams;
                info.declNode = fn;
                info.returnType = nullptr;
                info.globalIndex = 0;  // no global slot for templates
                if (hasVariadic) {
                    info.isVariadic = true;
                    info.fixedParamCount = (int)fn->params.size() - 1;
                }
                // Count default arguments for templates too
                int numDefaults = 0;
                for (auto& param : fn->params) {
                    if (param.defaultExpr) numDefaults++;
                }
                if (numDefaults > 0) {
                    info.numDefaults = numDefaults;
                    info.minArity = (int)fn->params.size() - numDefaults;
                }
                functions_[fn->name].push_back(info);
                fn->resolvedFuncGlobalIndex = -2;  // sentinel: template
                continue;
            }

            // Resolve parameter types (with preceding params in scope for default inference)
            std::vector<Type*> paramTypes = resolveAllParamTypes(fn->params);

            // Resolve return type (omitted = infer from body)
            Type* retType;
            if (!fn->returnType) {
                retType = nullptr;  // needs inference from body
            } else {
                retType = resolveTypeExpr(fn->returnType.get());
            }

            // If this is a coroutine, the declared return type is the yield type
            // and the function's external type is Coroutine<T>
            if (fn->isCoroutine && retType) {
                retType = compiler_.coroutineType(retType);
            }

            // Count default arguments
            int numDefaults = 0;
            for (auto& param : fn->params) {
                if (param.defaultExpr) numDefaults++;
            }

            // Allocate a global slot for the function's CodeBlock
            u32 globalIdx = compiler_.addGlobal(false);  // CodeBlock is not a GCObj

            FuncInfo info{};
            info.returnType = retType;
            info.paramTypes = paramTypes;
            info.globalIndex = globalIdx;
            if (hasVariadic) {
                info.isVariadic = true;
                info.fixedParamCount = (int)fn->params.size() - 1;
            }
            if (numDefaults > 0) {
                info.numDefaults = numDefaults;
                info.minArity = (int)fn->params.size() - numDefaults;
            }
            // Store AST node for demand-driven inference of omitted return types
            if (retType == nullptr) {
                info.declNode = fn;
            }
            functions_[fn->name].push_back(info);
            fn->resolvedFuncGlobalIndex = (i32)globalIdx;

            // Register partial-arity entries for default arguments
            if (numDefaults > 0) {
                int totalParams = (int)fn->params.size();
                int minArity = totalParams - numDefaults;
                FuncInfo* canonical = &functions_[fn->name].back();
                for (int arity = minArity; arity < totalParams; ++arity) {
                    FuncInfo partialInfo{};
                    partialInfo.returnType = retType;
                    partialInfo.paramTypes = std::vector<Type*>(paramTypes.begin(), paramTypes.begin() + arity);
                    partialInfo.globalIndex = globalIdx;
                    partialInfo.canonicalFunc = canonical;
                    partialInfo.numDefaults = numDefaults;
                    partialInfo.minArity = minArity;
                    if (retType == nullptr) {
                        partialInfo.declNode = fn;
                    }
                    functions_[fn->name].push_back(partialInfo);
                }
            }
        }
    }

    // Second pass: type-check everything (omitted return types inferred on demand)
    for (auto& item : program.items) {
        checkNode(item.get());
    }
}

void TypeChecker::checkREPLInput(Program& program) {
    clearErrors();

    // Register builtins only on first call
    if (!builtinsRegistered_) {
        registerBuiltins();
        builtinsRegistered_ = true;
    }

    // Phase 0: Process import declarations (before any other registration)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::ImportDecl) {
            checkImportDecl(static_cast<ImportDeclNode*>(item.get()));
        }
    }

    // First pass: register all struct and enum type names (empty shells)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::StructDecl) {
            auto* sd = static_cast<StructDeclNode*>(item.get());
            if (!sd->typeParams.empty()) {
                templateStructs_[sd->name] = sd;
                sd->resolvedType = nullptr;
                continue;
            }
            NameTypePairVec empty(rt::STLAllocator<NameTypePair>(nullptr));
            auto* structType = new StructType(compiler_.intern(sd->name), std::move(empty), sd->isTupleStruct);
            structTypes_[sd->name] = structType;
            sd->resolvedType = structType;
        }
    }
    for (auto& item : program.items) {
        if (item->kind == ASTNode::UnionDecl) {
            auto* ud = static_cast<UnionDeclNode*>(item.get());
            if (!ud->typeParams.empty()) {
                templateEnums_[ud->name] = ud;
                ud->resolvedType = nullptr;
                continue;
            }
            NameTypePairVec empty(rt::STLAllocator<NameTypePair>(nullptr));
            auto* enumType = new EnumType(compiler_.intern(ud->name), std::move(empty));
            enumTypes_[ud->name] = enumType;
            ud->resolvedType = enumType;
        }
    }

    // Register type aliases (after struct/enum names, before resolving fields/cases)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::TypeAliasDecl) {
            auto* ta = static_cast<TypeAliasDeclNode*>(item.get());
            if (!ta->typeParams.empty()) {
                templateTypeAliases_[ta->name] = ta;
            } else {
                Type* resolved = resolveTypeExpr(ta->typeExpr.get());
                typeAliases_[ta->name] = resolved;
                ta->resolvedType = resolved;
            }
        }
    }

    // Second pass: resolve field/case types (all type names and aliases now registered)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::StructDecl) {
            auto* sd = static_cast<StructDeclNode*>(item.get());
            if (!sd->typeParams.empty()) continue;
            NameTypePairVec fields(rt::STLAllocator<NameTypePair>(nullptr));
            for (auto& field : sd->fields) {
                Type* t = resolveTypeExpr(field.typeExpr.get());
                fields.push_back(NameTypePair{compiler_.intern(field.name), t});
            }
            static_cast<StructType*>(sd->resolvedType)->setFields(std::move(fields));
        }
    }
    for (auto& item : program.items) {
        if (item->kind == ASTNode::UnionDecl) {
            auto* ud = static_cast<UnionDeclNode*>(item.get());
            if (!ud->typeParams.empty()) continue;
            NameTypePairVec cases(rt::STLAllocator<NameTypePair>(nullptr));
            for (auto& ucase : ud->cases) {
                Type* t = ucase.typeExpr ? resolveTypeExpr(ucase.typeExpr.get()) : compiler_.voidType();
                cases.push_back(NameTypePair{compiler_.intern(ucase.name), t});
            }
            static_cast<EnumType*>(ud->resolvedType)->setCases(std::move(cases));
        }
    }

    // Register constraints (after types/aliases, before functions)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::ConstraintDecl) {
            checkConstraintDecl(static_cast<ConstraintDeclNode*>(item.get()));
        }
    }

    // Desugar constraint-as-param-type sugar before function registration
    for (auto& item : program.items) {
        if (item->kind == ASTNode::FnDecl) {
            desugarConstraintParams(static_cast<FnDeclNode*>(item.get()));
        }
    }

    // Register all function declarations at global scope
    for (auto& item : program.items) {
        if (item->kind == ASTNode::FnDecl) {
            auto* fn = static_cast<FnDeclNode*>(item.get());
            bool hasVariadic = !fn->params.empty() && fn->params.back().isVariadic;

            if (!fn->typeParams.empty() || (hasVariadic && !fn->params.back().typeExpr)) {
                FuncInfo info{};
                info.isTemplate = true;
                info.typeParams = fn->typeParams;
                info.declNode = fn;
                info.returnType = nullptr;
                info.globalIndex = 0;
                if (hasVariadic) {
                    info.isVariadic = true;
                    info.fixedParamCount = (int)fn->params.size() - 1;
                }
                // Count default arguments for templates too
                int numDefaults = 0;
                for (auto& param : fn->params) {
                    if (param.defaultExpr) numDefaults++;
                }
                if (numDefaults > 0) {
                    info.numDefaults = numDefaults;
                    info.minArity = (int)fn->params.size() - numDefaults;
                }
                functions_[fn->name].push_back(info);
                fn->resolvedFuncGlobalIndex = -2;
                continue;
            }

            std::vector<Type*> paramTypes = resolveAllParamTypes(fn->params);

            Type* retType;
            if (!fn->returnType) {
                retType = nullptr;
            } else {
                retType = resolveTypeExpr(fn->returnType.get());
            }

            // If this is a coroutine, the declared return type is the yield type
            // and the function's external type is Coroutine<T>
            if (fn->isCoroutine && retType) {
                retType = compiler_.coroutineType(retType);
            }

            // In REPL mode, check if there's an existing overload with matching
            // param types so we can replace it instead of appending a duplicate.
            auto& overloads = functions_[fn->name];
            FuncInfo* existing = nullptr;
            for (auto& fi : overloads) {
                if (fi.isTemplate) continue;
                if (fi.paramTypes.size() == paramTypes.size()) {
                    bool match = true;
                    for (size_t i = 0; i < paramTypes.size(); ++i) {
                        if (fi.paramTypes[i] != paramTypes[i]) { match = false; break; }
                    }
                    if (match) { existing = &fi; break; }
                }
            }

            // Count default arguments
            int numDefaults = 0;
            for (auto& param : fn->params) {
                if (param.defaultExpr) numDefaults++;
            }

            if (existing) {
                // Reuse the existing global slot so the new body overwrites the old one
                existing->returnType = retType;
                existing->bodyChecked = false;
                existing->declNode = (retType == nullptr) ? fn : nullptr;
                if (numDefaults > 0) {
                    existing->numDefaults = numDefaults;
                    existing->minArity = (int)fn->params.size() - numDefaults;
                }
                fn->resolvedFuncGlobalIndex = (i32)existing->globalIndex;
            } else {
                u32 globalIdx = compiler_.addGlobal(false);

                FuncInfo info{};
                info.returnType = retType;
                info.paramTypes = paramTypes;
                info.globalIndex = globalIdx;
                if (hasVariadic) {
                    info.isVariadic = true;
                    info.fixedParamCount = (int)fn->params.size() - 1;
                }
                if (numDefaults > 0) {
                    info.numDefaults = numDefaults;
                    info.minArity = (int)fn->params.size() - numDefaults;
                }
                if (retType == nullptr) {
                    info.declNode = fn;
                }
                overloads.push_back(info);
                fn->resolvedFuncGlobalIndex = (i32)globalIdx;

                // Register partial-arity entries for default arguments
                if (numDefaults > 0) {
                    int totalParams = (int)fn->params.size();
                    int minArity = totalParams - numDefaults;
                    FuncInfo* canonical = &overloads.back();
                    for (int arity = minArity; arity < totalParams; ++arity) {
                        FuncInfo partialInfo{};
                        partialInfo.returnType = retType;
                        partialInfo.paramTypes = std::vector<Type*>(paramTypes.begin(), paramTypes.begin() + arity);
                        partialInfo.globalIndex = globalIdx;
                        partialInfo.canonicalFunc = canonical;
                        partialInfo.numDefaults = numDefaults;
                        partialInfo.minArity = minArity;
                        if (retType == nullptr) {
                            partialInfo.declNode = fn;
                        }
                        overloads.push_back(partialInfo);
                    }
                }
            }
        }
    }

    // Second pass: type-check everything
    for (auto& item : program.items) {
        checkNode(item.get());
    }
}

// --- Check nodes ---

void TypeChecker::checkNode(ASTNode* node) {
    switch (node->kind) {
        case ASTNode::Block:
            checkBlock(static_cast<BlockStmt*>(node));
            break;
        case ASTNode::ImportDecl:
            // Already processed in Phase 0
            break;
        case ASTNode::LetDecl:
            checkLetDecl(static_cast<LetDeclNode*>(node));
            break;
        case ASTNode::VarDecl:
            checkVarDecl(static_cast<VarDeclNode*>(node));
            break;
        case ASTNode::ConstDecl:
            checkConstDecl(static_cast<ConstDeclNode*>(node));
            break;
        case ASTNode::FnDecl:
            checkFnDecl(static_cast<FnDeclNode*>(node));
            break;
        case ASTNode::StructDecl:
            checkStructDecl(static_cast<StructDeclNode*>(node));
            break;
        case ASTNode::UnionDecl:
            checkUnionDecl(static_cast<UnionDeclNode*>(node));
            break;
        case ASTNode::IfStmt:
            checkIfStmt(static_cast<IfStmtNode*>(node));
            break;
        case ASTNode::WhileStmt:
            checkWhileStmt(static_cast<WhileStmtNode*>(node));
            break;
        case ASTNode::ForStmt:
            checkForStmt(static_cast<ForStmtNode*>(node));
            break;
        case ASTNode::SwitchStmt:
            checkSwitchStmt(static_cast<SwitchStmtNode*>(node));
            break;
        case ASTNode::ReturnStmt:
            checkReturnStmt(static_cast<ReturnStmtNode*>(node));
            break;
        case ASTNode::AssignStmt:
            checkAssignStmt(static_cast<AssignStmtNode*>(node));
            break;
        case ASTNode::BreakStmt:
            checkBreakStmt(static_cast<BreakStmtNode*>(node));
            break;
        case ASTNode::ContinueStmt:
            checkContinueStmt(static_cast<ContinueStmtNode*>(node));
            break;
        case ASTNode::ExprStmt:
            checkExprStmt(static_cast<ExprStmtNode*>(node));
            break;
        case ASTNode::TypeAliasDecl:
            // Already processed during registration
            break;
        case ASTNode::ConstraintDecl:
            // Already processed during registration
            break;
        default:
            error(node->loc, "Unexpected node in type checker");
            break;
    }
}

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
    } else {
        error(stmt->iterable->loc, "For-loop iterable must be an Array, List, Range, or Coroutine");
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

    for (auto& clause : stmt->cases) {
        pushScope();
        checkPattern(clause.pattern.get(), subjType, false, /*inMatch=*/true);
        checkNode(clause.body.get());
        popScope();
    }
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

void TypeChecker::checkExprStmt(ExprStmtNode* stmt) {
    Type* ctx = (stmt->isTrailing && currentReturnType_) ? currentReturnType_ : nullptr;
    inferExpr(static_cast<Expr*>(stmt->expr.get()), ctx);
}

// --- Infer expression types ---

Type* TypeChecker::inferExpr(Expr* expr, Type* expectedType) {
    if (!expr) return nullptr;

    Type* result = nullptr;

    switch (expr->kind) {
        case ASTNode::IntLiteral:
            result = compiler_.intType();
            break;

        case ASTNode::FloatLiteral:
            result = compiler_.floatType();
            break;

        case ASTNode::ImaginaryLiteral:
            result = compiler_.complexType();
            break;

        case ASTNode::FractionLiteral:
            result = compiler_.fractionType();
            break;

        case ASTNode::StringLiteral:
            result = compiler_.stringType();
            break;

        case ASTNode::BoolLiteral:
            result = compiler_.boolType();
            break;

        case ASTNode::SymbolLiteral:
            result = compiler_.symbolType();
            break;

        case ASTNode::Identifier:
            result = inferIdentifier(static_cast<IdentifierExpr*>(expr));
            break;

        case ASTNode::BinaryOp:
            result = inferBinaryOp(static_cast<BinaryOpExpr*>(expr));
            break;

        case ASTNode::UnaryOp:
            result = inferUnaryOp(static_cast<UnaryOpExpr*>(expr));
            break;

        case ASTNode::CallExpr:
            result = inferCall(static_cast<CallExpr_*>(expr));
            break;

        case ASTNode::NilLiteral:
            result = nullptr;  // polymorphic nil, resolved contextually
            break;

        case ASTNode::ListLiteral: {
            auto* lit = static_cast<ListLiteralExpr*>(expr);
            if (lit->elements.empty()) {
                if (auto* lt = dynamic_cast<ListType*>(expectedType)) {
                    result = lt;
                } else {
                    error(expr->loc, "Cannot infer type of empty List() literal; use 'let x List<T> = nil' instead");
                    result = compiler_.intType();
                }
            } else {
                Type* elemType = inferExpr(static_cast<Expr*>(lit->elements[0].get()));
                for (size_t i = 1; i < lit->elements.size(); ++i) {
                    Type* t = inferExpr(static_cast<Expr*>(lit->elements[i].get()));
                    if (isNumeric(elemType) && isNumeric(t)) {
                        elemType = commonNumericType(elemType, t);
                    } else if (elemType != t) {
                        error(lit->elements[i]->loc, "List element type mismatch");
                    }
                }
                result = compiler_.listType(elemType);
            }
            break;
        }

        case ASTNode::ArrayLiteral: {
            auto* lit = static_cast<ArrayLiteralExpr*>(expr);
            if (lit->elemTypeExpr) {
                // Typed array constructor: [Type](...)
                Type* elemType = resolveTypeExpr(lit->elemTypeExpr.get());
                for (auto& elem : lit->elements) {
                    Type* et = inferExpr(static_cast<Expr*>(elem.get()));
                    if (et && !isAssignable(et, elemType)) {
                        if (isNumeric(et) && isNumeric(elemType)) {
                            // Allow numeric promotion (e.g. Int -> Float)
                        } else {
                            error(elem->loc, "Array element type mismatch: expected " +
                                  std::string(elemType->str()) + " but got " + std::string(et->str()));
                        }
                    }
                }
                result = compiler_.arrayType(elemType);
            } else if (lit->elements.empty()) {
                if (auto* at = dynamic_cast<ArrayType*>(expectedType)) {
                    result = at;
                } else {
                    error(expr->loc, "Cannot infer type of empty array literal");
                    result = compiler_.intType();
                }
            } else {
                // Detect explicit @ annotations on elements
                std::vector<AutoMapArg> autoMap(lit->elements.size());
                bool anyAutoMap = false;

                for (size_t i = 0; i < lit->elements.size(); ++i) {
                    Expr* elem = static_cast<Expr*>(lit->elements[i].get());
                    if (elem->kind == ASTNode::AutoMap) {
                        auto* am = static_cast<AutoMapExpr*>(elem);
                        autoMap[i].depth = am->depth;
                        autoMap[i].cartesianIndex = am->cartesianIndex;
                        anyAutoMap = true;
                    }
                }

                // Infer all element types
                std::vector<Type*> elemTypes;
                for (size_t i = 0; i < lit->elements.size(); ++i) {
                    elemTypes.push_back(inferExpr(static_cast<Expr*>(lit->elements[i].get())));
                }

                if (anyAutoMap) {
                    // Unwrap @-tagged element types to get inner types
                    std::vector<Type*> unwrappedTypes;
                    for (size_t i = 0; i < elemTypes.size(); ++i) {
                        if (autoMap[i].depth > 0) {
                            bool isList = false;
                            Type* inner = unwrapAutoMapLayers(elemTypes[i], autoMap[i].depth, isList, lit->elements[i]->loc);
                            autoMap[i].isList = isList;
                            unwrappedTypes.push_back(inner);
                        } else {
                            unwrappedTypes.push_back(elemTypes[i]);
                        }
                    }

                    // Compute common element type from unwrapped types
                    Type* elemType = unwrappedTypes[0];
                    for (size_t i = 1; i < unwrappedTypes.size(); ++i) {
                        if (isNumeric(elemType) && isNumeric(unwrappedTypes[i])) {
                            elemType = commonNumericType(elemType, unwrappedTypes[i]);
                        } else if (elemType != unwrappedTypes[i]) {
                            error(lit->elements[i]->loc, "Array element type mismatch");
                        }
                    }

                    lit->autoMapElements = std::move(autoMap);

                    // Compute result type based on cartesian vs zip
                    int maxCartesian = 0;
                    for (auto& am : lit->autoMapElements) {
                        if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                    }

                    // Inner type is Array[elemType] (each produced inner element is an array)
                    Type* innerArrayType = compiler_.arrayType(elemType);
                    // Wrap in Array levels: zip = 1 outer level, cartesian = maxCartesian outer levels
                    int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                    Type* wrapped = innerArrayType;
                    for (int level = 0; level < wrapLevels; ++level) {
                        wrapped = compiler_.arrayType(wrapped);
                    }
                    result = wrapped;
                } else {
                    Type* elemType = elemTypes[0];
                    for (size_t i = 1; i < elemTypes.size(); ++i) {
                        if (isNumeric(elemType) && isNumeric(elemTypes[i]) &&
                            numericRank(elemType) <= 4 && numericRank(elemTypes[i]) <= 4) {
                            // Only promote between scalar numeric types in array literals
                            elemType = commonNumericType(elemType, elemTypes[i]);
                        } else if (elemType != elemTypes[i]) {
                            error(lit->elements[i]->loc, "Array element type mismatch: expected '" +
                                  std::string(elemType->str().data(), elemType->str().size()) +
                                  "', got '" + std::string(elemTypes[i]->str().data(), elemTypes[i]->str().size()) + "'");
                        }
                    }
                    result = compiler_.arrayType(elemType);
                }
            }
            break;
        }

        case ASTNode::MapLiteral: {
            auto* lit = static_cast<MapLiteralExpr*>(expr);
            if (lit->entries.empty()) {
                if (auto* mt = dynamic_cast<MapType*>(expectedType)) {
                    result = mt;
                } else {
                    error(expr->loc, "Cannot infer type of empty map literal [:]");
                    result = compiler_.intType();
                }
            } else {
                Type* keyType = inferExpr(static_cast<Expr*>(lit->entries[0].key.get()));
                Type* valType = inferExpr(static_cast<Expr*>(lit->entries[0].value.get()));
                for (size_t i = 1; i < lit->entries.size(); ++i) {
                    Type* kt = inferExpr(static_cast<Expr*>(lit->entries[i].key.get()));
                    Type* vt = inferExpr(static_cast<Expr*>(lit->entries[i].value.get()));
                    if (isNumeric(keyType) && isNumeric(kt)) {
                        keyType = commonNumericType(keyType, kt);
                    } else if (keyType != kt) {
                        error(lit->entries[i].key->loc, "Map key type mismatch");
                    }
                    if (isNumeric(valType) && isNumeric(vt)) {
                        valType = commonNumericType(valType, vt);
                    } else if (valType != vt) {
                        error(lit->entries[i].value->loc, "Map value type mismatch");
                    }
                }
                result = compiler_.mapType(keyType, valType);
            }
            break;
        }

        case ASTNode::SetLiteral: {
            auto* lit = static_cast<SetLiteralExpr*>(expr);
            if (lit->elements.empty()) {
                if (auto* st = dynamic_cast<SetType*>(expectedType)) {
                    result = st;
                } else {
                    error(expr->loc, "Cannot infer type of empty Set() literal");
                    result = compiler_.intType();
                }
            } else {
                Type* elemType = inferExpr(static_cast<Expr*>(lit->elements[0].get()));
                for (size_t i = 1; i < lit->elements.size(); ++i) {
                    Type* t = inferExpr(static_cast<Expr*>(lit->elements[i].get()));
                    if (isNumeric(elemType) && isNumeric(t)) {
                        elemType = commonNumericType(elemType, t);
                    } else if (elemType != t) {
                        error(lit->elements[i]->loc, "Set element type mismatch");
                    }
                }
                result = compiler_.setType(elemType);
            }
            break;
        }

        case ASTNode::TupleLiteral: {
            auto* lit = static_cast<TupleLiteralExpr*>(expr);

            // Detect explicit @ annotations on elements
            std::vector<AutoMapArg> autoMap(lit->elements.size());
            bool anyAutoMap = false;

            for (size_t i = 0; i < lit->elements.size(); ++i) {
                Expr* elem = static_cast<Expr*>(lit->elements[i].get());
                if (elem->kind == ASTNode::AutoMap) {
                    auto* am = static_cast<AutoMapExpr*>(elem);
                    autoMap[i].depth = am->depth;
                    autoMap[i].cartesianIndex = am->cartesianIndex;
                    anyAutoMap = true;
                }
            }

            // Infer all element types
            Vec<Type*> fieldTypes(rt::STLAllocator<Type*>(nullptr));
            for (auto& elem : lit->elements) {
                fieldTypes.push_back(inferExpr(static_cast<Expr*>(elem.get())));
            }

            if (anyAutoMap) {
                // Unwrap @-tagged element types to get inner tuple field types
                Vec<Type*> unwrappedTypes(rt::STLAllocator<Type*>(nullptr));
                for (size_t i = 0; i < fieldTypes.size(); ++i) {
                    if (autoMap[i].depth > 0) {
                        bool isList = false;
                        Type* inner = unwrapAutoMapLayers(fieldTypes[i], autoMap[i].depth, isList, lit->elements[i]->loc);
                        autoMap[i].isList = isList;
                        unwrappedTypes.push_back(inner);
                    } else {
                        unwrappedTypes.push_back(fieldTypes[i]);
                    }
                }

                lit->autoMapElements = std::move(autoMap);

                // The inner tuple type uses unwrapped types
                Type* tupleType = compiler_.tupleType(unwrappedTypes);

                // Compute result type based on cartesian vs zip
                int maxCartesian = 0;
                for (auto& am : lit->autoMapElements) {
                    if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                }

                // Wrap in Array levels: zip = 1 outer level, cartesian = maxCartesian outer levels
                // tupleType is already the inner element type
                // For zip @: result = Array[Array[tupleType]]... no, result = Array[tupleType]
                // Actually for tuples: ([1,2,3]@, 'b, 'c) → [(1,'b,'c),...] = Array[(Int, Symbol, Symbol)]
                // So only wrap maxCartesian (or 1) times total, starting from tupleType
                int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                Type* wrapped = static_cast<Type*>(tupleType);
                for (int level = 0; level < wrapLevels; ++level) {
                    wrapped = compiler_.arrayType(wrapped);
                }
                result = wrapped;
            } else {
                result = compiler_.tupleType(fieldTypes);
            }
            break;
        }

        case ASTNode::StructLiteral: {
            auto* lit = static_cast<StructLiteralExpr*>(expr);
            StructType* stype = nullptr;

            // Try non-template struct first
            auto it = structTypes_.find(lit->structName);
            if (it != structTypes_.end()) {
                stype = it->second;
            }

            // If not found, try type alias that resolves to a struct
            if (!stype) {
                auto ait = typeAliases_.find(lit->structName);
                if (ait != typeAliases_.end()) {
                    stype = dynamic_cast<StructType*>(ait->second);
                }
            }

            // If not found, try template struct
            if (!stype) {
                auto tmplIt = templateStructs_.find(lit->structName);
                if (tmplIt != templateStructs_.end()) {
                    StructDeclNode* decl = tmplIt->second;

                    if (!lit->typeArgs.empty()) {
                        // Explicit type args: Pair<Int, Float> { ... }
                        std::vector<Type*> typeArgs;
                        for (auto& ta : lit->typeArgs) {
                            typeArgs.push_back(resolveTypeExpr(ta.get()));
                        }
                        stype = monomorphizeStruct(lit->structName, typeArgs, expr->loc);
                    } else {
                        // If spread is present, try to infer from spread type first
                        if (lit->spreadExpr) {
                            Type* spreadType = inferExpr(static_cast<Expr*>(lit->spreadExpr.get()));
                            auto* spreadStruct = dynamic_cast<StructType*>(spreadType);
                            if (spreadStruct && spreadStruct->name_->str().find(lit->structName) != std::string::npos) {
                                stype = spreadStruct;
                                // Assign field names for positional construction
                                if (lit->positional) {
                                    for (size_t i = 0; i < lit->fields.size() && i < stype->fields_.size(); ++i) {
                                        lit->fields[i].name = std::string(stype->fields_[i].name->str());
                                    }
                                }
                                // Validate field count (spread fills remaining)
                                if (lit->fields.size() > stype->fields_.size()) {
                                    error(expr->loc, "Struct '" + lit->structName + "' expects " +
                                          std::to_string(stype->fields_.size()) + " fields, got " +
                                          std::to_string(lit->fields.size()));
                                }
                                result = stype;
                                break;
                            }
                        }

                        // Infer type args from field values
                        // First, type-check all field values
                        std::vector<Type*> fieldTypes;
                        for (size_t i = 0; i < lit->fields.size(); ++i) {
                            fieldTypes.push_back(inferExpr(static_cast<Expr*>(lit->fields[i].value.get())));
                        }

                        // Build field name -> type mapping
                        std::unordered_map<std::string, Type*> fieldTypeMap;
                        if (lit->positional) {
                            for (size_t i = 0; i < fieldTypes.size() && i < decl->fields.size(); ++i) {
                                fieldTypeMap[decl->fields[i].name] = fieldTypes[i];
                            }
                        } else {
                            for (size_t i = 0; i < lit->fields.size(); ++i) {
                                fieldTypeMap[lit->fields[i].name] = fieldTypes[i];
                            }
                        }

                        // Unify each declared field's type expr with the actual type
                        std::unordered_map<std::string, Type*> bindings;
                        bool ok = true;
                        for (auto& field : decl->fields) {
                            auto ftIt = fieldTypeMap.find(field.name);
                            if (ftIt != fieldTypeMap.end() && ftIt->second) {
                                if (!unifyTypeExpr(field.typeExpr.get(), ftIt->second,
                                                   decl->typeParams, bindings)) {
                                    ok = false;
                                    break;
                                }
                            }
                        }

                        if (ok) {
                            // Check all type params are bound
                            std::vector<Type*> typeArgs;
                            for (auto& tp : decl->typeParams) {
                                auto bIt = bindings.find(tp);
                                if (bIt == bindings.end()) {
                                    error(expr->loc, "Cannot infer type parameter '" + tp +
                                          "' for template struct '" + lit->structName + "'");
                                    ok = false;
                                    break;
                                }
                                typeArgs.push_back(bIt->second);
                            }
                            if (ok) {
                                stype = monomorphizeStruct(lit->structName, typeArgs, expr->loc);
                            }
                        }

                        if (!ok || !stype) {
                            error(expr->loc, "Cannot infer type arguments for template struct '" +
                                  lit->structName + "'");
                            result = compiler_.intType();
                            break;
                        }

                        // Assign field names for positional construction
                        if (lit->positional) {
                            for (size_t i = 0; i < lit->fields.size() && i < stype->fields_.size(); ++i) {
                                lit->fields[i].name = std::string(stype->fields_[i].name->str());
                            }
                        }

                        // Fields already type-checked, validate against monomorphized type
                        if (!lit->spreadExpr && lit->fields.size() != stype->fields_.size()) {
                            error(expr->loc, "Struct '" + lit->structName + "' expects " +
                                  std::to_string(stype->fields_.size()) + " fields, got " +
                                  std::to_string(lit->fields.size()));
                        } else if (lit->spreadExpr && lit->fields.size() > stype->fields_.size()) {
                            error(expr->loc, "Struct '" + lit->structName + "' expects " +
                                  std::to_string(stype->fields_.size()) + " fields, got " +
                                  std::to_string(lit->fields.size()));
                        }
                        result = stype;
                        break;
                    }
                } else {
                    error(expr->loc, "Unknown struct type '" + lit->structName + "'");
                    result = compiler_.intType();
                    break;
                }
            }

            if (!stype) {
                result = compiler_.intType();
                break;
            }

            // Validate fields match declaration
            if (lit->spreadExpr) {
                // With spread, fewer fields are allowed
                if (lit->fields.size() > stype->fields_.size()) {
                    error(expr->loc, "Struct '" + lit->structName + "' expects " +
                          std::to_string(stype->fields_.size()) + " fields, got " +
                          std::to_string(lit->fields.size()));
                }
                // Type-check the spread expression
                Type* spreadType = inferExpr(static_cast<Expr*>(lit->spreadExpr.get()));
                if (!typesEqual(spreadType, stype)) {
                    error(lit->spreadExpr->loc, "Spread expression type does not match struct '" +
                          lit->structName + "'");
                }
            } else if (lit->fields.size() != stype->fields_.size()) {
                error(expr->loc, "Struct '" + lit->structName + "' expects " +
                      std::to_string(stype->fields_.size()) + " fields, got " +
                      std::to_string(lit->fields.size()));
            }

            // For positional construction, assign field names from the struct declaration
            if (lit->positional) {
                for (size_t i = 0; i < lit->fields.size() && i < stype->fields_.size(); ++i) {
                    lit->fields[i].name = std::string(stype->fields_[i].name->str());
                }
            }

            // Type-check each field, verify names match, and detect auto-mapping
            bool anyAutoMap = false;
            std::vector<AutoMapArg> autoMap(lit->fields.size());

            for (size_t i = 0; i < lit->fields.size(); ++i) {
                // Check for explicit @-annotation on field value
                Expr* fieldExpr = static_cast<Expr*>(lit->fields[i].value.get());
                AutoMapArg explicitAM;
                if (fieldExpr->kind == ASTNode::AutoMap) {
                    auto* am = static_cast<AutoMapExpr*>(fieldExpr);
                    explicitAM.depth = am->depth;
                    explicitAM.cartesianIndex = am->cartesianIndex;
                }

                Type* fieldType = inferExpr(fieldExpr);

                // Find the field in the struct type by name
                bool found = false;
                for (size_t j = 0; j < stype->fields_.size(); ++j) {
                    if (stype->fields_[j].name->str() == lit->fields[i].name) {
                        found = true;
                        Type* declType = stype->fields_[j].type;

                        if (explicitAM.depth > 0) {
                            // Explicit @ on this field
                            auto* arrT = dynamic_cast<ArrayType*>(fieldType);
                            if (!arrT) {
                                error(lit->fields[i].loc, "Explicit '@' on field '" +
                                      lit->fields[i].name + "' requires Array type");
                            } else {
                                // Validate inner type matches declared type
                                Type* innerType = arrT->elemType_;
                                if (!typesEqual(innerType, declType) &&
                                    !(declType == compiler_.floatType() && innerType == compiler_.intType())) {
                                    error(lit->fields[i].loc, "Field '" + lit->fields[i].name +
                                          "' type mismatch in struct '" + lit->structName + "'");
                                }
                            }
                            autoMap[i] = explicitAM;
                            anyAutoMap = true;
                        } else if (fieldType && !typesEqual(fieldType, declType)) {
                            if (declType == compiler_.floatType() && fieldType == compiler_.intType()) {
                                // promotion OK
                            } else if (auto* arrT = dynamic_cast<ArrayType*>(fieldType)) {
                                // Implicit auto-mapping: Array[T] provided where T expected
                                if (typesEqual(arrT->elemType_, declType) ||
                                    (declType == compiler_.floatType() && arrT->elemType_ == compiler_.intType())) {
                                    autoMap[i] = AutoMapArg{1, 0};
                                    anyAutoMap = true;
                                } else {
                                    error(lit->fields[i].loc, "Field '" + lit->fields[i].name +
                                          "' type mismatch in struct '" + lit->structName + "'");
                                }
                            } else {
                                error(lit->fields[i].loc, "Field '" + lit->fields[i].name +
                                      "' type mismatch in struct '" + lit->structName + "'");
                            }
                        }
                        break;
                    }
                }
                if (!found) {
                    error(lit->fields[i].loc, "Unknown field '" + lit->fields[i].name +
                          "' in struct '" + lit->structName + "'");
                }
            }

            if (anyAutoMap) {
                lit->autoMapFields = std::move(autoMap);
                // Compute cartesian nesting depth
                int maxCartesian = 0;
                for (auto& am : lit->autoMapFields) {
                    if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
                }
                int wrapLevels = (maxCartesian > 0) ? maxCartesian : 1;
                Type* wrapped = static_cast<Type*>(stype);
                for (int level = 0; level < wrapLevels; ++level) {
                    wrapped = compiler_.arrayType(wrapped);
                }
                result = wrapped;
            } else {
                result = stype;
            }
            break;
        }

        case ASTNode::EnumConstructor: {
            // This node may be a re-tagged CallExpr_ or FieldExpr_ from a previous
            // type-check pass (e.g., recheckTemplateBody).  In that case, the
            // resolvedType is already set correctly — reuse it to avoid UB from
            // static_cast<EnumConstructExpr*> on a node that isn't one.
            if (auto* existingEnum = dynamic_cast<EnumType*>(expr->resolvedType)) {
                result = existingEnum;
                break;
            }

            auto* ec = static_cast<EnumConstructExpr*>(expr);
            EnumType* etype = nullptr;

            // Try non-template enum first
            auto it = enumTypes_.find(ec->enumName);
            if (it != enumTypes_.end()) {
                etype = it->second;
            }

            // If not found, try template enum
            if (!etype) {
                auto tmplIt = templateEnums_.find(ec->enumName);
                if (tmplIt != templateEnums_.end()) {
                    UnionDeclNode* decl = tmplIt->second;

                    if (!ec->typeArgs.empty()) {
                        // Explicit type args: Option<Int>.some(42) or Option<Int>.none
                        std::vector<Type*> typeArgs;
                        for (auto& ta : ec->typeArgs) {
                            typeArgs.push_back(resolveTypeExpr(ta.get()));
                        }
                        etype = monomorphizeEnum(ec->enumName, typeArgs, expr->loc);
                    } else if (ec->arg) {
                        // Infer type args from the argument
                        Type* argType = inferExpr(static_cast<Expr*>(ec->arg.get()));
                        // Find the case in the template declaration
                        for (auto& ucase : decl->cases) {
                            if (ucase.name == ec->caseName && ucase.typeExpr) {
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
                                        etype = monomorphizeEnum(ec->enumName, typeArgs, expr->loc);
                                    }
                                }
                                break;
                            }
                        }
                        if (!etype) {
                            error(expr->loc, "Cannot infer type arguments for template enum '" +
                                  ec->enumName + "'");
                            result = compiler_.intType();
                            break;
                        }
                        // Validate the already-inferred arg against the monomorphized case type
                        for (size_t i = 0; i < etype->cases_.size(); ++i) {
                            if (etype->cases_[i].name->str() == ec->caseName) {
                                Type* caseType = etype->cases_[i].type;
                                if (argType && !typesEqual(argType, caseType)) {
                                    if (isAssignable(argType, caseType)) {
                                        ec->arg->resolvedType = caseType;
                                    } else {
                                        error(ec->arg->loc, "Enum case '" + ec->caseName +
                                              "' expects type '" + std::string(caseType->str().data(), caseType->str().size()) + "'");
                                    }
                                }
                                break;
                            }
                        }
                        result = etype;
                        break;
                    } else {
                        // No-data case without explicit type args: cannot infer
                        error(expr->loc, "Cannot infer type parameters for no-data enum case '" +
                              ec->caseName + "'; use explicit type arguments: " +
                              ec->enumName + "<...>." + ec->caseName);
                        result = compiler_.intType();
                        break;
                    }
                } else {
                    error(expr->loc, "Unknown enum type '" + ec->enumName + "'");
                    result = compiler_.intType();
                    break;
                }
            }

            if (!etype) {
                result = compiler_.intType();
                break;
            }

            // Find the case (for non-template or explicit-type-arg template)
            bool found = false;
            for (size_t i = 0; i < etype->cases_.size(); ++i) {
                if (etype->cases_[i].name->str() == ec->caseName) {
                    found = true;
                    Type* caseType = etype->cases_[i].type;
                    if (ec->arg) {
                        Type* argType = inferExpr(static_cast<Expr*>(ec->arg.get()));
                        if (caseType == compiler_.voidType()) {
                            error(ec->arg->loc, "Enum case '" + ec->caseName + "' takes no data");
                        } else if (argType && !typesEqual(argType, caseType)) {
                            if (isAssignable(argType, caseType)) {
                                ec->arg->resolvedType = caseType;
                            } else {
                                error(ec->arg->loc, "Enum case '" + ec->caseName +
                                      "' expects type '" + std::string(caseType->str().data(), caseType->str().size()) + "'");
                            }
                        }
                    } else {
                        if (caseType != compiler_.voidType()) {
                            error(expr->loc, "Enum case '" + ec->caseName + "' requires a value");
                        }
                    }
                    break;
                }
            }
            if (!found) {
                error(expr->loc, "Unknown case '" + ec->caseName + "' in enum '" + ec->enumName + "'");
            }

            result = etype;
            break;
        }

        case ASTNode::AutoMap: {
            // Explicit postfix auto-map: expr @, expr @@, expr @1, expr @2
            // The AutoMapExpr itself resolves to the inner expression's type;
            // the actual unwrapping/wrapping happens at the call site in inferCall.
            auto* am = static_cast<AutoMapExpr*>(expr);
            result = inferExpr(static_cast<Expr*>(am->inner.get()));
            break;
        }

        case ASTNode::RangeExpr: {
            auto* range = static_cast<RangeExprNode*>(expr);
            Type* startType = inferExpr(static_cast<Expr*>(range->start.get()));
            Type* elemType = startType;

            if (startType != compiler_.intType() && startType != compiler_.fractionType()) {
                error(range->start->loc, "Range start must be Int or Fraction");
                elemType = compiler_.intType();
            }

            if (range->next) {
                Type* nextType = inferExpr(static_cast<Expr*>(range->next.get()));
                if (nextType != compiler_.intType() && nextType != compiler_.fractionType()) {
                    error(range->next->loc, "Range step value must be Int or Fraction");
                }
                if (isNumeric(elemType) && isNumeric(nextType)) {
                    elemType = commonNumericType(elemType, nextType);
                }
            }

            if (range->end) {
                Type* endType = inferExpr(static_cast<Expr*>(range->end.get()));
                if (endType != compiler_.intType() && endType != compiler_.fractionType()) {
                    error(range->end->loc, "Range end must be Int or Fraction");
                }
                if (isNumeric(elemType) && isNumeric(endType)) {
                    elemType = commonNumericType(elemType, endType);
                }
            }

            result = compiler_.rangeType(elemType);
            break;
        }

        case ASTNode::AsTypeExpr: {
            auto* node = static_cast<AsTypeExprNode*>(expr);
            Type* subjType = inferExpr(static_cast<Expr*>(node->subject.get()));
            if (subjType && !dynamic_cast<AnyType*>(subjType)) {
                error(node->loc, "as(Type) can only be used on values of type Any");
            }
            Type* targetType = resolveTypeExpr(node->targetType.get());
            node->resolvedTargetType = targetType;
            result = compiler_.optionType(targetType);
            break;
        }

        case ASTNode::LambdaExpr:
            result = inferLambdaExpr(static_cast<LambdaExprNode*>(expr));
            break;

        case ASTNode::IfExpr: {
            auto* ie = static_cast<IfExprNode*>(expr);
            Type* condType = inferExpr(static_cast<Expr*>(ie->condition.get()));
            if (condType && condType != compiler_.boolType() && condType != compiler_.intType()) {
                error(ie->condition->loc, "Ternary condition must be bool or int");
            }
            // Type-check both branches
            checkNode(ie->thenBranch.get());
            if (ie->elseBranch) {
                checkNode(ie->elseBranch.get());
            }
            // Infer type from trailing expression in then branch
            if (ie->thenBranch->kind == ASTNode::Block) {
                auto* block = static_cast<BlockStmt*>(ie->thenBranch.get());
                if (!block->stmts.empty()) {
                    auto* last = block->stmts.back().get();
                    if (last->kind == ASTNode::ExprStmt) {
                        auto* es = static_cast<ExprStmtNode*>(last);
                        if (es->isTrailing) {
                            result = es->expr->resolvedType;
                        }
                    }
                }
            }
            if (!result) result = compiler_.voidType();
            break;
        }

        case ASTNode::BlockExpr: {
            auto* be = static_cast<BlockExprNode*>(expr);
            checkNode(be->body.get());
            if (be->body->kind == ASTNode::Block) {
                auto* block = static_cast<BlockStmt*>(be->body.get());
                if (!block->stmts.empty()) {
                    auto* last = block->stmts.back().get();
                    if (last->kind == ASTNode::ExprStmt) {
                        auto* es = static_cast<ExprStmtNode*>(last);
                        if (es->isTrailing) {
                            result = es->expr->resolvedType;
                        }
                    }
                }
            }
            if (!result) result = compiler_.voidType();
            break;
        }

        case ASTNode::FieldExpr: {
            auto* fe = static_cast<FieldExpr_*>(expr);

            // Check for module-qualified access: module.name
            if (fe->object->kind == ASTNode::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(fe->object.get());
                auto modIt = importedModules_.find(ident->name);
                if (modIt != importedModules_.end()) {
                    ModuleInfo* mod = modIt->second;
                    auto expIt = mod->exports.find(fe->field);
                    if (expIt == mod->exports.end()) {
                        error(expr->loc, "Module '" + ident->name +
                              "' does not export '" + fe->field + "'");
                        result = compiler_.intType();
                        break;
                    }
                    const ExportEntry& entry = expIt->second;
                    switch (entry.kind) {
                        case ExportEntry::Var:
                            result = entry.type;
                            // Tag the expr with the global index for codegen
                            fe->resolvedType = entry.type;
                            break;
                        case ExportEntry::Func:
                            // For a bare function reference (not a call), use the first overload's type
                            if (!entry.funcOverloads.empty()) {
                                result = entry.funcOverloads[0].returnType;
                            } else {
                                result = compiler_.voidType();
                            }
                            break;
                        case ExportEntry::StructT:
                            result = entry.structType;
                            break;
                        case ExportEntry::EnumT:
                            result = entry.enumType;
                            break;
                        case ExportEntry::TemplateStructT:
                        case ExportEntry::TemplateEnumT:
                            error(expr->loc, "Template type '" + fe->field +
                                  "' requires type arguments");
                            result = compiler_.intType();
                            break;
                        case ExportEntry::TypeAlias:
                            result = entry.aliasType;
                            break;
                        case ExportEntry::TemplateTypeAlias:
                            error(expr->loc, "Generic type alias '" + fe->field +
                                  "' requires type arguments");
                            result = compiler_.intType();
                            break;
                        case ExportEntry::ConstraintT:
                            error(expr->loc, "'" + fe->field + "' is a constraint, not a value");
                            result = compiler_.intType();
                            break;
                    }
                    break;
                }
            }

            // Check for enum no-data case construction: EnumName.caseName
            if (fe->object->kind == ASTNode::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(fe->object.get());
                auto enumIt = enumTypes_.find(ident->name);
                if (enumIt != enumTypes_.end()) {
                    EnumType* etype = enumIt->second;
                    // Find the case
                    bool found = false;
                    for (size_t i = 0; i < etype->cases_.size(); ++i) {
                        if (etype->cases_[i].name->str() == fe->field) {
                            found = true;
                            if (etype->cases_[i].type != compiler_.voidType()) {
                                error(expr->loc, "Enum case '" + fe->field + "' requires a value");
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
                    result = etype;
                    break;
                }
            }

            // Check for explicit @ on the object
            AutoMapArg objAutoMap = extractAutoMapAnnotation(static_cast<Expr*>(fe->object.get()));

            Type* objType = inferExpr(static_cast<Expr*>(fe->object.get()));
            if (!objType) {
                result = compiler_.intType();
                break;
            }

            // Explicit @ auto-map on field access: array @ .field
            if (objAutoMap) {
                bool isList = false;
                Type* innerType = unwrapAutoMapLayers(objType, objAutoMap.depth, isList, fe->object->loc);
                objAutoMap.isList = isList;

                // Resolve field on inner type
                Type* fieldType = nullptr;
                if (auto* stype = dynamic_cast<StructType*>(innerType)) {
                    for (size_t i = 0; i < stype->fields_.size(); ++i) {
                        if (stype->fields_[i].name->str() == fe->field) {
                            fieldType = stype->fields_[i].type;
                            break;
                        }
                    }
                    if (!fieldType) {
                        error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                              "' has no field '" + fe->field + "'");
                        result = compiler_.intType();
                        break;
                    }
                } else if (auto* ttype = dynamic_cast<TupleType*>(innerType)) {
                    try {
                        size_t idx = std::stoul(fe->field);
                        if (idx < ttype->fields_.size()) {
                            fieldType = ttype->fields_[idx];
                        } else {
                            error(expr->loc, "Tuple index " + fe->field + " out of bounds");
                            result = compiler_.intType();
                            break;
                        }
                    } catch (...) {
                        error(expr->loc, "Tuples can only be accessed by numeric index");
                        result = compiler_.intType();
                        break;
                    }
                } else {
                    error(expr->loc, "Explicit '@' on field access requires Array/List of structs or tuples");
                    result = compiler_.intType();
                    break;
                }

                fe->autoMap = objAutoMap;
                // Wrap field type in the same container layers
                result = fieldType;
                for (int d = 0; d < objAutoMap.depth; ++d) {
                    result = isList ? static_cast<Type*>(compiler_.listType(result))
                                    : static_cast<Type*>(compiler_.arrayType(result));
                }
                break;
            }

            // Implicit auto-mapping: peel Array/List layers to find Struct/Tuple
            {
                Type* t = objType;
                int depth = 0;
                bool isList = false;
                while (true) {
                    if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
                        t = arrT->elemType_;
                        depth++;
                    } else if (auto* listT = dynamic_cast<ListType*>(t)) {
                        t = listT->elemType_;
                        depth++;
                        isList = true;
                    } else {
                        break;
                    }
                }
                if (depth > 0 && (dynamic_cast<StructType*>(t) || dynamic_cast<TupleType*>(t))) {
                    // Resolve field on the inner struct/tuple type
                    Type* fieldType = nullptr;
                    if (auto* stype = dynamic_cast<StructType*>(t)) {
                        for (size_t i = 0; i < stype->fields_.size(); ++i) {
                            if (stype->fields_[i].name->str() == fe->field) {
                                fieldType = stype->fields_[i].type;
                                break;
                            }
                        }
                        if (!fieldType) {
                            error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                                  "' has no field '" + fe->field + "'");
                            result = compiler_.intType();
                            break;
                        }
                    } else if (auto* ttype = dynamic_cast<TupleType*>(t)) {
                        try {
                            size_t idx = std::stoul(fe->field);
                            if (idx < ttype->fields_.size()) {
                                fieldType = ttype->fields_[idx];
                            } else {
                                error(expr->loc, "Tuple index " + fe->field + " out of bounds");
                                result = compiler_.intType();
                                break;
                            }
                        } catch (...) {
                            error(expr->loc, "Tuples can only be accessed by numeric index");
                            result = compiler_.intType();
                            break;
                        }
                    }

                    fe->autoMap = AutoMapArg{depth, 0, isList};
                    // Wrap field type preserving container types at each level
                    // Walk objType again to rebuild the same nesting structure
                    result = fieldType;
                    Type* wrapper = objType;
                    // Collect container types from outer to inner
                    std::vector<bool> isListAtLevel;
                    for (int d = 0; d < depth; ++d) {
                        if (auto* arrT = dynamic_cast<ArrayType*>(wrapper)) {
                            isListAtLevel.push_back(false);
                            wrapper = arrT->elemType_;
                        } else if (auto* listT = dynamic_cast<ListType*>(wrapper)) {
                            isListAtLevel.push_back(true);
                            wrapper = listT->elemType_;
                        }
                    }
                    // Wrap from innermost to outermost
                    for (int d = depth - 1; d >= 0; --d) {
                        result = isListAtLevel[d] ? static_cast<Type*>(compiler_.listType(result))
                                                  : static_cast<Type*>(compiler_.arrayType(result));
                    }
                    break;
                }
            }

            // Handle struct field access
            if (auto* stype = dynamic_cast<StructType*>(objType)) {
                for (size_t i = 0; i < stype->fields_.size(); ++i) {
                    if (stype->fields_[i].name->str() == fe->field) {
                        result = stype->fields_[i].type;
                        break;
                    }
                }
                // For tuple structs, allow numeric field access (e.g., p.0, p.1)
                if (!result && stype->isTupleStruct_) {
                    try {
                        size_t idx = std::stoul(fe->field);
                        if (idx < stype->fields_.size()) {
                            result = stype->fields_[idx].type;
                        } else {
                            error(expr->loc, "Tuple struct field index " + fe->field + " out of bounds");
                            result = compiler_.intType();
                        }
                    } catch (...) {
                        error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                              "' has no field '" + fe->field + "'");
                        result = compiler_.intType();
                    }
                } else if (!result) {
                    error(expr->loc, "Struct '" + std::string(stype->name_->str()) +
                          "' has no field '" + fe->field + "'");
                    result = compiler_.intType();
                }
                break;
            }

            // Handle tuple field access (e.g., tuple.0, tuple.1)
            if (auto* ttype = dynamic_cast<TupleType*>(objType)) {
                // Try parsing field as index
                try {
                    size_t idx = std::stoul(fe->field);
                    if (idx < ttype->fields_.size()) {
                        result = ttype->fields_[idx];
                    } else {
                        error(expr->loc, "Tuple index " + fe->field + " out of bounds");
                        result = compiler_.intType();
                    }
                } catch (...) {
                    error(expr->loc, "Tuples can only be accessed by numeric index");
                    result = compiler_.intType();
                }
                break;
            }

            error(expr->loc, "Field access not supported on this type");
            result = compiler_.intType();
            break;
        }

        case ASTNode::IndexExpr: {
            auto* ie = static_cast<IndexExpr_*>(expr);

            // Check for explicit @ on the object
            AutoMapArg objAutoMap = extractAutoMapAnnotation(static_cast<Expr*>(ie->object.get()));

            Type* objType = inferExpr(static_cast<Expr*>(ie->object.get()));
            Type* idxType = inferExpr(static_cast<Expr*>(ie->index.get()));

            // A. Explicit @ on object: arr @ [i] or arr @ [[i1,i2,...]]
            if (objAutoMap) {
                bool isList = false;
                Type* innerType = unwrapAutoMapLayers(objType, objAutoMap.depth, isList, ie->object->loc);
                objAutoMap.isList = isList;

                // Resolve indexing on inner type, also handling Array/List index
                Type* elemResult = nullptr;

                // Check if index is Array of indices (combined @ + index automap)
                if (auto* idxArrType = dynamic_cast<ArrayType*>(idxType)) {
                    if (auto* arrType = dynamic_cast<ArrayType*>(innerType)) {
                        if (idxArrType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "Array index array must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, false};
                        elemResult = static_cast<Type*>(compiler_.arrayType(arrType->elemType_));
                    } else if (auto* mapType = dynamic_cast<MapType*>(innerType)) {
                        if (idxArrType->elemType_ != mapType->keyType_) {
                            if (!(isNumeric(idxArrType->elemType_) && isNumeric(mapType->keyType_))) {
                                error(ie->index->loc, "Map index array key type mismatch");
                            }
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, false};
                        elemResult = static_cast<Type*>(compiler_.arrayType(compiler_.optionType(mapType->valueType_)));
                    } else if (innerType == compiler_.stringType()) {
                        if (idxArrType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "String index array must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, false};
                        elemResult = static_cast<Type*>(compiler_.arrayType(compiler_.intType()));
                    }
                }
                // Check if index is List of indices
                if (!elemResult) if (auto* idxListType = dynamic_cast<ListType*>(idxType)) {
                    if (auto* arrType = dynamic_cast<ArrayType*>(innerType)) {
                        if (idxListType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "Array index list must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, true};
                        elemResult = static_cast<Type*>(compiler_.listType(arrType->elemType_));
                    } else if (auto* mapType = dynamic_cast<MapType*>(innerType)) {
                        if (idxListType->elemType_ != mapType->keyType_) {
                            if (!(isNumeric(idxListType->elemType_) && isNumeric(mapType->keyType_))) {
                                error(ie->index->loc, "Map index list key type mismatch");
                            }
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, true};
                        elemResult = static_cast<Type*>(compiler_.listType(compiler_.optionType(mapType->valueType_)));
                    } else if (innerType == compiler_.stringType()) {
                        if (idxListType->elemType_ != compiler_.intType()) {
                            error(ie->index->loc, "String index list must contain Int");
                        }
                        ie->indexAutoMap = AutoMapArg{1, 0, true};
                        elemResult = static_cast<Type*>(compiler_.listType(compiler_.intType()));
                    }
                }
                // Scalar indexing
                if (!elemResult) {
                    if (auto* arrType = dynamic_cast<ArrayType*>(innerType)) {
                        if (idxType && idxType != compiler_.intType()) {
                            error(ie->index->loc, "Array index must be Int");
                        }
                        elemResult = arrType->elemType_;
                    } else if (auto* mapType = dynamic_cast<MapType*>(innerType)) {
                        if (idxType && idxType != mapType->keyType_) {
                            if (!(isNumeric(idxType) && isNumeric(mapType->keyType_))) {
                                error(ie->index->loc, "Map index type mismatch");
                            }
                        }
                        elemResult = compiler_.optionType(mapType->valueType_);
                    } else if (innerType == compiler_.stringType()) {
                        if (idxType && idxType != compiler_.intType()) {
                            error(ie->index->loc, "String index must be Int");
                        }
                        elemResult = compiler_.intType();
                    } else {
                        error(expr->loc, "Explicit '@' on index access requires Array/List of indexable types");
                        result = compiler_.intType();
                        break;
                    }
                }

                ie->autoMap = objAutoMap;
                // Wrap result type in the same container layers
                result = elemResult;
                for (int d = 0; d < objAutoMap.depth; ++d) {
                    result = isList ? static_cast<Type*>(compiler_.listType(result))
                                    : static_cast<Type*>(compiler_.arrayType(result));
                }
                break;
            }

            // B. Index is Array/List of indices: arr[[3,1,2,0]]
            if (auto* idxArrType = dynamic_cast<ArrayType*>(idxType)) {
                // Array of indices/keys
                if (auto* arrType = dynamic_cast<ArrayType*>(objType)) {
                    if (idxArrType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "Array index array must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, false};
                    result = compiler_.arrayType(arrType->elemType_);
                } else if (auto* mapType = dynamic_cast<MapType*>(objType)) {
                    if (idxArrType->elemType_ != mapType->keyType_) {
                        if (!(isNumeric(idxArrType->elemType_) && isNumeric(mapType->keyType_))) {
                            error(ie->index->loc, "Map index array key type mismatch");
                        }
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, false};
                    result = compiler_.arrayType(compiler_.optionType(mapType->valueType_));
                } else if (objType == compiler_.stringType()) {
                    if (idxArrType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "String index array must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, false};
                    result = compiler_.arrayType(compiler_.intType());
                } else {
                    error(expr->loc, "Indexing requires an Array, Map, or String type");
                    result = compiler_.intType();
                }
                break;
            }
            if (auto* idxListType = dynamic_cast<ListType*>(idxType)) {
                // List of indices/keys
                if (auto* arrType = dynamic_cast<ArrayType*>(objType)) {
                    if (idxListType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "Array index list must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, true};
                    result = compiler_.listType(arrType->elemType_);
                } else if (auto* mapType = dynamic_cast<MapType*>(objType)) {
                    if (idxListType->elemType_ != mapType->keyType_) {
                        if (!(isNumeric(idxListType->elemType_) && isNumeric(mapType->keyType_))) {
                            error(ie->index->loc, "Map index list key type mismatch");
                        }
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, true};
                    result = compiler_.listType(compiler_.optionType(mapType->valueType_));
                } else if (objType == compiler_.stringType()) {
                    if (idxListType->elemType_ != compiler_.intType()) {
                        error(ie->index->loc, "String index list must contain Int");
                    }
                    ie->indexAutoMap = AutoMapArg{1, 0, true};
                    result = compiler_.listType(compiler_.intType());
                } else {
                    error(expr->loc, "Indexing requires an Array, Map, or String type");
                    result = compiler_.intType();
                }
                break;
            }

            // C. Normal indexing (no auto-mapping)
            if (auto* arrType = dynamic_cast<ArrayType*>(objType)) {
                if (idxType && idxType != compiler_.intType()) {
                    error(ie->index->loc, "Array index must be Int");
                }
                result = arrType->elemType_;
            } else if (auto* mapType = dynamic_cast<MapType*>(objType)) {
                if (idxType && idxType != mapType->keyType_) {
                    if (isNumeric(idxType) && isNumeric(mapType->keyType_)) {
                        // numeric promotion is ok
                    } else {
                        error(ie->index->loc, "Map index type mismatch");
                    }
                }
                result = compiler_.optionType(mapType->valueType_);
            } else if (objType == compiler_.stringType()) {
                if (idxType && idxType != compiler_.intType()) {
                    error(ie->index->loc, "String index must be Int");
                }
                result = compiler_.intType();
            } else {
                error(expr->loc, "Indexing requires an Array, Map, or String type");
                result = compiler_.intType();
            }
            break;
        }

        default:
            error(expr->loc, "Cannot infer type of expression");
            result = compiler_.intType();
            break;
    }

    expr->resolvedType = result;
    return result;
}

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

    // Set up for capture detection
    currentCaptures_ = &expr->captures;

    if (inferLambdaReturn) {
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
                    expr->isBuiltinCall = func->isBuiltin;
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
                if (leftType == compiler_.complexType() || rightType == compiler_.complexType()) {
                    error(expr->loc, "Complex numbers are not ordered");
                    return compiler_.boolType();
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
                    if (refType->elemType_ == compiler_.floatType() && rightType == compiler_.intType()) {
                        // int-to-float promotion OK
                    } else {
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
                    if (refType->elemType_ == compiler_.floatType() && leftType == compiler_.intType()) {
                        // int-to-float promotion OK
                    } else {
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
                expr->isBuiltinCall = func->isBuiltin;
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
                expr->isBuiltinCall = func->isBuiltin;
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

Type* TypeChecker::inferCall(CallExpr_* expr) {
    // Clear stale autoMapArgs/innerAutoMapArgs from previous template instantiation
    // of the same AST node (template bodies share AST across monomorphizations)
    expr->autoMapArgs.clear();
    expr->innerAutoMapArgs.clear();

    // Check for module-qualified function call: module.func(args)
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
                    for (auto& fi : const_cast<std::vector<FuncInfo>&>(entry.funcOverloads)) {
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
                        for (auto& fi : const_cast<std::vector<FuncInfo>&>(entry.funcOverloads)) {
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
                            // Multi-arg construction for tuple-typed case:
                            // synthesize a TupleLiteralExpr and let the normal
                            // tuple inference + single-arg validation handle it.
                            ExprList tupleElems;
                            for (size_t j = 0; j < expr->args.size(); ++j) {
                                tupleElems.push_back(std::move(expr->args[j]));
                            }
                            auto tupleLit = std::make_unique<TupleLiteralExpr>(expr->loc, std::move(tupleElems));
                            expr->args.clear();
                            expr->args.push_back(std::move(tupleLit));
                            // Now fall through to re-check with the single arg
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
        }
    }

    // Handle non-identifier callees (e.g., a[i](x, y))
    if (expr->callee->kind != ASTNode::Identifier) {
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
            return funcType->returnType_;
        }
        // Not a function type — try callable object via `call` function
        if (functions_.find("call") == functions_.end()) {
            error(expr->callee->loc, "Expression is not callable");
            return compiler_.intType();
        }
        // Rewrite: expr(args...) → call(expr, args...)
        expr->args.insert(expr->args.begin(), std::move(expr->callee));
        expr->callee = std::make_unique<IdentifierExpr>(expr->loc, "call");
        // Fall through to identifier-based resolution below
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
                        // Implicit auto-mapping: Array[T] provided where T expected
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
                        // Implicit auto-mapping: List[T] provided where T expected
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

            // Direct unification failed or explicit @ present — try auto-mapping
            // Unwrap Array/List from args and retry unification
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

    // Check if callee is a variable holding a lambda/function type
    VarInfo* calleeVar = lookupVar(ident->name);
    if (calleeVar) {
        // Deferred lambda called directly: infer param types from call arguments
        if (calleeVar->deferredLambda) {
            LambdaExprNode* lambda = calleeVar->deferredLambda;
            // Infer argument types first
            std::vector<Type*> callArgTypes;
            for (auto& arg : expr->args) {
                callArgTypes.push_back(inferExpr(static_cast<Expr*>(arg.get())));
            }
            // Set param types from arg types
            for (size_t j = 0; j < lambda->params.size() && j < callArgTypes.size(); ++j) {
                if (!lambda->params[j].typeExpr && !lambda->params[j].resolvedType)
                    lambda->params[j].resolvedType = callArgTypes[j];
            }
            // Complete the deferred inference
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
            // Infer argument types
            std::vector<Type*> callArgTypes;
            for (auto& arg : expr->args) {
                callArgTypes.push_back(inferExpr(static_cast<Expr*>(arg.get())));
            }

            // Monomorphize
            LambdaType* concreteLT = monomorphizeTemplateLambda(tmplLambda, callArgTypes, expr->loc);
            if (!concreteLT) return compiler_.intType();

            // Store for codegen
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
                    argTypes.push_back(inferExpr(arg));
                } else {
                    argTypes.push_back(inferExpr(arg));
                }
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

            // No auto-map match either — report type errors
            for (size_t i = 0; i < argTypes.size() && i < funcType->argTypes_.size(); ++i) {
                if (argTypes[i] && !typesEqual(argTypes[i], funcType->argTypes_[i])) {
                    if (!(funcType->argTypes_[i] == compiler_.floatType() && argTypes[i] == compiler_.intType())) {
                        error(expr->args[i]->loc, "Argument type mismatch");
                    }
                }
            }
            return funcType->returnType_;
        }
    }

    // Callable object: variable of non-function type with a `call` function.
    // Rewrite: myValue(args...) → call(myValue, args...)
    if (calleeVar && calleeVar->type && functions_.find("call") != functions_.end()) {
        auto calleeArg = std::make_unique<IdentifierExpr>(ident->loc, ident->name);
        expr->args.insert(expr->args.begin(), std::move(calleeArg));
        ident->name = "call";
        // Fall through to standard function resolution
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
        // Check if this is a lambda with untyped parameters — defer inference
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
            auto* ident = static_cast<IdentifierExpr*>(arg);
            VarInfo* vi = lookupVar(ident->name);
            if (vi && vi->deferredLambda) {
                isUntypedLambda = true;
            }
        }
        // Check if this is a template-only function reference (no concrete overloads)
        bool isTemplateFuncRef = false;
        if (!isUntypedLambda && arg->kind == ASTNode::Identifier) {
            auto* ident = static_cast<IdentifierExpr*>(arg);
            if (!lookupVar(ident->name)) {
                auto funcIt = functions_.find(ident->name);
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
            auto* ident = static_cast<IdentifierExpr*>(arg);
            VarInfo* vi = lookupVar(ident->name);
            if (vi && dynamic_cast<TemplateLambdaType*>(vi->type)) {
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
            argTypes.push_back(nullptr);  // defer — will resolve after backward inference
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

            // Get the lambda node — either inline or via deferred variable
            LambdaExprNode* lambda = nullptr;
            VarInfo* deferredVarInfo = nullptr;
            Expr* arg = static_cast<Expr*>(expr->args[i].get());
            if (arg->kind == ASTNode::LambdaExpr) {
                lambda = static_cast<LambdaExprNode*>(arg);
            } else if (arg->kind == ASTNode::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(arg);
                VarInfo* vi = lookupVar(ident->name);
                if (vi && vi->deferredLambda) {
                    lambda = vi->deferredLambda;
                    deferredVarInfo = vi;
                }
            }
            // Check for template lambda (inline or via variable)
            TemplateLambdaType* tmplLambdaType = nullptr;
            if (lambda && !lambda->typeParams.empty()) {
                // Inline template lambda — infer it first to create the TemplateLambdaType
                Type* tmplType = inferExpr(arg);
                tmplLambdaType = dynamic_cast<TemplateLambdaType*>(tmplType);
                lambda = nullptr;  // not a regular lambda
            } else if (!lambda && arg->kind == ASTNode::Identifier) {
                auto* ident = static_cast<IdentifierExpr*>(arg);
                VarInfo* vi = lookupVar(ident->name);
                if (vi) {
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
                // Not a lambda — check if it's a template function reference
                if (arg->kind == ASTNode::Identifier) {
                    auto* ident = static_cast<IdentifierExpr*>(arg);
                    auto funcIt = functions_.find(ident->name);
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
                    FuncInfo* resolved = tryResolveTemplate(ident->name, guessArgs, nullptr);
                    if (!resolved) continue;

                    // Ensure return type is inferred
                    if (resolved->returnType == nullptr && resolved->declNode)
                        inferFunctionReturnType(resolved->declNode, resolved);
                    Type* ret = resolved->returnType ? resolved->returnType : compiler_.voidType();
                    TypeVec paramTV(rt::STLAllocator<Type*>(nullptr));
                    for (Type* pt : resolved->paramTypes) paramTV.push_back(pt);
                    TypeVec emptyFV(rt::STLAllocator<Type*>(nullptr));
                    auto* lt = new LambdaType(TypeVec(paramTV), ret, std::move(emptyFV));

                    ident->resolvedFuncGlobalIndex = (i32)resolved->globalIndex;
                    ident->funcRefLambdaType = lt;
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
            // If no context available, params stay null — inferLambdaExpr will error

            if (deferredVarInfo) {
                // Identifier referencing a deferred lambda variable — infer the lambda
                // directly and update the variable's type and declaration node
                Type* resolvedType = inferLambdaExpr(lambda);
                lambda->resolvedType = resolvedType;
                deferredVarInfo->type = resolvedType;
                if (deferredVarInfo->deferredDecl)
                    deferredVarInfo->deferredDecl->resolvedType = resolvedType;
                deferredVarInfo->deferredLambda = nullptr;  // no longer deferred
                deferredVarInfo->deferredDecl = nullptr;
                argTypes[i] = resolvedType;
            } else {
                // Inline lambda — infer normally
                argTypes[i] = inferExpr(arg);
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

        // Exact match → template → promotion (same order as non-@ path)
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

        // Recursive auto-mapping: if overload resolution fails with the
        // unwrapped explicit-@ types, try also unwrapping non-@ Array/List
        // args (implicit auto-mapping). These form a separate inner loop.
        if (!func) {
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

                // Exact match → template → promotion
                {
                    auto it2 = functions_.find(ident->name);
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
                if (!func) func = tryResolveTemplate(ident->name, furtherUnwrapped, expr);
                if (!func) func = tryResolveOverload(ident->name, furtherUnwrapped);
                if (func) {
                    expr->innerAutoMapArgs = std::move(innerAutoMap);
                }
            }
        }

        if (!func) {
            func = resolveOverload(ident->name, unwrappedTypes, expr->loc);
            if (!func) return compiler_.intType();
        }

        // Demand-driven inference
        if (func->returnType == nullptr) {
            if (func->declNode) {
                inferFunctionReturnType(func->declNode, func);
            } else {
                error(expr->loc, "Cannot call function '" + ident->name +
                      "' whose return type has not been inferred yet");
                return compiler_.intType();
            }
        }

        expr->autoMapArgs = std::move(explicitAutoMap);
        expr->resolvedFuncGlobalIndex = (i32)func->globalIndex;
        expr->isBuiltinCall = func->isBuiltin;

        // Mark coroutine calls: if resolved function returns CoroutineType, this is a coro call
        if (func->returnType && dynamic_cast<CoroutineType*>(func->returnType)) {
            expr->isCoroCall = true;
        }

        // Mark next() on Coroutine as coroutine resume
        if (ident->name == "next" && func->paramTypes.size() == 1 &&
            dynamic_cast<CoroutineType*>(func->paramTypes[0])) {
            expr->isCoroResume = true;
        }

        // Mark yield() as coroutine yield
        if (ident && ident->name == "yield" && func->isBuiltin && func->paramTypes.size() == 1) {
            if (!inCoroutineBody_) {
                error(expr->loc, "'yield' can only be used inside a 'coro fn' body");
            } else if (currentYieldType_ && !isAssignable(unwrappedTypes[0], currentYieldType_)) {
                error(expr->loc, "Yield type mismatch: expected '" +
                      std::string(currentYieldType_->str()) + "', got '" +
                      std::string(unwrappedTypes[0]->str()) + "'");
            }
            expr->isCoroYield = true;
        }

        // Mark yieldAll() as coroutine delegation
        if (ident && ident->name == "yieldAll" && func->isBuiltin && func->paramTypes.size() == 1) {
            if (!inCoroutineBody_) {
                error(expr->loc, "'yieldAll' can only be used inside a 'coro fn' body");
            } else {
                auto* ct = dynamic_cast<CoroutineType*>(unwrappedTypes[0]);
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

        // Compute result type
        Type* retType = func->returnType;

        // If the function returns Void, auto-mapping just returns Void
        // (side-effects only, no collection of results)
        if (retType == compiler_.voidType()) {
            return retType;
        }

        // First wrap for inner (implicit) auto-mapping
        if (!expr->innerAutoMapArgs.empty()) {
            int innerDepth = 0;
            bool innerList = false;
            for (auto& iam : expr->innerAutoMapArgs) {
                if (iam.depth > innerDepth) innerDepth = iam.depth;
                if (iam.isList) innerList = true;
            }
            for (int level = 0; level < innerDepth; ++level) {
                retType = innerList ? static_cast<Type*>(compiler_.listType(retType))
                                    : static_cast<Type*>(compiler_.arrayType(retType));
            }
        }

        // Then wrap for outer (explicit @) auto-mapping
        if (hasCartesian) {
            // Cartesian: nest maxCartesianIndex levels of Array (or List) around return type
            for (int level = maxCartesianIndex; level >= 1; --level) {
                retType = anyListArg ? static_cast<Type*>(compiler_.listType(retType)) : static_cast<Type*>(compiler_.arrayType(retType));
            }
        } else {
            // Plain @ or @@: find the max depth among auto-mapped args
            int maxDepth = 0;
            for (auto& am : expr->autoMapArgs) {
                if (am.depth > maxDepth) maxDepth = am.depth;
            }
            // Wrap return type in maxDepth levels of Array or List
            for (int level = 0; level < maxDepth; ++level) {
                retType = anyListArg ? static_cast<Type*>(compiler_.listType(retType)) : static_cast<Type*>(compiler_.arrayType(retType));
            }
        }

        return retType;
    }

    // Resolution order: (1) exact overload match, (2) template resolution,
    // (3) promotion-based overload. This ensures concrete overloads are preferred,
    // but builtins like println that preserve exact arg types don't get matched
    // via numeric promotion against cached overloads (e.g. println(Int) → println(Float))
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
    // If a function expects T but receives Array^D[T] or List^D[T], auto-map at depth D.
    bool isAutoMapped = false;
    bool hasListArg = false;
    if (!func) {
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

            func = tryResolveOverload(ident->name, unwrappedTypes);
            if (func) {
                expr->autoMapArgs = std::move(autoMap);
                isAutoMapped = true;
                hasListArg = anyList;
            }
        }

        // If still no match, try template resolution at the unwrapped types
        if (!func) {
            // Try template matching with unwrapped types from the last iteration
            // Re-attempt at each depth level
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

                func = tryResolveTemplate(ident->name, unwrappedTypes, expr);
                if (func) {
                    expr->autoMapArgs = std::move(autoMap);
                    isAutoMapped = true;
                    hasListArg = anyList;
                }
            }
        }

        // If still no match, use the error-reporting version for diagnostics
        if (!func) {
            func = resolveOverload(ident->name, argTypes, expr->loc);
            if (!func) {
                return compiler_.intType();
            }
        }
    }

    // RT safety check: reject non-RT-safe functions when compiling for a real-time VM
    checkRTSafety(func, ident->name, expr->loc);

    // Demand-driven inference: if return type not yet resolved, infer it now
    if (func->returnType == nullptr) {
        // If this is a partial-arity entry (from default args), find the canonical
        // full-arity entry — inferFunctionReturnType needs the full param types.
        FuncInfo* inferTarget = func;
        if (func->canonicalFunc) {
            auto it2 = functions_.find(ident->name);
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
            error(expr->loc, "Cannot call function '" + ident->name +
                  "' whose return type has not been inferred yet");
            return compiler_.intType();
        }
    }

    // Store resolved global index on the AST node
    expr->resolvedFuncGlobalIndex = (i32)func->globalIndex;
    expr->isBuiltinCall = func->isBuiltin;

    // Mark coroutine calls: if resolved function returns CoroutineType, this is a coro call
    if (func->returnType && dynamic_cast<CoroutineType*>(func->returnType)) {
        expr->isCoroCall = true;
    }

    // Mark next() on Coroutine as coroutine resume
    if (ident->name == "next" && func->paramTypes.size() == 1 &&
        dynamic_cast<CoroutineType*>(func->paramTypes[0])) {
        expr->isCoroResume = true;
    }

    // Mark yield() as coroutine yield
    if (ident && ident->name == "yield" && func->isBuiltin && func->paramTypes.size() == 1) {
        if (!inCoroutineBody_) {
            error(expr->loc, "'yield' can only be used inside a 'coro fn' body");
        } else if (currentYieldType_ && !isAssignable(argTypes[0], currentYieldType_)) {
            error(expr->loc, "Yield type mismatch: expected '" +
                  std::string(currentYieldType_->str()) + "', got '" +
                  std::string(argTypes[0]->str()) + "'");
        }
        expr->isCoroYield = true;
    }

    // Mark yieldAll() as coroutine delegation
    if (ident && ident->name == "yieldAll" && func->isBuiltin && func->paramTypes.size() == 1) {
        if (!inCoroutineBody_) {
            error(expr->loc, "'yieldAll' can only be used inside a 'coro fn' body");
        } else {
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

    // If auto-mapped, wrap return type in Array or List for each depth level
    if (isAutoMapped) {
        Type* retType = func->returnType;
        // If the function returns Void, auto-mapping just returns Void
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

    return func->returnType;
}

// --- Resolve type expressions ---

Type* TypeChecker::resolveTypeExpr(TypeExpr* typeExpr) {
    if (!typeExpr) return nullptr;

    if (typeExpr->kind == ASTNode::NamedType) {
        auto* named = static_cast<NamedTypeNode*>(typeExpr);
        // Check active type parameter bindings (for template monomorphization)
        auto tpIt = typeParamBindings_.find(named->name);
        if (tpIt != typeParamBindings_.end()) return tpIt->second;
        if (named->name == "Int") return compiler_.intType();
        if (named->name == "Float") return compiler_.floatType();
        if (named->name == "String") return compiler_.stringType();
        if (named->name == "Bool") return compiler_.boolType();
        if (named->name == "Symbol") return compiler_.symbolType();
        if (named->name == "Void") return compiler_.voidType();
        if (named->name == "Fraction") return compiler_.fractionType();
        if (named->name == "Complex") return compiler_.complexType();
        if (named->name == "Any") return compiler_.anyType();
        // Check struct types
        auto it = structTypes_.find(named->name);
        if (it != structTypes_.end()) return it->second;
        // Check enum types
        auto eit = enumTypes_.find(named->name);
        if (eit != enumTypes_.end()) return eit->second;
        // Check concrete type aliases
        auto ait = typeAliases_.find(named->name);
        if (ait != typeAliases_.end()) return ait->second;
        // Check generic type alias used without args — error
        if (templateTypeAliases_.count(named->name)) {
            error(typeExpr->loc, "Generic type alias '" + named->name + "' requires type arguments");
            return compiler_.intType();
        }
        // Check template struct/enum names (used without type args — error with helpful message)
        if (templateStructs_.count(named->name)) {
            error(typeExpr->loc, "Template struct '" + named->name + "' requires type arguments");
            return compiler_.intType();
        }
        if (templateEnums_.count(named->name)) {
            error(typeExpr->loc, "Template enum '" + named->name + "' requires type arguments");
            return compiler_.intType();
        }
        error(typeExpr->loc, "Unknown type '" + named->name + "'");
        return compiler_.intType();
    }

    if (typeExpr->kind == ASTNode::ArrayType) {
        auto* arrayNode = static_cast<ArrayTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(arrayNode->elemType.get());
        return compiler_.arrayType(elemType);
    }

    if (typeExpr->kind == ASTNode::ListType) {
        auto* listNode = static_cast<ListTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(listNode->elemType.get());
        return compiler_.listType(elemType);
    }

    if (typeExpr->kind == ASTNode::RefType) {
        auto* refNode = static_cast<RefTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(refNode->elemType.get());
        return compiler_.refType(elemType);
    }

    if (typeExpr->kind == ASTNode::MapType) {
        auto* mapNode = static_cast<MapTypeNode*>(typeExpr);
        Type* keyType = resolveTypeExpr(mapNode->keyType.get());
        Type* valueType = resolveTypeExpr(mapNode->valueType.get());
        return compiler_.mapType(keyType, valueType);
    }

    if (typeExpr->kind == ASTNode::SetType) {
        auto* setNode = static_cast<SetTypeNode*>(typeExpr);
        Type* elemType = resolveTypeExpr(setNode->elemType.get());
        return compiler_.setType(elemType);
    }

    if (typeExpr->kind == ASTNode::TupleType) {
        auto* tupleNode = static_cast<TupleTypeNode*>(typeExpr);
        if (tupleNode->elemTypes.empty()) {
            return compiler_.voidType();  // () is Void
        }
        Vec<Type*> fields(rt::STLAllocator<Type*>(nullptr));
        for (auto& elem : tupleNode->elemTypes)
            fields.push_back(resolveTypeExpr(elem.get()));
        return compiler_.tupleType(fields);
    }

    if (typeExpr->kind == ASTNode::TemplateType) {
        auto* tmplNode = static_cast<TemplateTypeNode*>(typeExpr);
        // Resolve each type argument
        std::vector<Type*> typeArgs;
        for (auto& arg : tmplNode->typeArgs) {
            typeArgs.push_back(resolveTypeExpr(arg.get()));
        }
        // Look up as template struct
        auto sIt = templateStructs_.find(tmplNode->name);
        if (sIt != templateStructs_.end()) {
            return monomorphizeStruct(tmplNode->name, typeArgs, typeExpr->loc);
        }
        // Look up as template enum
        auto eIt = templateEnums_.find(tmplNode->name);
        if (eIt != templateEnums_.end()) {
            return monomorphizeEnum(tmplNode->name, typeArgs, typeExpr->loc);
        }
        // Look up as generic type alias
        auto taIt = templateTypeAliases_.find(tmplNode->name);
        if (taIt != templateTypeAliases_.end()) {
            return resolveTypeAlias(tmplNode->name, typeArgs, typeExpr->loc);
        }
        // Built-in parameterized types
        if (tmplNode->name == "Coroutine" && typeArgs.size() == 1) {
            return compiler_.coroutineType(typeArgs[0]);
        }
        error(typeExpr->loc, "Unknown template type '" + tmplNode->name + "'");
        return compiler_.intType();
    }

    if (typeExpr->kind == ASTNode::FunctionType) {
        auto* fnNode = static_cast<FunctionTypeNode*>(typeExpr);
        Vec<Type*> paramTypes(rt::STLAllocator<Type*>(nullptr));
        for (auto& param : fnNode->paramTypes) {
            paramTypes.push_back(resolveTypeExpr(param.get()));
        }
        Type* retType = fnNode->returnType ? resolveTypeExpr(fnNode->returnType.get()) : compiler_.voidType();
        return compiler_.functionType(paramTypes, retType);
    }

    error(typeExpr->loc, "Unknown type expression");
    return compiler_.intType();
}

Type* TypeChecker::resolveParamType(FnParam& param) {
    if (param.typeExpr) {
        if (param.isVariadic) {
            return compiler_.arrayType(resolveTypeExpr(param.typeExpr.get()));
        }
        return resolveTypeExpr(param.typeExpr.get());
    }
    // No type annotation — infer from default expression literal
    if (param.defaultExpr) {
        auto* expr = param.defaultExpr.get();
        switch (expr->kind) {
            case ASTNode::IntLiteral:      return compiler_.intType();
            case ASTNode::FloatLiteral:    return compiler_.floatType();
            case ASTNode::StringLiteral:   return compiler_.stringType();
            case ASTNode::BoolLiteral:     return compiler_.boolType();
            case ASTNode::SymbolLiteral:   return compiler_.symbolType();
            case ASTNode::FractionLiteral: return compiler_.fractionType();
            default: break;
        }
        // Try full inference (safe when called during body checking / monomorphization)
        Type* t = inferExpr(static_cast<Expr*>(expr));
        if (t) return t;
        error(param.loc, "Cannot infer type for parameter '" + param.name +
              "' from its default value; add an explicit type annotation");
        return compiler_.intType();
    }
    // No type and no default — should not happen (parser generates synthetic type params)
    return nullptr;
}

std::vector<Type*> TypeChecker::resolveAllParamTypes(std::vector<FnParam>& params) {
    // Check if any default needs inferExpr (non-literal default without type annotation)
    bool needsScope = false;
    for (auto& param : params) {
        if (param.defaultExpr && !param.typeExpr) {
            auto* expr = param.defaultExpr.get();
            switch (expr->kind) {
                case ASTNode::IntLiteral: case ASTNode::FloatLiteral:
                case ASTNode::StringLiteral: case ASTNode::BoolLiteral:
                case ASTNode::SymbolLiteral: case ASTNode::FractionLiteral:
                    break;
                default:
                    needsScope = true;
                    break;
            }
        }
        if (needsScope) break;
    }

    if (needsScope) pushScope();
    std::vector<Type*> paramTypes;
    for (auto& param : params) {
        Type* t = resolveParamType(param);
        paramTypes.push_back(t);
        if (needsScope && t) {
            declareVar(param.name, t, false);
        }
    }
    if (needsScope) popScope();
    return paramTypes;
}

// --- Template struct/enum monomorphization ---

StructType* TypeChecker::monomorphizeStruct(const std::string& name,
                                             const std::vector<Type*>& typeArgs,
                                             SourceRange loc) {
    // Check cache
    MonoKey key{name, typeArgs};
    auto cit = monoStructCache_.find(key);
    if (cit != monoStructCache_.end()) return cit->second;

    auto sIt = templateStructs_.find(name);
    if (sIt == templateStructs_.end()) {
        error(loc, "Unknown template struct '" + name + "'");
        return nullptr;
    }
    StructDeclNode* decl = sIt->second;

    if (typeArgs.size() != decl->typeParams.size()) {
        error(loc, "Template struct '" + name + "' expects " +
              std::to_string(decl->typeParams.size()) + " type arguments, got " +
              std::to_string(typeArgs.size()));
        return nullptr;
    }

    // Save and set type parameter bindings
    auto savedBindings = typeParamBindings_;
    for (size_t i = 0; i < decl->typeParams.size(); ++i) {
        typeParamBindings_[decl->typeParams[i]] = typeArgs[i];
    }

    // Check where constraints on struct
    if (!decl->whereConstraints.empty()) {
        std::unordered_map<std::string, Type*> bindings;
        for (size_t i = 0; i < decl->typeParams.size(); ++i)
            bindings[decl->typeParams[i]] = typeArgs[i];
        if (!checkWhereConstraints(decl->whereConstraints, bindings, name)) {
            typeParamBindings_ = savedBindings;
            return nullptr;
        }
    }

    // Resolve field types with bindings active
    NameTypePairVec fields(rt::STLAllocator<NameTypePair>(nullptr));
    for (auto& field : decl->fields) {
        Type* t = resolveTypeExpr(field.typeExpr.get());
        fields.push_back(NameTypePair{compiler_.intern(field.name), t});
    }

    // Build display name: Name<T1, T2, ...>
    std::string displayName = name + "<";
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        if (i > 0) displayName += ", ";
        auto s = typeArgs[i]->str();
        displayName += std::string(s.data(), s.size());
    }
    displayName += ">";

    // Create the monomorphized StructType
    auto* structType = new StructType(compiler_.intern(displayName), std::move(fields), decl->isTupleStruct);

    // Cache and register (by display name for pattern matching lookup)
    monoStructCache_[key] = structType;
    structTypes_[displayName] = structType;

    // Restore bindings
    typeParamBindings_ = savedBindings;

    return structType;
}

EnumType* TypeChecker::monomorphizeEnum(const std::string& name,
                                         const std::vector<Type*>& typeArgs,
                                         SourceRange loc) {
    // Check cache
    MonoKey key{name, typeArgs};
    auto cit = monoEnumCache_.find(key);
    if (cit != monoEnumCache_.end()) return cit->second;

    // Built-in Option<T>: delegate to VM's canonical optionType() for pointer identity
    if (name == "Option" && typeArgs.size() == 1) {
        auto* enumType = compiler_.optionType(typeArgs[0]);
        monoEnumCache_[key] = enumType;
        auto nameStr = enumType->str();
        enumTypes_[std::string(nameStr.data(), nameStr.size())] = enumType;
        return enumType;
    }

    auto eIt = templateEnums_.find(name);
    if (eIt == templateEnums_.end()) {
        error(loc, "Unknown template enum '" + name + "'");
        return nullptr;
    }
    UnionDeclNode* decl = eIt->second;

    if (typeArgs.size() != decl->typeParams.size()) {
        error(loc, "Template enum '" + name + "' expects " +
              std::to_string(decl->typeParams.size()) + " type arguments, got " +
              std::to_string(typeArgs.size()));
        return nullptr;
    }

    // Save and set type parameter bindings
    auto savedBindings = typeParamBindings_;
    for (size_t i = 0; i < decl->typeParams.size(); ++i) {
        typeParamBindings_[decl->typeParams[i]] = typeArgs[i];
    }

    // Check where constraints on enum
    if (!decl->whereConstraints.empty()) {
        std::unordered_map<std::string, Type*> bindings;
        for (size_t i = 0; i < decl->typeParams.size(); ++i)
            bindings[decl->typeParams[i]] = typeArgs[i];
        if (!checkWhereConstraints(decl->whereConstraints, bindings, name)) {
            typeParamBindings_ = savedBindings;
            return nullptr;
        }
    }

    // Resolve case types with bindings active
    NameTypePairVec cases(rt::STLAllocator<NameTypePair>(nullptr));
    for (auto& ucase : decl->cases) {
        Type* t = ucase.typeExpr ? resolveTypeExpr(ucase.typeExpr.get()) : compiler_.voidType();
        cases.push_back(NameTypePair{compiler_.intern(ucase.name), t});
    }

    // Build display name: Name<T1, T2, ...>
    std::string displayName = name + "<";
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        if (i > 0) displayName += ", ";
        auto s = typeArgs[i]->str();
        displayName += std::string(s.data(), s.size());
    }
    displayName += ">";

    // Create the monomorphized EnumType
    auto* enumType = new EnumType(compiler_.intern(displayName), std::move(cases));

    // Cache and register (by display name for pattern matching lookup)
    monoEnumCache_[key] = enumType;
    enumTypes_[displayName] = enumType;

    // Restore bindings
    typeParamBindings_ = savedBindings;

    return enumType;
}

// --- Generic type alias resolution ---

Type* TypeChecker::resolveTypeAlias(const std::string& name,
                                     const std::vector<Type*>& typeArgs,
                                     SourceRange loc) {
    // Check cache
    MonoKey key{name, typeArgs};
    auto cit = monoAliasCache_.find(key);
    if (cit != monoAliasCache_.end()) return cit->second;

    auto taIt = templateTypeAliases_.find(name);
    if (taIt == templateTypeAliases_.end()) {
        error(loc, "Unknown generic type alias '" + name + "'");
        return compiler_.intType();
    }
    TypeAliasDeclNode* decl = taIt->second;

    if (typeArgs.size() != decl->typeParams.size()) {
        error(loc, "Generic type alias '" + name + "' expects " +
              std::to_string(decl->typeParams.size()) + " type arguments, got " +
              std::to_string(typeArgs.size()));
        return compiler_.intType();
    }

    // Save and set type parameter bindings
    auto savedBindings = typeParamBindings_;
    for (size_t i = 0; i < decl->typeParams.size(); ++i) {
        typeParamBindings_[decl->typeParams[i]] = typeArgs[i];
    }

    // Check where constraints on type alias
    if (!decl->whereConstraints.empty()) {
        std::unordered_map<std::string, Type*> bindings;
        for (size_t i = 0; i < decl->typeParams.size(); ++i)
            bindings[decl->typeParams[i]] = typeArgs[i];
        if (!checkWhereConstraints(decl->whereConstraints, bindings, name)) {
            typeParamBindings_ = savedBindings;
            return compiler_.intType();
        }
    }

    // Resolve the alias body with bindings active
    Type* resolved = resolveTypeExpr(decl->typeExpr.get());

    // Cache the result
    monoAliasCache_[key] = resolved;

    // Restore bindings
    typeParamBindings_ = savedBindings;

    return resolved;
}

// --- Constraint checking ---

// Desugar constraint names used directly as parameter types into fresh type params.
// e.g. fn foo(a AsSignal, b AsSignal) Signal
//   => fn foo<__T0: AsSignal, __T1: AsSignal>(a __T0, b __T1) Signal
void TypeChecker::desugarConstraintParams(FnDeclNode* decl) {
    static const std::set<std::string> builtinTypeNames = {
        "Int", "Float", "String", "Bool", "Symbol", "Void", "Fraction", "Complex", "Any"
    };

    int genCounter = 0;

    // Recursive helper: walk a type expression tree, replacing constraint
    // names with fresh type parameters and adding where constraints.
    std::function<void(TypeExpr*)> walkTypeExpr = [&](TypeExpr* expr) {
        if (!expr) return;
        switch (expr->kind) {
        case ASTNode::NamedType: {
            auto* named = static_cast<NamedTypeNode*>(expr);
            if (constraints_.count(named->name) == 0) break;
            if (builtinTypeNames.count(named->name)) break;
            if (structTypes_.count(named->name)) break;
            if (enumTypes_.count(named->name)) break;
            if (typeAliases_.count(named->name)) break;
            bool isTypeParam = false;
            for (auto& tp : decl->typeParams) {
                if (tp == named->name) { isTypeParam = true; break; }
            }
            if (isTypeParam) break;

            std::string constraintName = named->name;
            std::string typeParamName = "__T" + std::to_string(genCounter++);
            decl->typeParams.push_back(typeParamName);
            decl->whereConstraints.push_back(
                WhereConstraint{typeParamName, constraintName, expr->loc});
            named->name = typeParamName;
            break;
        }
        case ASTNode::ArrayType:
            walkTypeExpr(static_cast<ArrayTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::ListType:
            walkTypeExpr(static_cast<ListTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::SetType:
            walkTypeExpr(static_cast<SetTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::RefType:
            walkTypeExpr(static_cast<RefTypeNode*>(expr)->elemType.get());
            break;
        case ASTNode::MapType: {
            auto* m = static_cast<MapTypeNode*>(expr);
            walkTypeExpr(m->keyType.get());
            walkTypeExpr(m->valueType.get());
            break;
        }
        case ASTNode::TupleType:
            for (auto& e : static_cast<TupleTypeNode*>(expr)->elemTypes)
                walkTypeExpr(e.get());
            break;
        case ASTNode::FunctionType: {
            auto* f = static_cast<FunctionTypeNode*>(expr);
            for (auto& p : f->paramTypes) walkTypeExpr(p.get());
            walkTypeExpr(f->returnType.get());
            break;
        }
        case ASTNode::TemplateType:
            for (auto& a : static_cast<TemplateTypeNode*>(expr)->typeArgs)
                walkTypeExpr(a.get());
            break;
        default:
            break;
        }
    };

    for (auto& param : decl->params) {
        if (!param.typeExpr) continue;
        walkTypeExpr(param.typeExpr.get());
    }
}

void TypeChecker::checkConstraintDecl(ConstraintDeclNode* decl) {
    if (constraints_.count(decl->name)) {
        error(decl->loc, "Duplicate constraint name '" + decl->name + "'");
        return;
    }

    // Pre-register the constraint name (empty info) so self-references work
    ConstraintInfo info;
    info.name = decl->name;
    info.typeParams = decl->typeParams;
    info.declNode = decl;
    constraints_[decl->name] = std::move(info);

    ConstraintInfo& infoRef = constraints_[decl->name];

    // Union items form: build constraint patterns
    if (!decl->items.empty()) {
        for (auto& typeExpr : decl->items) {
            infoRef.patterns.push_back(buildConstraintPattern(typeExpr.get()));
        }
    }

    // Structural form: store references to AST type exprs (resolved at check time)
    if (!decl->requiredFns.empty()) {
        for (auto& sig : decl->requiredFns) {
            ConstraintInfo::ReqFn reqFn;
            reqFn.name = sig.name;
            for (auto& pt : sig.paramTypes) {
                reqFn.paramTypeExprs.push_back(&pt);
            }
            reqFn.returnTypeExpr = &sig.returnType;
            infoRef.requiredFns.push_back(std::move(reqFn));
        }
    }

    // Composition form: store references to component constraints
    if (!decl->components.empty()) {
        for (auto& comp : decl->components) {
            ConstraintInfo::ComponentRef ref;
            ref.name = comp.name;
            for (auto& ta : comp.typeArgs) {
                ref.typeArgExprs.push_back(&ta);
            }
            infoRef.components.push_back(std::move(ref));
        }
    }
}

TypeChecker::ConstraintPattern TypeChecker::buildConstraintPattern(TypeExpr* expr) {
    ConstraintPattern pat;

    if (expr->kind == ASTNode::NamedType) {
        auto* named = static_cast<NamedTypeNode*>(expr);
        // Is it a known constraint or the constraint being defined?
        if (constraints_.count(named->name)) {
            pat.kind = ConstraintPattern::ConstraintRef;
            pat.constraintName = named->name;
            return pat;
        }
        // Otherwise resolve as concrete type
        pat.kind = ConstraintPattern::ConcreteType;
        pat.type = resolveTypeExpr(expr);
        return pat;
    }

    // Parameterized container types: [T], List[T], {K:V}, etc.
    if (expr->kind == ASTNode::ArrayType) {
        auto* arr = static_cast<ArrayTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(arr->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            // Pure concrete — resolve as concrete type
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Array;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::ListType) {
        auto* lst = static_cast<ListTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(lst->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::List;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::SetType) {
        auto* s = static_cast<SetTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(s->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Set;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::RefType) {
        auto* r = static_cast<RefTypeNode*>(expr);
        auto elemPat = buildConstraintPattern(r->elemType.get());
        if (elemPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Ref;
        pat.args.push_back(std::move(elemPat));
        return pat;
    }

    if (expr->kind == ASTNode::MapType) {
        auto* m = static_cast<MapTypeNode*>(expr);
        auto keyPat = buildConstraintPattern(m->keyType.get());
        auto valPat = buildConstraintPattern(m->valueType.get());
        if (keyPat.kind == ConstraintPattern::ConcreteType &&
            valPat.kind == ConstraintPattern::ConcreteType) {
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = resolveTypeExpr(expr);
            return pat;
        }
        pat.kind = ConstraintPattern::Parameterized;
        pat.ctor = ConstraintPattern::Ctor::Map;
        pat.args.push_back(std::move(keyPat));
        pat.args.push_back(std::move(valPat));
        return pat;
    }

    // TemplateType: user-defined parameterized types like Pair<T>
    if (expr->kind == ASTNode::TemplateType) {
        auto* tmpl = static_cast<TemplateTypeNode*>(expr);
        // Check if template name is a known constraint
        if (constraints_.count(tmpl->name)) {
            // Not supported as parameterized constraint pattern for now
            error(expr->loc, "Parameterized constraint references not supported in union items");
            pat.kind = ConstraintPattern::ConcreteType;
            pat.type = compiler_.intType();
            return pat;
        }
        // All concrete args → resolve as concrete
        pat.kind = ConstraintPattern::ConcreteType;
        pat.type = resolveTypeExpr(expr);
        return pat;
    }

    // All other type expressions: resolve as concrete type
    pat.kind = ConstraintPattern::ConcreteType;
    pat.type = resolveTypeExpr(expr);
    return pat;
}

bool TypeChecker::matchConstraintPattern(Type* concrete, const ConstraintPattern& pattern) {
    switch (pattern.kind) {
        case ConstraintPattern::ConcreteType:
            return concrete == pattern.type;

        case ConstraintPattern::ConstraintRef:
            return checkConstraint(concrete, pattern.constraintName, "", "", {}, false);

        case ConstraintPattern::Parameterized: {
            // Decompose concrete type and match constructor + args
            switch (pattern.ctor) {
                case ConstraintPattern::Ctor::Array: {
                    auto* at = dynamic_cast<ArrayType*>(concrete);
                    if (!at || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(at->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::List: {
                    auto* lt = dynamic_cast<ListType*>(concrete);
                    if (!lt || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(lt->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Set: {
                    auto* st = dynamic_cast<SetType*>(concrete);
                    if (!st || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(st->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Ref: {
                    auto* rt = dynamic_cast<RefType*>(concrete);
                    if (!rt || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(rt->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Range: {
                    auto* rng = dynamic_cast<RangeType*>(concrete);
                    if (!rng || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(rng->elemType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Coroutine: {
                    auto* co = dynamic_cast<CoroutineType*>(concrete);
                    if (!co || pattern.args.size() != 1) return false;
                    return matchConstraintPattern(co->yieldType_, pattern.args[0]);
                }
                case ConstraintPattern::Ctor::Map: {
                    auto* mt = dynamic_cast<MapType*>(concrete);
                    if (!mt || pattern.args.size() != 2) return false;
                    return matchConstraintPattern(mt->keyType_, pattern.args[0]) &&
                           matchConstraintPattern(mt->valueType_, pattern.args[1]);
                }
            }
            return false;
        }
    }
    return false;
}

bool TypeChecker::hasBuiltinOperator(const std::string& opName, Type* lhs, Type* rhs) {
    // Check if a binary operator is built-in for these types
    bool lhsNumeric = isNumeric(lhs);
    bool rhsNumeric = isNumeric(rhs);
    bool bothNumeric = lhsNumeric && rhsNumeric;
    bool bothInt = (lhs == compiler_.intType() || lhs == compiler_.boolType()) &&
                   (rhs == compiler_.intType() || rhs == compiler_.boolType());
    bool bothString = lhs == compiler_.stringType() && rhs == compiler_.stringType();

    if (opName == "+" || opName == "-" || opName == "*" || opName == "/") {
        return bothNumeric;
    }
    if (opName == "%" || opName == "//") {
        return bothInt;
    }
    if (opName == "==" || opName == "!=") {
        return bothNumeric || bothString;
    }
    if (opName == "<" || opName == "<=" || opName == ">" || opName == ">=") {
        if (bothString) return true;
        if (bothNumeric) {
            // Complex numbers don't support ordering
            return lhs != compiler_.complexType() && rhs != compiler_.complexType();
        }
        return false;
    }
    if (opName == "&" || opName == "|" || opName == "^" || opName == "<<" || opName == ">>" || opName == ">>>") {
        return lhs == compiler_.intType() && rhs == compiler_.intType();
    }
    if (opName == "&&" || opName == "||") {
        return true; // built-in for all types
    }
    return false;
}

bool TypeChecker::checkConstraint(Type* concreteType, const std::string& constraintName,
                                   const std::string& typeParamName, const std::string& contextName,
                                   SourceRange loc, bool emitError) {
    auto it = constraints_.find(constraintName);
    if (it == constraints_.end()) {
        if (emitError) {
            error(loc, "Unknown constraint '" + constraintName + "'");
        }
        return false;
    }

    // Recursion guard: if we're already checking this (type, constraint) pair, return false
    auto guardKey = std::make_pair(concreteType, constraintName);
    if (constraintCheckStack_.count(guardKey)) {
        return false;
    }
    constraintCheckStack_.insert(guardKey);

    // RAII cleanup for the recursion guard
    struct GuardCleanup {
        std::set<std::pair<Type*, std::string>>& stack;
        std::pair<Type*, std::string> key;
        ~GuardCleanup() { stack.erase(key); }
    } cleanup{constraintCheckStack_, guardKey};

    const ConstraintInfo& info = it->second;

    // Pattern-based constraint: check if concreteType matches any pattern (union semantics)
    if (!info.patterns.empty()) {
        for (const auto& pat : info.patterns) {
            if (matchConstraintPattern(concreteType, pat)) return true;
        }
        if (emitError) {
            auto ts = concreteType->str();
            error(loc, "Type '" + std::string(ts.data(), ts.size()) +
                  "' does not satisfy constraint '" + constraintName +
                  "' (not in type set)");
        }
        return false;
    }

    // Structural constraint: check required functions exist
    if (!info.requiredFns.empty()) {
        // Set up temporary type param bindings for resolving the constraint's type exprs
        auto savedBindings = typeParamBindings_;
        if (!info.typeParams.empty()) {
            typeParamBindings_[info.typeParams[0]] = concreteType;
        }

        bool allOk = true;
        for (auto& reqFn : info.requiredFns) {
            // Resolve param types with the concrete type substituted
            std::vector<Type*> paramTypes;
            for (auto* ptExpr : reqFn.paramTypeExprs) {
                paramTypes.push_back(resolveTypeExpr(ptExpr->get()));
            }
            // Resolve return type (for future use in return type checking)
            (void)resolveTypeExpr(reqFn.returnTypeExpr->get());

            // Check if the function exists via overload resolution
            FuncInfo* func = tryResolveOverload(reqFn.name, paramTypes);
            if (!func) {
                // Also check built-in operators
                if (paramTypes.size() == 2 && hasBuiltinOperator(reqFn.name, paramTypes[0], paramTypes[1])) {
                    // Built-in operator exists
                    continue;
                }
                // Try template resolution too
                func = tryResolveTemplate(reqFn.name, paramTypes, nullptr);
            }
            if (!func) {
                if (emitError) {
                    auto ts = concreteType->str();
                    std::string sigStr = reqFn.name + "(";
                    for (size_t i = 0; i < paramTypes.size(); ++i) {
                        if (i > 0) sigStr += ", ";
                        auto ps = paramTypes[i]->str();
                        sigStr += std::string(ps.data(), ps.size());
                    }
                    sigStr += ")";
                    error(loc, "Type '" + std::string(ts.data(), ts.size()) +
                          "' does not satisfy constraint '" + constraintName +
                          "': missing fn " + sigStr);
                }
                allOk = false;
            }
        }

        typeParamBindings_ = savedBindings;
        return allOk;
    }

    // Composition constraint: check all components
    if (!info.components.empty()) {
        auto savedBindings = typeParamBindings_;
        if (!info.typeParams.empty()) {
            typeParamBindings_[info.typeParams[0]] = concreteType;
        }

        bool allOk = true;
        for (auto& comp : info.components) {
            // Resolve type args for parameterized constraint refs
            // For simple refs (no type args), just pass the concrete type
            if (!checkConstraint(concreteType, comp.name, typeParamName, contextName, loc, emitError)) {
                allOk = false;
            }
        }

        typeParamBindings_ = savedBindings;
        return allOk;
    }

    // Empty constraint (no type-set, no requires, no components) — always satisfied
    return true;
}

bool TypeChecker::checkWhereConstraints(const std::vector<WhereConstraint>& constraints,
                                         const std::unordered_map<std::string, Type*>& bindings,
                                         const std::string& contextName, bool emitError) {
    bool allOk = true;
    for (auto& wc : constraints) {
        auto it = bindings.find(wc.typeParam);
        if (it == bindings.end()) {
            if (emitError) {
                error(wc.loc, "Where clause references unknown type param '" + wc.typeParam + "'");
            }
            allOk = false;
            continue;
        }
        Type* concreteType = it->second;
        if (!checkConstraint(concreteType, wc.constraintName, wc.typeParam, contextName, wc.loc, emitError)) {
            allOk = false;
        }
    }
    return allOk;
}

// --- Type utilities ---

bool TypeChecker::isNumeric(Type* t) const {
    if (t == compiler_.intType() || t == compiler_.floatType() || t == compiler_.boolType()
        || t == compiler_.fractionType() || t == compiler_.complexType())
        return true;
    if (auto* at = dynamic_cast<ArrayType*>(t))
        return isNumeric(at->elemType_);
    if (auto* lt = dynamic_cast<ListType*>(t))
        return isNumeric(lt->elemType_);
    if (auto* tt = dynamic_cast<TupleType*>(t)) {
        for (Type* f : tt->fields_)
            if (!isNumeric(f)) return false;
        return !tt->fields_.empty();
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

int TypeChecker::numericRank(Type* t) const {
    if (t == compiler_.boolType()) return 0;
    if (t == compiler_.intType()) return 1;
    if (t == compiler_.fractionType()) return 2;
    if (t == compiler_.floatType()) return 3;
    if (t == compiler_.complexType()) return 4;
    if (dynamic_cast<ArrayType*>(t)) return 5;
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

Type* TypeChecker::unwrapAutoMapLayers(Type* type, int depth, bool& isList, SourceRange loc) {
    Type* t = type;
    for (int level = 0; level < depth; ++level) {
        if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
            t = arrT->elemType_;
        } else if (auto* listT = dynamic_cast<ListType*>(t)) {
            t = listT->elemType_;
            isList = true;
        } else {
            error(loc, "Explicit '@' requires Array or List type (need " +
                  std::to_string(depth) + " levels, found " +
                  std::to_string(level) + ")");
            return type;
        }
    }
    return t;
}

Type* TypeChecker::wrapAutoMapResult(Type* scalarResult, const AutoMapArg& leftAM,
                                     const AutoMapArg& rightAM, bool anyList) {
    Type* retType = scalarResult;
    bool hasCartesian = (leftAM.cartesianIndex > 0) || (rightAM.cartesianIndex > 0);
    if (hasCartesian) {
        int maxIdx = std::max(leftAM.cartesianIndex, rightAM.cartesianIndex);
        for (int level = maxIdx; level >= 1; --level) {
            retType = anyList ? static_cast<Type*>(compiler_.listType(retType))
                              : static_cast<Type*>(compiler_.arrayType(retType));
        }
    } else {
        int maxDepth = std::max(leftAM.depth, rightAM.depth);
        for (int level = 0; level < maxDepth; ++level) {
            retType = anyList ? static_cast<Type*>(compiler_.listType(retType))
                              : static_cast<Type*>(compiler_.arrayType(retType));
        }
    }
    return retType;
}

// --- Overload resolution ---

bool TypeChecker::isAssignable(Type* from, Type* to) const {
    if (from == to) return true;
    // nil (nullptr) is assignable to List types
    if (!from && dynamic_cast<ListType*>(to)) return true;
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

        // For typed variadic: last paramType is Array<ElemType>, verify excess args
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
        if (fi.builtinTemplate) {
            std::vector<Type*> paramTypes;
            Type* retType = nullptr;
            CFun cfun = nullptr;
            if (fi.builtinTemplate(compiler_, argTypes, paramTypes, retType, cfun)) {
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
                    for (auto& fi2 : it->second) {
                        if (fi2.isTemplate || fi2.paramTypes.size() != paramTypes.size()) continue;
                        bool match = true;
                        for (size_t j = 0; j < paramTypes.size(); ++j) {
                            if (fi2.paramTypes[j] != paramTypes[j]) { match = false; break; }
                        }
                        if (match) {
                            if (callExpr) {
                                callExpr->resolvedFuncGlobalIndex = (i32)fi2.globalIndex;
                                callExpr->isBuiltinCall = fi2.isBuiltin;
                            }
                            return &fi2;
                        }
                    }
                }

                // For variadic builtins, check cache by paramTypes (the packed signature)
                if (builtinVariadic) {
                    if (auto* existing = tryResolveOverload(name, paramTypes)) {
                        if (callExpr) {
                            callExpr->resolvedFuncGlobalIndex = (i32)existing->globalIndex;
                            callExpr->isBuiltinCall = existing->isBuiltin;
                            // Set variadic packing info
                            int fixedCount = (int)paramTypes.size() - 1;
                            callExpr->variadicPackStart = fixedCount;
                            callExpr->variadicPackType = paramTypes.back();
                        }
                        return existing;
                    }
                }

                // Create monomorphized FuncInfo
                u32 globalIdx = compiler_.addGlobal(true);
                // Store a TupleType of paramTypes in the Primitive's type_
                // so builtins like print/println can access arg types at runtime
                TypeVec primFields(rt::STLAllocator<Type*>(nullptr));
                for (Type* pt2 : paramTypes) primFields.push_back(pt2);
                auto* primTupleType = compiler_.tupleType(primFields);
                auto* prim = new Primitive(primTupleType);
                prim->cfun_ = cfun;
                prim->rtSafe_ = fi.rtSafe;
                compiler_.global(globalIdx).o = prim;

                auto monoPtr = std::make_unique<FuncInfo>();
                monoPtr->returnType = retType;
                monoPtr->paramTypes = paramTypes;
                monoPtr->globalIndex = globalIdx;
                monoPtr->isBuiltin = true;
                monoPtr->bodyChecked = true;
                monoPtr->rtSafe = fi.rtSafe;
                monoPtr->builtinVariadicPacked = builtinVariadic;

                FuncInfo* result = monoPtr.get();
                monoStorage_.push_back(std::move(monoPtr));
                functions_[name].push_back(*result);

                if (callExpr) {
                    callExpr->resolvedFuncGlobalIndex = (i32)globalIdx;
                    callExpr->isBuiltinCall = true;
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

    // For variadic templates, pass the inferArgTypes so monomorphize knows the variadic type
    FuncInfo* result = monomorphize(*best->fi, best->bindings, best->typeArgs, callExpr);
    if (result && best->fi->isVariadic && best->fi->fixedParamCount >= 0 && callExpr) {
        callExpr->variadicPackStart = best->fi->fixedParamCount;
        callExpr->variadicPackType = result->paramTypes.back();
    }
    return result;
}

FuncInfo* TypeChecker::tryResolveModuleTemplate(
    const std::string& name,
    const std::vector<FuncInfo>& overloads,
    const std::vector<Type*>& argTypes,
    CallExpr_* callExpr) {

    struct Match {
        FuncInfo* fi;
        std::unordered_map<std::string, Type*> bindings;
        std::vector<Type*> typeArgs;
    };
    std::vector<Match> matches;

    for (auto& fi : const_cast<std::vector<FuncInfo>&>(overloads)) {
        if (!fi.isTemplate) continue;

        // Built-in template: use the resolver callback
        if (fi.builtinTemplate) {
            std::vector<Type*> paramTypes;
            Type* retType = nullptr;
            CFun cfun = nullptr;
            if (fi.builtinTemplate(compiler_, argTypes, paramTypes, retType, cfun)) {
                u32 globalIdx = compiler_.addGlobal(true);
                auto* prim = new Primitive(compiler_.voidType());
                prim->cfun_ = cfun;
                prim->rtSafe_ = fi.rtSafe;
                compiler_.global(globalIdx).o = prim;

                auto monoPtr = std::make_unique<FuncInfo>();
                monoPtr->returnType = retType;
                monoPtr->paramTypes = paramTypes;
                monoPtr->globalIndex = globalIdx;
                monoPtr->isBuiltin = true;
                monoPtr->bodyChecked = true;
                monoPtr->rtSafe = fi.rtSafe;

                FuncInfo* result = monoPtr.get();
                monoStorage_.push_back(std::move(monoPtr));

                callExpr->resolvedFuncGlobalIndex = (i32)globalIdx;
                callExpr->isBuiltinCall = true;
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

    return monomorphize(*best->fi, best->bindings, best->typeArgs, callExpr);
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

    // Allocate global slot
    u32 globalIdx = compiler_.addGlobal(true);

    // Create monomorphized FuncInfo (heap-allocated for pointer stability)
    auto monoPtr = std::make_unique<FuncInfo>();
    monoPtr->returnType = retType;
    monoPtr->paramTypes = paramTypes;
    monoPtr->globalIndex = globalIdx;
    monoPtr->isTemplate = false;
    monoPtr->isBuiltin = false;
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

        if (decl->isCoroutine) {
            auto* coroType = dynamic_cast<CoroutineType*>(retType);
            if (coroType) {
                inCoroutineBody_ = true;
                currentYieldType_ = coroType->yieldType_;
                currentReturnType_ = compiler_.voidType();
            }
        } else {
            currentReturnType_ = retType;
        }

        checkNode(decl->body.get());
        currentReturnType_ = savedReturnType;
        inCoroutineBody_ = savedInCoro;
        currentYieldType_ = savedYieldType;
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
