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
//  compiler.hpp
//  lang
//
//  Compiler: owns compilation state, fully decoupled from VM execution.
//  One Compiler per process, runs on a non-real-time thread.
//  Multiple VMs may run on any thread. CompileResult is the sole bridge.
//

#ifndef compiler_hpp
#define compiler_hpp

#include "vm.hpp"
#include "error.hpp"
#include <string>
#include <vector>
#include <memory>

namespace ts {

// Forward declarations
class ArrayType;
class ListType;
class RangeType;
class RefType;
class MapType;
class SetType;
class TupleType;
class FunctionType;
class EnumType;
class CoroutineType;
class ModuleCompiler;

// CFun is the runtime function pointer type — always takes VM& (not Compiler&)
using CFun = void (*)(VM&, u16 resultReg, u16 argc, u16 argBase);

// Result of compilation — everything needed to install and execute compiled code.
// Forward declare VMTarget so CompileResult can hold one.
struct VMTargetData;
using VMTarget = std::shared_ptr<VMTargetData>;

struct CompileResult {
    bool success = false;
    CodeBlock* mainBlock = nullptr;
    std::vector<CompileError> errors;

    // New global slots to be installed into the VM
    struct GlobalSlot {
        Word value;
        bool isObj = false;
        // Non-null on the BASE slot of a multi-word INLINE composite global that
        // embeds Obj* fields: the GC must walk this slot's layout (gcScanPayload)
        // to trace those embedded pointers, since isObj single-word marking can't.
        Type* inlineObjType = nullptr;
    };
    std::vector<GlobalSlot> newGlobals;
    u32 globalBase = 0;  // VM global index where newGlobals starts

    // Number of dynamic scope variables used
    u32 numDynVars = 0;

    // Per-dynvar GC root flag (size == numDynVars). True where a dynvar slot
    // holds an Obj* and must be scanned as a GC root. Like globals, this uses
    // storesObjPtr(), not isObjType(): a DiscriminantEnum/Atom dynvar holds a
    // non-pointer and must NOT be marked.
    std::vector<u8> dynVarIsObj;
    // Per-dynvar inline-composite type (size == numDynVars, mostly null): a
    // multi-word inline dynvar embedding Obj* fields. The GC walks its layout.
    std::vector<Type*> dynVarInlineType;

    // The target this result was compiled against
    VMTarget target;

    // Exported function metadata (for host-to-VM calling)
    struct ExportedFunc {
        std::string name;
        u32 globalIndex;
        std::vector<Type*> paramTypes;
        Type* returnType;
        bool isCodeBlock;  // true = CodeBlock*, false = Primitive*
    };
    std::vector<ExportedFunc> exportedFunctions;
};

// Shared compilation target — holds the global layout that a Compiler and
// any number of VMs share.  Created by Compiler::createTarget().
struct VMTargetData {
    u32 globalCount = 0;
    std::vector<CompileResult::GlobalSlot> allGlobals;
    u32 compileGlobalBase = 0;
    bool rtRestricted = false;
};

class Compiler {
public:
    // Construct with a TypeUniverse (required).
    // The Compiler is fully decoupled from any VM.
    explicit Compiler(TypeUniverse& types);

    ~Compiler();

    // --- VM target management ---
    // Create a shared compilation target. The Compiler and any number of VMs
    // may hold a reference. Lifetime is managed by shared_ptr.
    VMTarget createTarget(bool rtRestricted = false);

    // --- Compilation ---

    // Compile source code through the full pipeline: Lex → Parse → TypeCheck → CodeGen.
    // target identifies which global layout to use.
    // Returns a CompileResult with the main CodeBlock (or errors on failure).
    // Internally calls makeCurrent()/endCurrent() to manage thread-local state.
    CompileResult compile(const std::string& source, const std::string& filename,
                          const VMTarget& target, ModuleCompiler* moduleCompiler = nullptr);

    // --- Manual compilation pipeline support (for REPL) ---
    // Begin a compilation session for a specific VM target.
    // Sets thread-locals for compilation context.
    // Must be paired with endCurrent().
    void makeCurrent(const VMTarget& target);

    // End a compilation session, restoring thread-locals.
    // Does NOT switch to any VM context — caller must call vm.makeCurrent() if needed.
    void endCurrent();

    // Harvest pending globals from the current compilation session.
    // Returns (newGlobals, globalBase) for building a CompileResult manually.
    // Call this after codegen but before endCurrent().
    std::pair<std::vector<CompileResult::GlobalSlot>, u32> takePendingGlobals();

    // Track a newly allocated object created during compilation.
    // These objects live forever (never collected by GC).
    void trackObject(GCObj* obj);

    // Number of objects allocated during compilation (cumulative across all compiles)
    u32 numTrackedObjects() const { return numTrackedObjects_; }

    // Evaluate a primitive at compile time (constant folding).
    // Uses a private eval VM with a tiny memory pool.
    // Returns true if evaluation succeeded (result stored in outResult).
    bool evalPrimitive(Primitive* prim, const Word* args, u16 argc, Word& outResult);

    // --- Type accessors (delegate to TypeUniverse) ---
    Type* typeType() const { return typeUniverse_.types().typeType; }
    Type* boolType() const { return typeUniverse_.types().boolType; }
    Type* intType() const { return typeUniverse_.types().intType; }
    Type* floatType() const { return typeUniverse_.types().floatType; }
    Type* symbolType() const { return typeUniverse_.types().symbolType; }
    Type* stringType() const { return typeUniverse_.types().stringType; }
    Type* fractionType() const { return typeUniverse_.types().fractionType; }
    Type* complexType() const { return typeUniverse_.types().complexType; }
    Type* voidType() const { return typeUniverse_.types().voidType; }
    Type* anyType() const { return typeUniverse_.types().anyType; }
    TupleType* unitType() const { return typeUniverse_.types().unitType; }

