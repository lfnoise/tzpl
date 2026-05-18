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
//  vm.hpp
//  lang
//
//  Register-based Virtual Machine with integrated memory management
//

#ifndef vm_hpp
#define vm_hpp

#include "base_types.hpp"
#include "tlsf_allocator.hpp"
#include "stl_allocator.hpp"
#include "symbol.hpp"
#include "gc.hpp"
#include "arc.hpp"
#include "type_universe.hpp"
#include <atomic>
#include <cstdio>
#include <memory>

namespace ts {

// Forward declarations
class Type;
class Obj;
class CodeBlock;
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
class CoroutineObj;
class CoroutineFrame;
struct CompileResult;
class Primitive;
struct VMTargetData;
using VMTarget = std::shared_ptr<VMTargetData>;

// VM-allocated type aliases for STL containers
template <typename T>
using Vec = rt::VMVector<T>;

using VMString = rt::VMString;

template <typename Key, typename Value,
          typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
using Map = std::unordered_map<Key, Value, Hash, Equal,
    rt::STLAllocator<std::pair<const Key, Value>>>;

// Word - 64-bit untagged union for register values
// Words are untagged because types are statically known at compile time
union Word {
    i64 i;         // Integer / Bool
    f64 f;         // Float
    SymbolPtr s;   // Symbol (interned string)
    void* p;       // Generic pointer
    Obj* o;        // Object pointer

    Word() : i(0) {}
    explicit Word(i64 val) : i(val) {}
    explicit Word(f64 val) : f(val) {}
    explicit Word(SymbolPtr val) : s(val) {}
    explicit Word(void* val) : p(val) {}
    explicit Word(Obj* val) : o(val) {}
};

// Base class for all runtime objects
class Obj : public GCObj {
public:
    Type* type_;

    Obj(Type* type) : type_(type) {}

    virtual ~Obj() = default;

    virtual VMString str() const = 0;

    rt::TLSFAllocator* getAllocator() const;
};

// Forward declarations for direct-threaded dispatch
class VM;
union Code;

// Operation function pointer: each opcode handler tail-calls the next
using Operation = void (*)(VM& vm, Code* pc);

// Code word - each instruction is 2-3 Code words
union Code {
    Operation op;      // Opcode handler function pointer
    i64       i;       // Integer immediate
    f64       f;       // Float immediate
    u16       regs[4]; // Packed register indices (dst, src1, src2, src3)
    SymbolPtr s;       // Symbol immediate
    void*     p;       // Non-GC pointer (e.g., Code* jump target)

    Code() : i(0) {}
    explicit Code(Operation val) : op(val) {}
    explicit Code(i64 val) : i(val) {}
    explicit Code(f64 val) : f(val) {}
    explicit Code(SymbolPtr val) : s(val) {}
    explicit Code(void* val) : p(val) {}

    // Helper to pack register indices
    static Code makeRegs(u16 r0, u16 r1 = 0, u16 r2 = 0, u16 r3 = 0) {
        Code c;
        c.regs[0] = r0;
        c.regs[1] = r1;
        c.regs[2] = r2;
        c.regs[3] = r3;
        return c;
    }
};

// Dynamic scope save entry: saved value before rebinding.
//
// Phase 4g.5: inline-composite dynvars occupy sizeWords_ consecutive slots.
// For those, sizeWords > 1, type is non-null, and savedValue.i is a Word
// offset into dynStackPayload_ where the saved payload starts. For 1-word
// dynvars, sizeWords = 1, type may be null, and savedValue holds the Word.
struct DynSaveEntry {
    u32   varIndex;    // first slot index of the rebound dynvar
    u32   sizeWords;   // 1 for single-word; >1 for inline composite
    Type* type;        // layout for ARC walking on restore (multi-word only)
    Word  savedValue;  // 1-word: saved value. Multi-word: payloadOffset.
    bool  isObj;       // whether savedValue is an Obj* (1-word case only)
};

// Call frame for register-based VM
struct CallFrame {
    Code*      returnPC;    // Resume point in caller
    CodeBlock* codeBlock;   // Compiled function (GC root - holds objConstants)
    u32        baseReg;     // Start of this frame's register window
    u32        numRegs;     // Registers allocated for this frame
    u16        resultReg;   // Caller's register for return value
    u32        dynStackMark; // Dynamic scope stack level at function entry
};

// Forward declaration — defined in compiler.hpp
class Compiler;

// xoshiro256** — fast, high-quality PRNG (Blackman & Vigna, public domain)
struct Xoshiro256 {
    u64 s[4];

