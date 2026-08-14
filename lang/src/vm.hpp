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
#include "tracing_gc.hpp"   // for gcMonoNanos() used by gcHeartbeat()
#include "type_universe.hpp"
#include "vm_config.hpp"
#include "async_io.hpp"     // AsyncIOJob for the submitAsyncIO host hook
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>

namespace ts {

// Forward declarations
class Type;
class Obj;
class UpVar;
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
class FutureType;
class Future;
class CoroutineObj;
class CoroutineFrame;
class ActorObj;
class Lambda;
class ActorType;
struct CompileResult;
class Primitive;
struct VMTargetData;
using VMTarget = std::shared_ptr<VMTargetData>;

// Global index spaces. Globals are split into an immutable CODE image
// (function CodeBlock* / builtin Primitive*, never mutated, never GC roots) and
// a mutable DATA segment (var/let, the only globals holding heap roots). DATA
// indices are numbered [0, kCodeGlobalBase); CODE indices [kCodeGlobalBase, ..).
// A single global(idx) accessor dispatches by range, so the code/data decision
// is made once at allocation and every read/write routes automatically.
// kCodeGlobalBase is 2^30 -- below 2^31 so a code index stays positive in the
// i32 AST field resolvedFuncGlobalIndex (sentinels there are negative).
// See lang/docs/Code_Image_Swap_Design.md.
inline constexpr u32 kCodeGlobalBase = 0x40000000u;

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

// An immutable snapshot of the CODE image (function CodeBlock* / builtin
// Primitive* slots, indexed by absoluteIdx - kCodeGlobalBase). Built on the NRT
// thread and published to a VM by swapping the pointer on the VM's OWNING thread
// (RT for a silo, NRT for the main VM); since a VM is single-threaded, the swap
// needs no atomics. Uses the system heap (std::vector), never the VM's TLSF pool:
// it is only READ on the audio thread, never allocated there, so a swap costs
// nothing on the RT thread. CodeBlock*/Primitive* are immortal, so retiring an
// old image frees only its slot array, not the pointed-to code.
struct CodeImage {
    std::vector<Word> slots;
    u32 generation = 0;
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
    Code*      returnPC;    // Resume point in caller (control flow)
    Code*      gcReturnPC;  // PC in the caller used to locate the caller's stack
                            // map during GC root scanning. Equals returnPC for
                            // ordinary bytecode calls; for a synchronous call
                            // into C++ (returnPC == syncReturnCode sentinel) it
                            // holds the real caller PC so the caller frame's
                            // live registers are still scanned. See pushFrame.
    CodeBlock* codeBlock;   // Compiled function (GC root - holds objConstants)
    u32        baseReg;     // Start of this frame's register window
    u32        numRegs;     // Registers allocated for this frame
    u16        resultReg;   // Caller's register for return value
    u32        dynStackMark; // Dynamic scope stack level at function entry
};

// Snapshot of the VM's execution state, taken when the main thread parks in a
// top-level `await` on a cross-thread future. While parked the render thread
// runs lang code (setup / scheduled handlers) on this SAME VM, resetting the
// shared frame stack + register file; we save the parked context before the
// wait and restore it on wake so the script continues where it left off.
struct ExecSnapshot {
    u32   frameCount = 0;
    u32   baseReg = 0;
    u32   dynStackTop = 0;
    u32   dynStackPayloadTop = 0;
    Code* pc = nullptr;
    Word* currentRegs = nullptr;
    class CoroutineObj* coro = nullptr;
    class CoroutineFrame* coroFrame = nullptr;
    Vec<Word>      regs;     // regs_[0 .. high-water] at park time
    Vec<CallFrame> frames;   // frames_[0 .. frameCount)
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
    friend class TracingGC;  // needs frame walk + register file for root scan

private:
    // Memory allocator
    rt::TLSFAllocator allocator_;

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
    // Return PC of the innermost active op_call_primitive (its call-site stack
    // map in the builtin's CALLER frame). A higher-order builtin pushes a
    // synchronous frame per element to run user code; that frame records this as
    // its gcReturnPC so the GC root scan can still find the caller frame's stack
    // map. Unlike pc_ (overwritten by every nested safepoint), this stays stable
    // across the builtin's loop; op_call_primitive save/restores it for nesting.
    Code* syncCallerPC_ = nullptr;

