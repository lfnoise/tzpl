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
    obj->allObjsPrev_ = nullptr;
    obj->allObjsNext_ = gCurrentVM->allObjsHead_;
    if (gCurrentVM->allObjsHead_) gCurrentVM->allObjsHead_->allObjsPrev_ = obj;
    gCurrentVM->allObjsHead_ = obj;
    // Phase 3 incremental: if a tracing cycle is in flight, color this new
    // object Black so it is conservatively considered reachable for the
    // remainder of the cycle. Otherwise sweep could classify it as garbage
    // before any reference has had a chance to reach it. The next cycle
    // resets colors back to White.
    auto& gc = gCurrentVM->tracingGC();
    if (gc.phase() != TracingGC::Phase::Idle) {
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

void unlinkObjFromAllList(GCObj* obj) {
    if (!gCurrentVM) return;
    GCObj* p = obj->allObjsPrev_;
    GCObj* n = obj->allObjsNext_;
    if (p) p->allObjsNext_ = n;
    else if (gCurrentVM->allObjsHead_ == obj) gCurrentVM->allObjsHead_ = n;
    if (n) n->allObjsPrev_ = p;
    obj->allObjsPrev_ = nullptr;
    obj->allObjsNext_ = nullptr;
}

void registerNewObj(GCObj* obj) {
    if (gCurrentCompiler) {
        gCurrentCompiler->trackObject(obj);
        // Compiler objects are immortal -- refcount stays at kImmortalRefcount.
        // They are not added to any VM's all-objects list; they live for the
        // lifetime of the compile.
    } else if (gCurrentVM) {
        // Set initial refcount for VM-allocated objects and add to auto-release pool
        obj->setInitialRefcount();
        gCurrentVM->autoReleasePool().add(obj);
        // Phase 3: link onto the VM's all-objects list so sweep can find it.
        linkObjToAllList(obj);
    }
}

void arcEnqueueForDeletion(GCObj* obj) {
    rt::TLSFAllocator* home = obj->homeAllocator();

    if (home && home != rt::gCurrentAllocator) {
        // Cross-thread deletion: object belongs to a different VM's allocator.
        // Enqueue on the home allocator's foreign delete queue so the owning
        // VM can delete it on its own thread during gcHeartbeat().
        auto* queue = static_cast<ForeignDeleteQueue*>(home->getForeignDeleteQueue());
        if (queue) {
            queue->enqueue(obj);
            return;
        }
        // Fallthrough: no foreign queue registered (shouldn't happen in normal
        // multi-VM operation, but handle gracefully).
    }

    if (gCurrentVM) {
        auto& q = gCurrentVM->deferredDeleteQueue();
        q.enqueue(obj);
        // Phase 1: trip the safepoint flag when the queue grows past the
        // trigger size. Next backward jump's op_safepoint will drain it.
        if (q.size() >= VM::kSafepointTriggerSize) {
            gCurrentVM->gcRequested_.store(true, std::memory_order_relaxed);
        }
    } else {
        // Fallback: immediate delete (should not happen in normal operation)
        obj->releaseChildren();
        delete obj;
    }
}

TracingGC& VM::tracingGC() { return *tracingGC_; }

// Shared by safepointPoll, rtTick, and nrtTick. Checks the proportional
// trigger and advances any in-flight cycle until deadlineNanos.
void VM::hostTick_(u64 deadlineNanos) {
    auto& gc = *tracingGC_;
    if (gc.phase() == TracingGC::Phase::Idle) {
        if (gc.allocsSinceLastCycle() >= gc.cycleTriggerAllocs()) {
            gc.requestCycle();
        }
    }
    if (gc.phase() != TracingGC::Phase::Idle) {
        gc.step(deadlineNanos);
    }
}

void VM::rtTick(u64 deadlineNanos) { hostTick_(deadlineNanos); }
void VM::nrtTick(u64 deadlineNanos) { hostTick_(deadlineNanos); }

void VM::safepointPoll() {
    // Clear flag first so concurrent enqueuers can re-trip it during drain.
    gcRequested_.store(false, std::memory_order_relaxed);
    foreignDeleteQueue_.drainInto(deferredDeleteQueue_);

    // Phase 6: time-based budget. The deadline is sampled inside step() every
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
        gc.step(deadline);
    }

    // Deferred-delete queue (vestigial under Phase 5 -- release is a no-op
    // so nothing is ever enqueued). Drain only when no tracing cycle is in
    // flight; an SATB barrier could otherwise pin a still-Gray Obj* on the
    // tracer's worklist that we'd then delete out from under it.
    if (gc.phase() == TracingGC::Phase::Idle) {
        deferredDeleteQueue_.processN(1024);
    }

    // If the queue is still large or a cycle is in progress, re-arm the
    // flag so the next safepoint continues the work.
    if (deferredDeleteQueue_.size() >= kSafepointTriggerSize ||
        gc.phase() != TracingGC::Phase::Idle) {
        gcRequested_.store(true, std::memory_order_relaxed);
    }
}

} // namespace ts (temporarily close to define rt::gCurrentAllocator)

namespace rt {
// Thread-local current allocator (set by VM::makeCurrent)
thread_local TLSFAllocator* gCurrentAllocator = nullptr;
} // namespace rt