    // Seed from a single u64 using SplitMix64 to fill all 4 state words
    void seed(u64 seed) {
        auto splitmix = [](u64& x) -> u64 {
            u64 z = (x += 0x9e3779b97f4a7c15ULL);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            return z ^ (z >> 31);
        };
        s[0] = splitmix(seed);
        s[1] = splitmix(seed);
        s[2] = splitmix(seed);
        s[3] = splitmix(seed);
    }

    u64 next() {
        auto rotl = [](u64 x, int k) -> u64 { return (x << k) | (x >> (64 - k)); };
        u64 result = rotl(s[1] * 5, 7) * 9;
        u64 t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
};

// Virtual Machine
class VM {
    friend class GCObj;

private:
    // Memory allocator
    rt::TLSFAllocator allocator_;

    // ARC infrastructure
    AutoReleasePool autoReleasePool_;
    DeferredDeleteQueue deferredDeleteQueue_;
    ForeignDeleteQueue foreignDeleteQueue_;

    // Register file (TLSF-allocated flat array)
    Word* regs_;
    u32   maxRegs_;

    // Call frame stack (TLSF-allocated)
    CallFrame* frames_;
    u32        maxFrames_;
    u32        frameCount_;

    // Current execution state
    u32   baseReg_;
    Code* pc_;

    // Global variables (mutable, persist across events)
    Vec<Word> globals_;
    Vec<u8> globalIsObj_;  // Track which globals hold Obj* for GC

    // Dynamic scope variables
    Vec<Word> dynVars_;
    Vec<u8>   dynVarIsObj_;  // Track which dynvars hold Obj* for GC

    // Dynamic scope save stack (for save/restore on function return)
    DynSaveEntry* dynStack_;
    u32           dynStackTop_;
    u32           maxDynStack_;

    // Phase 4g.5: side buffer for saved payloads of inline-composite dynvars.
    // Each multi-word DynSaveEntry holds a Word offset into this buffer; the
    // next sizeWords Words there are the saved payload.
    Word* dynStackPayload_;
    u32   dynStackPayloadTop_;
    u32   maxDynStackPayload_;

    // Flag set by HALT instruction
    bool halted_;

    // Coroutine state
    CoroutineObj*    currentCoroutine_ = nullptr;
    CoroutineFrame*  currentCoroFrame_ = nullptr;
    Word*            currentRegs_ = nullptr;

    // Type universe (shared, system-allocated)
    TypeUniverse& typeUniverse_;

    // Current primitive being called (set by op_call_primitive)
    Primitive* currentPrimitive_;

    // Print output callback (defaults to stdout)
    FILE* printOutput_;

    // List print limit (default 10 elements before showing "...")
    i64 listPrintLimit_;

    // Per-VM random number generator (xoshiro256**, seeded per instance)
    Xoshiro256 rng_;

    // Opaque user data for foreign functions to access host state
    void* userData_ = nullptr;

    // Shared compilation target (links this VM to its global layout)
    VMTarget target_;

public:
    // Phase 1 of tracing-GC project: safepoint poll flag. Read on the hot path
    // by op_safepoint (relaxed load + branch). Set by arcEnqueueForDeletion
    // when the deferred-delete queue crosses kSafepointTriggerSize so that the
    // queue gets drained inside hot loops, not only between events. Atomic
    // because future phases (audio-thread VM, foreign-delete-queue producers)
    // may set the flag from off-thread.
    std::atomic<bool> gcRequested_{false};
    static constexpr u32 kSafepointTriggerSize = 1024;
    static constexpr u32 kSafepointBudget = 512;