    // CODE image: function CodeBlock* / builtin Primitive* slots. Immutable,
    // immortal pointers, never GC roots. A heap snapshot swapped wholesale (see
    // CodeImage). Owned by the VM; freed in the destructor.
    CodeImage* codeImage_ = nullptr;

    // DATA segment: var/let globals (mutable, persist across events, the only
    // globals that can hold heap roots). Indexed [0, kCodeGlobalBase).
    Vec<Word> dataGlobals_;
    Vec<u8> dataIsObj_;  // Track which data globals hold Obj* for GC

    // Dynamic scope variables
    Vec<Word> dynVars_;
    Vec<u8>   dynVarIsObj_;  // Track which dynvars hold Obj* for GC

    // Inline-composite roots: (baseIndex, type) for a multi-word inline data
    // global / dynvar that embeds Obj* fields. dataIsObj/dynVarIsObj single-word
    // marking can't reach those; the GC walks the layout via gcScanPayload.
    // (Code globals are never inline composites.)
    Vec<std::pair<u32, Type*>> inlineObjData_;
    Vec<std::pair<u32, Type*>> inlineObjDynVars_;

    // Extra GC root scanners registered by host wrappers (e.g. NRTVM's
    // HandlerTable holds Obj* pointers that aren't reachable from globals
    // or any live frame). Each callback is invoked once per mark cycle
    // from the GC's root-scanning substate; the callback walks its own
    // table and calls gc.mark() on every live Obj* it owns.
    Vec<std::function<void(class TracingGC&)>> extraRootScanners_;

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

    // Set alongside halted_ when the halt is an ERROR (panic, unwrap on
    // none, no-match trap, ...) rather than normal top-level completion.
    // Hosts read it after a run to exit nonzero on a failed script.
    bool errorHalted_ = false;

    // Coroutine state
    CoroutineObj*    currentCoroutine_ = nullptr;
    CoroutineFrame*  currentCoroFrame_ = nullptr;
    Word*            currentRegs_ = nullptr;

    // --- Async event loop (Phase B) ---
    // A single-threaded, virtual-beat scheduler. `delay(beats)` (op_delay)
    // enqueues a timer; a top-level `await` (op_future_block) pumps the loop:
    // it drains ready coroutines, then advances the virtual beat to the
    // earliest timer, resolves that timer's Future, and resumes the async
    // coroutines waiting on it. Resolving a Future moves its waiters onto
    // asyncReady_. All single-threaded -- no mutexes (the cross-thread
    // resolution path arrives with renderNRT in Phase C). GC roots: the
    // futures held by timers and the coroutines in asyncReady_ (tracing_gc).
    struct AsyncTimer { double beat; Future* fut; };
    Vec<AsyncTimer>     asyncTimers_;       // pending delay() timers
    Vec<CoroutineObj*>  asyncReady_;        // coroutines ready to resume
    double              asyncBeat_ = 0.0;   // current virtual beat

    // Externally-resolved futures (Phase C): e.g. renderNRT completion fired
    // from a background thread. These are GC roots while in flight and signal
    // the pump that an await may make progress only via the host-wait hook (the
    // VM cannot advance them itself). The host (NRTVM) registers hostBlockingWait_
    // so op_future_block can release the host mutex and park on a condition
    // variable until a cross-thread resolution notifies it.
    Vec<Future*>        asyncExternalFutures_;
    std::function<void(std::function<bool()> const&)> hostBlockingWait_;
    // Notify any thread parked in serveActors/pump that the ready queue may have
    // changed (e.g. a NATS handler delivered a message on another thread). The
    // host (NRTVM) wires this to its condition variable's notify_all.
    std::function<void()> notifyAsyncProgress_;
    // Host-provided async I/O executor (async_io.hpp). The async NRT I/O
    // builtins (readFileAsync & co) submit {work, complete} jobs: `work` runs
    // on the host's worker thread with no VM access, `complete` under the host
    // mutex with the VM current. Unset for bare-VM hosts -- builtins then run
    // the job inline (synchronous fallback).
    std::function<bool(AsyncIOJob&&)> hostSubmitAsyncIO_;
    // Execution snapshots of main threads parked in a cross-thread await. While
    // parked, the live frame stack is reset to empty and belongs to whoever runs
    // lang code next (e.g. the render thread); the parked context lives here and
    // is a GC root (tracing_gc scans each snapshot's saved frames precisely via
    // the stack map at op_future_block). LIFO -- supports nested parks.
    Vec<ExecSnapshot*>  awaitSnapshots_;

