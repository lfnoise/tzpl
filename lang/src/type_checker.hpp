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
//  type_checker.hpp
//  lang
//
//  Source-to-sink type checker
//

#ifndef type_checker_hpp
#define type_checker_hpp

#include "ast.hpp"
#include "compiler.hpp"
#include "type_system.hpp"
#include <unordered_map>
#include <vector>
#include <functional>
#include <set>

namespace ts {

// Forward declarations for module system
class ModuleCompiler;
struct ModuleInfo;

// Forward declaration for built-in function pointer type
class Callable;
// CFun is the runtime function pointer type — always takes VM& (not Compiler&)
using CFun = void (*)(VM&, u16 resultReg, u16 argc, u16 argBase);

// Variable info in scope
struct VarInfo {
    Type* type;
    bool isMutable;
    bool isGlobal;       // stored in globals table
    u32  globalIndex;    // index if global
    LambdaExprNode* deferredLambda = nullptr;  // Set when var holds an untyped lambda awaiting inference
    ASTNode* deferredDecl = nullptr;           // The declaration node to update after deferred inference
};

// Callback for built-in template functions: resolves concrete types from argument types.
// Returns true if the resolver can handle these argTypes.
// On success, fills outParamTypes, outReturnType, and outCfun.
using BuiltinTemplateResolver = bool (*)(Compiler& compiler,
    const std::vector<Type*>& argTypes,
    std::vector<Type*>& outParamTypes,
    Type*& outReturnType,
    CFun& outCfun);

// Function info
struct FuncInfo {
    Type* returnType;
    std::vector<Type*> paramTypes;
    u32 globalIndex;     // CodeBlock stored as global
    bool bodyChecked = false;   // true after inference pass has checked body
    bool inferring = false;     // true while actively inferring return type (cycle detection)
    FnDeclNode* declNode = nullptr; // AST node for demand-driven inference of _ return types
    bool isBuiltin = false;     // true for primitive built-in functions (e.g. math)
    bool isForeign = false;     // true for host-registered foreign functions

    // Template function support
    bool isTemplate = false;
    std::vector<std::string> typeParams;          // ["T", "U"] for templates
    std::unordered_map<std::string, Type*> monoBindings;  // non-empty for monomorphized instances

    // Built-in template support (no AST node needed)
    BuiltinTemplateResolver builtinTemplate = nullptr;

    // Variadic function support
    bool isVariadic = false;
    int fixedParamCount = -1;  // number of non-variadic params (-1 = not variadic)
    bool builtinVariadicPacked = false; // true for monomorphized variadic builtins (args were packed)

    // Default argument support
    int numDefaults = 0;           // number of params with defaults
    int minArity = -1;             // min argc (-1 = no defaults)
    FuncInfo* canonicalFunc = nullptr; // for partial-arity entries, points to full-arity entry

    // RT safety: safe to call from a real-time VM. Default true for user-defined fns
    // (the type checker transitively prevents them from calling non-RT-safe functions).
    bool rtSafe = true;

    // Source module for imported template functions (needed for body re-checking)
    struct ModuleInfo* sourceModule = nullptr;
};

class TypeChecker {
public:
    TypeChecker(Compiler& compiler, ModuleCompiler* mc = nullptr);

    // Type-check a program, annotating AST nodes with resolved types
    void check(Program& program);

    // Type-check a REPL input (incremental: preserves state across calls)
    void checkREPLInput(Program& program);

    // Set the source file path (for module resolution and error reporting)
    void setSourceFilePath(const std::string& path) { sourceFilePath_ = path; }
    void setSourceText(const std::string& src) { sourceText_ = src; }

    // Set foreign functions to inject during registerBuiltins() (for foreign module support)
    void setForeignModuleFunctions(const std::vector<Compiler::ForeignFuncEntry>* entries) {
        foreignModuleFunctions_ = entries;
    }

    // Error access
    void clearErrors() { errors_.clear(); }
    const std::vector<CompileError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

    // Access function table (for codegen to look up globals)
    const std::unordered_map<std::string, std::vector<FuncInfo>>& functions() const { return functions_; }
    const std::unordered_map<std::string, VarInfo>& globalVars() const { return globalVars_; }

