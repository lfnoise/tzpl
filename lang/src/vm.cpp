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
//  vm.cpp
//  lang
//
//  Register-based VM implementation
//

#include "vm.hpp"
#include "compiler.hpp"
#include "type_system.hpp"
#include "tracing_gc.hpp"
#include "value.hpp"
#include "opcodes.hpp"   // op_halt sentinel for the async driver re-entry
#include <algorithm>
#include <cstdlib>
#include <random>

namespace ts {

// Thread-local current VM (null during compilation)
thread_local VM* gCurrentVM = nullptr;

// Thread-local current TypeUniverse
thread_local TypeUniverse* gCurrentTypeUniverse = nullptr;

// Thread-local current Compiler (null during execution)
thread_local Compiler* gCurrentCompiler = nullptr;

void linkObjToAllList(GCObj* obj) {
    if (!gCurrentVM) return;
    // Singly-linked: prepend obj to head. O(1). Sweep unlinks inline via
    // its slot-pointer walk so no per-object prev pointer is needed.
    obj->allObjsNext_ = gCurrentVM->allObjsHead_;
    gCurrentVM->allObjsHead_ = obj;
    // Phase 3 incremental: if a tracing cycle is in flight, protect this new
    // object from the current sweep. The next cycle resets colors to White.
    //
    // During MARK we color it Gray (enqueue for scanning) rather than Black:
    // a new object's fields are filled in by the very next bytecode ops, and
    // those fields may point at OLD white objects (e.g. an immutable struct
    // built around a reused array/enum). Coloring it Black would skip the
    // scan entirely, so those white children would never be marked and would
    // be swept out from under a live, marked parent -- a premature free.
    // Gray means the marker scans it (after its fields are populated, at the
    // next safepoint) and marks its children. During SWEEP marking is already
    // finished and the worklist is no longer drained, so we keep the old
    // Black coloring to protect the object for the remainder of the cycle.
    auto& gc = gCurrentVM->tracingGC();
    if (gc.phase() == TracingGC::Phase::Mark) {
        gc.mark(obj);  // White -> Gray, enqueue for scanning
    } else if (gc.phase() == TracingGC::Phase::Sweep) {
        obj->setColor(GCColor::Black);
    }
    gc.recordAllocation();
    // Phase 5: with tracing as the sole liveness mechanism, alloc pressure
    // is the trigger for the next cycle. Set the safepoint flag once the
    // threshold is exceeded so the next backjump's op_safepoint kicks off
    // (or advances) a cycle. We don't requestCycle() here directly because
    // the new object is not yet visible to any root -- the bytecode op
    // that allocated it will write the result register on its very next
    // instruction, before any safepoint can fire.
    if (gc.phase() == TracingGC::Phase::Idle &&
        gc.allocsSinceLastCycle() >= gc.cycleTriggerAllocs()) {
        gCurrentVM->gcRequested_.store(true, std::memory_order_relaxed);
    }
}

void registerNewObj(GCObj* obj, GCTag tag) {
    obj->setGCTag(tag);
    if (gCurrentCompiler) {
        gCurrentCompiler->trackObject(obj);
        // Compiler objects are immortal -- they stay outside the VM's all-
        // objects list and the tracer never visits them (mark() short-circuits
        // on isImmortal()). They live for the lifetime of the compile.
    } else if (gCurrentVM) {
        obj->setMortal();
        linkObjToAllList(obj);
    }
}

TracingGC& VM::tracingGC() { return *tracingGC_; }

// Shared by rtTick / nrtTick. The source is forwarded into step() so the
// driver-split telemetry can distinguish audio-callback pauses from looser
// NRT-thread pauses.
static void hostTickImpl(VM& vm, u64 deadlineNanos, GCStepSource source) {
    auto& gc = vm.tracingGC();
    if (gc.phase() == TracingGC::Phase::Idle) {
        if (gc.allocsSinceLastCycle() >= gc.cycleTriggerAllocs()) {
            gc.requestCycle();
        }
    }
    if (gc.phase() != TracingGC::Phase::Idle) {
        gc.step(deadlineNanos, source);
    }
}

void VM::hostTick_(u64 deadlineNanos) {
    // Internal helper retained for callers that don't have a specific
    // source -- routes through nrtTick to keep semantics conservative.
    hostTickImpl(*this, deadlineNanos, GCStepSource::NrtTick);
}
void VM::rtTick(u64 deadlineNanos)  { hostTickImpl(*this, deadlineNanos, GCStepSource::RtTick); }
void VM::nrtTick(u64 deadlineNanos) { hostTickImpl(*this, deadlineNanos, GCStepSource::NrtTick); }

void VM::safepointPoll() {
    // Clear flag first so concurrent setters can re-trip it during the step.
    gcRequested_.store(false, std::memory_order_relaxed);

    // Time-based budget. The deadline is sampled inside step() every
    // kCheckEvery work units so a single safepoint cannot stall the mutator
    // for longer than (kCheckEvery * worstUnitCost) past the budget.
    auto& gc = *tracingGC_;
    u64 deadline = gcMonoNanos() + gcStepBudgetNanos_;
    if (gc.phase() == TracingGC::Phase::Idle) {
        if (gc.allocsSinceLastCycle() >= gc.cycleTriggerAllocs()) {
            gc.requestCycle();
        }
    }
    if (gc.phase() != TracingGC::Phase::Idle) {
        gc.step(deadline, GCStepSource::Safepoint);
        // Cycle still in flight: re-arm so the next safepoint continues.
        if (gc.phase() != TracingGC::Phase::Idle) {
            gcRequested_.store(true, std::memory_order_relaxed);
        }
    }
}

} // namespace ts (temporarily close to define rt::gCurrentAllocator)