    // Actor registry (Phase 1): every live actor owned by this VM, indexed by
    // id (its slot here). A GC root -- a parked actor (its coroutine on its
    // mailbox future's waiter list) is otherwise unreachable. A stopped actor
    // leaves a null slot. actorNames_ maps a registered name to an actor id.
    Vec<ActorObj*>      liveActors_;
    std::unordered_map<SymbolPtr, i64> actorNames_;

    // Type universe (shared, system-allocated)
    TypeUniverse& typeUniverse_;

    // Current primitive being called (set by op_call_primitive)
    Primitive* currentPrimitive_;

    // Print output callback (defaults to stdout)
    FILE* printOutput_;

    // List print limit (default 10 elements before showing "...")
    i64 listPrintLimit_;

    // Cycle-safe graph-traversal limits. Seeded from VMConfig (defaults match
    // the historical value_graph.hpp constants); adjustable at runtime via
    // the setGraphMaxDepth / setLazyForceLimit / setPrintMaxDepth builtins so
    // a valid program that trips a limit can raise it for itself.
    u32 graphMaxDepth_;
    i64 lazyForceLimit_;
    u32 printMaxDepth_;

    // Heap-growth chunk bounds used by the backup allocator installed in the
    // constructor (see VMConfig).
    usize growthChunkMin_;
    usize growthChunkMax_;

    // Per-VM random number generator (xoshiro256**, seeded per instance)
    Xoshiro256 rng_;

    // Opaque user data for foreign functions to access host state
    void* userData_ = nullptr;

    // Shared compilation target (links this VM to its global layout)
    VMTarget target_;

public:
    // Phase 1 of tracing-GC project: safepoint poll flag. Read on the hot path
    // by op_safepoint (relaxed load + branch). Set by the tracing GC when
    // mutator work needs attention (in-flight cycle, allocation pressure past
    // the next trigger threshold). Atomic because future phases (audio-thread
    // VM, foreign-allocation producers) may set the flag from off-thread.
    std::atomic<bool> gcRequested_{false};
    // Phase 6: time-based budgets. The safepoint default is the NRT preset
    // (REPL / file execution); audio-thread VMs override via setGCConfig.
    // kAudioRT preset shrinks this to ~200 us to fit alongside DSP work in
    // a single audio callback.
    static constexpr u64 kSafepointStepNanos = 2'000'000;     // 2 ms
    u64 gcStepBudgetNanos_ = kSafepointStepNanos;

    // Drain the deferred-delete queue under a bounded budget. Safe to call
    // from any safepoint inside execution -- it does not touch the register
    // file, just consumes already-released objects.
    void safepointPoll();

    // Phase 6: host-driven heartbeat hooks. Both call shared hostTick_ which
    // checks the proportional cycle trigger and advances any in-flight cycle
    // until the deadline is reached. The split exists so future per-context
    // divergence (different telemetry buckets, different scheduling policy)
    // doesn't need API churn -- they go through gcStepBudgetNanos_ today.
    //
    // rtTick: called from the audio thread that owns this VM, typically once
    //         per block, at the start of the callback. Must use a deadline
    //         that leaves enough block time for DSP.
    // nrtTick: called from the scheduler / REPL / GUI idle path that owns
    //         this VM. Idempotent and safe to call from idle handlers.
    //
    // Both are safe (and required) to call regardless of whether any language
    // code is currently running on this VM -- they exist precisely so that
    // an in-flight cycle keeps making progress when the mutator is idle.
    void rtTick(u64 deadlineNanos);
    void nrtTick(u64 deadlineNanos);

