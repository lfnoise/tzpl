//
//  module_compiler.hpp
//  static-lang-3
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
#include <unordered_map>
#include <memory>

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
    std::vector<FuncInfo> funcOverloads;  // for overloaded functions
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

    // Source file info — needed for correct error diagnostics in imported templates
    std::string sourceFilePath;
    std::string sourceText;

    // All functions (including private) — needed for template body re-checking
    std::unordered_map<std::string, std::vector<FuncInfo>> allFunctions;
    // All types from the module scope — needed for template body re-checking
    std::unordered_map<std::string, StructType*> allStructTypes;
    std::unordered_map<std::string, EnumType*> allEnumTypes;
    std::unordered_map<std::string, Type*> allTypeAliases;
    std::unordered_map<std::string, TypeChecker::ConstraintInfo> allConstraints;
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

private:
    Compiler& compiler_;
    std::vector<std::string> includePaths_;  // CLI + env paths
    std::unordered_map<std::string, std::unique_ptr<ModuleInfo>> modules_;

    // Resolve a module path to a filesystem path. Returns empty string on failure.
    std::string resolveModulePath(const std::vector<std::string>& modulePath,
                                   const std::string& importingDir) const;
};

} // namespace ts

#endif /* module_compiler_hpp */