    // Access dynamic variable registry (delegated to Compiler)
    using DynVarInfo = Compiler::DynVarInfo;
    const std::unordered_map<std::string, DynVarInfo>& dynamicVars() const { return compiler_.dynamicVars(); }
    u32 numDynVars() const { return compiler_.numDynVars(); }

    // Access struct and enum type registries
    const std::unordered_map<std::string, StructType*>& structTypes() const { return structTypes_; }
    const std::unordered_map<std::string, EnumType*>& enumTypes() const { return enumTypes_; }

    // Access template struct/enum registries (for module export)
    const std::unordered_map<std::string, StructDeclNode*>& templateStructs() const { return templateStructs_; }
    const std::unordered_map<std::string, UnionDeclNode*>& templateEnums() const { return templateEnums_; }

    // Access type alias registries (for module export)
    const std::unordered_map<std::string, Type*>& typeAliases() const { return typeAliases_; }
    const std::unordered_map<std::string, TypeAliasDeclNode*>& templateTypeAliases() const { return templateTypeAliases_; }

    // Constraint system types (public for module export/import)
    struct ConstraintPattern {
        enum Kind { ConcreteType, ConstraintRef, Parameterized };
        Kind kind;
        Type* type = nullptr;              // ConcreteType: exact match
        std::string constraintName;        // ConstraintRef: check recursively

        enum class Ctor { Array, List, Map, Set, Ref, Range, Coroutine, Tuple, Function, Template };
        Ctor ctor;                                  // Parameterized: type constructor
        std::string templateName;                   // Template: original template name
        std::vector<ConstraintPattern> args;        // Parameterized: sub-patterns
    };

    struct ConstraintInfo {
        std::string name;
        std::vector<std::string> typeParams;   // e.g. ["T"]

        // Union items: patterns joined by | (types, constraint refs, parameterized)
        std::vector<ConstraintPattern> patterns;

        // Structural: required function signatures
        struct ReqFn {
            std::string name;
            std::vector<TypeExprPtr*> paramTypeExprs;  // pointers into the AST
            TypeExprPtr* returnTypeExpr;
        };
        std::vector<ReqFn> requiredFns;

        // Composition: names of constituent constraints joined by &
        struct ComponentRef {
            std::string name;
            std::vector<TypeExprPtr*> typeArgExprs;  // pointers into the AST
        };
        std::vector<ComponentRef> components;

        ConstraintDeclNode* declNode = nullptr;
    };

    // Constraint registry access (for module export)
    const std::unordered_map<std::string, ConstraintInfo>& constraints() const { return constraints_; }

    // Module system access
    const std::unordered_map<std::string, ModuleInfo*>& importedModules() const { return importedModules_; }
    const std::vector<ModuleInfo*>& allImportedModules() const { return allImportedModules_; }

    // Template function support
    const std::vector<FuncInfo*>& monoInstances() const { return monoInstances_; }
    void recheckTemplateBody(FnDeclNode* decl, FuncInfo* fi,
                             const std::unordered_map<std::string, Type*>& bindings);

    // RAII guard: temporarily merges an imported module's internal scope for template body re-checking
    struct ImportedModuleScopeGuard {
        TypeChecker& tc;
        std::vector<std::pair<std::string, size_t>> addedFunctions;  // name → original size
        std::vector<std::string> addedStructs, addedEnums, addedAliases, addedConstraints;
        std::string savedSourceFilePath;
        std::string savedSourceText;

        ImportedModuleScopeGuard(TypeChecker& tc, ModuleInfo* mod);
        ~ImportedModuleScopeGuard();
    };
    void recheckTemplateLambdaBody(LambdaExprNode* expr, LambdaType* lambdaType,
                                   TemplateLambdaType* tmplType);

private:
    Compiler& compiler_;
    ModuleCompiler* moduleCompiler_ = nullptr;
    const std::vector<Compiler::ForeignFuncEntry>* foreignModuleFunctions_ = nullptr;
    std::string sourceFilePath_;
    std::string sourceText_;
    bool builtinsRegistered_ = false;