    // Per-VM GC budget knob. Phase 6 ships with NRT defaults; engines that
    // create RT VMs call this with a tighter budget before execution begins.
    void setGCStepBudgetNanos(u64 ns) { gcStepBudgetNanos_ = ns; }
    u64  gcStepBudgetNanos() const { return gcStepBudgetNanos_; }

    // Minimum Mutator Utilization scheduling. Off by default; RT VMs enable a
    // conservative preset at attach. setMMUTarget takes the mutator's target
    // share in parts-per-thousand (900 = 90% mutator) and the window length W
    // in nanoseconds. Defined in vm.cpp (TracingGC is pimpl-ed).
    void setMMUEnabled(bool on);
    void setMMUTarget(u32 mutatorPermille, u64 windowNanos);

private:
    void hostTick_(u64 deadlineNanos);
public:

    // Phase 3 of tracing-GC project: head of an intrusive doubly-linked list
    // of every GCObj owned by this VM. Sweep walks this list to find whites.
    // linkObjToAllList()/unlinkObjFromAllList() (in vm.cpp) maintain it.
    GCObj* allObjsHead_ = nullptr;

    // Lua-style open upvalues list. Single singly-linked list per VM, kept
    // sorted by descending location_ via the stack discipline below: every
    // new UpVar points into a deeper (higher-address) register slot than
    // any older open UpVar (because deeper frames live at higher regs_
    // offsets), so cons-to-head naturally maintains the order. That lets:
    //   - capture get-or-create do a single short walk from the head and
    //     either reuse the existing UpVar for `location` or push a new
    //     one (so sibling/nested closures over the same `var` share the
    //     same cell);
    //   - frame-exit close walk from the head while location_ >= base of
    //     the frame's register window and stop at the first cell that
    //     belongs to an outer frame.
    UpVar* openUpVars_ = nullptr;

    // GC keepalive stack for objects that are temporarily reachable ONLY from
    // the C++ call stack (invisible to the register/heap root scan) across a
    // call that can trigger collection. ListNode::force() uses this to pin the
    // generator while generate() runs: force() drops the generator from the
    // node's union before invoking generate(), and generate() may run user
    // lambdas that hit safepoints before it re-homes the generator onto a new
    // tail node -- a window in which the generator would otherwise be swept.
    // Scanned as a root (see TracingGC::step_root_frames).
    Vec<GCObj*> gcKeepAlive_;
    void gcKeepAlivePush(GCObj* o) { gcKeepAlive_.push_back(o); }
    void gcKeepAlivePop() { gcKeepAlive_.pop_back(); }
    // Replace the most-recently-pushed keepalive entry. For a builtin that
    // rebuilds an accumulator by reassignment each iteration (e.g. a persistent
    // vector's immutable push returns a NEW object), pin the initial value, then
    // call this with the new value after each step so the live one stays rooted.
    void gcKeepAliveUpdateTop(GCObj* o) { if (!gcKeepAlive_.empty()) gcKeepAlive_.back() = o; }

    // Get-or-create an UpVar pointing to `location`. Returns the existing
    // open UpVar if one already references that slot; otherwise allocates
    // a fresh UpVar with `sizeWords` payload, masks Obj* words via
    // `gcMaskBits`, conses it onto openUpVars_, and returns it. `valueType`
    // is stashed as the UpVar's Obj::type_ (debug / introspection only).
    UpVar* newUpVar(Type* valueType, Word* location, u16 sizeWords, u16 gcMaskBits);

    // Close every open UpVar whose location_ is at or above `pos`. Called
    // from popFrame (with pos = regs_ + baseReg of the frame being popped)
    // and from tail-call opcodes that reuse the current frame (with pos =
    // currentRegs_, the base of the about-to-be-overwritten window).
    void closeUpVarsAtOrAbove(Word* pos);

    // Tracing GC instance (defined in tracing_gc.hpp). Pimpl-ed via
    // unique_ptr so vm.hpp doesn't need to include the full class definition
    // (and so the GC's data can be reset/replaced in tests).
    std::unique_ptr<class TracingGC> tracingGC_;
    class TracingGC& tracingGC();

