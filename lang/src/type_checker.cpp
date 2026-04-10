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
//  type_checker.cpp
//  lang
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

    // Register host-provided global foreign functions
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

    // Register foreign module functions (injected by ModuleCompiler for merged modules)
    if (foreignModuleFunctions_) {
        for (const auto& entry : *foreignModuleFunctions_) {
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
            info.isForeign = true;
            info.rtSafe = entry.rtSafe;
            functions_[entry.name].push_back(info);
        }
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

    // Build synthetic std ModuleInfo from all builtins
    if (!stdModuleInfo_) {
        stdModuleInfo_ = std::make_unique<ModuleInfo>();
        stdModuleInfo_->canonicalPath = "<builtin:std>";
        stdModuleInfo_->moduleName = "std";
        for (auto& [name, overloads] : functions_) {
            ExportEntry entry;
            entry.kind = ExportEntry::Func;
            entry.name = name;
            entry.funcOverloads = overloads;
            if (!overloads.empty()) {
                entry.globalIndex = overloads[0].globalIndex;
            }
            stdModuleInfo_->exports[name] = std::move(entry);
        }
        // Also export Option template enum
        {
            ExportEntry entry;
            entry.kind = ExportEntry::TemplateEnumT;
            entry.name = "Option";
            entry.templateEnumDecl = syntheticOptionDecl_.get();
            stdModuleInfo_->exports["Option"] = std::move(entry);
        }
        importedModules_["std"] = stdModuleInfo_.get();
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

    // If any import failed, stop here to avoid cascading errors
    if (hasErrors()) return;

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

    // Pre-scan: register all dynamic variable declarations (enables forward references)
    for (auto& item : program.items) {
        prescanDynVars(item.get());
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

    // Per-eval working state. allImportedModules_ is consumed by codegen
    // (genImportDecl) for the current input only — clearing it prevents the
    // codegen pass from following stale ModuleInfo* pointers from previous
    // evaluations whose modules may have since been re-compiled.
    allImportedModules_.clear();

    // monoCache_ holds template monomorphizations whose declNode pointers may
    // reference a now-destroyed AST after a dependent module re-compile.
    // However, the REPL keeps all prior programs alive (programs vector), so
    // declNode pointers remain valid across evaluations of the same session.
    // Clearing the cache unconditionally causes 100+ addGlobal calls per eval
    // (one per re-monomorphization), exhausting the rt pool within ~5 evals.
    //
    // Instead, the cache is kept across evaluations. Module recompilation is
    // handled by checkImportDecl which drops and re-adds stale overloads.
    // TODO: if a user redefines a template function body in the REPL, old
    // monomorphizations may still reference the previous body until the cache
    // is manually invalidated.

    // Phase 0: Process import declarations (before any other registration)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::ImportDecl) {
            checkImportDecl(static_cast<ImportDeclNode*>(item.get()));
        }
    }

    // If any import failed, stop here to avoid cascading errors
    if (hasErrors()) return;

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

    // Pre-scan: register all dynamic variable declarations (enables forward references)
    for (auto& item : program.items) {
        prescanDynVars(item.get());
    }

    // Second pass: type-check everything
    for (auto& item : program.items) {
        checkNode(item.get());
    }
}

// --- Pre-scan for dynamic variable declarations ---

void TypeChecker::prescanDynVars(ASTNode* node) {
    if (!node) return;
    switch (node->kind) {
        case ASTNode::VarDecl: {
            auto* decl = static_cast<VarDeclNode*>(node);
            if (decl->isDynamic) {
                Type* varType = nullptr;
                if (decl->typeExpr) {
                    varType = resolveTypeExpr(decl->typeExpr.get());
                } else if (decl->init) {
                    // Extract type from simple literals only — avoid inferExpr
                    // which could reference not-yet-registered dynamic vars
                    switch (decl->init->kind) {
                        case ASTNode::IntLiteral:     varType = compiler_.intType(); break;
                        case ASTNode::FloatLiteral:   varType = compiler_.floatType(); break;
                        case ASTNode::BoolLiteral:    varType = compiler_.boolType(); break;
                        case ASTNode::StringLiteral:  varType = compiler_.stringType(); break;
                        case ASTNode::SymbolLiteral:  varType = compiler_.symbolType(); break;
                        case ASTNode::ImaginaryLiteral: varType = compiler_.complexType(); break;
                        default: break;  // complex init — will be registered by another decl
                    }
                }
                if (varType) {
                    auto* existing = compiler_.lookupDynVar(decl->name);
                    if (!existing) {
                        u32 idx;
                        compiler_.registerDynVar(decl->name, varType, idx);
                    } else if (typesNominallyEqual(existing->type, varType)) {
                        // Refresh the stored Type*. The existing entry may hold a
                        // pointer from a previous TypeChecker whose user-defined
                        // StructType pointers have been replaced. Without this,
                        // builtin overload resolvers (which compare element types
                        // by pointer) and checkVarDecl produce a cascade of false
                        // type errors when a module is re-compiled.
                        compiler_.refreshDynVarType(decl->name, varType);
                    }
                    // Else: leave the existing type alone — checkVarDecl will detect
                    // the genuine type mismatch and report it at the offending decl.
                }
            }
            break;
        }
        case ASTNode::Block: {
            auto* block = static_cast<BlockStmt*>(node);
            for (auto& stmt : block->stmts) prescanDynVars(stmt.get());
            break;
        }
        case ASTNode::FnDecl: {
            auto* fn = static_cast<FnDeclNode*>(node);
            // Skip functions whose bodies failed to parse — their AST is unreliable.
            if (fn->hasParseError) break;
            if (fn->body) prescanDynVars(fn->body.get());
            break;
        }
        case ASTNode::IfStmt: {
            auto* ifStmt = static_cast<IfStmtNode*>(node);
            prescanDynVars(ifStmt->thenBranch.get());
            if (ifStmt->elseBranch) prescanDynVars(ifStmt->elseBranch.get());
            break;
        }
        case ASTNode::WhileStmt: {
            auto* ws = static_cast<WhileStmtNode*>(node);
            prescanDynVars(ws->body.get());
            break;
        }
        case ASTNode::ForStmt: {
            auto* fs = static_cast<ForStmtNode*>(node);
            prescanDynVars(fs->body.get());
            break;
        }
        case ASTNode::SwitchStmt: {
            auto* ss = static_cast<SwitchStmtNode*>(node);
            for (auto& c : ss->cases) prescanDynVars(c.body.get());
            break;
        }
        default:
            break;
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

} // namespace ts
