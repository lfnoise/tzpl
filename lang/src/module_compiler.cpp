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
//  module_compiler.cpp
//  lang
//
//  Multi-file module compilation implementation
//

#include "module_compiler.hpp"
#include "compiler.hpp"
#include "value.hpp"
#include "type_system.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "type_checker.hpp"
#include "codegen.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace ts {

ModuleCompiler::ModuleCompiler(Compiler& compiler, std::vector<std::string> includePaths)
    : compiler_(compiler), includePaths_(std::move(includePaths))
{
    // Append TZPL_PATH directories
    if (const char* envPath = std::getenv("TZPL_PATH")) {
        std::string pathStr(envPath);
        size_t start = 0;
        while (start < pathStr.size()) {
            size_t end = pathStr.find(':', start);
            if (end == std::string::npos) end = pathStr.size();
            std::string dir = pathStr.substr(start, end - start);
            if (!dir.empty()) {
                includePaths_.push_back(std::move(dir));
            }
            start = end + 1;
        }
    }
}

std::string ModuleCompiler::resolveModulePath(
    const std::vector<std::string>& modulePath,
    const std::string& importingDir) const
{
    // Build relative file path from module path: ["std", "math"] -> "std/math.x"
    std::string relPath;
    for (size_t i = 0; i < modulePath.size(); ++i) {
        if (i > 0) relPath += '/';
        relPath += modulePath[i];
    }
    relPath += ".x";

    // 1. Try relative to importing file's directory
    if (!importingDir.empty()) {
        std::filesystem::path candidate = std::filesystem::path(importingDir) / relPath;
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::canonical(candidate).string();
        }
    }

    // 2. Try each include path (CLI -I paths + TZPL_PATH)
    for (const auto& dir : includePaths_) {
        std::filesystem::path candidate = std::filesystem::path(dir) / relPath;
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::canonical(candidate).string();
        }
    }

    return "";  // not found
}

// Read entire file into string (compile-time only, system allocator OK)
static std::string readModuleFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Materialize foreign function entries into FuncInfos, adding globals and Primitives.
// Used both for pure foreign modules and for injecting into a .x module's TypeChecker.
static void materializeForeignFunctions(
    Compiler& compiler,
    const std::vector<Compiler::ForeignFuncEntry>& entries,
    std::unordered_map<std::string, std::deque<FuncInfo>>& functions)
{
    for (const auto& entry : entries) {
        u32 idx = compiler.addGlobal(true);
        auto* prim = new Primitive(compiler.voidType());
        prim->cfun_ = entry.cfun;
        prim->pure_ = entry.pure;
        prim->rtSafe_ = entry.rtSafe;
        prim->ffiData_ = entry.ffiData;
        compiler.global(idx).o = prim;

        FuncInfo info;
        info.returnType = entry.returnType;
        info.paramTypes = entry.paramTypes;
        info.globalIndex = idx;
        info.bodyChecked = true;
        info.isBuiltin = true;
        info.isForeign = true;
        info.rtSafe = entry.rtSafe;
        functions[entry.name].push_back(info);
    }
}