    // Constructor with pool size, shared type universe, and optional target
    explicit VM(VMConfig const& config, TypeUniverse& typeUniverse, const VMTarget& target = {});
    // Legacy convenience: all-default config with a custom pool size.
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

    // NRT heartbeat. Drives tracing-GC progress between events so an
    // in-flight cycle keeps advancing whenever the language is idle.
    // Invoked from the existing ~20 ms host idle thread (nrt_vm.hpp) and
    // from between-event call sites in the bridges and scheduler.
    void gcHeartbeat() {
        nrtTick(gcMonoNanos() + gcStepBudgetNanos_);
    }

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
    Type* bytesType() const { return typeUniverse_.types().bytesType; }
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
    PersistentVectorType* persistentVectorType(Type* elemType) { return typeUniverse_.persistentVectorType(elemType); }
    PersistentMapType* persistentMapType(Type* keyType, Type* valueType) { return typeUniverse_.persistentMapType(keyType, valueType); }
    EnumType* optionType(Type* elemType) { return typeUniverse_.optionType(elemType); }
    CoroutineType* coroutineType(Type* yieldType) { return typeUniverse_.coroutineType(yieldType); }

    // Coroutine accessors
    bool inCoroutine() const { return currentCoroutine_ != nullptr; }
    CoroutineObj* currentCoroutine() const { return currentCoroutine_; }
    CoroutineFrame* currentCoroFrame() const { return currentCoroFrame_; }
    void setCurrentCoroutine(CoroutineObj* c) { currentCoroutine_ = c; }
    void setCurrentCoroFrame(CoroutineFrame* f) { currentCoroFrame_ = f; }

    // --- Async event loop driver (Phase B) ---
    // Enqueue a virtual-beat timer for a Pending Future<Void> (op_delay).
    void scheduleDelay(Future* fut, double beats);
    // Move a resolved Future's awaiting coroutines onto the ready queue.
    void asyncEnqueueWaiters(Future* fut);
    // Resume a suspended async coroutine, injecting its awaited Future's value
    // into the await result register, then run it to its next suspend/return.
    void resumeAsync(CoroutineObj* coro);
    // Pump the loop on this thread until `target` resolves (op_future_block).
    void pumpUntilResolved(Future* target);
    double asyncBeat() const { return asyncBeat_; }

    // --- Actors (Phase 1) ---
    // Add an actor to this VM's registry, assigning + returning its id.
    i64 registerActor(ActorObj* a);
    // Look up a live actor by id (null if out of range / stopped).
    ActorObj* actorById(i64 id) const;
    // Create the behavior coroutine for a freshly-registered actor and drive it
    // to its first suspension (its first `await receive`). `self` is injected as
    // arg 0; `extraArgs` are the remaining declared params (e.g. the init msg).
    void spawnActorDrive(ActorObj* actor, Lambda* behavior,
                         Word const* extraArgs, u16 nExtra);
    // Drain ready actors (and fire delay timers) until the system is quiescent
    // -- every actor parked on an empty mailbox. The NRT actor run loop.
    void runActorLoop();
    // Like runActorLoop, but instead of returning when quiescent it PARKS on the
    // host-wait hook until a cross-thread delivery (NATS, a silo) wakes it -- a
    // long-lived actor server. Returns only if there are no live actors or no
    // way to be woken.
    void serveActorLoop();
    // Name registry: give an actor a stable name in this VM, look one up.
    void registerActorName(SymbolPtr name, i64 id) { actorNames_[name] = id; }
    i64 actorIdByName(SymbolPtr name) const {
        auto it = actorNames_.find(name);
        return it == actorNames_.end() ? -1 : it->second;
    }

    // Drive the actor/async loop one tick against an external clock: advance the
    // virtual beat to `beat`, fire due delay() timers, and resume up to `budget`
    // ready actors (bounded so an audio block never overruns). This is how silo
    // actors are clocked -- `await delay(n)` resolves when the audio beat reaches
    // it. No host allocation beyond the VM's TLSF heap; safe on the RT thread.
    void tickActors(double beat, int budget);

