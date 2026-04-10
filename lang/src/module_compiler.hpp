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
//  module_compiler.hpp
//  lang
//
//  Multi-file module compilation coordinator
//

#ifndef module_compiler_hpp
#define module_compiler_hpp

#include "ast.hpp"
#include "error.hpp"
#include "type_checker.hpp"
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <memory>
#include <filesystem>

namespace ts {

// Forward declarations
class VM;
class Compiler;
class Type;
class CodeBlock;
class StructType;
class EnumType;
struct StructDeclNode;
struct UnionDeclNode;
struct TypeAliasDeclNode;

// A single exported name from a module
struct ExportEntry {
    enum Kind { Func, Var, StructT, EnumT, TemplateStructT, TemplateEnumT, TypeAlias, TemplateTypeAlias, ConstraintT };
    Kind kind;
    std::string name;
    Type* type = nullptr;
    u32 globalIndex = 0;
    std::deque<FuncInfo> funcOverloads;  // for overloaded functions
    StructType* structType = nullptr;
    EnumType* enumType = nullptr;
    StructDeclNode* templateStructDecl = nullptr;
    UnionDeclNode* templateEnumDecl = nullptr;
    Type* aliasType = nullptr;
    TypeAliasDeclNode* templateTypeAliasDecl = nullptr;
    TypeChecker::ConstraintInfo constraintInfo;  // for constraint exports
};

// Information about a compiled module
struct ModuleInfo {
    std::string canonicalPath;
    std::string moduleName;
    std::unordered_map<std::string, ExportEntry> exports;
    Program ast;  // kept alive so template declNode pointers remain valid
    CodeBlock* initBlock = nullptr;
    u32 initBlockGlobalIndex = 0; // global slot storing the init CodeBlock pointer
    u32 initFlagGlobalIndex = 0;  // global slot for "initialized" flag
    bool initialized = false;
    bool compiling = false;  // cycle detection
    bool failed = false;     // true if compilation failed (suppress cascading errors)
    std::filesystem::file_time_type fileModTime {};  // for cache invalidation
    std::vector<CompileError> compilationErrors;  // stored so re-imports can report them

    // Source file info — needed for correct error diagnostics in imported templates
    std::string sourceFilePath;
    std::string sourceText;

    // All functions (including private) — needed for template body re-checking
    std::unordered_map<std::string, std::deque<FuncInfo>> allFunctions;
    // All types from the module scope — needed for template body re-checking
    std::unordered_map<std::string, StructType*> allStructTypes;
    std::unordered_map<std::string, EnumType*> allEnumTypes;
    std::unordered_map<std::string, Type*> allTypeAliases;
    std::unordered_map<std::string, TypeChecker::ConstraintInfo> allConstraints;

    // Canonical paths of modules this module directly imports. Used for cascade
    // invalidation: when a dependency is edited and re-compiled, every module
    // that has stale declNode/type pointers into the old AST must also be
    // invalidated. Populated after successful compilation.
    std::vector<std::string> dependencies;
};

class ModuleCompiler {
public:
    ModuleCompiler(Compiler& compiler, std::vector<std::string> includePaths);

    // Compile a module identified by its dotted path.
    // Returns nullptr on error (errors appended to the provided vector).
    ModuleInfo* compileModule(const std::vector<std::string>& modulePath,
                              const std::string& importingFilePath,
                              SourceRange loc, std::vector<CompileError>& errors);

    // Look up a cached module by canonical path
    ModuleInfo* getModule(const std::string& canonicalPath) const;

    Compiler& compiler() { return compiler_; }
    const std::vector<std::string>& includePaths() const { return includePaths_; }

    // Invalidate a single module from the cache and recursively invalidate
    // any other cached module that depends on it. Used by sweepStaleModules.
    void invalidateModule(const std::string& cacheKey);

    // Walk every cached module and invalidate (with cascade) any whose source
    // file has been modified on disk since it was compiled. Called at the
    // start of every outermost compileModule() call so that editing a
    // transitive dependency forces a re-compile of all dependents.
    void sweepStaleModules();

private:
    Compiler& compiler_;
    std::vector<std::string> includePaths_;  // CLI + env paths
    std::unordered_map<std::string, std::unique_ptr<ModuleInfo>> modules_;
    int compileDepth_ = 0;  // re-entrancy counter so we sweep only at outermost call

    // Resolve a module path to a filesystem path. Returns empty string on failure.
    std::string resolveModulePath(const std::vector<std::string>& modulePath,
                                   const std::string& importingDir) const;
};

} // namespace ts

#endif /* module_compiler_hpp */
