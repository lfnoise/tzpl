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
//  compiler.cpp
//  lang
//
//  Compiler implementation — fully decoupled from VM.
//

#include "compiler.hpp"
#include "lexer.hpp"
#include <cassert>
#include "parser.hpp"
#include "type_checker.hpp"
#include "codegen.hpp"
#include "module_compiler.hpp"

namespace ts {

namespace {

class CompilerCurrentScope {
    Compiler& compiler_;
    bool active_ = true;

public:
    CompilerCurrentScope(Compiler& compiler, const VMTarget& target)
        : compiler_(compiler)
    {
        compiler_.makeCurrent(target);
    }

    ~CompilerCurrentScope() {
        if (active_) compiler_.endCurrent();
    }

    CompilerCurrentScope(const CompilerCurrentScope&) = delete;
    CompilerCurrentScope& operator=(const CompilerCurrentScope&) = delete;
};

} // namespace

Compiler::Compiler(TypeUniverse& types)
    : typeUniverse_(types)
{}

Compiler::~Compiler() = default;

// --- VM target management ---

VMTarget Compiler::createTarget(bool rtRestricted) {
    auto target = std::make_shared<VMTargetData>();
    target->rtRestricted = rtRestricted;
    return target;
}

// --- Compilation context management ---

void Compiler::makeCurrent(const VMTarget& target) {
    currentTarget_ = target;

    // Prepare the layout for this compilation session: record each space's size
    // so takePending{Code,Data}Globals slices only this session's new slots.
    currentTarget_->codeCompileBase = (u32)currentTarget_->codeGlobals.size();
    currentTarget_->dataCompileBase = (u32)currentTarget_->dataGlobals.size();

    // Set compiler as current; clear VM and allocator so compilation
    // uses system allocator (::malloc) for all object allocations.
    gCurrentCompiler = this;
    gCurrentVM = nullptr;
    rt::gCurrentAllocator = nullptr;
    gCurrentTypeUniverse = &typeUniverse_;
}

void Compiler::endCurrent() {
    // Clear compiler context. Caller must call vm.makeCurrent() if needed.
    gCurrentCompiler = nullptr;
    currentTarget_.reset();
}

std::pair<std::vector<CompileResult::GlobalSlot>, u32> Compiler::takePendingCodeGlobals() {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    u32 base = currentTarget_->codeCompileBase;
    std::vector<CompileResult::GlobalSlot> pending(
        currentTarget_->codeGlobals.begin() + base,
        currentTarget_->codeGlobals.end());
    return {std::move(pending), base};
}

std::pair<std::vector<CompileResult::GlobalSlot>, u32> Compiler::takePendingDataGlobals() {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    u32 base = currentTarget_->dataCompileBase;
    std::vector<CompileResult::GlobalSlot> pending(
        currentTarget_->dataGlobals.begin() + base,
        currentTarget_->dataGlobals.end());
    return {std::move(pending), base};
}

void Compiler::trackObject(GCObj* obj) {
    // Compile-time objects are system-allocated and immortal.
    // They keep kImmortalRefcount so ARC ignores them.
    ++numTrackedObjects_;
}

// --- Global variable management (per-VMTarget layout, two index spaces) ---

CodeBlock* Compiler::poisonCodeBlock() {
    if (!poisonCode_) {
        poisonCode_ = new CodeBlock();
        // No args and no registers: every caller's arg-copy loop is bounded by
        // funcType->argTypes_ (empty) or writes only into the caller's own
        // window, and pushFrame with numRegs 0 is fine because the first
        // instruction throws before anything reads a register.
        poisonCode_->numRegs = 0;
        poisonCode_->numArgs = 0;
        poisonCode_->name = intern("<undefined function>");
        Vec<Type*> noArgs(rt::STLAllocator<Type*>(nullptr));
        poisonCode_->funcType = typeUniverse_.functionType(noArgs, typeUniverse_.types().voidType);
        poisonCode_->emit(Code(op_undefined_function));
    }
    return poisonCode_;
}

u32 Compiler::addCodeGlobal() {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    u32 local = (u32)currentTarget_->codeGlobals.size();
    // Seed with the poison block, never null: a slot whose function is
    // declared but never codegen'd (a REPL cell whose body failed to
    // type-check) stays callable-but-diagnosing instead of crashing every
    // call site that dereferences the CodeBlock without checking.
    currentTarget_->codeGlobals.push_back({Word((void*)poisonCodeBlock()), false});
    return kCodeGlobalBase + local;
}

u32 Compiler::addDataGlobal(bool isObj) {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    u32 idx = (u32)currentTarget_->dataGlobals.size();
    currentTarget_->dataGlobals.push_back({Word(), isObj});
    return idx;
}

u32 Compiler::addInlineDataGlobal(u32 sizeWords, Type* inlineType) {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    if (sizeWords == 0) sizeWords = 1;
    u32 idx = (u32)currentTarget_->dataGlobals.size();
    for (u32 i = 0; i < sizeWords; ++i) {
        currentTarget_->dataGlobals.push_back({Word(), false, nullptr});
    }
    // If the inline composite embeds Obj* fields, tag the BASE slot with its
    // type so the GC walks the layout (gcScanPayload) and traces them. The
    // remaining slots stay plain (their words are part of the same composite).
    if (inlineType && inlineHasObjPtr(inlineType)) {
        currentTarget_->dataGlobals[idx].inlineObjType = inlineType;
    }
    return idx;
}

void Compiler::setDataGlobalIsObj(u32 idx, bool isObj) {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    assert(idx < currentTarget_->dataGlobals.size() && "Data global index out of range");
    currentTarget_->dataGlobals[idx].isObj = isObj;
}

// Compile-time global accessor, dispatching by index range like the VM's.
Word& Compiler::global(u32 idx) {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    if (idx >= kCodeGlobalBase) {
        u32 local = idx - kCodeGlobalBase;
        assert(local < currentTarget_->codeGlobals.size() && "Code global index out of range");
        return currentTarget_->codeGlobals[local].value;
    }
    assert(idx < currentTarget_->dataGlobals.size() && "Data global index out of range");
    return currentTarget_->dataGlobals[idx].value;
}

const Word& Compiler::global(u32 idx) const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    if (idx >= kCodeGlobalBase) {
        u32 local = idx - kCodeGlobalBase;
        assert(local < currentTarget_->codeGlobals.size() && "Code global index out of range");
        return currentTarget_->codeGlobals[local].value;
    }
    assert(idx < currentTarget_->dataGlobals.size() && "Data global index out of range");
    return currentTarget_->dataGlobals[idx].value;
}