    // Cross-thread future support (Phase C). The host registers a blocking-wait
    // callback that releases its mutex and parks until the predicate holds.
    void setNotifyAsyncProgress(std::function<void()> fn) {
        notifyAsyncProgress_ = std::move(fn);
    }
    void setHostBlockingWait(std::function<void(std::function<bool()> const&)> fn) {
        hostBlockingWait_ = std::move(fn);
    }
    // Install / use the host async-I/O executor. submitAsyncIO returns false
    // -- with the job untouched, so the caller may still run it inline --
    // when no host installed an executor.
    void setSubmitAsyncIO(std::function<bool(AsyncIOJob&&)> fn) {
        hostSubmitAsyncIO_ = std::move(fn);
    }
    bool submitAsyncIO(AsyncIOJob&& j) {
        return hostSubmitAsyncIO_ ? hostSubmitAsyncIO_(std::move(j)) : false;
    }
    // Save/restore the execution context across a cross-thread await park.
    void saveExecSnapshot(ExecSnapshot& s);
    void restoreExecSnapshot(ExecSnapshot const& s);
    // Track a Pending future that will be resolved from another thread (roots it).
    void registerExternalFuture(Future* f) { if (f) asyncExternalFutures_.push_back(f); }
    // Resolve such a future: copy its value (if any was staged by the caller is
    // not needed for Void), enqueue its waiters, and drop it from the in-flight
    // set. MUST be called with the host mutex held (the VM made current).
    void resolveExternalFuture(Future* f, Word const* value = nullptr, u16 stride = 0);
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
    // Publish the executing instruction pointer so the incremental GC's
    // root scanner can locate the top frame's stack map. The direct-threaded
    // dispatch keeps pc in a local register, so it must be synced here before
    // any operation that can drive a GC step (op_safepoint).
    void setPc(Code* pc) { pc_ = pc; }
    // GC-stable caller PC for synchronous (C++) calls. See syncCallerPC_.
    Code* syncCallerPC() const { return syncCallerPC_; }
    void  setSyncCallerPC(Code* pc) { syncCallerPC_ = pc; }
    CodeBlock* currentCodeBlock() const;

    // Update current frame's codeBlock (used by tail call opcodes)
    void updateCurrentCodeBlock(CodeBlock* cb) { frames_[frameCount_ - 1].codeBlock = cb; }
    CodeBlock* currentFrameCodeBlock() const {
        return frameCount_ ? frames_[frameCount_ - 1].codeBlock : nullptr;
    }
    void growCurrentFrameNumRegs(u32 n) {
        auto& f = frames_[frameCount_ - 1];
        if (n > f.numRegs) f.numRegs = n;
    }

    // Current frame's register count (used by coro_resume to place registers after caller's window)
    u32 currentFrameNumRegs() const { return frames_[frameCount_ - 1].numRegs; }

    // Install a CompileResult: make compiled code ready for execution.
    // Validates that the result's target matches this VM's target. Grows the code
    // image in place (for the NRT main VM / REPL, where there is no audio thread).
    void install(const CompileResult& result);

    // Install only the DATA-segment part of a result (append data globals + grow
    // dynvars), leaving the code image untouched. Paired with swapCodeImage() on
    // the silo path, where the code half is replaced wholesale off-thread.
    void installData(const CompileResult& result);

    // Target accessors
    void setTarget(const VMTarget& target) { target_ = target; }
    const VMTarget& target() const { return target_; }

    // Execute a code block (entry point for running compiled code)
    Word execute(CodeBlock* block);

    // Push a call frame for function calls (used by CALL opcode).
    // gcReturnPC: PC used by GC to locate the CALLER's stack map while this
    // frame runs. Pass nullptr for ordinary bytecode calls (it then mirrors
    // returnPC). Synchronous calls into C++ (returnPC == syncReturnCode) must
    // pass the real caller PC (vm.pc()) so the caller frame stays scannable.
    void pushFrame(Code* returnPC, CodeBlock* codeBlock, u32 newBase, u32 numRegs, u16 resultReg,
                   Code* gcReturnPC = nullptr);

    // Pop a call frame - returns the popped frame's data
    CallFrame popFrame();

    // Debug: dump call stack to stderr
    void dumpCallStack() const;

    // --- Global variables ---

