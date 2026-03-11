//
//  vm.cpp
//  lang
//
//  Register-based VM implementation
//

#include "vm.hpp"
#include "compiler.hpp"
#include "type_system.hpp"
#include "value.hpp"
#include <random>

namespace ts {

// Thread-local current VM (null during compilation)
thread_local VM* gCurrentVM = nullptr;

// Thread-local current TypeUniverse
thread_local TypeUniverse* gCurrentTypeUniverse = nullptr;

// Thread-local current Compiler (null during execution)
thread_local Compiler* gCurrentCompiler = nullptr;

void registerNewObj(GCObj* obj) {
    if (gCurrentCompiler) {
        gCurrentCompiler->trackObject(obj);
    } else if (gCurrentVM) {
        gCurrentVM->gc().addObj(obj);
    }
}

} // namespace ts (temporarily close to define rt::gCurrentAllocator)

namespace rt {
// Thread-local current allocator (set by VM::makeCurrent)
thread_local TLSFAllocator* gCurrentAllocator = nullptr;
} // namespace rt

namespace ts { // re-open

// GCObj implementation

GCObj::GCObj() {}

void* GCObj::operator new(usize size) {
    if (rt::gCurrentAllocator) {
        void* mem = rt::gCurrentAllocator->allocate(size);
        if (!mem) throw std::bad_alloc();
        return mem;
    }
    // Compile-thread path: use system allocator
    void* mem = ::malloc(size);
    if (!mem) throw std::bad_alloc();
    return mem;
}

void GCObj::operator delete(void* ptr) noexcept {
    if (!ptr) return;
    if (rt::gCurrentAllocator) {
        rt::gCurrentAllocator->deallocate(ptr);
    } else {
        ::free(ptr);
    }
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
    , gc_()
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

    // Allocate register file from TLSF
    regs_ = static_cast<Word*>(allocator_.allocate(maxRegs_ * sizeof(Word)));
    if (!regs_) throw std::bad_alloc();
    std::memset(regs_, 0, maxRegs_ * sizeof(Word));

    // Allocate frame stack from TLSF
    frames_ = static_cast<CallFrame*>(allocator_.allocate(maxFrames_ * sizeof(CallFrame)));
    if (!frames_) throw std::bad_alloc();

    // Allocate dynamic scope save stack from TLSF
    dynStack_ = static_cast<DynSaveEntry*>(allocator_.allocate(maxDynStack_ * sizeof(DynSaveEntry)));
    if (!dynStack_) throw std::bad_alloc();

    // Types are already created by TypeUniverse — nothing to do here.

    // Initialize currentRegs_ to point at base of register file
    currentRegs_ = regs_;

    // Make all objects created during VM init immortal (built-in types, etc.)
    gc_.makeAllImmortal();
}

VM::~VM() {
    // Complete any ongoing GC before destroying objects
    while (gc_.isCollecting()) {
        gc_.heartbeat();
    }

    // Deallocate register file, frame stack, and dynamic scope stack
    if (regs_) allocator_.deallocate(regs_);
    if (frames_) allocator_.deallocate(frames_);
    if (dynStack_) allocator_.deallocate(dynStack_);
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
        assert(numGlobals() == result.globalBase && "VM globals out of sync — missed a prior install?");
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

// GC::markRoots() - scan globals and persistent state
// Registers are empty at GC time (event-driven: stack collapses between events)
// Types are system-allocated and immortal — no type cache scanning needed.
void GC::markRoots() {
    if (!gCurrentVM) return;

    // Scan global variables that hold Obj pointers
    for (u32 i = 0; i < gCurrentVM->numGlobals(); ++i) {
        if (gCurrentVM->globalIsObj_[i]) {
            Obj* obj = gCurrentVM->globals_[i].o;
            if (obj) mark(obj);
        }
    }

    // Scan dynamic scope variables that hold Obj pointers
    for (u32 i = 0; i < gCurrentVM->numDynVars(); ++i) {
        if (gCurrentVM->dynVarIsObj_[i]) {
            Obj* obj = gCurrentVM->dynVars_[i].o;
            if (obj) mark(obj);
        }
    }

    // Scan saved values on the dynamic scope stack
    for (u32 i = 0; i < gCurrentVM->dynStackTop_; ++i) {
        auto& entry = gCurrentVM->dynStack_[i];
        if (entry.isObj) {
            Obj* obj = entry.savedValue.o;
            if (obj) mark(obj);
        }
    }

    // Scan active coroutine frame chain
    if (gCurrentVM->currentCoroutine_)
        mark(gCurrentVM->currentCoroutine_);
    if (gCurrentVM->currentCoroFrame_) {
        CoroutineFrame* f = gCurrentVM->currentCoroFrame_;
        while (f) { mark(f); f = f->caller_; }
    }
}

} // namespace ts