namespace rt {
// Thread-local current allocator (set by VM::makeCurrent)
thread_local TLSFAllocator* gCurrentAllocator = nullptr;
} // namespace rt

namespace ts { // re-open

// GCObj implementation

void* GCObj::operator new(usize size) {
#if __has_feature(address_sanitizer)
    // Under ASan, use system malloc so ASan can track each GCObj
    // individually and detect use-after-free within the TLSF pool.
    void* mem = ::malloc(size);
    if (!mem) throw std::bad_alloc();
    return mem;
#else
    if (rt::gCurrentAllocator) {
        void* mem = rt::gCurrentAllocator->allocate(size);
        if (!mem) throw std::bad_alloc();
        return mem;
    }
    // Compile-thread path: use system allocator. These allocations are
    // marked immortal and never reach operator delete.
    void* mem = ::malloc(size);
    if (!mem) throw std::bad_alloc();
    return mem;
#endif
}

void GCObj::operator delete(void* ptr) noexcept {
    if (!ptr) return;
    // Singly-linked sweep unlinks before it calls delete, so operator delete
    // no longer touches the all-objects list. The only writers of that list
    // are linkObjToAllList (on alloc) and the sweep itself.
    //
    // Sweep is the only path that ever deletes a GCObj, and it runs on the
    // VM thread with rt::gCurrentAllocator pointing at the same allocator
    // that served the original `new`. Compile-thread immortals never reach
    // here (they're never deleted; CodeBlock::objConstants has no dtor).
#if __has_feature(address_sanitizer)
    ::free(ptr);
#else
    rt::gCurrentAllocator->deallocate(ptr);
#endif
}

// Obj methods
rt::TLSFAllocator* Obj::getAllocator() const {
    return rt::gCurrentAllocator;
}

CodeBlock* VM::currentCodeBlock() const {
    // The executing CodeBlock is always the top flat frame: op_coro_resume
    // pushes the coroutine body as a flat frame, and any function it calls
    // pushes its own frame above that. Keying on currentCoroFrame_ here was a
    // bug -- it returned the coroutine's CodeBlock while a *nested* call was
    // running, so the callee's op_load_obj read the coroutine's objConstants.
    return frames_[frameCount_ - 1].codeBlock;
}

// VM implementation

VM::VM(usize poolSize, TypeUniverse& typeUniverse, const VMTarget& target)
    : allocator_(poolSize)
    , regs_(nullptr)
    , maxRegs_(4096)
    , frames_(nullptr)
    , maxFrames_(512)
    , frameCount_(0)
    , baseReg_(0)
    , pc_(nullptr)
    , dataGlobals_(rt::STLAllocator<Word>(&allocator_))
    , dataIsObj_(rt::STLAllocator<u8>(&allocator_))
    , dynVars_(rt::STLAllocator<Word>(&allocator_))
    , dynVarIsObj_(rt::STLAllocator<u8>(&allocator_))
    , dynStack_(nullptr)
    , dynStackTop_(0)
    , maxDynStack_(256)
    , dynStackPayload_(nullptr)
    , dynStackPayloadTop_(0)
    , maxDynStackPayload_(1024)
    , halted_(false)
    , typeUniverse_(typeUniverse)
    , currentPrimitive_(nullptr)
    , printOutput_(stdout)
    , listPrintLimit_(10)
    , target_(target)
{
    // Seed the RNG from std::random_device (unique per VM instance)
    std::random_device rd;
    rng_.seed(((u64)rd() << 32) | rd());

    // Set gCurrentVM and gCurrentAllocator
    gCurrentVM = this;
    rt::gCurrentAllocator = &allocator_;
    gCurrentTypeUniverse = &typeUniverse_;

    // Install a backup allocator so a pool exhaustion grows the heap by
    // mallocing a fresh chunk instead of hard-failing. On the audio thread
    // this trades a one-time glitch for a hard crash; in offline/REPL
    // contexts it just keeps long-running scripts (binary_trees and other
    // allocation-heavy workloads) from aborting when the initial pool is
    // too small. The new chunk is owned by TLSFAllocator and freed in its
    // destructor.
    allocator_.setBackupAllocator(
        [](void* userData, usize* outSize, usize needed) -> void* {
            // Doubling growth: each new chunk is at least as large as the
            // current pool. Otherwise long-running allocation-heavy programs
            // accumulate dozens of 64 MB chunks and hit the per-allocator
            // region-count cap (kMaxRegions) before they hit real OOM. With
            // doubling, the chunk count stays log(total) and the cap is
            // unreachable in practice.
            constexpr usize kMinChunk = 64ULL * 1024 * 1024;
            constexpr usize kMaxChunk = 4ULL * 1024 * 1024 * 1024;
            auto* alloc = static_cast<rt::TLSFAllocator*>(userData);
            usize current = alloc->getPoolSize();
            usize wanted = needed + 4096;
            usize chunk = std::max(kMinChunk, std::max(current, wanted));
            if (chunk > kMaxChunk && wanted <= kMaxChunk) chunk = kMaxChunk;
            void* p = std::malloc(chunk);
            // If the optimistic chunk failed, fall back halving until we can
            // at least satisfy `wanted`.
            while (!p && chunk > wanted) {
                chunk /= 2;
                if (chunk < wanted) chunk = wanted;
                p = std::malloc(chunk);
            }
            if (p) *outSize = chunk;
            return p;
        },
        &allocator_
    );

    // Start with an empty (system-heap) code image; install/swap fills it.
    codeImage_ = new CodeImage();

    // Allocate register file from TLSF
    regs_ = static_cast<Word*>(allocator_.allocate(maxRegs_ * sizeof(Word)));
    if (!regs_) throw std::bad_alloc();

    // Phase 3b: lazy-init the tracing GC. unique_ptr keeps the header light.
    tracingGC_ = std::make_unique<TracingGC>(*this);
    std::memset(regs_, 0, maxRegs_ * sizeof(Word));

    // Allocate frame stack from TLSF
    frames_ = static_cast<CallFrame*>(allocator_.allocate(maxFrames_ * sizeof(CallFrame)));
    if (!frames_) throw std::bad_alloc();

    // Allocate dynamic scope save stack from TLSF
    dynStack_ = static_cast<DynSaveEntry*>(allocator_.allocate(maxDynStack_ * sizeof(DynSaveEntry)));
    if (!dynStack_) throw std::bad_alloc();

    // Phase 4g.5: dynStackPayload_ is lazily allocated on first inline-
    // composite dynvar save -- many programs never need it, and the
    // compile-time evalVM_ in particular has a tiny 64KB pool that would
    // not fit a pre-allocated payload buffer.

    // Types are already created by TypeUniverse — nothing to do here.

    // Initialize currentRegs_ to point at base of register file
    currentRegs_ = regs_;
}

VM::~VM() {
    makeCurrent();

    // Deallocate register file, frame stack, and dynamic scope stack
    if (regs_) allocator_.deallocate(regs_);
    if (frames_) allocator_.deallocate(frames_);
    if (dynStack_) allocator_.deallocate(dynStack_);
    if (dynStackPayload_) allocator_.deallocate(dynStackPayload_);

    // The current code image (system heap). Retired old images are freed by
    // their swap command's doNRT, not here.
    delete codeImage_;
}

// Phase 4g.5: implementations for inline-composite dynvar save/restore.
void VM::dynScopePushInline(u32 varIdx, Word const* newPayload, Type* type) {
    if (dynStackTop_ >= maxDynStack_) {
        throw std::runtime_error("Dynamic scope stack overflow");
    }
    u32 n = type ? (u32)type->sizeWords_ : 1u;
    if (n == 0) n = 1;
    // Lazy first-use allocation of the side payload buffer.
    if (!dynStackPayload_) {
        dynStackPayload_ = static_cast<Word*>(
            allocator_.allocate(maxDynStackPayload_ * sizeof(Word)));
        if (!dynStackPayload_) throw std::bad_alloc();
    }
    if (dynStackPayloadTop_ + n > maxDynStackPayload_) {
        throw std::runtime_error("Dynamic scope payload buffer overflow");
    }
    auto& entry = dynStack_[dynStackTop_++];
    entry.varIndex = varIdx;
    entry.sizeWords = n;
    entry.type = type;
    entry.savedValue.i = (i64)dynStackPayloadTop_;
    entry.isObj = false;
    // Transfer OLD payload (with its Obj* ownership) into the save buffer.
    Word* save = dynStackPayload_ + dynStackPayloadTop_;
    for (u32 i = 0; i < n; ++i) save[i] = dynVars_[varIdx + i];
    dynStackPayloadTop_ += n;
    // Overwrite dynvar with NEW payload; retain its Obj* fields since the
    // source registers will be reclaimed by the caller after this op.
    for (u32 i = 0; i < n; ++i) dynVars_[varIdx + i] = newPayload[i];
}

void VM::dynScopeRestore(u32 mark) {
    while (dynStackTop_ > mark) {
        --dynStackTop_;
        auto& entry = dynStack_[dynStackTop_];
        if (entry.sizeWords > 1 && entry.type) {
            // Inline-composite: release current dynvar's Obj* fields (they are
            // being replaced and we are NOT transferring them anywhere), then
            // copy the saved payload back (transferring its Obj* ownership).
            u32 n = entry.sizeWords;
            u32 off = (u32)entry.savedValue.i;
            Word* save = dynStackPayload_ + off;
            for (u32 i = 0; i < n; ++i) dynVars_[entry.varIndex + i] = save[i];
            dynStackPayloadTop_ = off; // pop payload words (saves are LIFO)
        } else {
            dynVars_[entry.varIndex] = entry.savedValue;
        }
    }
}

void VM::makeCurrent() {
    gCurrentVM = this;
    rt::gCurrentAllocator = &allocator_;
    gCurrentTypeUniverse = &typeUniverse_;
}

void VM::pushFrame(Code* returnPC, CodeBlock* codeBlock, u32 newBase, u32 numRegs, u16 resultReg) {
    if (frameCount_ >= maxFrames_) {
        throw std::runtime_error("Call stack overflow");
    }
    if (newBase + numRegs > maxRegs_) {
        throw std::runtime_error("Register file overflow");
    }

    CallFrame& frame = frames_[frameCount_++];
    frame.returnPC = returnPC;
    frame.codeBlock = codeBlock;
    frame.baseReg = baseReg_;
    frame.numRegs = numRegs;
    frame.resultReg = resultReg;
    frame.dynStackMark = dynStackTop_;

    baseReg_ = newBase;
    currentRegs_ = regs_ + baseReg_;
}

CallFrame VM::popFrame() {
    if (frameCount_ == 0) {
        throw std::runtime_error("Call stack underflow");
    }

    --frameCount_;
    CallFrame frame = frames_[frameCount_];

    // Close any open upvalues pointing into this frame's register window.
    // The frame's regs span [regs_ + baseReg_, regs_ + baseReg_ + numRegs).
    // closeUpVarsAtOrAbove walks the head of openUpVars_ (sorted descending
    // by location_) and stops at the first cell that belongs to an outer
    // frame, so callers' upvars are left untouched.
    closeUpVarsAtOrAbove(regs_ + baseReg_);

    // Restore dynamic scope bindings pushed during this frame
    dynScopeRestore(frame.dynStackMark);

    baseReg_ = frame.baseReg;
    currentRegs_ = regs_ + baseReg_;
    return frame;
}

UpVar* VM::newUpVar(Type* valueType, Word* location, u16 sizeWords, u16 gcMaskBits) {
    // Search the open list for an existing UpVar at this exact stack
    // location. Reusing it is what makes sibling closures over the same
    // `var` see each other's mutations.
    for (UpVar* uv = openUpVars_; uv != nullptr; uv = uv->next_) {
        if (uv->location_ == location) return uv;
    }
    UpVar* uv = UpVar::create(valueType, location, openUpVars_, sizeWords, gcMaskBits);
    openUpVars_ = uv;  // cons to head; stack discipline keeps list sorted
                       // by descending location_ as deeper frames push
                       // captures into the register file at higher offsets.
    return uv;
}

void VM::closeUpVarsAtOrAbove(Word* pos) {
    while (openUpVars_ && openUpVars_->location_ >= pos) {
        UpVar* uv = openUpVars_;
        openUpVars_ = uv->next_;
        uv->close();  // snapshot live words into uv->value_, retarget location_
    }
}

void VM::dumpCallStack() const {
    fprintf(stderr, "  Call stack (frameCount=%d):\n", frameCount_);
    for (u32 i = frameCount_; i > 0; --i) {
        auto& frame = frames_[i - 1];
        const char* name = frame.codeBlock && frame.codeBlock->name
            ? frame.codeBlock->name->cstr() : "<anon>";
        fprintf(stderr, "    [%d] %s (baseReg=%d numRegs=%d)\n",
                i - 1, name, frame.baseReg, frame.numRegs);
    }
}

void VM::install(const CompileResult& result) {
    // Validate code base sync (this in-place path is for the NRT main VM / REPL).
    if (!result.newCodeGlobals.empty()) {
        assert(result.target == target_ && "CompileResult target does not match VM target");
        if (numCodeGlobals() != result.codeBase) {
            throw std::runtime_error("VM code globals out of sync: VM has "
                + std::to_string(numCodeGlobals()) + " but compiler expects base "
                + std::to_string(result.codeBase));
        }
    }

    // Extend the immutable CODE image in place (no audio thread reads it here).
    // No isObj/root tracking: code slots hold immortal pointers, never GC roots.
    codeImage_->slots.reserve(codeImage_->slots.size() + result.newCodeGlobals.size());
    for (auto& slot : result.newCodeGlobals) {
        codeImage_->slots.push_back(slot.value);
    }

    // Apply in-place updates to PRE-EXISTING CODE slots (redefinitions from an
    // incremental compile). The index is an absolute (ranged) code index; map it
    // into the image and overwrite the pointer. A redefined fn slot stays a
    // CodeBlock*. Function call sites read global(idx) at call time, so already-
    // running code picks up the new body.
    for (auto& ru : result.reusedGlobals) {
        if (ru.index >= kCodeGlobalBase) {
            u32 local = ru.index - kCodeGlobalBase;
            if (local < codeImage_->slots.size()) codeImage_->slots[local] = ru.value;
        }
    }

    installData(result);
}

void VM::installData(const CompileResult& result) {
    if (!result.newDataGlobals.empty()) {
        assert(result.target == target_ && "CompileResult target does not match VM target");
        if (numDataGlobals() != result.dataBase) {
            throw std::runtime_error("VM data globals out of sync: VM has "
                + std::to_string(numDataGlobals()) + " but compiler expects base "
                + std::to_string(result.dataBase));
        }
    }

    // Pre-allocate to avoid repeated TLSF reallocations during push_back,
    // which can corrupt adjacent heap objects due to TLSF coalescing.
    dataGlobals_.reserve(dataGlobals_.size() + result.newDataGlobals.size());
    dataIsObj_.reserve(dataIsObj_.size() + result.newDataGlobals.size());

    // Extend the mutable DATA segment (var/let). These carry GC-root flags and
    // may be multi-word inline composites.
    for (auto& slot : result.newDataGlobals) {
        u32 didx = (u32)dataGlobals_.size();
        dataGlobals_.push_back(slot.value);
        dataIsObj_.push_back(slot.isObj ? 1 : 0);
        // A multi-word inline data global embedding Obj* fields: record (baseIdx,
        // type) so the GC walks its layout instead of single-word marking.
        if (slot.inlineObjType) inlineObjData_.push_back({didx, slot.inlineObjType});
    }

    // Ensure dynamic variable table is large enough. Carry each new dynvar's
    // GC root flag so dynvars holding Obj* are scanned as roots (and tag/atom
    // dynvars are not). Older results without the vector default to 0.
    while (dynVars_.size() < result.numDynVars) {
        u32 idx = (u32)dynVars_.size();
        dynVars_.push_back(Word());
        u8 isObj = (idx < result.dynVarIsObj.size()) ? result.dynVarIsObj[idx] : 0;
        dynVarIsObj_.push_back(isObj);
        // Multi-word inline dynvar embedding Obj* fields: record (idx, type) so
        // the GC walks its layout.
        Type* it = (idx < result.dynVarInlineType.size()) ? result.dynVarInlineType[idx] : nullptr;
        if (it) inlineObjDynVars_.push_back({idx, it});
    }
}

Word VM::evalPrimitive(Primitive* prim, const Word* args, u16 argc) {
    u32 savedBase = baseReg_;
    Primitive* savedPrim = currentPrimitive_;
    baseReg_ = 0;
    currentPrimitive_ = prim;
    for (u16 i = 0; i < argc; i++) regs_[i] = args[i];
    u16 dst = argc;  // result register right after args
    prim->cfun_(*this, dst, argc, 0);
    Word result = regs_[dst];
    baseReg_ = savedBase;
    currentPrimitive_ = savedPrim;
    return result;
}

Word VM::callFunction(CodeBlock* block, const Word* args, u16 argc) {
    halted_ = false;
    frameCount_ = 0;
    baseReg_ = 0;
    currentRegs_ = regs_;

    // Zero the registers for the initial frame
    std::memset(regs_, 0, block->numRegs * sizeof(Word));

    // Copy args into argument registers
    for (u16 i = 0; i < argc; i++) {
        regs_[i] = args[i];
    }

    // Push a frame for the function
    pushFrame(nullptr, block, 0, block->numRegs, 0);

    // Direct-threaded dispatch
    Code* entry = block->code.data();
    entry->op(*this, entry);

    return reg(0);
}

Word VM::callCallable(Obj* callable, const Word* args, u16 argc) {
    // Check if it's a Lambda (closure)
    auto* lambda = dynamic_cast<Lambda*>(static_cast<Obj*>(callable));
    if (lambda) {
        CodeBlock* block = lambda->codeBlock_;
        halted_ = false;
        frameCount_ = 0;
        baseReg_ = 0;
        currentRegs_ = regs_;
        std::memset(regs_, 0, block->numRegs * sizeof(Word));

        // Copy args
        for (u16 i = 0; i < argc; i++) {
            regs_[i] = args[i];
        }

        // Copy free vars after the declared parameters
        for (u16 i = 0; i < lambda->numFreeVars_; ++i) {
            regs_[block->numArgs + i] = lambda->freeVars_[i];
        }

        pushFrame(nullptr, block, 0, block->numRegs, 0);

        // Handle default arguments
        Code* entry;
        if (!block->defaultEntryOffsets.empty()) {
            u16 idx = argc - block->minArity;
            entry = block->code.data() + block->defaultEntryOffsets[idx];
        } else {
            entry = block->code.data();
        }
        entry->op(*this, entry);
        return reg(0);
    }

    // Primitive: call directly
    auto* prim = dynamic_cast<Primitive*>(static_cast<Obj*>(callable));
    if (prim) {
        return evalPrimitive(prim, args, argc);
    }

    // Fallback: shouldn't happen
    return Word();
}

Word VM::execute(CodeBlock* block) {
    halted_ = false;
    frameCount_ = 0;
    baseReg_ = 0;
    currentRegs_ = regs_;

    // Zero the registers for the initial frame
    std::memset(regs_, 0, block->numRegs * sizeof(Word));

    // Push a frame for the main block so function calls have a frame to return to.
    // The returnPC is never used because the main block ends with HALT.
    pushFrame(nullptr, block, 0, block->numRegs, 0);

    // Start execution at the first instruction
    Code* entry = block->code.data();

    // Direct-threaded dispatch: call the first opcode handler
    // Each handler tail-calls the next via [[clang::musttail]]
    entry->op(*this, entry);

    // When HALT runs, it returns here
    return reg(0);
}

// --- Async event loop driver (Phase B) -----------------------------------
//
// Single-threaded virtual-beat scheduler. The pump (driven by op_future_block
// at a top-level `await`) resolves delay() timers in beat order and resumes the
// async coroutines waiting on them. resumeAsync() re-enters the VM the same way
// VM::execute does -- it runs the coroutine until an opcode (here, the suspend/
// return path's restored caller PC = g_asyncDriverHalt) plainly returns, which
// unwinds the [[clang::musttail]] chain back to C++.

// A one-instruction "return to the C++ driver" program: when a resumed coroutine
// next suspends (op_future_await) or completes (op_async_return) it restores its
// callerReturnPC_ to this and tail-calls it, unwinding back into resumeAsync().
static Code g_asyncDriverHalt[1] = { Code(op_halt) };

void VM::scheduleDelay(Future* fut, double beats) {
    asyncTimers_.push_back(AsyncTimer{ asyncBeat_ + (beats > 0.0 ? beats : 0.0), fut });
}

void VM::asyncEnqueueWaiters(Future* fut) {
    CoroutineObj* w = fut->waiters_;
    fut->waiters_ = nullptr;
    bool any = false;
    while (w) {
        CoroutineObj* next = w->nextWaiter_;
        w->nextWaiter_ = nullptr;
        asyncReady_.push_back(w);
        any = true;
        w = next;
    }
    // Wake a thread parked in serveActors when this enqueue came from another
    // thread (e.g. a NATS handler). Harmless on the loop's own thread.
    if (any && notifyAsyncProgress_) notifyAsyncProgress_();
}

void VM::resumeAsync(CoroutineObj* coro) {
    if (coro->state_ != CoroutineObj::Suspended || !coro->topFrame_) return;
    Future* awaited = coro->awaitedFuture_;
    coro->awaitedFuture_ = nullptr;

    // Save the driver's context as the synthetic caller. On the coroutine's next
    // suspend/return, op_future_await / op_async_return restore this and tail-call
    // callerReturnPC_ (= the halt sentinel), unwinding back to us below.
    coro->callerReturnPC_   = g_asyncDriverHalt;
    coro->callerResultReg_  = 0;                 // unused: coro holds its own future
    coro->callerBaseReg_    = baseReg_;
    coro->callerFrameCount_ = frameCount_;       // saved BEFORE pushFrame
    coro->callerCoroFrame_  = currentCoroFrame_;
    coro->callerCoroutine_  = currentCoroutine_;

    setCurrentCoroutine(coro);                   // roots the coroutine
    coro->state_ = CoroutineObj::Running;

    // When driven from an idle VM (e.g. the silo tick between commands) there is
    // no active frame; place the coroutine's window at the current base (0).
    u32 newBase = (frameCount_ > 0) ? (baseReg_ + currentFrameNumRegs()) : baseReg_;
    CoroutineFrame* frame = coro->topFrame_;
    setCurrentCoroFrame(frame);
    for (u16 i = 0; i < frame->numRegs_; ++i) regs_[newBase + i] = frame->regs_[i];

    // Inject the awaited value into the await's destination register (the value
    // op_future_await did not have when it suspended).
    if (awaited) {
        u16 dst = coro->awaitResultReg_;
        u16 stride = awaited->valueWords_;
        for (u16 i = 0; i < stride; ++i) regs_[newBase + dst + i] = awaited->value_[i];
    }
    frame->gcMapIndex_ = UINT16_MAX;             // nothing retained until next suspend

    pushFrame(nullptr, frame->codeBlock_, newBase, frame->numRegs_, 0);

    Code* resumePC = coro->resumePC_;
    bool wasHalted = halted_;
    halted_ = false;
    resumePC->op(*this, resumePC);               // runs until g_asyncDriverHalt returns
    halted_ = wasHalted;
}

// --- Actors (Phase 1) ---

i64 VM::registerActor(ActorObj* a) {
    i64 id = (i64)liveActors_.size();
    a->id_ = id;
    liveActors_.push_back(a);
    return id;
}

ActorObj* VM::actorById(i64 id) const {
    if (id < 0 || (usize)id >= liveActors_.size()) return nullptr;
    return liveActors_[(usize)id];
}

// Create the actor's behavior coroutine (from a Lambda) and drive it to its
// first suspension. Mirrors op_async_call but starts from a Lambda's CodeBlock
// + free vars and uses the g_asyncDriverHalt synthetic caller so control unwinds
// back here (a C++ frame) when the body first suspends or completes.
void VM::spawnActorDrive(ActorObj* actor, Lambda* behavior,
                         Word const* extraArgs, u16 nExtra) {
    CodeBlock* codeBlock = behavior->codeBlock_;
    auto* funcType = static_cast<FunctionType*>(codeBlock->funcType);
    u16 nFree = behavior->numFreeVars_;
    u16 numArgs = (u16)(1 + nExtra + nFree);     // self + init... + freevars

    // An async fn body's coroutine carries a FutureType (Future<Void>) and a
    // resultFuture_ that op_async_return resolves on completion.
    Type* voidT = typeUniverse_.types().voidType;
    FutureType* futVoid = typeUniverse_.futureType(voidT);

    auto* coro = CoroutineObj::create(reinterpret_cast<CoroutineType*>(futVoid),
                                      funcType, codeBlock, numArgs);
    coro->args_[0].o = actor;
    for (u16 i = 0; i < nExtra; ++i) coro->args_[(u16)(1 + i)] = extraArgs[i];
    for (u16 i = 0; i < nFree; ++i)  coro->args_[(u16)(1 + nExtra + i)] = behavior->freeVars_[i];

    coro->callerReturnPC_   = g_asyncDriverHalt;
    coro->callerResultReg_  = 0;
    coro->callerBaseReg_    = baseReg_;
    coro->callerFrameCount_ = frameCount_;
    coro->callerCoroFrame_  = currentCoroFrame_;
    coro->callerCoroutine_  = currentCoroutine_;
    setCurrentCoroutine(coro);                   // root the coroutine
    coro->state_ = CoroutineObj::Running;

    auto* future = Future::create(futVoid, voidT, 1);  // coro rooted
    coro->resultFuture_ = future;
    actor->behavior_ = coro;

    u32 newBase = (frameCount_ > 0) ? (baseReg_ + currentFrameNumRegs()) : baseReg_;
    CodeBlock* callee = coro->entryBlock_;
    auto* frame = CoroutineFrame::create(nullptr, callee, callee->numRegs);
    setCurrentCoroFrame(frame);                  // root the save frame
    for (u16 i = 0; i < coro->numArgs_; ++i) regs_[newBase + i] = coro->args_[i];
    pushFrame(nullptr, callee, newBase, callee->numRegs, 0);
    Code* entry;
    if (!callee->defaultEntryOffsets.empty()) {
        u16 idx = (u16)(coro->numArgs_ - callee->minArity);
        entry = callee->code.data() + callee->defaultEntryOffsets[idx];
    } else {
        entry = callee->code.data();
    }
    bool wasHalted = halted_;
    halted_ = false;
    entry->op(*this, entry);                      // run to first suspend/complete
    halted_ = wasHalted;
}

void VM::runActorLoop() {
    while (true) {
        if (!asyncReady_.empty()) {
            CoroutineObj* coro = asyncReady_.front();
            asyncReady_.erase(asyncReady_.begin());
            resumeAsync(coro);
            continue;
        }
        if (!asyncTimers_.empty()) {
            size_t best = 0;
            for (size_t i = 1; i < asyncTimers_.size(); ++i)
                if (asyncTimers_[i].beat < asyncTimers_[best].beat) best = i;
            AsyncTimer t = asyncTimers_[best];
            asyncTimers_.erase(asyncTimers_.begin() + (std::ptrdiff_t)best);
            if (t.beat > asyncBeat_) asyncBeat_ = t.beat;
            t.fut->state_ = Future::Resolved;
            asyncEnqueueWaiters(t.fut);
            continue;
        }
        break;   // quiescent: every actor parked on an empty mailbox
    }
}

void VM::serveActorLoop() {
    while (true) {
        if (!asyncReady_.empty()) {
            CoroutineObj* coro = asyncReady_.front();
            asyncReady_.erase(asyncReady_.begin());
            resumeAsync(coro);
            continue;
        }
        if (!asyncTimers_.empty()) {
            size_t best = 0;
            for (size_t i = 1; i < asyncTimers_.size(); ++i)
                if (asyncTimers_[i].beat < asyncTimers_[best].beat) best = i;
            AsyncTimer t = asyncTimers_[best];
            asyncTimers_.erase(asyncTimers_.begin() + (std::ptrdiff_t)best);
            if (t.beat > asyncBeat_) asyncBeat_ = t.beat;
            t.fut->state_ = Future::Resolved;
            asyncEnqueueWaiters(t.fut);
            continue;
        }
        // Idle. With no live actors nothing can ever happen; with no host-wait
        // hook we can't be woken from another thread -- either way, return.
        if (liveActors_.empty() || !hostBlockingWait_) break;
        // Park until a cross-thread delivery enqueues a ready actor. Snapshot the
        // execution context out of the (now-cleared) live frame stack so the
        // delivering thread can run lang code on this VM while we wait, and a
        // concurrent GC scans the snapshot rather than our frozen frames.
        ExecSnapshot snap;
        saveExecSnapshot(snap);
        awaitSnapshots_.push_back(&snap);
        frameCount_ = 0;
        baseReg_ = 0;
        currentRegs_ = regs_;
        currentCoroutine_ = nullptr;
        currentCoroFrame_ = nullptr;
        hostBlockingWait_([this]() { return !asyncReady_.empty(); });
        awaitSnapshots_.pop_back();
        restoreExecSnapshot(snap);
    }
}

void VM::tickActors(double beat, int budget) {
    // The silo VM is idle (no active frame) between commands; reset to a clean
    // base so resumed actor coroutines get a valid register window.
    if (frameCount_ == 0) { baseReg_ = 0; currentRegs_ = regs_; }
    if (beat > asyncBeat_) asyncBeat_ = beat;
    int n = 0;
    while (n < budget) {
        if (!asyncReady_.empty()) {
            CoroutineObj* coro = asyncReady_.front();
            asyncReady_.erase(asyncReady_.begin());
            resumeAsync(coro);
            ++n;
            continue;
        }
        // Fire one delay() timer whose beat the clock has reached, then loop to
        // drain the actor it unblocks.
        int dueIdx = -1;
        for (size_t i = 0; i < asyncTimers_.size(); ++i) {
            if (asyncTimers_[i].beat <= asyncBeat_) { dueIdx = (int)i; break; }
        }
        if (dueIdx >= 0) {
            Future* fut = asyncTimers_[(usize)dueIdx].fut;
            asyncTimers_.erase(asyncTimers_.begin() + dueIdx);
            fut->state_ = Future::Resolved;
            asyncEnqueueWaiters(fut);
            continue;
        }
        break;   // nothing ready, no due timers
    }
}

void VM::resolveExternalFuture(Future* f, Word const* value, u16 stride) {
    if (!f || f->state_ == Future::Resolved) return;
    for (u16 i = 0; i < stride && i < f->valueWords_; ++i) f->value_[i] = value[i];
    f->state_ = Future::Resolved;
    asyncEnqueueWaiters(f);                 // move its waiters to asyncReady_
    for (size_t i = 0; i < asyncExternalFutures_.size(); ++i) {
        if (asyncExternalFutures_[i] == f) {
            asyncExternalFutures_.erase(asyncExternalFutures_.begin() + (std::ptrdiff_t)i);
            break;
        }
    }
}

void VM::saveExecSnapshot(ExecSnapshot& s) {
    s.frameCount = frameCount_;
    s.baseReg = baseReg_;
    s.dynStackTop = dynStackTop_;
    s.dynStackPayloadTop = dynStackPayloadTop_;
    s.pc = pc_;
    s.currentRegs = currentRegs_;
    s.coro = currentCoroutine_;
    s.coroFrame = currentCoroFrame_;
    u32 regHi = (frameCount_ > 0)
              ? (baseReg_ + frames_[frameCount_ - 1].numRegs) : 0;
    s.regs.assign(regs_, regs_ + regHi);
    s.frames.assign(frames_, frames_ + frameCount_);
}

void VM::restoreExecSnapshot(ExecSnapshot const& s) {
    if (!s.regs.empty())   std::copy(s.regs.begin(), s.regs.end(), regs_);
    if (!s.frames.empty()) std::copy(s.frames.begin(), s.frames.end(), frames_);
    frameCount_ = s.frameCount;
    baseReg_ = s.baseReg;
    dynStackTop_ = s.dynStackTop;
    dynStackPayloadTop_ = s.dynStackPayloadTop;
    pc_ = s.pc;
    currentRegs_ = s.currentRegs;
    currentCoroutine_ = s.coro;
    currentCoroFrame_ = s.coroFrame;
}

void VM::pumpUntilResolved(Future* target) {
    if (!target) return;
    // `target` may be reachable only through op_future_block's register slot,
    // which is not a GC root at the (non-safepoint) re-entries below -- pin it.
    gcKeepAlivePush(reinterpret_cast<GCObj*>(target));
    while (target->state_ != Future::Resolved) {
        // 1. Resume one ready coroutine (it may resolve `target` or enqueue more).
        if (!asyncReady_.empty()) {
            CoroutineObj* coro = asyncReady_.front();
            asyncReady_.erase(asyncReady_.begin());
            resumeAsync(coro);
            continue;
        }
        // 2. Advance virtual time to the earliest pending delay() timer.
        if (!asyncTimers_.empty()) {
            size_t best = 0;
            for (size_t i = 1; i < asyncTimers_.size(); ++i) {
                if (asyncTimers_[i].beat < asyncTimers_[best].beat) best = i;
            }
            AsyncTimer t = asyncTimers_[best];
            asyncTimers_.erase(asyncTimers_.begin() + (std::ptrdiff_t)best);
            if (t.beat > asyncBeat_) asyncBeat_ = t.beat;
            t.fut->state_ = Future::Resolved;    // Future<Void>: no value to copy
            asyncEnqueueWaiters(t.fut);
            continue;
        }
        // 3. No in-VM work left. If a future is in flight on another thread and
        //    the host gave us a blocking-wait hook, release the host mutex and
        //    park until a cross-thread resolution makes progress. Otherwise the
        //    target can never resolve from here -- bail (op_future_block then
        //    yields a default value).
        if (!asyncExternalFutures_.empty() && hostBlockingWait_) {
            // The resolving thread runs lang code (render setup / handlers) on
            // this same VM while we wait, trampling the shared frame stack and
            // register file. Snapshot our execution context, register it as a GC
            // root, and clear the live frame stack so a concurrent collection
            // (heartbeat thread, or the resolver thread) scans a consistent state
            // -- the empty live stack plus our rooted snapshot -- never our frozen
            // frames via a stale pc. Restore on wake.
            ExecSnapshot snap;
            saveExecSnapshot(snap);
            awaitSnapshots_.push_back(&snap);
            frameCount_ = 0;
            baseReg_ = 0;
            currentRegs_ = regs_;
            currentCoroutine_ = nullptr;
            currentCoroFrame_ = nullptr;
            Future* tgt = target;
            hostBlockingWait_([this, tgt]() {
                return tgt->state_ == Future::Resolved || !asyncReady_.empty();
            });
            awaitSnapshots_.pop_back();
            restoreExecSnapshot(snap);
            continue;
        }
        break;
    }
    gcKeepAlivePop();
}

} // namespace ts