u32 Compiler::numCodeGlobals() const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    return (u32)currentTarget_->codeGlobals.size();
}

u32 Compiler::numDataGlobals() const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    return (u32)currentTarget_->dataGlobals.size();
}

u32 Compiler::codeCompileBase() const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    return currentTarget_->codeCompileBase;
}

u32 Compiler::dataCompileBase() const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    return currentTarget_->dataCompileBase;
}

// --- RT restriction query ---

bool Compiler::isRTRestricted() const {
    if (!currentTarget_) return false;
    return currentTarget_->rtRestricted;
}

// --- Dynamic scope variable registry ---

const std::unordered_map<std::string, Compiler::DynVarInfo>& Compiler::dynamicVars() const {
    return dynamicVars_;
}

u32 Compiler::numDynVars() const {
    return nextDynIndex_;
}

bool Compiler::registerDynVar(const std::string& name, Type* type, u32& outIndex) {
    auto it = dynamicVars_.find(name);
    if (it != dynamicVars_.end()) {
        outIndex = it->second.dynIndex;
        return false;
    }
    outIndex = nextDynIndex_;
    // Phase 4g.5: inline-composite dynvars occupy sizeWords_ consecutive slots
    // so their payload is stored inline; everything else is one slot.
    u32 sizeWords = 1;
    if (type && type->repr_ == Type::Repr::Inline && type->sizeWords_ > 1) {
        sizeWords = (u32)type->sizeWords_;
    }
    nextDynIndex_ += sizeWords;
    dynamicVars_[name] = DynVarInfo{type, outIndex};
    return true;
}

// Per-dynvar GC root flags. A dynvar that holds an Obj* must be a GC root,
// else it can be collected while only the dynvar references it (its value
// survives in registers only by luck), corrupting later marks. Use
// storesObjPtr(), not isObjType() -- see the global-root fix in declareVar.
// EVERY CompileResult whose numDynVars extends the VM's dynvar table must
// carry these flags: VM::install defaults missing entries to "not a root",
// which silently unroots the dynvar (REPL sessions hit exactly that).
void Compiler::fillDynVarRootFlags(CompileResult& result) const {
    result.dynVarIsObj.assign(result.numDynVars, 0);
    result.dynVarInlineType.assign(result.numDynVars, nullptr);
    for (auto const& [name, info] : dynamicVars_) {
        Type* t = info.type;
        bool inlineMulti = t && t->repr_ == Type::Repr::Inline && t->sizeWords_ > 1;
        if (info.dynIndex >= result.dynVarIsObj.size()) continue;
        if (inlineMulti) {
            // A multi-word inline dynvar embedding Obj* fields: the GC walks its
            // layout (gcScanPayload) rather than single-word marking.
            if (inlineHasObjPtr(t)) result.dynVarInlineType[info.dynIndex] = t;
        } else {
            // storesObjPtrUnboxed: a 1-word inline dynvar stores its field's raw
            // word, not an Obj* -- storesObjPtr over-approximates Repr::Inline.
            result.dynVarIsObj[info.dynIndex] = (t && storesObjPtrUnboxed(t)) ? 1 : 0;
        }
    }
}

const Compiler::DynVarInfo* Compiler::lookupDynVar(const std::string& name) const {
    auto it = dynamicVars_.find(name);
    return it != dynamicVars_.end() ? &it->second : nullptr;
}

void Compiler::refreshDynVarType(const std::string& name, Type* newType) {
    auto it = dynamicVars_.find(name);
    if (it != dynamicVars_.end()) {
        it->second.type = newType;
    }
}

// --- Foreign function registration ---