    // Module system: whole-module imports (alias -> ModuleInfo*)
    std::unordered_map<std::string, ModuleInfo*> importedModules_;
    // All imported modules (for codegen to emit init calls)
    std::vector<ModuleInfo*> allImportedModules_;
    std::vector<CompileError> errors_;

    // Scope stack (local variables)
    std::vector<std::unordered_map<std::string, VarInfo>> scopes_;

    // Global variables
    std::unordered_map<std::string, VarInfo> globalVars_;

    // Function table (supports overloading: multiple FuncInfos per name)
    std::unordered_map<std::string, std::vector<FuncInfo>> functions_;

    // Synthetic std module (built from builtins, registered as importedModules_["std"])
    std::unique_ptr<ModuleInfo> stdModuleInfo_;

    // Struct type registry
    std::unordered_map<std::string, StructType*> structTypes_;

    // Enum type registry
    std::unordered_map<std::string, EnumType*> enumTypes_;

    // Template struct/enum declarations (unresolved)
    std::unordered_map<std::string, StructDeclNode*> templateStructs_;
    std::unordered_map<std::string, UnionDeclNode*> templateEnums_;

    // Current function return type (for checking return statements)
    Type* currentReturnType_ = nullptr;

    // Loop nesting depth (for break/continue validation)
    int loopDepth_ = 0;

    // Coroutine body tracking
    bool inCoroutineBody_ = false;
    Type* currentYieldType_ = nullptr;

    // Return type inference state
    bool inferringReturnType_ = false;   // true when inferring return type from body
    Type* inferredReturnType_ = nullptr; // collected from return statements during inference

    // Lambda capture tracking
    int lambdaBoundary_ = -1;  // scope index marking lambda boundary (-1 = not in lambda)
    std::vector<LambdaExprNode::CapturedVar>* currentCaptures_ = nullptr;

    // Template function support
    std::unordered_map<std::string, Type*> typeParamBindings_;