namespace ts { // re-open

// GCObj implementation

GCObj::GCObj() {
    homeAllocator_ = rt::gCurrentAllocator;
}

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
    // Compile-thread path: use system allocator
    void* mem = ::malloc(size);
    if (!mem) throw std::bad_alloc();
    return mem;
#endif
}

void GCObj::operator delete(void* ptr) noexcept {
    if (!ptr) return;
    auto* obj = static_cast<GCObj*>(ptr);
    // Phase 3: take the object out of the VM's all-objects list before we
    // release its memory. Safe to call when the list is empty / obj isn't
    // linked (e.g., compiler-tracked immortal objects).
    unlinkObjFromAllList(obj);
#if __has_feature(address_sanitizer)
    ::free(ptr);
#else
    // Use the object's home allocator for deallocation. This is correct even
    // in cross-thread deletion scenarios: the object is always deleted by the
    // home VM's heartbeat, which has set gCurrentAllocator to the home
    // allocator. Using homeAllocator_ directly is belt-and-suspenders.
    rt::TLSFAllocator* alloc = obj->homeAllocator_;
    if (alloc) {
        alloc->deallocate(ptr);
    } else {
        ::free(ptr);
    }
#endif
}

// Obj methods
rt::TLSFAllocator* Obj::getAllocator() const {
    return rt::gCurrentAllocator;
}

CodeBlock* VM::currentCodeBlock() const {
    if (currentCoroFrame_) return currentCoroFrame_->codeBlock_;
    return frames_[frameCount_ - 1].codeBlock;
}

// VM implementation

VM::VM(usize poolSize, TypeUniverse& typeUniverse, const VMTarget& target)
    : allocator_(poolSize)
    , autoReleasePool_(&allocator_)
    , deferredDeleteQueue_(&allocator_)
    , regs_(nullptr)
    , maxRegs_(4096)
    , frames_(nullptr)
    , maxFrames_(512)
    , frameCount_(0)
    , baseReg_(0)
    , pc_(nullptr)
    , globals_(rt::STLAllocator<Word>(&allocator_))
    , globalIsObj_(rt::STLAllocator<u8>(&allocator_))
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

    // Register this VM's foreign delete queue with the allocator so that
    // cross-thread arcEnqueueForDeletion can find it from homeAllocator_.
    allocator_.setForeignDeleteQueue(&foreignDeleteQueue_);

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

    // Unregister the foreign delete queue so no new objects are enqueued
    // after we start tearing down.
    allocator_.setForeignDeleteQueue(nullptr);

    // Drain foreign deletes, auto-release pool, and deferred deletions
    foreignDeleteQueue_.drainInto(deferredDeleteQueue_);
    autoReleasePool_.drain();
    while (!deferredDeleteQueue_.empty()) {
        foreignDeleteQueue_.drainInto(deferredDeleteQueue_);
        deferredDeleteQueue_.processN(1024);
    }

    // Deallocate register file, frame stack, and dynamic scope stack
    if (regs_) allocator_.deallocate(regs_);
    if (frames_) allocator_.deallocate(frames_);
    if (dynStack_) allocator_.deallocate(dynStack_);
    if (dynStackPayload_) allocator_.deallocate(dynStackPayload_);
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
    inlineWalkPointers(&dynVars_[varIdx], type, /*release_=*/false);
}

void VM::dynScopeRestore(u32 mark) {
    while (dynStackTop_ > mark) {
        --dynStackTop_;
        auto& entry = dynStack_[dynStackTop_];
        if (entry.sizeWords > 1 && entry.type) {
            // Inline-composite: release current dynvar's Obj* fields (they are
            // being replaced and we are NOT transferring them anywhere), then
            // copy the saved payload back (transferring its Obj* ownership).
            inlineWalkPointers(&dynVars_[entry.varIndex], entry.type, /*release_=*/true);
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

    // Restore dynamic scope bindings pushed during this frame
    dynScopeRestore(frame.dynStackMark);

    baseReg_ = frame.baseReg;
    currentRegs_ = regs_ + baseReg_;
    return frame;
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
    // Validate target match when there are new globals to install
    if (!result.newGlobals.empty()) {
        assert(result.target == target_ && "CompileResult target does not match VM target");
        if (numGlobals() != result.globalBase) {
            throw std::runtime_error("VM globals out of sync: VM has "
                + std::to_string(numGlobals()) + " globals but compiler expects base "
                + std::to_string(result.globalBase) + " (delta "
                + std::to_string((i64)numGlobals() - (i64)result.globalBase) + ")");
        }
    }

    // Pre-allocate to avoid repeated TLSF reallocations during push_back,
    // which can corrupt adjacent heap objects due to TLSF coalescing.
    globals_.reserve(globals_.size() + result.newGlobals.size());
    globalIsObj_.reserve(globalIsObj_.size() + result.newGlobals.size());

    // Extend globals_ with newly compiled global slots
    for (auto& slot : result.newGlobals) {
        globals_.push_back(slot.value);
        globalIsObj_.push_back(slot.isObj ? 1 : 0);
    }

    // Ensure dynamic variable table is large enough
    while (dynVars_.size() < result.numDynVars) {
        dynVars_.push_back(Word());
        dynVarIsObj_.push_back(0);
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

} // namespace ts