    // Drain the deferred-delete queue under a bounded budget. Safe to call
    // from any safepoint inside execution -- it does not touch the register
    // file, just consumes already-released objects.
    void safepointPoll();

    // Phase 3 of tracing-GC project: head of an intrusive doubly-linked list
    // of every GCObj owned by this VM. Sweep walks this list to find whites.
    // linkObjToAllList()/unlinkObjFromAllList() (in vm.cpp) maintain it.
    GCObj* allObjsHead_ = nullptr;

    // Tracing GC instance (defined in tracing_gc.hpp). Pimpl-ed via
    // unique_ptr so vm.hpp doesn't need to include the full class definition
    // (and so the GC's data can be reset/replaced in tests).
    std::unique_ptr<class TracingGC> tracingGC_;
    class TracingGC& tracingGC();

    // Constructor with pool size, shared type universe, and optional target
    explicit VM(usize poolSize, TypeUniverse& typeUniverse, const VMTarget& target = {});

    ~VM();

    // Non-copyable
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    // Object allocation
    template<typename T, typename... Args>
    T* allocObj(Args&&... args) {
        usize size = sizeof(T);
        void* mem = allocator_.allocate(size);
        if (!mem) return nullptr;
        T* obj = new (mem) T(std::forward<Args>(args)...);
        return obj;
    }

    // Accessors
    rt::TLSFAllocator& allocator() { return allocator_; }
    const rt::TLSFAllocator& allocator() const { return allocator_; }

    // ARC heartbeat - drain auto-release pool and process deferred deletions.
    // Also drains the foreign delete queue (objects whose last reference was
    // dropped on a different thread).
    void gcHeartbeat() {
        foreignDeleteQueue_.drainInto(deferredDeleteQueue_);
        autoReleasePool_.drain();
        // Process at least 256 items, or 2% of the queue, whichever is larger.
        // This scales with pressure while keeping idle heartbeats cheap.
        u32 qsz = deferredDeleteQueue_.size();
        u32 budget = (qsz + 49) / 50;
        if (budget < 256) budget = 256;
        deferredDeleteQueue_.processN(budget);
    }

    // ARC access
    AutoReleasePool& autoReleasePool() { return autoReleasePool_; }
    DeferredDeleteQueue& deferredDeleteQueue() { return deferredDeleteQueue_; }
    ForeignDeleteQueue& foreignDeleteQueue() { return foreignDeleteQueue_; }

    // Type universe access
    TypeUniverse& typeUniverse() { return typeUniverse_; }
    const TypeUniverse& typeUniverse() const { return typeUniverse_; }

    // Built-in types (convenience — delegate to TypeUniverse)
    const BuiltinTypes& types() const { return typeUniverse_.types(); }
    const BuiltinSymbols& syms() const { return typeUniverse_.syms(); }
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

    // Interned composite types (convenience — delegate to TypeUniverse)
    ArrayType* arrayType(Type* elemType) { return typeUniverse_.arrayType(elemType); }
    ListType* listType(Type* elemType) { return typeUniverse_.listType(elemType); }
    RangeType* rangeType(Type* elemType) { return typeUniverse_.rangeType(elemType); }
    RefType* refType(Type* elemType) { return typeUniverse_.refType(elemType); }
    TupleType* tupleType(const Vec<Type*>& fields) { return typeUniverse_.tupleType(fields); }
    FunctionType* functionType(const Vec<Type*>& argTypes, Type* returnType) {
        return typeUniverse_.functionType(argTypes, returnType);
    }
    MapType* mapType(Type* keyType, Type* valueType) { return typeUniverse_.mapType(keyType, valueType); }
    SetType* setType(Type* elemType) { return typeUniverse_.setType(elemType); }
    EnumType* optionType(Type* elemType) { return typeUniverse_.optionType(elemType); }
    CoroutineType* coroutineType(Type* yieldType) { return typeUniverse_.coroutineType(yieldType); }