    // --- Composite type interning (delegate to TypeUniverse) ---
    ArrayType* arrayType(Type* elemType) { return typeUniverse_.arrayType(elemType); }
    ListType* listType(Type* elemType) { return typeUniverse_.listType(elemType); }
    RangeType* rangeType(Type* elemType) { return typeUniverse_.rangeType(elemType); }
    RefType* refType(Type* elemType) { return typeUniverse_.refType(elemType); }
    MapType* mapType(Type* keyType, Type* valueType) { return typeUniverse_.mapType(keyType, valueType); }
    SetType* setType(Type* elemType) { return typeUniverse_.setType(elemType); }
    PersistentVectorType* persistentVectorType(Type* elemType) { return typeUniverse_.persistentVectorType(elemType); }
    PersistentMapType* persistentMapType(Type* keyType, Type* valueType) { return typeUniverse_.persistentMapType(keyType, valueType); }
    EnumType* optionType(Type* elemType) { return typeUniverse_.optionType(elemType); }
    CoroutineType* coroutineType(Type* yieldType) { return typeUniverse_.coroutineType(yieldType); }
    TupleType* tupleType(const Vec<Type*>& fields) { return typeUniverse_.tupleType(fields); }
    FunctionType* functionType(const Vec<Type*>& argTypes, Type* returnType) {
        return typeUniverse_.functionType(argTypes, returnType);
    }

    // --- Symbol interning ---
    SymbolPtr intern(std::string_view name) { return ts::intern(name); }

    // --- Global variables (compiler-owned, deferred until install) ---
    // These operate on the current VMTarget's layout (set by makeCurrent or compile).
    u32 addGlobal(bool isObj = false);
    // Phase 4g.5: allocate sizeWords consecutive slots for an inline-composite
    // global so its payload is stored inline (no per-store box). Returns the
    // first slot index. Continuation slots get isObj=false.
    u32 addInlineGlobal(u32 sizeWords, Type* inlineType = nullptr);
    void setGlobalIsObj(u32 idx, bool isObj);
    bool isGlobalObj(u32 idx) const;
    Word& global(u32 idx);
    const Word& global(u32 idx) const;
    u32 numGlobals() const;
    u32 compileGlobalBase() const;

    // Query whether the current VM target is RT-restricted
    bool isRTRestricted() const;

    // --- Foreign function registration ---
    struct ForeignFuncEntry {
        std::string name;
        Type* returnType;
        std::vector<Type*> paramTypes;
        CFun cfun;
        bool pure;
        bool rtSafe;
        void* ffiData = nullptr;  // Opaque data stored on the Primitive (e.g. C callback pointer)
    };

    void registerForeignFunction(const std::string& name, Type* returnType,
                                  std::vector<Type*> paramTypes, CFun cfun,
                                  bool pure = false, bool rtSafe = false);

    const std::vector<ForeignFuncEntry>& foreignFunctions() const { return foreignFunctions_; }

    // --- Foreign module registration ---
    // Register a foreign function into a named module namespace.
    // Functions prefixed with '_' will be private to the module.
    void registerForeignModuleFunction(const std::string& moduleName,
                                        const std::string& funcName,
                                        Type* returnType,
                                        std::vector<Type*> paramTypes, CFun cfun,
                                        bool pure = false, bool rtSafe = false);

    // Look up foreign functions for a module. Returns nullptr if no foreign module registered.
    const std::vector<ForeignFuncEntry>* foreignModuleFunctions(const std::string& moduleName) const;

    // Optimization flags (can be disabled individually for debugging)
    bool enableRegReclaim = true;
    bool enableConstFold = true;
    bool enableTailCalls = true;

    // --- Dynamic scope variable registry (shared across all TypeCheckers) ---
    struct DynVarInfo {
        Type* type;
        u32 dynIndex;
    };

    const std::unordered_map<std::string, DynVarInfo>& dynamicVars() const;
    u32 numDynVars() const;
    bool registerDynVar(const std::string& name, Type* type, u32& outIndex);
    const DynVarInfo* lookupDynVar(const std::string& name) const;
    // Replace the stored Type* for an existing dynamic variable. Used to refresh
    // stale Type* pointers left over from a previous TypeChecker instance whose
    // user-defined StructType/EnumType pointers have been replaced.
    void refreshDynVarType(const std::string& name, Type* newType);

private:
    TypeUniverse& typeUniverse_;
    u32 numTrackedObjects_ = 0;

    // Current compilation target (set by makeCurrent, cleared by endCurrent)
    VMTarget currentTarget_;

    // Private eval VM for constant folding (lazily created)
    std::unique_ptr<VM> evalVM_;

    // Foreign functions registered by host before compilation
    std::vector<ForeignFuncEntry> foreignFunctions_;

    // Foreign functions grouped by module name
    std::unordered_map<std::string, std::vector<ForeignFuncEntry>> foreignModuleFunctions_;

    // Dynamic scope variable registry (shared across all TypeCheckers/modules)
    std::unordered_map<std::string, DynVarInfo> dynamicVars_;
    u32 nextDynIndex_ = 0;
};

} // namespace ts

#endif /* compiler_hpp */
