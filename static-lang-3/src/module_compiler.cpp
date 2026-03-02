//
//  module_compiler.cpp
//  static-lang-3
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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace ts {

ModuleCompiler::ModuleCompiler(Compiler& compiler, std::vector<std::string> includePaths)
    : compiler_(compiler), includePaths_(std::move(includePaths))
{
    // Append LANGX_PATH directories
    if (const char* envPath = std::getenv("LANGX_PATH")) {
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

    // 2. Try each include path (CLI -I paths + LANGX_PATH)
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

ModuleInfo* ModuleCompiler::compileModule(
    const std::vector<std::string>& modulePath,
    const std::string& importingFilePath,
    SourceRange loc, std::vector<CompileError>& errors)
{
    // Determine importing file's directory
    std::string importingDir;
    if (!importingFilePath.empty()) {
        importingDir = std::filesystem::path(importingFilePath).parent_path().string();
    }

    // Resolve file path
    std::string resolvedPath = resolveModulePath(modulePath, importingDir);
    if (resolvedPath.empty()) {
        std::string modName;
        for (size_t i = 0; i < modulePath.size(); ++i) {
            if (i > 0) modName += '.';
            modName += modulePath[i];
        }
        errors.push_back(CompileError(CompileError::TypeError, loc,
            "Cannot find module '" + modName + "'"));
        return nullptr;
    }

    // Check cache
    auto it = modules_.find(resolvedPath);
    if (it != modules_.end()) {
        ModuleInfo* mod = it->second.get();
        if (mod->compiling) {
            errors.push_back(CompileError(CompileError::TypeError, loc,
                "Circular import detected for module '" + resolvedPath + "'"));
            return nullptr;
        }
        return mod;
    }

    // Create module info
    auto modPtr = std::make_unique<ModuleInfo>();
    ModuleInfo* mod = modPtr.get();
    mod->canonicalPath = resolvedPath;
    mod->moduleName = modulePath.back();
    mod->compiling = true;
    modules_[resolvedPath] = std::move(modPtr);

    // Read source file
    std::string source = readModuleFile(resolvedPath);
    if (source.empty()) {
        errors.push_back(CompileError(CompileError::TypeError, loc,
            "Cannot read module file '" + resolvedPath + "'"));
        mod->compiling = false;
        return nullptr;
    }

    // Lex
    Lexer lexer(source, resolvedPath);

    // Parse — store AST in ModuleInfo so template declNode pointers stay valid
    Parser parser(lexer);
    mod->ast = parser.parseProgram();
    if (parser.hasErrors()) {
        for (const auto& err : parser.errors()) errors.push_back(err);
        mod->compiling = false;
        return nullptr;
    }
    Program& program = mod->ast;

    // Type check (with module compiler for recursive imports)
    TypeChecker typeChecker(compiler_, this);
    typeChecker.setSourceFilePath(resolvedPath);
    typeChecker.check(program);
    if (typeChecker.hasErrors()) {
        for (const auto& err : typeChecker.errors()) errors.push_back(err);
        mod->compiling = false;
        return nullptr;
    }

    // Code generation
    CodeGen codegen(compiler_, typeChecker);
    CodeBlock* initBlock = codegen.generate(program, true);  // module init ends with return, not halt
    if (codegen.hasErrors()) {
        for (const auto& err : codegen.errors()) errors.push_back(err);
        mod->compiling = false;
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
        std::vector<FuncInfo> exportedOverloads;
        for (const auto& fi : overloads) {
            if (fi.isBuiltin) continue;
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

    mod->compiling = false;
    return mod;
}

ModuleInfo* ModuleCompiler::getModule(const std::string& canonicalPath) const {
    auto it = modules_.find(canonicalPath);
    if (it != modules_.end()) return it->second.get();
    return nullptr;
}

} // namespace ts