    // Coroutine accessors
    bool inCoroutine() const { return currentCoroutine_ != nullptr; }
    CoroutineObj* currentCoroutine() const { return currentCoroutine_; }
    CoroutineFrame* currentCoroFrame() const { return currentCoroFrame_; }
    void setCurrentCoroutine(CoroutineObj* c) { currentCoroutine_ = c; }
    void setCurrentCoroFrame(CoroutineFrame* f) { currentCoroFrame_ = f; }
    void setCurrentRegs(Word* r) { currentRegs_ = r; }
    void setBaseReg(u32 b) { baseReg_ = b; }
    void setFrameCount(u32 c) { frameCount_ = c; }
    Word* regsBase() { return regs_; }

    // Make this VM current for allocations
    void makeCurrent();

    // --- Register-based VM execution ---

    // Access register relative to current frame's base
    inline Word& reg(u16 i) { return currentRegs_[i]; }
    inline const Word& reg(u16 i) const { return currentRegs_[i]; }

    // Current frame access
    u32 baseReg() const { return baseReg_; }
    u32 frameCount() const { return frameCount_; }
    Code* pc() const { return pc_; }
    CodeBlock* currentCodeBlock() const;

    // Update current frame's codeBlock (used by tail call opcodes)
    void updateCurrentCodeBlock(CodeBlock* cb) { frames_[frameCount_ - 1].codeBlock = cb; }
    void growCurrentFrameNumRegs(u32 n) {
        auto& f = frames_[frameCount_ - 1];
        if (n > f.numRegs) f.numRegs = n;
    }

    // Current frame's register count (used by coro_resume to place registers after caller's window)
    u32 currentFrameNumRegs() const { return frames_[frameCount_ - 1].numRegs; }

    // Install a CompileResult: make compiled code ready for execution.
    // Validates that the result's target matches this VM's target.
    void install(const CompileResult& result);

    // Target accessors
    void setTarget(const VMTarget& target) { target_ = target; }
    const VMTarget& target() const { return target_; }

    // Execute a code block (entry point for running compiled code)
    Word execute(CodeBlock* block);

    // Push a call frame for function calls (used by CALL opcode)
    void pushFrame(Code* returnPC, CodeBlock* codeBlock, u32 newBase, u32 numRegs, u16 resultReg);

    // Pop a call frame - returns the popped frame's data
    CallFrame popFrame();

    // Debug: dump call stack to stderr
    void dumpCallStack() const;

    // --- Global variables ---

    u32 addGlobal(bool isObj = false) {
        u32 idx = (u32)globals_.size();
        globals_.push_back(Word());
        globalIsObj_.push_back(isObj ? 1 : 0);
        return idx;
    }

    void setGlobalIsObj(u32 idx, bool isObj) { globalIsObj_[idx] = isObj ? 1 : 0; }
    bool globalIsObj(u32 idx) const { return globalIsObj_[idx] != 0; }

    Word& global(u32 idx) { return globals_[idx]; }
    const Word& global(u32 idx) const { return globals_[idx]; }
    u32 numGlobals() const { return (u32)globals_.size(); }

    // --- Dynamic scope variables ---

    u32 addDynVar(bool isObj = false) {
        u32 idx = (u32)dynVars_.size();
        dynVars_.push_back(Word());
        dynVarIsObj_.push_back(isObj ? 1 : 0);
        return idx;
    }

    // Phase 4g.5: allocate sizeWords consecutive slots for an inline-composite
    // dynvar. Continuation slots get isObj=false (per-Word isObj is unused for
    // multi-word dynvars; ARC walks the layout instead).
    u32 addInlineDynVar(u32 sizeWords) {
        if (sizeWords == 0) sizeWords = 1;
        u32 idx = (u32)dynVars_.size();
        for (u32 i = 0; i < sizeWords; ++i) {
            dynVars_.push_back(Word());
            dynVarIsObj_.push_back(0);
        }
        return idx;
    }

