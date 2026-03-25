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

    // Prepare the layout for this compilation session
    currentTarget_->compileGlobalBase = currentTarget_->globalCount;

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

std::pair<std::vector<CompileResult::GlobalSlot>, u32> Compiler::takePendingGlobals() {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    u32 base = currentTarget_->compileGlobalBase;
    // Slice allGlobals[compileGlobalBase..end] as the new globals for this session
    std::vector<CompileResult::GlobalSlot> pending(
        currentTarget_->allGlobals.begin() + base,
        currentTarget_->allGlobals.end());
    return {std::move(pending), base};
}

void Compiler::trackObject(GCObj* obj) {
    // Compile-time objects are system-allocated and immortal.
    // They keep kImmortalRefcount so ARC ignores them.
    ++numTrackedObjects_;
}

// --- Global variable management (per-VMTarget layout) ---

u32 Compiler::addGlobal(bool isObj) {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    u32 idx = (u32)currentTarget_->allGlobals.size();
    currentTarget_->allGlobals.push_back({Word(), isObj});
    currentTarget_->globalCount = (u32)currentTarget_->allGlobals.size();
    return idx;
}

void Compiler::setGlobalIsObj(u32 idx, bool isObj) {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    assert(idx < currentTarget_->allGlobals.size() && "Global index out of range");
    currentTarget_->allGlobals[idx].isObj = isObj;
}

bool Compiler::isGlobalObj(u32 idx) const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    assert(idx < currentTarget_->allGlobals.size() && "Global index out of range");
    return currentTarget_->allGlobals[idx].isObj;
}

Word& Compiler::global(u32 idx) {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    assert(idx < currentTarget_->allGlobals.size() && "Global index out of range");
    return currentTarget_->allGlobals[idx].value;
}

const Word& Compiler::global(u32 idx) const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    assert(idx < currentTarget_->allGlobals.size() && "Global index out of range");
    return currentTarget_->allGlobals[idx].value;
}

u32 Compiler::numGlobals() const {
    assert(currentTarget_ && "No current target set (call makeCurrent first)");
    return (u32)currentTarget_->allGlobals.size();
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
    outIndex = nextDynIndex_++;
    dynamicVars_[name] = DynVarInfo{type, outIndex};
    return true;
}

const Compiler::DynVarInfo* Compiler::lookupDynVar(const std::string& name) const {
    auto it = dynamicVars_.find(name);
    return it != dynamicVars_.end() ? &it->second : nullptr;
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
    if (!evalVM_) {
        evalVM_ = std::make_unique<VM>(64 * 1024, typeUniverse_);
    }
    outResult = evalVM_->evalPrimitive(prim, args, argc);
    return true;
}

// --- Full compilation pipeline ---

CompileResult Compiler::compile(const std::string& source, const std::string& filename,
                                 const VMTarget& target, ModuleCompiler* moduleCompiler) {
    CompileResult result;

    // Set up compilation context for this VM target
    makeCurrent(target);

    // Lex
    Lexer lexer(source, filename);

    // Parse
    Parser parser(lexer);
    Program program = parser.parseProgram();
    if (parser.hasErrors()) {
        result.errors = parser.errors();
        endCurrent();
        return result;
    }

    // Type check
    TypeChecker typeChecker(*this, moduleCompiler);
    typeChecker.setSourceFilePath(filename);
    typeChecker.setSourceText(source);
    typeChecker.check(program);
    if (typeChecker.hasErrors()) {
        result.errors = typeChecker.errors();
        endCurrent();
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
        endCurrent();
        return result;
    }

    // Harvest pending globals into the result
    auto [newGlobals, globalBase] = takePendingGlobals();

    result.success = true;
    result.mainBlock = mainBlock;
    result.newGlobals = std::move(newGlobals);
    result.globalBase = globalBase;
    result.numDynVars = numDynVars();
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

    endCurrent();
    return result;
}

} // namespace ts