ModuleInfo* ModuleCompiler::compileModule(
    const std::vector<std::string>& modulePath,
    const std::string& importingFilePath,
    SourceRange loc, std::vector<CompileError>& errors)
{
    // Re-entrancy guard. compileModule() is called recursively while resolving
    // imports; we only want to sweep stale modules at the OUTERMOST call so that
    // the dependency cascade has a consistent view of the cache.
    struct DepthGuard {
        int& d;
        DepthGuard(int& depth) : d(depth) { ++d; }
        ~DepthGuard() { --d; }
    } guard(compileDepth_);

    // At the outermost call, walk every cached module and invalidate any whose
    // source file has been edited on disk. Cascade invalidation ensures every
    // dependent of an edited module is also dropped, so its stale declNode
    // pointers into the destroyed AST cannot be used.
    if (compileDepth_ == 1) {
        sweepStaleModules();
    }

    // Determine importing file's directory
    std::string importingDir;
    if (!importingFilePath.empty()) {
        importingDir = std::filesystem::path(importingFilePath).parent_path().string();
    }

    // Check for host-registered foreign module functions
    std::string modName = modulePath.back();
    const auto* foreignFuncs = compiler_.foreignModuleFunctions(modName);

    // Resolve file path
    std::string resolvedPath = resolveModulePath(modulePath, importingDir);

    if (resolvedPath.empty() && !foreignFuncs) {
        std::string dotName;
        for (size_t i = 0; i < modulePath.size(); ++i) {
            if (i > 0) dotName += '.';
            dotName += modulePath[i];
        }
        errors.push_back(CompileError(CompileError::TypeError, loc,
            "Cannot find module '" + dotName + "'"));
        return nullptr;
    }

    // Use a synthetic cache key for pure foreign modules (no .x file)
    std::string cacheKey = resolvedPath.empty()
        ? "<foreign:" + modName + ">"
        : resolvedPath;

    // Targeted invalidation for the requested module: covers nested compileModule
    // calls (within an outermost call) where the sweep has already run but a
    // recursive import might still race a freshly modified file.
    if (!resolvedPath.empty()) {
        auto it = modules_.find(cacheKey);
        if (it != modules_.end()) {
            std::error_code ec;
            auto currentModTime = std::filesystem::last_write_time(resolvedPath, ec);
            if (!ec && currentModTime != it->second->fileModTime) {
                invalidateModule(cacheKey);
            }
        }
    }

    // Check cache
    {
        auto it = modules_.find(cacheKey);
        if (it != modules_.end()) {
            ModuleInfo* mod = it->second.get();
            if (mod->compiling) {
                errors.push_back(CompileError(CompileError::TypeError, loc,
                    "Circular import detected for module '" + cacheKey + "'"));
                return nullptr;
            }
            if (mod->failed) {
                for (const auto& err : mod->compilationErrors) {
                    errors.push_back(err);
                }
                errors.push_back(CompileError(CompileError::TypeError, loc,
                    "Module '" + modName + "' had compilation errors"));
                return nullptr;
            }
            return mod;
        }
    }

    // --- Pure foreign module (no .x file) ---
    if (resolvedPath.empty()) {
        auto modPtr = std::make_unique<ModuleInfo>();
        ModuleInfo* mod = modPtr.get();
        mod->canonicalPath = cacheKey;
        mod->moduleName = modName;
        modules_[cacheKey] = std::move(modPtr);

        // Materialize foreign functions into FuncInfo entries
        std::unordered_map<std::string, std::deque<FuncInfo>> functions;
        materializeForeignFunctions(compiler_, *foreignFuncs, functions);

        // Build exports (skip '_' prefixed — they are module-private)
        for (const auto& [name, overloads] : functions) {
            if (name.empty() || name[0] == '_') continue;
            ExportEntry entry;
            entry.kind = ExportEntry::Func;
            entry.name = name;
            entry.funcOverloads = overloads;
            entry.type = overloads[0].returnType;
            entry.globalIndex = overloads[0].globalIndex;
            mod->exports[name] = std::move(entry);
        }

        mod->allFunctions = std::move(functions);
        return mod;
    }

    // --- File-based module (possibly augmented with foreign functions) ---

    // Create module info
    auto modPtr = std::make_unique<ModuleInfo>();
    ModuleInfo* mod = modPtr.get();
    mod->canonicalPath = resolvedPath;
    mod->moduleName = modName;
    mod->compiling = true;
    {
        std::error_code ec;
        mod->fileModTime = std::filesystem::last_write_time(resolvedPath, ec);
    }
    modules_[resolvedPath] = std::move(modPtr);

    // Read source file
    std::string source = readModuleFile(resolvedPath);
    if (source.empty()) {
        CompileError err(CompileError::TypeError, loc,
            "Cannot read module file '" + resolvedPath + "'");
        mod->compilationErrors.push_back(err);
        errors.push_back(err);
        mod->compiling = false;
        mod->failed = true;
        return nullptr;
    }

    // Compute display path (relative to CWD) for error diagnostics
    std::string displayPath = std::filesystem::relative(
        resolvedPath, std::filesystem::canonical(std::filesystem::current_path())).string();

    // Lex
    Lexer lexer(source, displayPath);

    // Parse — store AST in ModuleInfo so template declNode pointers stay valid
    Parser parser(lexer);
    mod->ast = parser.parseProgram();
    if (parser.hasErrors()) {
        mod->compilationErrors.insert(mod->compilationErrors.end(),
            parser.errors().begin(), parser.errors().end());
        for (const auto& err : parser.errors()) errors.push_back(err);
        mod->compiling = false;
        mod->failed = true;
        return nullptr;
    }
    Program& program = mod->ast;

    // Type check (with module compiler for recursive imports)
    TypeChecker typeChecker(compiler_, this);
    typeChecker.setSourceFilePath(displayPath);
    typeChecker.setSourceText(source);

    // Pass foreign module functions to the TypeChecker so they're materialized
    // during registerBuiltins(), after standard builtins are registered
    if (foreignFuncs) {
        typeChecker.setForeignModuleFunctions(foreignFuncs);
    }

    typeChecker.check(program);
    if (typeChecker.hasErrors()) {
        mod->compilationErrors.insert(mod->compilationErrors.end(),
            typeChecker.errors().begin(), typeChecker.errors().end());
        for (const auto& err : typeChecker.errors()) errors.push_back(err);
        mod->compiling = false;
        mod->failed = true;
        return nullptr;
    }

    // Code generation
    CodeGen codegen(compiler_, typeChecker);
    codegen.setSourceFilePath(displayPath);
    codegen.setSourceText(source);
    CodeBlock* initBlock = codegen.generate(program, true);  // module init ends with return, not halt
    if (codegen.hasErrors()) {
        mod->compilationErrors.insert(mod->compilationErrors.end(),
            codegen.errors().begin(), codegen.errors().end());
        for (const auto& err : codegen.errors()) errors.push_back(err);
        mod->compiling = false;
        mod->failed = true;
        return nullptr;
    }

    mod->initBlock = initBlock;

    // Allocate global slots for the init block and init flag
    mod->initBlockGlobalIndex = compiler_.addGlobal(false);  // CodeBlock is not a GCObj
    compiler_.global(mod->initBlockGlobalIndex).p = initBlock;
    mod->initFlagGlobalIndex = compiler_.addGlobal(false);   // flag is an integer

    // Build export table
    // Export functions (skip private and underscore-prefixed)
    for (const auto& [name, overloads] : typeChecker.functions()) {
        if (name.empty() || name[0] == '_') continue;
        bool allPrivate = true;
        std::deque<FuncInfo> exportedOverloads;
        for (const auto& fi : overloads) {
            if (fi.isBuiltin && !fi.isForeign) continue;
            bool isPriv = false;
            if (fi.declNode && fi.declNode->isPrivate) isPriv = true;
            if (!isPriv) {
                exportedOverloads.push_back(fi);
                allPrivate = false;
            }
        }
        if (!allPrivate && !exportedOverloads.empty()) {
            ExportEntry entry;
            entry.kind = ExportEntry::Func;
            entry.name = name;
            entry.funcOverloads = std::move(exportedOverloads);
            entry.type = entry.funcOverloads[0].returnType;
            entry.globalIndex = entry.funcOverloads[0].globalIndex;
            mod->exports[name] = std::move(entry);
        }
    }

    // Export global variables
    for (const auto& [name, varInfo] : typeChecker.globalVars()) {
        if (name.empty() || name[0] == '_') continue;
        ExportEntry entry;
        entry.kind = ExportEntry::Var;
        entry.name = name;
        entry.type = varInfo.type;
        entry.globalIndex = varInfo.globalIndex;
        mod->exports[name] = std::move(entry);
    }

    // Export struct types
    for (const auto& [name, stype] : typeChecker.structTypes()) {
        if (name.empty() || name[0] == '_') continue;
        ExportEntry entry;
        entry.kind = ExportEntry::StructT;
        entry.name = name;
        entry.structType = stype;
        mod->exports[name] = std::move(entry);
    }

    // Export enum types
    for (const auto& [name, etype] : typeChecker.enumTypes()) {
        if (name.empty() || name[0] == '_') continue;
        ExportEntry entry;
        entry.kind = ExportEntry::EnumT;
        entry.name = name;
        entry.enumType = etype;
        mod->exports[name] = std::move(entry);
    }

    // Export template struct types (AST kept alive in mod->ast)
    for (const auto& [name, sdecl] : typeChecker.templateStructs()) {
        if (name.empty() || name[0] == '_') continue;
        if (sdecl->isPrivate) continue;
        ExportEntry entry;
        entry.kind = ExportEntry::TemplateStructT;
        entry.name = name;
        entry.templateStructDecl = sdecl;
        mod->exports[name] = std::move(entry);
    }

    // Export template enum types (AST kept alive in mod->ast)
    for (const auto& [name, udecl] : typeChecker.templateEnums()) {
        if (name.empty() || name[0] == '_') continue;
        if (udecl->isPrivate) continue;
        ExportEntry entry;
        entry.kind = ExportEntry::TemplateEnumT;
        entry.name = name;
        entry.templateEnumDecl = udecl;
        mod->exports[name] = std::move(entry);
    }

    // Export concrete type aliases
    for (const auto& [name, type] : typeChecker.typeAliases()) {
        if (name.empty() || name[0] == '_') continue;
        // Check isPrivate by scanning program items
        bool isPriv = false;
        for (auto& item : program.items) {
            if (item->kind == ASTNode::TypeAliasDecl) {
                auto* ta = static_cast<TypeAliasDeclNode*>(item.get());
                if (ta->name == name && ta->isPrivate) { isPriv = true; break; }
            }
        }
        if (isPriv) continue;
        ExportEntry entry;
        entry.kind = ExportEntry::TypeAlias;
        entry.name = name;
        entry.aliasType = type;
        mod->exports[name] = std::move(entry);
    }

    // Export generic type aliases
    for (const auto& [name, decl] : typeChecker.templateTypeAliases()) {
        if (name.empty() || name[0] == '_') continue;
        if (decl->isPrivate) continue;
        ExportEntry entry;
        entry.kind = ExportEntry::TemplateTypeAlias;
        entry.name = name;
        entry.templateTypeAliasDecl = decl;
        mod->exports[name] = std::move(entry);
    }

    // Export constraints
    for (const auto& [name, cinfo] : typeChecker.constraints()) {
        if (name.empty() || name[0] == '_') continue;
        ExportEntry entry;
        entry.kind = ExportEntry::ConstraintT;
        entry.name = name;
        entry.constraintInfo = cinfo;
        mod->exports[name] = std::move(entry);
    }

    // Store source info for correct error diagnostics in imported templates
    mod->sourceFilePath = displayPath;
    mod->sourceText = source;

    // Store all internal names for template body re-checking in importers
    mod->allFunctions = typeChecker.functions();
    mod->allStructTypes = typeChecker.structTypes();
    mod->allEnumTypes = typeChecker.enumTypes();
    mod->allTypeAliases = typeChecker.typeAliases();
    mod->allConstraints = typeChecker.constraints();

    // Record direct module dependencies so editing a transitive dependency can
    // cascade-invalidate this module on a future compileModule call.
    mod->dependencies.clear();
    for (auto* dep : typeChecker.allImportedModules()) {
        if (dep && !dep->canonicalPath.empty()) {
            mod->dependencies.push_back(dep->canonicalPath);
        }
    }

    mod->compiling = false;
    return mod;
}