    void setDynVarIsObj(u32 idx, bool isObj) { dynVarIsObj_[idx] = isObj ? 1 : 0; }
    bool dynVarIsObj(u32 idx) const { return dynVarIsObj_[idx] != 0; }

    Word& dynVar(u32 idx) { return dynVars_[idx]; }
    const Word& dynVar(u32 idx) const { return dynVars_[idx]; }
    u32 numDynVars() const { return (u32)dynVars_.size(); }

    // Save current value and set new one (called by op_dynscope_push)
    void dynScopePush(u32 varIdx, Word newValue) {
        if (dynStackTop_ >= maxDynStack_) {
            throw std::runtime_error("Dynamic scope stack overflow");
        }
        auto& entry = dynStack_[dynStackTop_++];
        entry.varIndex = varIdx;
        entry.sizeWords = 1;
        entry.type = nullptr;
        entry.savedValue = dynVars_[varIdx];
        entry.isObj = dynVarIsObj_[varIdx];
        dynVars_[varIdx] = newValue;
    }

    // Phase 4g.5: save the current inline-composite payload onto the side
    // payload buffer, then overwrite the dynvar with the new payload read from
    // `newPayload[0..sizeWords)`. ARC: Obj* fields in the OLD payload are
    // transferred to the save buffer (no retain/release); Obj* fields in the
    // NEW payload are retained.
    void dynScopePushInline(u32 varIdx, Word const* newPayload, Type* type);

    // Restore dynamic variables back to a saved mark
    void dynScopeRestore(u32 mark);

    u32 dynStackTop() const { return dynStackTop_; }

    // Halted state
    bool isHalted() const { return halted_; }
    void setHalted(bool h) { halted_ = h; }

    // Print output
    FILE* printOutput() const { return printOutput_; }
    void setPrintOutput(FILE* f) { printOutput_ = f; }

    // List print limit
    i64 listPrintLimit() const { return listPrintLimit_; }
    void setListPrintLimit(i64 n) { listPrintLimit_ = n; }

    // Current primitive (set during op_call_primitive for builtins that need type info)
    Primitive* currentPrimitive() const { return currentPrimitive_; }
    void setCurrentPrimitive(Primitive* p) { currentPrimitive_ = p; }

    // Random number generator (xoshiro256**)
    Xoshiro256& rng() { return rng_; }

    // Evaluate a Primitive with given arguments at compile time.
    // Safe because the VM isn't executing during codegen.
    Word evalPrimitive(Primitive* prim, const Word* args, u16 argc);

    // User data (opaque pointer for foreign functions to access host state)
    void  setUserData(void* data) { userData_ = data; }
    void* userData() const { return userData_; }

    // Call a compiled function from the host (e.g. event handler from audio callback).
    // Args are copied into registers. Returns the value in register 0.
    Word callFunction(CodeBlock* block, const Word* args, u16 argc);

    // Call a Callable (Lambda or Primitive) from the host.
    // Handles free variable setup for closures.
    Word callCallable(Obj* callable, const Word* args, u16 argc);
};

// Thread-local current VM (set by VM::makeCurrent(), null during compilation)
extern thread_local VM* gCurrentVM;

// Thread-local current TypeUniverse (set by VM::makeCurrent() and Compiler::makeCurrent())
extern thread_local TypeUniverse* gCurrentTypeUniverse;

// Thread-local current Compiler (set by Compiler::makeCurrent(), null during execution)
class Compiler;
extern thread_local Compiler* gCurrentCompiler;

// Register a newly constructed GCObj.
// During compilation: tracked by the Compiler (immortal, system-allocated).
// During runtime: added to auto-release pool with initial refcount.
void registerNewObj(GCObj* obj);

} // namespace ts

#endif /* vm_hpp */