void Compiler::registerForeignFunction(const std::string& name, Type* returnType,
                                        std::vector<Type*> paramTypes, CFun cfun,
                                        bool pure, bool rtSafe) {
    foreignFunctions_.push_back({name, returnType, std::move(paramTypes), cfun, pure, rtSafe});
}

void Compiler::registerForeignModuleFunction(const std::string& moduleName,
                                              const std::string& funcName,
                                              Type* returnType,
                                              std::vector<Type*> paramTypes, CFun cfun,
                                              bool pure, bool rtSafe) {
    foreignModuleFunctions_[moduleName].push_back(
        {funcName, returnType, std::move(paramTypes), cfun, pure, rtSafe});
}

const std::vector<Compiler::ForeignFuncEntry>* Compiler::foreignModuleFunctions(
    const std::string& moduleName) const {
    auto it = foreignModuleFunctions_.find(moduleName);
    if (it != foreignModuleFunctions_.end()) return &it->second;
    return nullptr;
}

// --- Constant folding ---

bool Compiler::evalPrimitive(Primitive* prim, const Word* args, u16 argc, Word& outResult) {
    // The VM constructor unconditionally sets rt::gCurrentAllocator to the new
    // VM's TLSF pool, and VM::evalPrimitive needs gCurrentAllocator pointed at
    // its own pool while the primitive runs.  Both leak the eval VM's allocator
    // back to the caller's thread, so subsequent compilation-thread allocations
    // (StringObj for string literals, AST helpers, etc.) end up in the eval
    // VM's tiny 64K pool and exhaust it on any non-trivial codegen.  Save and
    // restore around the call so the rest of compilation keeps using ::malloc.
    auto savedVM = gCurrentVM;
    auto savedAlloc = rt::gCurrentAllocator;
    bool created = false;
    if (!evalVM_) {
        evalVM_ = std::make_unique<VM>(64 * 1024, typeUniverse_);
        created = true;
    }
    if (!created) {
        // VM ctor already set gCurrentAllocator; if we reused an existing VM
        // we still need it pointing at the eval pool for the call itself.
        gCurrentVM = evalVM_.get();
        rt::gCurrentAllocator = &evalVM_->allocator();
    }
    outResult = evalVM_->evalPrimitive(prim, args, argc);
    gCurrentVM = savedVM;
    rt::gCurrentAllocator = savedAlloc;
    return true;
}

// --- Full compilation pipeline ---

CompileResult Compiler::compile(const std::string& source, const std::string& filename,
                                 const VMTarget& target, ModuleCompiler* moduleCompiler) {
    CompileResult result;

    // Set up compilation context for this VM target
    CompilerCurrentScope currentScope(*this, target);

    // Lex
    Lexer lexer(source, filename);

    // Parse
    Parser parser(lexer);
    Program program = parser.parseProgram();
    if (parser.hasErrors()) {
        result.errors = parser.errors();
        return result;
    }

    // Type check
    TypeChecker typeChecker(*this, moduleCompiler);
    typeChecker.setSourceFilePath(filename);
    typeChecker.setSourceText(source);
    typeChecker.check(program);
    if (typeChecker.hasErrors()) {
        result.errors = typeChecker.errors();
        return result;
    }

    // Code generation
    CodeGen codegen(*this, typeChecker);
    codegen.setSourceFilePath(filename);
    codegen.setSourceText(source);
    codegen.enableRegReclaim = enableRegReclaim;
    codegen.enableConstFold = enableConstFold;
    codegen.enableTailCalls = enableTailCalls;
    CodeBlock* mainBlock = codegen.generate(program);
    if (codegen.hasErrors()) {
        result.errors = codegen.errors();
        return result;
    }

    // Harvest globals, dynvar metadata, and exported functions into the result.
    finalizeResult(result, typeChecker, mainBlock, target);

    return result;
}

void Compiler::finalizeResult(CompileResult& result, TypeChecker& typeChecker,
                              CodeBlock* mainBlock, const VMTarget& target) {
    // Harvest pending globals (both index spaces) into the result.
    auto [newCodeGlobals, codeBase] = takePendingCodeGlobals();
    auto [newDataGlobals, dataBase] = takePendingDataGlobals();

    result.success = true;
    result.mainBlock = mainBlock;
    result.newCodeGlobals = std::move(newCodeGlobals);
    result.codeBase = codeBase;
    result.newDataGlobals = std::move(newDataGlobals);
    result.dataBase = dataBase;
    result.numDynVars = numDynVars();
    fillDynVarRootFlags(result);
    result.target = target;

    // Populate exported function metadata for host-to-VM calling
    for (auto& [name, overloads] : typeChecker.functions()) {
        for (auto& fi : overloads) {
            if (fi.isTemplate) continue;  // skip unresolved templates
            CompileResult::ExportedFunc ef;
            ef.name = name;
            ef.globalIndex = fi.globalIndex;
            ef.paramTypes = fi.paramTypes;
            ef.returnType = fi.returnType;
            ef.isCodeBlock = !fi.isBuiltin;
            result.exportedFunctions.push_back(std::move(ef));
        }
    }
}

} // namespace ts