void ModuleCompiler::invalidateModule(const std::string& cacheKey) {
    auto it = modules_.find(cacheKey);
    if (it == modules_.end()) return;

    // Erase first so the recursive walk below cannot see this entry.
    // Destroying the unique_ptr drops the ModuleInfo and its AST/codegen state.
    modules_.erase(it);

    // Find every cached module that lists `cacheKey` in its dependencies and
    // recursively invalidate it. Build the list before recursing so that map
    // mutations from nested invalidate calls cannot invalidate iterators.
    std::vector<std::string> dependents;
    for (auto& [key, modPtr] : modules_) {
        const auto& deps = modPtr->dependencies;
        if (std::find(deps.begin(), deps.end(), cacheKey) != deps.end()) {
            dependents.push_back(key);
        }
    }
    for (auto& dep : dependents) {
        invalidateModule(dep);
    }
}

void ModuleCompiler::sweepStaleModules() {
    // Snapshot the keys we need to check, since invalidateModule will mutate
    // modules_ and we cannot iterate-and-erase concurrently.
    std::vector<std::string> stale;
    for (auto& [key, modPtr] : modules_) {
        if (modPtr->canonicalPath.empty()) continue;  // foreign-only module
        std::error_code ec;
        auto currentModTime = std::filesystem::last_write_time(modPtr->canonicalPath, ec);
        if (!ec && currentModTime != modPtr->fileModTime) {
            stale.push_back(key);
        }
    }
    for (auto& key : stale) {
        invalidateModule(key);
    }
}

ModuleInfo* ModuleCompiler::getModule(const std::string& canonicalPath) const {
    auto it = modules_.find(canonicalPath);
    if (it != modules_.end()) return it->second.get();
    return nullptr;
}

} // namespace ts