    struct MonoKey {
        std::string name;
        std::vector<Type*> typeArgs;
        void* templateDecl = nullptr;  // disambiguate overloads with same name+typeArgs
        bool operator==(const MonoKey& other) const {
            return name == other.name && typeArgs == other.typeArgs
                && templateDecl == other.templateDecl;
        }
    };
    struct MonoKeyHash {
        size_t operator()(const MonoKey& k) const {
            size_t h = std::hash<std::string>{}(k.name);
            for (Type* t : k.typeArgs) {
                h ^= std::hash<void*>{}(t) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            h ^= std::hash<void*>{}(k.templateDecl) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<MonoKey, FuncInfo*, MonoKeyHash> monoCache_;
    std::vector<FuncInfo*> monoInstances_;
    std::vector<std::unique_ptr<FuncInfo>> monoStorage_;  // owns mono FuncInfos for pointer stability

    // Template struct/enum monomorphization caches
    std::unordered_map<MonoKey, StructType*, MonoKeyHash> monoStructCache_;
    std::unordered_map<MonoKey, EnumType*, MonoKeyHash> monoEnumCache_;

    // Reverse maps: monomorphized type -> template origin (for constraint pattern matching)
    struct MonoOrigin {
        std::string templateName;
        std::vector<Type*> typeArgs;
    };
    std::unordered_map<Type*, MonoOrigin> monoOrigin_;

    // Concrete type aliases (resolved)
    std::unordered_map<std::string, Type*> typeAliases_;

    // Generic type alias declarations (unresolved, like templateStructs_)
    std::unordered_map<std::string, TypeAliasDeclNode*> templateTypeAliases_;

    // Generic type alias resolution cache (like monoStructCache_)
    std::unordered_map<MonoKey, Type*, MonoKeyHash> monoAliasCache_;

    // RT safety enforcement
    bool rtRestricted_ = false;  // Set from compiler_.isRTRestricted() in constructor
    bool checkRTSafety(const FuncInfo* func, const std::string& name, SourceRange loc);

    // Synthetic Option<T> template declaration (built-in, not from source)
    std::unique_ptr<UnionDeclNode> syntheticOptionDecl_;

    // Constraint registry
    std::unordered_map<std::string, ConstraintInfo> constraints_;

    // Recursion guard for constraint checking
    std::set<std::pair<Type*, std::string>> constraintCheckStack_;

    // Scope management
    void pushScope();
    void popScope();
    void declareVar(const std::string& name, Type* type, bool isMutable);
    VarInfo* lookupVar(const std::string& name);

    // Desugar constraint-as-param-type: fn foo(a C) => fn foo<__T0: C>(a __T0)
    void desugarConstraintParams(FnDeclNode* decl);

    // Check nodes
    void checkNode(ASTNode* node);
    void checkBlock(BlockStmt* block);
    void checkImportDecl(ImportDeclNode* decl);
    void checkLetDecl(LetDeclNode* decl);
    void checkVarDecl(VarDeclNode* decl);
    void checkConstDecl(ConstDeclNode* decl);
    void checkFnDecl(FnDeclNode* decl);
    void checkStructDecl(StructDeclNode* decl);
    void checkUnionDecl(UnionDeclNode* decl);
    void checkIfStmt(IfStmtNode* stmt);
    void checkWhileStmt(WhileStmtNode* stmt);
    void checkForStmt(ForStmtNode* stmt);
    void checkSwitchStmt(SwitchStmtNode* stmt);
    void checkReturnStmt(ReturnStmtNode* stmt);
    void checkBreakStmt(BreakStmtNode* stmt);
    void checkContinueStmt(ContinueStmtNode* stmt);
    void checkAssignStmt(AssignStmtNode* stmt);
    void checkExprStmt(ExprStmtNode* stmt);

    // Pattern checking (validates and introduces bindings)
    void checkPattern(Pattern* pat, Type* subjectType, bool isMutable = false, bool inMatch = false);

    // Infer expression types (returns resolved type)
    Type* inferExpr(Expr* expr, Type* expectedType = nullptr);
    Type* inferBinaryOp(BinaryOpExpr* expr);
    Type* inferUnaryOp(UnaryOpExpr* expr);
    Type* inferCall(CallExpr_* expr);

    // Extracted helpers for inferCall (Steps 1-7)
    Type* finalizeResolvedCall(CallExpr_* expr, FuncInfo* func,
                               const std::string& name,
                               const std::vector<Type*>& argTypes);
    FuncInfo* tryImplicitAutoMap(const std::string& name,
                                 const std::vector<Type*>& argTypes,
                                 CallExpr_* expr,
                                 bool& isAutoMapped, bool& hasListArg);
    FuncInfo* tryImplicitAutoMapInner(const std::string& name,
                                      const std::vector<Type*>& unwrappedTypes,
                                      const std::vector<AutoMapArg>& explicitAutoMap,
                                      CallExpr_* expr);
    Type* computeAutoMapReturnType(Type* scalarReturn,
                                    const std::vector<AutoMapArg>& autoMapArgs,
                                    const std::vector<AutoMapArg>& innerAutoMapArgs,
                                    bool hasCartesian, int maxCartesianIndex,
                                    bool anyListArg);
    Type* tryInferEnumConstruct(CallExpr_* expr, FieldExpr_* fe, IdentifierExpr* ident);
    Type* tryInferTupleStructConstruct(CallExpr_* expr, IdentifierExpr* ident);
    Type* tryInferVariableCall(CallExpr_* expr, IdentifierExpr* ident);
    Type* inferIndirectCall(CallExpr_* expr);

    Type* inferIdentifier(IdentifierExpr* expr);
    Type* inferLambdaExpr(LambdaExprNode* expr);
    Type* inferTemplateLambdaExpr(LambdaExprNode* expr);
    LambdaType* monomorphizeTemplateLambda(TemplateLambdaType* tmplType,
                                            const std::vector<Type*>& argTypes,
                                            SourceRange loc);
    void discoverCaptures(LambdaExprNode* expr);

    // Return type inference helpers
    Type* getBlockTrailingType(ASTNode* node);
    Type* getNodeTrailingType(ASTNode* node);
    void inferFunctionReturnType(FnDeclNode* fn, FuncInfo* fi, bool isLocal = false);

    // Resolve a type expression AST node to a Type*
    Type* resolveTypeExpr(TypeExpr* typeExpr);

    // Resolve a parameter's type, inferring from default expression if no type annotation
    Type* resolveParamType(FnParam& param);

    // Resolve all parameter types, with preceding params in scope for default inference
    std::vector<Type*> resolveAllParamTypes(std::vector<FnParam>& params);

    // Built-in function registration
    void registerBuiltins();

    // Overload resolution
    FuncInfo* resolveOverload(const std::string& name, const std::vector<Type*>& argTypes, SourceRange loc);
    FuncInfo* tryResolveOverload(const std::string& name, const std::vector<Type*>& argTypes);
    bool isAssignable(Type* from, Type* to) const;

    // Template function support
    bool inferTypeParams(const std::vector<std::string>& typeParams,
                         const std::vector<FnParam>& params,
                         const std::vector<Type*>& argTypes,
                         std::unordered_map<std::string, Type*>& bindings);
    bool unifyTypeExpr(TypeExpr* texpr, Type* concrete,
                       const std::vector<std::string>& typeParams,
                       std::unordered_map<std::string, Type*>& bindings);
    FuncInfo* tryResolveTemplate(const std::string& name,
                                 const std::vector<Type*>& argTypes,
                                 CallExpr_* callExpr);
    FuncInfo* tryResolveModuleTemplate(const std::string& name,
                                       const std::vector<FuncInfo>& overloads,
                                       const std::vector<Type*>& argTypes,
                                       CallExpr_* callExpr);
    FuncInfo* monomorphize(FuncInfo& templateFI,
                           const std::unordered_map<std::string, Type*>& bindings,
                           const std::vector<Type*>& typeArgs,
                           CallExpr_* callExpr);

    // Template struct/enum monomorphization
    StructType* monomorphizeStruct(const std::string& name, const std::vector<Type*>& typeArgs, SourceRange loc);
    EnumType* monomorphizeEnum(const std::string& name, const std::vector<Type*>& typeArgs, SourceRange loc);

    // Generic type alias resolution
    Type* resolveTypeAlias(const std::string& name, const std::vector<Type*>& typeArgs, SourceRange loc);

    // Type utilities
    bool isNumeric(Type* t) const;
    bool isBoolComposite(Type* t) const;
    bool isIntComposite(Type* t) const;
    bool typesEqual(Type* a, Type* b) const;
    int numericRank(Type* t) const;
    Type* commonNumericType(Type* a, Type* b, bool isDiv = false) const;
    Type* comparisonResultType(Type* a, Type* b) const;

    // Constraint checking
    void checkConstraintDecl(ConstraintDeclNode* decl);
    ConstraintPattern buildConstraintPattern(TypeExpr* expr);
    bool matchConstraintPattern(Type* concrete, const ConstraintPattern& pattern);
    bool checkConstraint(Type* concreteType, const std::string& constraintName,
                         const std::string& typeParamName, const std::string& contextName,
                         SourceRange loc, bool emitError = true);
    bool checkWhereConstraints(const std::vector<WhereConstraint>& constraints,
                               const std::unordered_map<std::string, Type*>& bindings,
                               const std::string& contextName, bool emitError = true);
    bool hasBuiltinOperator(const std::string& opName, Type* lhs, Type* rhs);

    // Auto-map helpers (shared between inferCall and inferBinaryOp)
    AutoMapArg extractAutoMapAnnotation(Expr* expr) const;
    Type* unwrapAutoMapLayers(Type* type, int depth, bool& isList, SourceRange loc);
    Type* wrapAutoMapResult(Type* scalarResult, const AutoMapArg& leftAM,
                            const AutoMapArg& rightAM, bool anyList);

    void error(SourceRange loc, const std::string& msg);

    // Pre-scan: recursively find and register all dynamic variable declarations
    void prescanDynVars(ASTNode* node);
};

} // namespace ts

#endif /* type_checker_hpp */