    // Single accessor dispatching by index range (see kCodeGlobalBase). A code
    // index reads the immutable image; a data index reads the mutable segment.
    Word& global(u32 idx) {
        return idx >= kCodeGlobalBase ? codeImage_->slots[idx - kCodeGlobalBase]
                                      : dataGlobals_[idx];
    }
    const Word& global(u32 idx) const {
        return idx >= kCodeGlobalBase ? codeImage_->slots[idx - kCodeGlobalBase]
                                      : dataGlobals_[idx];
    }

    // Replace the code image with a prebuilt one, returning the old image for the
    // caller to retire. O(1): a single pointer store on the VM's owning thread
    // (no atomics -- a VM is single-threaded; for a silo this runs on the audio
    // thread inside processRTCommands, sequenced before that buffer's reads).
    CodeImage* swapCodeImage(CodeImage* img) {
        CodeImage* old = codeImage_;
        codeImage_ = img;
        return old;
    }

    // Sizes. numGlobals() is the combined count (host introspection); the GC and
    // op_call bounds use the per-space counts.
    u32 numCodeGlobals() const { return codeImage_ ? (u32)codeImage_->slots.size() : 0; }
    u32 numDataGlobals() const { return (u32)dataGlobals_.size(); }
    u32 numGlobals() const { return numCodeGlobals() + numDataGlobals(); }

    // DATA-segment GC accessors (only data globals are roots).
    bool dataGlobalIsObj(u32 dataIdx) const { return dataIsObj_[dataIdx] != 0; }
    Word& dataGlobal(u32 dataIdx) { return dataGlobals_[dataIdx]; }
    const Word& dataGlobal(u32 dataIdx) const { return dataGlobals_[dataIdx]; }

    // Register an extra GC root scanner. The callback is invoked once per
    // mark cycle from the tracing GC's root-scan substate; the callback
    // walks its own table and calls gc.mark() on every Obj* it owns alive.
    void addExtraRootScanner(std::function<void(class TracingGC&)> fn) {
        extraRootScanners_.push_back(std::move(fn));
    }
    u32 numExtraRootScanners() const { return (u32)extraRootScanners_.size(); }
    void invokeExtraRootScanner(u32 i, class TracingGC& gc) {
        extraRootScanners_[i](gc);
    }

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

    // Error halt: like setHalted(true), but marks the halt as a FAILURE
    // (panic, unwrap on none, no-match trap, ...) so hosts can distinguish
    // it from normal top-level completion and exit nonzero. Cleared at the
    // same entry points that clear halted_.
    void haltWithError() { halted_ = true; errorHalted_ = true; }
    bool isErrorHalted() const { return errorHalted_; }

    // Print output
    FILE* printOutput() const { return printOutput_; }
    void setPrintOutput(FILE* f) { printOutput_ = f; }

    // List print limit
    i64 listPrintLimit() const { return listPrintLimit_; }
    void setListPrintLimit(i64 n) { listPrintLimit_ = n; }

    // Graph-traversal limits (see VMConfig). Setters clamp to >= 1 so a
    // program can't wedge every ==/hash/print into an instant error.
    u32 graphMaxDepth() const { return graphMaxDepth_; }
    void setGraphMaxDepth(i64 n) { graphMaxDepth_ = (u32)std::clamp<i64>(n, 1, 0x7FFFFFFF); }
    i64 lazyForceLimit() const { return lazyForceLimit_; }
    void setLazyForceLimit(i64 n) { lazyForceLimit_ = n < 1 ? 1 : n; }
    u32 printMaxDepth() const { return printMaxDepth_; }
    void setPrintMaxDepth(i64 n) { printMaxDepth_ = (u32)std::clamp<i64>(n, 1, 0x7FFFFFFF); }

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
// During runtime: linked into the VM's all-objs list as mortal.
// `tag` controls how the tracer dispatches gcScan. Default keeps the legacy
// virtual fallback so untagged subclasses still work; tagged subclasses get
// a non-virtual qualified call in the marker's switch (faster, no vtable hit).
void registerNewObj(GCObj* obj, GCTag tag = GCTag::Default);

} // namespace ts

#endif /* vm_hpp */
