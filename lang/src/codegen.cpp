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
//  codegen.cpp
//  lang
//
//  Code generator: typed AST -> register-based instructions
//

#include "codegen.hpp"
#include "module_compiler.hpp"

namespace ts {

CodeGen::CodeGen(Compiler& compiler, TypeChecker& typeChecker)
    : compiler_(compiler)
    , typeChecker_(typeChecker)
    , currentBlock_(nullptr)
    , nextReg_(0)
    , maxReg_(0)
{}

u16 CodeGen::allocReg() {
    u16 r = nextReg_++;
    if (nextReg_ > maxReg_) maxReg_ = nextReg_;
    return r;
}

u16 CodeGen::allocRegs(u16 count) {
    u16 base = nextReg_;
    nextReg_ += count;
    if (nextReg_ > maxReg_) maxReg_ = nextReg_;
    return base;
}

void CodeGen::freeRegsTo(u16 reg) {
    // Captured-by-closure slots are pinned for the whole function: any
    // pinned slot at or above `reg` must stay allocated so the open
    // UpVar that points into it keeps reading the live value. Find the
    // highest pinned slot in [reg, nextReg_) and keep everything up to
    // and including it.
    u16 keep = reg;
    if (!regPinned_.empty()) {
        u16 end = (u16)std::min<size_t>(regPinned_.size(), (size_t)nextReg_);
        for (u16 i = reg; i < end; ++i) {
            if (regPinned_[i]) keep = (u16)(i + 1);
        }
    }
    if (enableConstFold) clearConsts(keep);
    // Phase 5.2: drop the per-register type info for regs that are now
    // free. Stack-map emitters scan [0..nextReg_), so anything beyond
    // the new nextReg_ is implicitly invisible -- but a future allocReg
    // could reuse a slot before its type is written, so we still null
    // the entries to keep the table honest.
    if (keep < regTypes_.size()) {
        for (size_t i = keep; i < regTypes_.size(); ++i) regTypes_[i] = nullptr;
    }
    nextReg_ = keep;
}

void CodeGen::pinReg(u16 reg) {
    if (regPinned_.size() <= reg) regPinned_.resize((size_t)reg + 1, 0);
    regPinned_[reg] = 1;
}

void CodeGen::setRegType(u16 reg, Type* t) {
    if (regTypes_.size() <= reg) regTypes_.resize((size_t)reg + 1, nullptr);
    regTypes_[reg] = t;
}

// --- Constant tracking for folding ---

void CodeGen::markConstInt(u16 reg, i64 val) {
    if (!enableConstFold) return;
    ConstVal cv; cv.kind = ConstVal::CInt; cv.intVal = val;
    constRegs_[reg] = cv;
}

void CodeGen::markConstFloat(u16 reg, f64 val) {
    if (!enableConstFold) return;
    ConstVal cv; cv.kind = ConstVal::CFloat; cv.floatVal = val;
    constRegs_[reg] = cv;
}

void CodeGen::markConstBool(u16 reg, bool val) {
    if (!enableConstFold) return;
    ConstVal cv; cv.kind = ConstVal::CBool; cv.boolVal = val;
    constRegs_[reg] = cv;
}

void CodeGen::clearConsts(u16 fromReg) {
    for (auto it = constRegs_.begin(); it != constRegs_.end(); ) {
        if (it->first >= fromReg)
            it = constRegs_.erase(it);
        else
            ++it;
    }
}

void CodeGen::clearConst(u16 reg) {
    if (!enableConstFold) return;
    constRegs_.erase(reg);
}

// Whitelist of ops whose regs[0] is a write-once destination and whose
// implementation reads its source operands before writing the destination
// (so it is safe to redirect dst to a register that aliases one of the
// sources, e.g. `j = j + 1` after redirecting becomes `ADDI_INT j, j, 1`).
// Ops where regs[0] is a *source* (op_return, op_store_global, op_jump_if_*)
// are excluded.
static bool isRedirectableProducer(Operation op) {
    return op == op_mov
        || op == op_load_int_const
        || op == op_load_float_const
        || op == op_load_bool_true
        || op == op_load_bool_false
        || op == op_load_nil
        || op == op_load_obj
        || op == op_load_global
        || op == op_load_global_inline
        || op == op_add_int      || op == op_sub_int      || op == op_mul_int
        || op == op_div_int      || op == op_mod_int      || op == op_neg_int
        || op == op_add_int_imm  || op == op_sub_int_imm  || op == op_mul_int_imm
        || op == op_add_float    || op == op_sub_float    || op == op_mul_float
        || op == op_div_float    || op == op_neg_float
        || op == op_cmp_eq_int   || op == op_cmp_ne_int
        || op == op_cmp_lt_int   || op == op_cmp_le_int
        || op == op_cmp_gt_int   || op == op_cmp_ge_int
        || op == op_cmp_eq_int_imm || op == op_cmp_ne_int_imm
        || op == op_cmp_lt_int_imm || op == op_cmp_le_int_imm
        || op == op_cmp_gt_int_imm || op == op_cmp_ge_int_imm
        || op == op_cmp_eq_float || op == op_cmp_ne_float
        || op == op_cmp_lt_float || op == op_cmp_le_float
        || op == op_cmp_gt_float || op == op_cmp_ge_float;
}

bool CodeGen::tryFuseRedirect(u16 from, u16 to, u32 nWords, u32 producerEmittedAt) {
    if (!enableConstFold) return false;     // Same gate as other peepholes.
    if (nWords != 1) return false;          // Multi-word slots span > 1 reg.
    if (from == to) return true;            // Already in place; caller can skip MOV.
    if (lastProducerSlot_ < 0) return false;
    if ((u32)lastProducerSlot_ < producerEmittedAt) return false; // Producer is from an earlier statement.
    if (lastProducerDst_ != from) return false;
    Code& slot = currentBlock_->code[lastProducerSlot_];
    if (slot.regs[0] != from) return false; // Defensive: tracking is consistent.
    Operation op = currentBlock_->code[lastProducerSlot_ - 1].op;
    if (!isRedirectableProducer(op)) return false;
    slot.regs[0] = to;
    lastProducerDst_ = to;
    return true;
}

void CodeGen::clearConstsForMutableLocals() {
    // Mutable locals are reassigned imperatively; their const tracking from the
    // initializer is only valid until the first reassignment. Inside a loop body
    // the emitted code runs many times, so we must clear const tracking before
    // entering the body even if no reassignment is visible yet.
    if (!enableConstFold) return;
    for (auto const& scope : localScopes_) {
        for (auto const& entry : scope) {
            if (entry.second.isMutable) {
                constRegs_.erase(entry.second.reg);
            }
        }
    }
}

const CodeGen::ConstVal* CodeGen::getConst(u16 reg) const {
    auto it = constRegs_.find(reg);
    return it != constRegs_.end() ? &it->second : nullptr;
}

void CodeGen::pushScope() {
    localScopes_.emplace_back();
}

void CodeGen::popScope() {
    localScopes_.pop_back();
}

void CodeGen::declareLocal(const std::string& name, u16 reg, Type* type, bool isMutable) {
    localScopes_.back()[name] = LocalVar{reg, type, isMutable, /*isUpvar=*/false};
    // Phase 5.2: mirror the type into the per-register table so stack-map
    // emission can find this local without separately walking localScopes_.
    setRegType(reg, type);
}

void CodeGen::declareLocalUpvar(const std::string& name, u16 reg, Type* type) {
    // The register holds an UpVar* (an Obj*). Record it as such so the
    // stack-map walker treats it as a live reference. The captured value's
    // type is preserved in LocalVar.type for read/write emission, but the
    // *register* type at this slot is "some Obj*" -- anyType works because
    // storesObjPtr returns true for it, which is all the marker checks.
    localScopes_.back()[name] = LocalVar{reg, type, /*isMutable=*/true, /*isUpvar=*/true};
    setRegType(reg, compiler_.anyType());
}

CodeGen::LocalVar* CodeGen::lookupLocal(const std::string& name) {
    for (int i = (int)localScopes_.size() - 1; i >= 0; --i) {
        auto it = localScopes_[i].find(name);
        if (it != localScopes_[i].end()) {
            return &it->second;
        }
    }
    return nullptr;
}

void CodeGen::error(SourceRange loc, const std::string& msg) {
    errors_.push_back(CompileError(CompileError::TypeError, loc, msg,
                                   sourceFilePath_, sourceText_));
}

// --- Jump helpers ---

u32 CodeGen::emitJump(Operation jumpOp, u16 condReg) {
    emitOp(jumpOp);
    if (jumpOp != op_jump) {
        emitRegs(condReg);
    }
    u32 patchPos = (u32)currentBlock_->code.size();
    emitInt(0);  // Placeholder for jump target index (will be patched)
    jumpFixups_.push_back(patchPos);
    // After a jump, anything we tracked as the last producer is no longer
    // adjacent in straight-line code.
    invalidateLastProducer();
    return patchPos;
}

void CodeGen::patchJump(u32 jumpPos) {
    // Store the current code position as the jump target index
    u32 targetIdx = (u32)currentBlock_->code.size();
    currentBlock_->code[jumpPos].i = (i64)targetIdx;
    // The current position is now a jump landing site. Producer info from the
    // fall-through path is not valid for the joined control flow.
    invalidateLastProducer();
}

void CodeGen::emitJumpTo(u32 targetIdx) {
    // Every emitJumpTo call sites a backward jump to a previously-captured
    // loop start. Emit a safepoint poll just before the jump so the deferred-
    // delete queue (and, in later phases, mark/sweep work) gets drained
    // inside hot loops, not only between events. The poll's hot path is one
    // relaxed load + branch.
    emitSafepointWithStackMap();
    emitOp(op_jump);
    u32 pos = (u32)currentBlock_->code.size();
    emitInt((i64)targetIdx);
    jumpFixups_.push_back(pos);
    invalidateLastProducer();
}

void CodeGen::emitReturnPcStackMap(u16 /*unusedResultReg*/, Type* /*unusedResultType*/) {
    // The returnPC stack map is consulted ONLY while the callee is still
    // executing (the marker walks the caller via frames_[i+1].returnPC).
    // During that window the caller's resultReg slot is OVERLAPPED with
    // callee frame storage: when argBase=R, the callee's r0 == caller's
    // rR, callee's r1 == caller's r(R+1), ..., and the result reg sits
    // inside that callee range. So the caller's resultReg slot is not
    // "live ref" while we're mid-call -- it's callee scratch -- and the
    // stack map must NOT include it. (After the call returns, the genExpr
    // wrapper sets regTypes_[resultReg] for future stack maps downstream
    // in the caller, which is the correct moment to consider it live.)
    u32 pcOffset = (u32)currentBlock_->code.size();
    StackMap sm;
    sm.pcOffset = pcOffset;
    u16 limit = (u16)std::min<size_t>(nextReg_, regTypes_.size());
    for (u16 r = 0; r < limit; ++r) {
        Type* t = regTypes_[r];
        if (!t) continue;
        if (isInlineMultiword(t)) continue;
        if (storesObjPtr(t)) sm.liveRefRegs.push_back(r);
    }
    currentBlock_->stackMaps_.push_back(std::move(sm));
}

void CodeGen::clearArgRegTypes(u16 argBase, u16 argEnd) {
    u16 lim = (u16)std::min<size_t>(argEnd, regTypes_.size());
    for (u16 r = argBase; r < lim; ++r) regTypes_[r] = nullptr;
}

void CodeGen::emitSafepointWithStackMap() {
    // Record stack map BEFORE emitting the op so pcOffset matches the
    // op_safepoint Code word position. Phase 5.2: walk the per-register
    // type table over [0..nextReg_) instead of just localScopes_, so
    // in-flight temporaries (e.g., the first build(d-1) result inside
    // Tree.node((build(d-1), build(d-1)))) are rooted too. Inline
    // multiword composites stay skipped -- their base word is a payload
    // word, not an Obj* slot.
    u32 pcOffset = (u32)currentBlock_->code.size();
    StackMap sm;
    sm.pcOffset = pcOffset;
    u16 limit = (u16)std::min<size_t>(nextReg_, regTypes_.size());
    for (u16 r = 0; r < limit; ++r) {
        Type* t = regTypes_[r];
        if (!t) continue;
        if (isInlineMultiword(t)) continue;
        if (storesObjPtr(t)) sm.liveRefRegs.push_back(r);
    }
    currentBlock_->stackMaps_.push_back(std::move(sm));
    emitOp(op_safepoint);
}

void CodeGen::resolveJumps(CodeBlock* block) {
    Code* base = block->code.data();
    for (u32 fixupPos : jumpFixups_) {
        i64 targetIdx = block->code[fixupPos].i;
        block->code[fixupPos].p = base + targetIdx;
    }
    jumpFixups_.clear();
}

// --- Insert conversions ---

u16 CodeGen::ensureFloat(u16 reg, Type* type) {
    if (type == compiler_.intType() || type == compiler_.boolType()) {
        u16 dst = allocReg();
        emitOp(op_int_to_float);
        emitRegs(dst, reg);
        return dst;
    }
    if (type == compiler_.fractionType()) {
        u16 dst = allocReg();
        emitOp(op_fraction_to_float);
        emitRegs(dst, reg);
        return dst;
    }
    return reg;
}

u16 CodeGen::ensureFraction(u16 reg, Type* type) {
    if (type == compiler_.intType() || type == compiler_.boolType()) {
        u16 dst = allocSlot(compiler_.fractionType());
        emitOp(op_int_to_fraction);
        emitRegs(dst, reg);
        return dst;
    }
    return reg;
}

u16 CodeGen::ensureComplex(u16 reg, Type* type) {
    if (type == compiler_.intType() || type == compiler_.boolType()) {
        u16 dst = allocSlot(compiler_.complexType());
        emitOp(op_int_to_complex);
        emitRegs(dst, reg);
        return dst;
    }
    if (type == compiler_.fractionType()) {
        u16 dst = allocSlot(compiler_.complexType());
        emitOp(op_fraction_to_complex);
        emitRegs(dst, reg);
        return dst;
    }
    if (type == compiler_.floatType()) {
        u16 dst = allocSlot(compiler_.complexType());
        emitOp(op_float_to_complex);
        emitRegs(dst, reg);
        return dst;
    }
    return reg;
}

u16 CodeGen::ensureInt(u16 reg, Type* type) {
    if (dynamic_cast<EnumType*>(type)) {
        // Phase 2: DiscriminantEnum values ARE the case index i64 already.
        if (type->repr_ == ts::Type::Repr::DiscriminantEnum) {
            return reg;
        }
        // Phase 4g.4: inline enum's word 0 IS the i64 discriminant.
        if (type->repr_ == ts::Type::Repr::Inline) {
            return reg;
        }
        u16 dst = allocReg();
        emitOp(op_enum_get_which);
        emitRegs(dst, reg);
        return dst;
    }
    return reg;
}

u16 CodeGen::ensureType(u16 reg, Type* fromType, Type* toType) {
    if (fromType == toType) return reg;
    if (toType == compiler_.intType()) return ensureInt(reg, fromType);
    if (toType == compiler_.fractionType()) return ensureFraction(reg, fromType);
    if (toType == compiler_.floatType()) return ensureFloat(reg, fromType);
    if (toType == compiler_.complexType()) return ensureComplex(reg, fromType);
    return reg;
}

// --- Get appropriate opcodes for types ---

Operation CodeGen::getArithOp(BinaryOpExpr::Op op, Type* type) {
    if (type == compiler_.complexType()) {
        switch (op) {
            case BinaryOpExpr::Add: return op_add_complex;
            case BinaryOpExpr::Sub: return op_sub_complex;
            case BinaryOpExpr::Mul: return op_mul_complex;
            case BinaryOpExpr::Div: return op_div_complex;
            default: return op_add_complex;
        }
    }
    if (type == compiler_.fractionType()) {
        switch (op) {
            case BinaryOpExpr::Add: return op_add_fraction;
            case BinaryOpExpr::Sub: return op_sub_fraction;
            case BinaryOpExpr::Mul: return op_mul_fraction;
            case BinaryOpExpr::Div: return op_div_fraction;
            default: return op_add_fraction;
        }
    }
    if (type == compiler_.floatType()) {
        switch (op) {
            case BinaryOpExpr::Add: return op_add_float;
            case BinaryOpExpr::Sub: return op_sub_float;
            case BinaryOpExpr::Mul: return op_mul_float;
            case BinaryOpExpr::Div: return op_div_float;
            default: return op_add_float;
        }
    }
    // Int
    switch (op) {
        case BinaryOpExpr::Add: return op_add_int;
        case BinaryOpExpr::Sub: return op_sub_int;
        case BinaryOpExpr::Mul: return op_mul_int;
        case BinaryOpExpr::Div: return op_div_int;
        case BinaryOpExpr::Mod: return op_mod_int;
        default: return op_add_int;
    }
}

Operation CodeGen::getCmpOp(BinaryOpExpr::Op op, Type* type) {
    if (type == compiler_.stringType()) {
        switch (op) {
            case BinaryOpExpr::Eq: return op_cmp_eq_str;
            case BinaryOpExpr::Ne: return op_cmp_ne_str;
            case BinaryOpExpr::Lt: return op_cmp_lt_str;
            case BinaryOpExpr::Le: return op_cmp_le_str;
            case BinaryOpExpr::Gt: return op_cmp_gt_str;
            case BinaryOpExpr::Ge: return op_cmp_ge_str;
            default: return op_cmp_eq_str;
        }
    }
    if (type == compiler_.complexType()) {
        switch (op) {
            case BinaryOpExpr::Eq: return op_cmp_eq_complex;
            case BinaryOpExpr::Ne: return op_cmp_ne_complex;
            // Ordering ops on Complex are rejected by the type checker
            // (containsComplex check). Reaching here is a type-checker bug.
            default:
                assert(false && "ordering operator emitted for Complex operand");
                return op_cmp_eq_complex;
        }
    }
    if (type == compiler_.fractionType()) {
        switch (op) {
            case BinaryOpExpr::Eq: return op_cmp_eq_fraction;
            case BinaryOpExpr::Ne: return op_cmp_ne_fraction;
            case BinaryOpExpr::Lt: return op_cmp_lt_fraction;
            case BinaryOpExpr::Le: return op_cmp_le_fraction;
            case BinaryOpExpr::Gt: return op_cmp_gt_fraction;
            case BinaryOpExpr::Ge: return op_cmp_ge_fraction;
            default: return op_cmp_eq_fraction;
        }
    }
    if (type == compiler_.symbolType()) {
        switch (op) {
            case BinaryOpExpr::Eq: return op_cmp_eq_int;
            case BinaryOpExpr::Ne: return op_cmp_ne_int;
            default: return op_cmp_eq_int;
        }
    }
    if (type == compiler_.floatType()) {
        switch (op) {
            case BinaryOpExpr::Eq: return op_cmp_eq_float;
            case BinaryOpExpr::Ne: return op_cmp_ne_float;
            case BinaryOpExpr::Lt: return op_cmp_lt_float;
            case BinaryOpExpr::Le: return op_cmp_le_float;
            case BinaryOpExpr::Gt: return op_cmp_gt_float;
            case BinaryOpExpr::Ge: return op_cmp_ge_float;
            default: return op_cmp_eq_float;
        }
    }
    if (type && storesObjPtr(type)) {
        switch (op) {
            case BinaryOpExpr::Eq: return op_cmp_eq_obj;
            case BinaryOpExpr::Ne: return op_cmp_ne_obj;
            default: return op_cmp_eq_obj;
        }
    }
    // Int (default, also handles Bool)
    switch (op) {
        case BinaryOpExpr::Eq: return op_cmp_eq_int;
        case BinaryOpExpr::Ne: return op_cmp_ne_int;
        case BinaryOpExpr::Lt: return op_cmp_lt_int;
        case BinaryOpExpr::Le: return op_cmp_le_int;
        case BinaryOpExpr::Gt: return op_cmp_gt_int;
        case BinaryOpExpr::Ge: return op_cmp_ge_int;
        default: return op_cmp_eq_int;
    }
}

// --- Composite (Array/Tuple) helpers ---

bool CodeGen::isCompositeNumeric(Type* type) const {
    return dynamic_cast<ArrayType*>(type) || dynamic_cast<TupleType*>(type)
        || dynamic_cast<ListType*>(type);
}

Operation CodeGen::getCompositeArithOp(BinaryOpExpr::Op op) {
    switch (op) {
        case BinaryOpExpr::Add: return op_add_composite;
        case BinaryOpExpr::Sub: return op_sub_composite;
        case BinaryOpExpr::Mul: return op_mul_composite;
        case BinaryOpExpr::Div: return op_div_composite;
        default: return op_add_composite;
    }
}

Operation CodeGen::getCompositeArithOpInline(BinaryOpExpr::Op op) {
    switch (op) {
        case BinaryOpExpr::Add: return op_add_composite_inline;
        case BinaryOpExpr::Sub: return op_sub_composite_inline;
        case BinaryOpExpr::Mul: return op_mul_composite_inline;
        case BinaryOpExpr::Div: return op_div_composite_inline;
        default: return op_add_composite_inline;
    }
}

Operation CodeGen::getCompositeCmpOp(BinaryOpExpr::Op op) {
    switch (op) {
        case BinaryOpExpr::Eq: return op_cmp_eq_composite;
        case BinaryOpExpr::Ne: return op_cmp_ne_composite;
        case BinaryOpExpr::Lt: return op_cmp_lt_composite;
        case BinaryOpExpr::Le: return op_cmp_le_composite;
        case BinaryOpExpr::Gt: return op_cmp_gt_composite;
        case BinaryOpExpr::Ge: return op_cmp_ge_composite;
        default: return op_cmp_eq_composite;
    }
}

Operation CodeGen::getCompositeCmpOpInline(BinaryOpExpr::Op op) {
    switch (op) {
        case BinaryOpExpr::Eq: return op_cmp_eq_composite_inline;
        case BinaryOpExpr::Ne: return op_cmp_ne_composite_inline;
        case BinaryOpExpr::Lt: return op_cmp_lt_composite_inline;
        case BinaryOpExpr::Le: return op_cmp_le_composite_inline;
        case BinaryOpExpr::Gt: return op_cmp_gt_composite_inline;
        case BinaryOpExpr::Ge: return op_cmp_ge_composite_inline;
        default: return op_cmp_eq_composite_inline;
    }
}

// --- Generate program ---

CodeBlock* CodeGen::generate(Program& program, bool isModule) {
    // Create top-level code block
    currentBlock_ = new CodeBlock();
    currentBlock_->name = compiler_.intern("<main>");
    nextReg_ = 0;
    maxReg_ = 0;
    regTypes_.clear();
    regPinned_.clear();

    pushScope();

    // Phase 0: emit module init calls for import declarations
    for (auto& item : program.items) {
        if (item->kind == ASTNode::ImportDecl) {
            genImportDecl(static_cast<ImportDeclNode*>(item.get()));
        }
    }

    // First pass: generate code for function declarations (compile each fn to its own CodeBlock)
    for (auto& item : program.items) {
        if (item->kind == ASTNode::FnDecl) {
            auto* decl = static_cast<FnDeclNode*>(item.get());
            if (decl->resolvedFuncGlobalIndex == -2) continue;  // skip template declarations
            genFnDecl(decl);
        }
    }

    // Generate monomorphized template instances created during this eval only.
    // In REPL mode, previous evals' instances may have stale declNode/sourceModule
    // pointers if modules were invalidated between evals.
    {
        const auto& monoInsts = typeChecker_.monoInstances();
        for (size_t i = typeChecker_.monoInstancesWatermark(); i < monoInsts.size(); ++i) {
            genMonoInstance(*monoInsts[i]);
        }
    }

    // Second pass: generate code for top-level statements (non-fn/struct-decl items)
    for (auto& item : program.items) {
        if (item->kind != ASTNode::FnDecl && item->kind != ASTNode::StructDecl &&
            item->kind != ASTNode::UnionDecl && item->kind != ASTNode::TypeAliasDecl) {
            genNode(item.get());
        }
    }

    // Emit HALT or RETURN_VOID depending on context
    if (isModule) {
        emitOp(op_return_void);
    } else {
        emitOp(op_halt);
    }

    popScope();

    currentBlock_->numRegs = maxReg_;

    // Resolve all jump indices to pointers now that code is stable
    resolveJumps(currentBlock_);

    return currentBlock_;
}

CodeBlock* CodeGen::generateREPL(Program& program) {
    currentBlock_ = new CodeBlock();
    currentBlock_->name = compiler_.intern("<repl>");
    nextReg_ = 0;
    maxReg_ = 0;
    regTypes_.clear();
    regPinned_.clear();

    pushScope();

    // Phase 0: emit module init calls for import declarations
    for (auto& item : program.items) {
        if (item->kind == ASTNode::ImportDecl) {
            genImportDecl(static_cast<ImportDeclNode*>(item.get()));
        }
    }

    // First pass: generate code for function declarations
    for (auto& item : program.items) {
        if (item->kind == ASTNode::FnDecl) {
            auto* decl = static_cast<FnDeclNode*>(item.get());
            if (decl->resolvedFuncGlobalIndex == -2) continue;
            genFnDecl(decl);
        }
    }

    // Generate monomorphized template instances created during this eval only.
    // Previous evals' instances are already code-generated, and their sourceModule
    // pointers may be dangling if modules were invalidated between evals.
    const auto& monoInsts = typeChecker_.monoInstances();
    for (size_t i = typeChecker_.monoInstancesWatermark(); i < monoInsts.size(); ++i) {
        genMonoInstance(*monoInsts[i]);
    }

    // Second pass: generate all top-level statements except the last item
    // (if the last is an ExprStmt, we handle it specially)
    bool lastIsExpr = false;
    ASTNode* lastItem = nullptr;
    if (!program.items.empty()) {
        lastItem = program.items.back().get();
        if (lastItem->kind == ASTNode::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(lastItem);
            if (es->expr->resolvedType && es->expr->resolvedType != compiler_.voidType()) {
                lastIsExpr = true;
            }
        }
    }

    for (size_t i = 0; i < program.items.size(); ++i) {
        auto* item = program.items[i].get();
        if (item->kind == ASTNode::FnDecl || item->kind == ASTNode::StructDecl ||
            item->kind == ASTNode::UnionDecl || item->kind == ASTNode::TypeAliasDecl) {
            continue;
        }
        // If this is the last item and it's a printable expression, gen as expression
        if (lastIsExpr && i == program.items.size() - 1) {
            u16 resultReg = genExpr(static_cast<ExprStmtNode*>(item)->expr.get());
            if (resultReg != 0) {
                emitMov(0, resultReg);
            }
        } else {
            genNode(item);
        }
    }

    emitOp(op_halt);

    popScope();

    currentBlock_->numRegs = maxReg_;
    resolveJumps(currentBlock_);

    return currentBlock_;
}

// --- Generate nodes ---

void CodeGen::genNode(ASTNode* node) {
    switch (node->kind) {
        case ASTNode::Block:        genBlock(static_cast<BlockStmt*>(node)); break;
        case ASTNode::LetDecl:      genLetDecl(static_cast<LetDeclNode*>(node)); break;
        case ASTNode::VarDecl:      genVarDecl(static_cast<VarDeclNode*>(node)); break;
        case ASTNode::ConstDecl:    genConstDecl(static_cast<ConstDeclNode*>(node)); break;
        case ASTNode::FnDecl: {
            auto* decl = static_cast<FnDeclNode*>(node);
            if (decl->localLambdaType) {
                // Local function: compile it and create a Lambda wrapper in a local register
                genFnDecl(decl);

                if (decl->captures.empty()) {
                    // No captures: use op_func_ref (wraps CodeBlock in Lambda with 0 free vars)
                    // codeBlock_ already set on localLambdaType in genFnDecl
                    u16 dst = allocReg();
                    emitOp(op_func_ref);
                    emitRegs(dst);
                    emitPtr(decl->localLambdaType);
                    declareLocal(decl->name, dst, decl->localLambdaType, false);
                } else {
                    // Has captures: load captured values, then use op_make_lambda
                    // codeBlock_ already set on localLambdaType in genFnDecl

                    // Apply byRef-aware capture layout. Same contract as
                    // genLambdaExpr.
                    {
                        std::vector<bool> isUpvarFlags;
                        isUpvarFlags.reserve(decl->captures.size());
                        for (auto& cap : decl->captures) isUpvarFlags.push_back(cap.byReference);
                        decl->localLambdaType->setCaptureLayout(isUpvarFlags);
                    }

                    u16 captureBase = nextReg_;
                    u16 totalCaptureWords = 0;
                    for (size_t i = 0; i < decl->captures.size(); ++i) {
                        auto& cap = decl->captures[i];
                        LocalVar* local = lookupLocal(cap.name);
                        if (!local) {
                            error(decl->loc, "Cannot find captured variable '" + cap.name + "'");
                            allocReg();
                            totalCaptureWords += 1;
                            continue;
                        }
                        if (cap.byReference) {
                            u16 captureReg = allocReg();
                            if (local->isUpvar) {
                                if (local->reg != captureReg) emitMov(captureReg, local->reg);
                            } else {
                                u16 sw = (u16)typeSlotWords(cap.type);
                                u16 mask = computeWordGCMask(cap.type);
                                emitOp(op_capture_upvar_local);
                                emitRegs(captureReg, local->reg, sw);
                                emitInt((i64)mask);
                                emitPtr(cap.type);
                            }
                            totalCaptureWords += 1;
                        } else {
                            u16 sw = (u16)typeSlotWords(cap.type);
                            u16 captureReg = allocRegs(sw);
                            if (local->isUpvar) {
                                emitOp(op_load_upvar_n);
                                emitRegs(captureReg, local->reg, sw);
                            } else if (local->reg != captureReg) {
                                emitMoveN(captureReg, local->reg, sw);
                            }
                            totalCaptureWords += sw;
                        }
                    }

                    u16 dst = allocReg();
                    emitOp(op_make_lambda);
                    emitRegs(dst, captureBase, totalCaptureWords);
                    emitPtr(decl->localLambdaType);
                    declareLocal(decl->name, dst, decl->localLambdaType, false);
                }
            }
            // Top-level functions are already handled in generate()/generateREPL()
            break;
        }
        case ASTNode::StructDecl:   /* type declaration, no code to generate */ break;
        case ASTNode::UnionDecl:    /* type declaration, no code to generate */ break;
        case ASTNode::ImportDecl:   /* already handled in generate() */ break;
        case ASTNode::TypeAliasDecl: /* type alias, no code to generate */ break;
        case ASTNode::ConstraintDecl: /* constraint decl, no code to generate */ break;
        case ASTNode::IfStmt:       genIfStmt(static_cast<IfStmtNode*>(node)); break;
        case ASTNode::WhileStmt:    genWhileStmt(static_cast<WhileStmtNode*>(node)); break;
        case ASTNode::ForStmt:      genForStmt(static_cast<ForStmtNode*>(node)); break;
        case ASTNode::SwitchStmt:   genSwitchStmt(static_cast<SwitchStmtNode*>(node)); break;
        case ASTNode::ReturnStmt:   genReturnStmt(static_cast<ReturnStmtNode*>(node)); break;
        case ASTNode::BreakStmt:    genBreakStmt(static_cast<BreakStmtNode*>(node)); break;
        case ASTNode::ContinueStmt: genContinueStmt(static_cast<ContinueStmtNode*>(node)); break;
        case ASTNode::AssignStmt:   genAssignStmt(static_cast<AssignStmtNode*>(node)); break;
        case ASTNode::IndexAssignStmt: genIndexAssignStmt(static_cast<IndexAssignStmtNode*>(node)); break;
        case ASTNode::ExprStmt:     genExprStmt(static_cast<ExprStmtNode*>(node)); break;
        default:
            error(node->loc, "Codegen: unsupported node type");
            break;
    }
}

void CodeGen::genBlock(BlockStmt* block) {
    u16 savedReg = nextReg_;
    pushScope();
    for (auto& stmt : block->stmts) {
        genNode(stmt.get());
    }
    popScope();
    if (enableRegReclaim) freeRegsTo(savedReg);
}

void CodeGen::genImportDecl(ImportDeclNode* decl) {
    // Find the module from all imported modules by matching the module path
    const auto& allModules = typeChecker_.allImportedModules();
    ModuleInfo* mod = nullptr;
    for (auto* m : allModules) {
        if (m->moduleName == decl->modulePath.back()) {
            mod = m;
            break;
        }
    }

    if (!mod || !mod->initBlock) return;

    // Emit: load init flag -> if true, skip -> call init block -> store flag=true -> skip:
    u16 flagReg = allocReg();
    emitOp(op_load_global);
    emitRegs(flagReg);
    emitInt(mod->initFlagGlobalIndex);

    u32 skipJump = emitJump(op_jump_if_true, flagReg);

    // Call the module init block (0 args)
    u16 argBase = nextReg_;
    u16 resultReg = allocReg();
    clearArgRegTypes(argBase, resultReg);
    emitOp(op_call);
    emitRegs(resultReg, 0, argBase);
    emitInt(mod->initBlockGlobalIndex);
    emitReturnPcStackMap();  // module init returns Void -- no result reg to seed

    // Store flag = true (1)
    u16 trueReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(trueReg);
    emitInt(1);
    emitOp(op_store_global);
    emitRegs(trueReg);
    emitInt(mod->initFlagGlobalIndex);

    patchJump(skipJump);

    freeRegsTo(flagReg);
}

void CodeGen::genLetDecl(LetDeclNode* decl) {
    u16 savedNextReg = nextReg_;
    u16 reg = genExpr(static_cast<Expr*>(decl->init.get()));

    // Pattern destructuring
    if (decl->pattern) {
        std::vector<u32> failJumps; // unused for declarations (always matches)
        genPatternMatch(decl->pattern.get(), reg, decl->resolvedType, failJumps, false);
        return;
    }

    // Handle numeric tower promotion if declared type differs from init type
    if (decl->resolvedType != decl->init->resolvedType) {
        reg = ensureType(reg, decl->init->resolvedType, decl->resolvedType);
    }

    // If the initializer returned an existing (borrowed) register -- e.g. a
    // bare identifier reference -- copy into a fresh register so later
    // mutations of the source don't silently change this binding's value.
    if (reg < savedNextReg) {
        u16 dst = allocSlot(decl->resolvedType);
        emitMoveN(dst, reg, typeSlotWords(decl->resolvedType));
        reg = dst;
    }

    // Check if this is a global (no local scopes)
    auto it = typeChecker_.globalVars().find(decl->name);
    if (it != typeChecker_.globalVars().end() && localScopes_.size() <= 1) {
        emitGlobalStore(reg, it->second.globalIndex, decl->resolvedType, /*init=*/true);
    }

    declareLocal(decl->name, reg, decl->resolvedType, false);
}

void CodeGen::genVarDecl(VarDeclNode* decl) {
    // Dynamic scope variable: var `name = expr;
    if (decl->isDynamic) {
        u16 reg = genExpr(static_cast<Expr*>(decl->init.get()));
        auto it = typeChecker_.dynamicVars().find(decl->name);
        u32 dynIdx = it->second.dynIndex;
        emitDynStore(reg, dynIdx, decl->resolvedType,
                     inFunctionBody_ ? DynScopePush : DynInit);
        return;
    }

    u16 savedNextReg = nextReg_;
    u16 reg = genExpr(static_cast<Expr*>(decl->init.get()));

    // Pattern destructuring
    if (decl->pattern) {
        std::vector<u32> failJumps;
        genPatternMatch(decl->pattern.get(), reg, decl->resolvedType, failJumps, true);
        return;
    }

    // Handle numeric tower promotion if declared type differs from init type
    if (decl->resolvedType != decl->init->resolvedType) {
        reg = ensureType(reg, decl->init->resolvedType, decl->resolvedType);
    }

    // Mutable binding must own its register. If the initializer returned an
    // existing (borrowed) register, copy into a fresh one so assignments to
    // this var don't mutate the source.
    if (reg < savedNextReg) {
        u16 dst = allocSlot(decl->resolvedType);
        emitMoveN(dst, reg, typeSlotWords(decl->resolvedType));
        reg = dst;
    }

    auto it = typeChecker_.globalVars().find(decl->name);
    if (it != typeChecker_.globalVars().end() && localScopes_.size() <= 1) {
        emitGlobalStore(reg, it->second.globalIndex, decl->resolvedType, /*init=*/true);
    }

    declareLocal(decl->name, reg, decl->resolvedType, true);

    // If this `var` is captured by any nested closure, pin its slot for
    // the whole function so freeRegsTo (called at end of every block
    // scope) doesn't reuse it for unrelated temps. The pinning is what
    // keeps the open UpVar's location_ valid until the function returns.
    if (decl->capturedByClosure) {
        u32 sw = typeSlotWords(decl->resolvedType);
        for (u32 k = 0; k < sw; ++k) pinReg((u16)(reg + k));
    }
}

void CodeGen::genConstDecl(ConstDeclNode* decl) {
    u16 reg = genExpr(static_cast<Expr*>(decl->init.get()));

    // Pattern destructuring
    if (decl->pattern) {
        std::vector<u32> failJumps;
        genPatternMatch(decl->pattern.get(), reg, decl->resolvedType, failJumps, false);
        return;
    }

    // Check if this is a global (no local scopes, e.g. REPL top level)
    auto it = typeChecker_.globalVars().find(decl->name);
    if (it != typeChecker_.globalVars().end() && localScopes_.size() <= 1) {
        emitGlobalStore(reg, it->second.globalIndex, decl->resolvedType, /*init=*/true);
    }

    declareLocal(decl->name, reg, decl->resolvedType, false);
}

void CodeGen::genFnDecl(FnDeclNode* decl) {
    if (decl->resolvedFuncGlobalIndex == -2) return;  // template declaration, skip
    if (decl->resolvedFuncGlobalIndex < 0) {
        error(decl->loc, "Function not resolved by type checker");
        return;
    }

    // Find the matching FuncInfo by globalIndex
    auto it = typeChecker_.functions().find(decl->name);
    if (it == typeChecker_.functions().end()) {
        error(decl->loc, "Function not found in type checker");
        return;
    }

    const FuncInfo* funcInfoPtr = nullptr;
    for (auto& fi : it->second) {
        if (fi.canonicalFunc) continue;  // skip partial-arity entries
        if ((i32)fi.globalIndex == decl->resolvedFuncGlobalIndex) {
            funcInfoPtr = &fi;
            break;
        }
    }
    if (!funcInfoPtr) {
        error(decl->loc, "Function overload not found in type checker");
        return;
    }

    const FuncInfo& funcInfo = *funcInfoPtr;

    // Save current codegen state
    CodeBlock* savedBlock = currentBlock_;
    u16 savedNextReg = nextReg_;
    u16 savedMaxReg = maxReg_;
    bool savedTailPos = inTailPosition_;
    auto savedScopes = std::move(localScopes_);
    auto savedFixups = std::move(jumpFixups_);
    auto savedConsts = std::move(constRegs_);
    auto savedPinned = std::move(regPinned_);
    inTailPosition_ = false;
    bool savedInFunctionBody = inFunctionBody_;
    inFunctionBody_ = true;

    // Save coroutine codegen state
    bool savedInCoroFn = inCoroutineFn_;
    u16 savedYieldCount = currentYieldCount_;
    Type* savedReturnType = currentReturnType_;
    if (decl->isCoroutine) {
        inCoroutineFn_ = true;
        currentYieldCount_ = 0;
    }
    currentReturnType_ = funcInfo.returnType;

    // Create new CodeBlock for this function
    currentBlock_ = new CodeBlock();
    currentBlock_->name = compiler_.intern(decl->name);
    currentBlock_->numArgs = (u16)decl->params.size();

    // Phase 4g.2: set funcType on every CodeBlock (not just coroutines) so
    // op_tail_call can read param slot widths to copy multi-word inline args.
    {
        TypeVec argTV(rt::STLAllocator<Type*>(nullptr));
        for (auto& pt : funcInfo.paramTypes) argTV.push_back(pt);
        currentBlock_->funcType = compiler_.functionType(argTV, funcInfo.returnType);
    }
    nextReg_ = 0;
    maxReg_ = 0;
    regTypes_.clear();
    regPinned_.clear();
    jumpFixups_.clear();
    constRegs_.clear();

    // Set up parameter registers
    localScopes_.clear();
    pushScope();
    for (size_t i = 0; i < decl->params.size(); ++i) {
        // Phase 4f: inline value-type params occupy sizeWords consecutive regs.
        u16 paramReg = allocSlot(funcInfo.paramTypes[i]);
        declareLocal(decl->params[i].name, paramReg, funcInfo.paramTypes[i], false);
    }

    // Allocate registers for captured free variables (right after params, like lambdas).
    // Layout matches the outer capture-loading site above: byRef captures
    // take 1 word (UpVar*); byValue captures take sizeWords words inline.
    if (decl->localLambdaType) {
        for (size_t i = 0; i < decl->captures.size(); ++i) {
            auto& cap = decl->captures[i];
            if (cap.byReference) {
                u16 freeVarReg = allocReg();
                declareLocalUpvar(cap.name, freeVarReg, cap.type);
            } else {
                u16 freeVarReg = allocSlot(cap.type);
                declareLocal(cap.name, freeVarReg, cap.type, false);
            }
        }
    }

    // Generate default argument preamble with multiple entry points.
    // Phase 4f: param i lives at the cumulative word offset (inline params
    // occupy sizeWords each), not at index i.
    if (funcInfo.numDefaults > 0) {
        int totalParams = (int)decl->params.size();
        int minArity = funcInfo.minArity;
        currentBlock_->minArity = (u16)minArity;

        // Cumulative word offsets per param.
        std::vector<u16> paramOffsets(totalParams + 1, 0);
        for (int i = 0; i < totalParams; ++i) {
            paramOffsets[i + 1] = paramOffsets[i]
                + (u16)typeSlotWords(funcInfo.paramTypes[i]);
        }
        u16 totalParamWords = paramOffsets[totalParams];

        for (int arity = minArity; arity <= totalParams; ++arity) {
            currentBlock_->defaultEntryOffsets.push_back((u32)currentBlock_->code.size());

            if (arity < totalParams) {
                u16 savedNextReg2 = nextReg_;
                nextReg_ = totalParamWords;  // temps start after all params
                u16 defReg = genExpr(static_cast<Expr*>(decl->params[arity].defaultExpr.get()));
                u16 paramSlot = paramOffsets[arity];
                Type* pt = funcInfo.paramTypes[arity];
                if (defReg != paramSlot) {
                    emitMoveN(paramSlot, defReg, typeSlotWords(pt));
                }
                nextReg_ = savedNextReg2;
            }
        }
    }

    // Phase 1 of tracing-GC project: every function-entry path polls a
    // safepoint. Recursive functions (e.g., binary_trees' build()) contain no
    // backward jumps, so without this they'd never drain the deferred-delete
    // queue during the recursion. The poll's hot path is one relaxed load +
    // branch. All default-arg entry points fall through to here. Phase 2
    // attaches a stack map covering parameter registers.
    emitSafepointWithStackMap();

    // Generate body
    if (decl->body->kind == ASTNode::Block) {
        auto* block = static_cast<BlockStmt*>(decl->body.get());
        for (size_t i = 0; i < block->stmts.size(); ++i) {
            auto* stmt = block->stmts[i].get();
            // Check for trailing expression (implicit return). For coroutine
            // functions the "return type" is actually the yield type; a
            // trailing expression cannot be used as a yield, so just lower it
            // as a normal statement and let op_coro_done close the function.
            if (stmt->kind == ASTNode::ExprStmt) {
                auto* exprStmt = static_cast<ExprStmtNode*>(stmt);
                if (exprStmt->isTrailing && !inCoroutineFn_) {
                    inTailPosition_ = true;
                    u16 resultReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
                    inTailPosition_ = false;
                    emitReturn(resultReg);
                    goto fn_body_done;
                }
            }
            // Check for trailing IfStmtNode with else (value-producing if-else).
            // Skip for Void return: some branches may not produce a value, and
            // there's no value to return anyway -- fall through to genNode.
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::IfStmt
                && !inCoroutineFn_
                && currentReturnType_ != compiler_.voidType()) {
                auto* ifStmt = static_cast<IfStmtNode*>(stmt);
                if (ifStmt->elseBranch) {
                    genIfStmtAsReturn(ifStmt);
                    goto fn_body_done;
                }
            }
            // Check for trailing SwitchStmt (value-producing match).
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::SwitchStmt
                && !inCoroutineFn_
                && currentReturnType_ != compiler_.voidType()) {
                genSwitchStmtAsReturn(static_cast<SwitchStmtNode*>(stmt));
                goto fn_body_done;
            }
            genNode(stmt);
        }
    }

    // If function doesn't explicitly return, emit appropriate terminator
    if (currentBlock_->code.empty() ||
        currentBlock_->code.back().op != op_return) {
        if (inCoroutineFn_) {
            // Coroutine: emit op_coro_done instead of return_void
            emitOp(op_coro_done);
        } else {
            emitOp(op_return_void);
        }
    }

    fn_body_done:
    popScope();
    currentBlock_->numRegs = maxReg_;

    // Resolve jumps for this function's CodeBlock
    resolveJumps(currentBlock_);

    // Store the CodeBlock as a global
    CodeBlock* fnBlock = currentBlock_;

    // Restore codegen state
    currentBlock_ = savedBlock;
    nextReg_ = savedNextReg;
    maxReg_ = savedMaxReg;
    inTailPosition_ = savedTailPos;
    inFunctionBody_ = savedInFunctionBody;
    inCoroutineFn_ = savedInCoroFn;
    currentYieldCount_ = savedYieldCount;
    currentReturnType_ = savedReturnType;
    localScopes_ = std::move(savedScopes);
    jumpFixups_ = std::move(savedFixups);
    constRegs_ = std::move(savedConsts);
    regPinned_ = std::move(savedPinned);

    // Store the function's CodeBlock in the VM's globals
    compiler_.global(funcInfo.globalIndex).p = fnBlock;

    // Set codeBlock_ on localLambdaType so Lambda constructor can read it
    if (decl->localLambdaType) {
        decl->localLambdaType->codeBlock_ = fnBlock;
    }
}

void CodeGen::genMonoInstance(FuncInfo& monoInfo) {
    FnDeclNode* decl = monoInfo.declNode;
    if (!decl) return;

    // For imported templates, switch source context for correct error diagnostics
    std::string savedFilePath = sourceFilePath_;
    std::string savedText = sourceText_;
    if (monoInfo.sourceModule && !monoInfo.sourceModule->sourceFilePath.empty()) {
        sourceFilePath_ = monoInfo.sourceModule->sourceFilePath;
        sourceText_ = monoInfo.sourceModule->sourceText;
    }

    // Re-type-check the body with the monomorphization bindings
    typeChecker_.recheckTemplateBody(decl, &monoInfo, monoInfo.monoBindings);

    // Save current codegen state
    CodeBlock* savedBlock = currentBlock_;
    u16 savedNextReg = nextReg_;
    u16 savedMaxReg = maxReg_;
    bool savedTailPos = inTailPosition_;
    auto savedScopes = std::move(localScopes_);
    auto savedFixups = std::move(jumpFixups_);
    auto savedConsts = std::move(constRegs_);
    auto savedPinned = std::move(regPinned_);
    inTailPosition_ = false;
    bool savedInFunctionBody = inFunctionBody_;
    inFunctionBody_ = true;

    // Save coroutine codegen state
    bool savedInCoroFn = inCoroutineFn_;
    u16 savedYieldCount = currentYieldCount_;
    Type* savedReturnType = currentReturnType_;
    if (decl->isCoroutine) {
        inCoroutineFn_ = true;
        currentYieldCount_ = 0;
    }
    currentReturnType_ = monoInfo.returnType;

    // Create new CodeBlock for this monomorphized function
    currentBlock_ = new CodeBlock();
    currentBlock_->name = compiler_.intern(decl->name);
    currentBlock_->numArgs = (u16)decl->params.size();

    // For coroutine functions, set funcType on CodeBlock (for op_coro_create)
    if (decl->isCoroutine) {
        TypeVec argTV(rt::STLAllocator<Type*>(nullptr));
        for (auto& pt : monoInfo.paramTypes) argTV.push_back(pt);
        currentBlock_->funcType = compiler_.functionType(argTV, monoInfo.returnType);
    }
    nextReg_ = 0;
    maxReg_ = 0;
    regTypes_.clear();
    regPinned_.clear();
    jumpFixups_.clear();
    constRegs_.clear();

    // Set up parameter registers using monomorphized types
    localScopes_.clear();
    pushScope();
    for (size_t i = 0; i < decl->params.size(); ++i) {
        // Phase 4f: inline value-type params occupy sizeWords consecutive regs.
        u16 paramReg = allocSlot(monoInfo.paramTypes[i]);
        declareLocal(decl->params[i].name, paramReg, monoInfo.paramTypes[i], false);
    }

    // Generate default argument preamble with multiple entry points.
    // Phase 4f: param i lives at the cumulative word offset.
    if (monoInfo.numDefaults > 0) {
        int totalParams = (int)decl->params.size();
        int minArity = monoInfo.minArity;
        currentBlock_->minArity = (u16)minArity;

        std::vector<u16> paramOffsets(totalParams + 1, 0);
        for (int i = 0; i < totalParams; ++i) {
            paramOffsets[i + 1] = paramOffsets[i]
                + (u16)typeSlotWords(monoInfo.paramTypes[i]);
        }
        u16 totalParamWords = paramOffsets[totalParams];

        for (int arity = minArity; arity <= totalParams; ++arity) {
            currentBlock_->defaultEntryOffsets.push_back((u32)currentBlock_->code.size());

            if (arity < totalParams) {
                u16 savedNextReg2 = nextReg_;
                nextReg_ = totalParamWords;
                u16 defReg = genExpr(static_cast<Expr*>(decl->params[arity].defaultExpr.get()));
                u16 paramSlot = paramOffsets[arity];
                Type* pt = monoInfo.paramTypes[arity];
                if (defReg != paramSlot) {
                    emitMoveN(paramSlot, defReg, typeSlotWords(pt));
                }
                nextReg_ = savedNextReg2;
            }
        }
    }

    // Generate body (same logic as genFnDecl)
    if (decl->body->kind == ASTNode::Block) {
        auto* block = static_cast<BlockStmt*>(decl->body.get());
        for (size_t i = 0; i < block->stmts.size(); ++i) {
            auto* stmt = block->stmts[i].get();
            if (stmt->kind == ASTNode::ExprStmt) {
                auto* exprStmt = static_cast<ExprStmtNode*>(stmt);
                if (exprStmt->isTrailing) {
                    inTailPosition_ = true;
                    u16 resultReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
                    inTailPosition_ = false;
                    emitReturn(resultReg);
                    goto mono_body_done;
                }
            }
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::IfStmt
                && currentReturnType_ != compiler_.voidType()) {
                auto* ifStmt = static_cast<IfStmtNode*>(stmt);
                if (ifStmt->elseBranch) {
                    genIfStmtAsReturn(ifStmt);
                    goto mono_body_done;
                }
            }
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::SwitchStmt
                && currentReturnType_ != compiler_.voidType()) {
                genSwitchStmtAsReturn(static_cast<SwitchStmtNode*>(stmt));
                goto mono_body_done;
            }
            genNode(stmt);
        }
    }

    if (currentBlock_->code.empty() ||
        currentBlock_->code.back().op != op_return) {
        if (inCoroutineFn_) {
            emitOp(op_coro_done);
        } else {
            emitOp(op_return_void);
        }
    }

    mono_body_done:
    popScope();
    currentBlock_->numRegs = maxReg_;
    resolveJumps(currentBlock_);

    CodeBlock* fnBlock = currentBlock_;

    // Restore codegen state
    currentBlock_ = savedBlock;
    nextReg_ = savedNextReg;
    maxReg_ = savedMaxReg;
    inTailPosition_ = savedTailPos;
    inFunctionBody_ = savedInFunctionBody;
    inCoroutineFn_ = savedInCoroFn;
    currentYieldCount_ = savedYieldCount;
    currentReturnType_ = savedReturnType;
    localScopes_ = std::move(savedScopes);
    jumpFixups_ = std::move(savedFixups);
    constRegs_ = std::move(savedConsts);
    regPinned_ = std::move(savedPinned);

    // Restore source context
    sourceFilePath_ = savedFilePath;
    sourceText_ = savedText;

    // Store the CodeBlock in the VM's globals
    compiler_.global(monoInfo.globalIndex).p = fnBlock;
}

void CodeGen::genIfStmt(IfStmtNode* stmt) {
    u16 savedReg = nextReg_;
    u16 condReg = genExpr(static_cast<Expr*>(stmt->condition.get()));

    if (stmt->elseBranch) {
        // if-else: jump to else on false
        u32 elseJump = emitJump(op_jump_if_false, condReg);

        if (enableRegReclaim) freeRegsTo(savedReg);
        genNode(stmt->thenBranch.get());
        u32 endJump = emitJump(op_jump);

        patchJump(elseJump);
        if (enableRegReclaim) freeRegsTo(savedReg);
        genNode(stmt->elseBranch.get());
        patchJump(endJump);
    } else {
        // if without else: jump past then on false
        u32 endJump = emitJump(op_jump_if_false, condReg);
        if (enableRegReclaim) freeRegsTo(savedReg);
        genNode(stmt->thenBranch.get());
        patchJump(endJump);
    }
    if (enableRegReclaim) freeRegsTo(savedReg);
}

void CodeGen::genWhileStmt(WhileStmtNode* stmt) {
    u16 savedReg = nextReg_;
    // Mutable locals from before the loop may be reassigned by the body or
    // before the condition is re-evaluated; their initializer-time const
    // tracking does not hold across iterations.
    clearConstsForMutableLocals();
    // Loop header - capture index (not pointer, since vector may reallocate)
    u32 loopStartIdx = (u32)currentBlock_->code.size();

    u16 condReg = genExpr(static_cast<Expr*>(stmt->condition.get()));
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // Push loop context for break/continue
    loopStack_.push_back({savedReg, {}, {}});

    genNode(stmt->body.get());

    // Patch continue jumps to here (re-evaluate condition)
    for (u32 cj : loopStack_.back().continueJumps) {
        patchJump(cj);
    }

    // Reclaim body temps before jumping back to condition
    if (enableRegReclaim) freeRegsTo(savedReg);

    // Jump back to loop header
    emitJumpTo(loopStartIdx);

    patchJump(exitJump);
    // Patch break jumps to here (after loop)
    for (u32 bj : loopStack_.back().breakJumps) {
        patchJump(bj);
    }
    loopStack_.pop_back();
    if (enableRegReclaim) freeRegsTo(savedReg);
}

void CodeGen::genForStmt(ForStmtNode* stmt) {
    Type* iterType = stmt->iterable->resolvedType;

    // Strategy 1: For-loop over Range — inline counter loop
    if (auto* rangeType = dynamic_cast<RangeType*>(iterType)) {
        auto* rangeExpr = dynamic_cast<RangeExprNode*>(stmt->iterable.get());
        if (rangeExpr && rangeType->elemType_ == compiler_.intType()) {
            // Inline Int range loop — no RangeObj allocation
            u16 startReg = genExpr(static_cast<Expr*>(rangeExpr->start.get()));

            // Compute end first (needed for step inference)
            u16 endReg = 0;
            if (!rangeExpr->isInfinite) {
                endReg = genExpr(static_cast<Expr*>(rangeExpr->end.get()));
            }

            // Compute step
            u16 stepReg;
            if (rangeExpr->next) {
                u16 nextReg = genExpr(static_cast<Expr*>(rangeExpr->next.get()));
                stepReg = allocReg();
                emitOp(op_sub_int);
                emitRegs(stepReg, nextReg, startReg);
            } else if (!rangeExpr->isInfinite) {
                // Infer step from direction: step = (start <= end) ? 1 : -1
                // cmp = start <= end  → 0 or 1
                // step = cmp * 2 - 1  → -1 or 1
                u16 cmpReg = allocReg();
                emitOp(op_cmp_le_int);
                emitRegs(cmpReg, startReg, endReg);

                u16 twoReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(twoReg);
                emitInt(2);

                u16 t = allocReg();
                emitOp(op_mul_int);
                emitRegs(t, cmpReg, twoReg);

                u16 oneReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(oneReg);
                emitInt(1);

                stepReg = allocReg();
                emitOp(op_sub_int);
                emitRegs(stepReg, t, oneReg);
            } else {
                // Infinite range with no next: default step = 1
                stepReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(stepReg);
                emitInt(1);
            }

            // iReg = copy of startReg
            u16 iReg = allocReg();
            emitMov(iReg, startReg);

            // For direction check: condReg for step >= 0
            u16 zeroReg = allocReg();
            emitOp(op_load_int_const);
            emitRegs(zeroReg);
            emitInt(0);

            u16 stepNonNegReg = allocReg();
            emitOp(op_cmp_ge_int);
            emitRegs(stepNonNegReg, stepReg, zeroReg);

            // loopStart:
            u16 loopSavedReg = nextReg_;
            u32 loopStartIdx = (u32)currentBlock_->code.size();

            if (!rangeExpr->isInfinite) {
                u16 condAscReg = allocReg();
                emitOp(op_cmp_le_int);
                emitRegs(condAscReg, iReg, endReg);

                u16 condDescReg = allocReg();
                emitOp(op_cmp_ge_int);
                emitRegs(condDescReg, iReg, endReg);

                u16 t1 = allocReg();
                emitOp(op_mul_int);
                emitRegs(t1, stepNonNegReg, condAscReg);

                u16 oneReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(oneReg);
                emitInt(1);

                u16 notStep = allocReg();
                emitOp(op_sub_int);
                emitRegs(notStep, oneReg, stepNonNegReg);

                u16 t2 = allocReg();
                emitOp(op_mul_int);
                emitRegs(t2, notStep, condDescReg);

                u16 condReg = allocReg();
                emitOp(op_add_int);
                emitRegs(condReg, t1, t2);

                u32 exitJump = emitJump(op_jump_if_false, condReg);

                // body
                loopStack_.push_back({loopSavedReg, {}, {}});
                pushScope();
                declareLocal(stmt->varName, iReg, rangeType->elemType_, false);
                clearConstsForMutableLocals();
                genNode(stmt->body.get());
                popScope();

                // Patch continue jumps to increment code
                for (u32 cj : loopStack_.back().continueJumps) {
                    patchJump(cj);
                }

                // increment: i = i + step
                emitOp(op_add_int);
                emitRegs(iReg, iReg, stepReg);

                if (enableRegReclaim) freeRegsTo(loopSavedReg);
                emitJumpTo(loopStartIdx);
                patchJump(exitJump);
                // Patch break jumps to after loop
                for (u32 bj : loopStack_.back().breakJumps) {
                    patchJump(bj);
                }
                loopStack_.pop_back();
            } else {
                // Infinite range — no exit condition (loop until break or forever)
                loopStack_.push_back({loopSavedReg, {}, {}});
                pushScope();
                declareLocal(stmt->varName, iReg, rangeType->elemType_, false);
                clearConstsForMutableLocals();
                genNode(stmt->body.get());
                popScope();

                // Patch continue jumps to increment code
                for (u32 cj : loopStack_.back().continueJumps) {
                    patchJump(cj);
                }

                emitOp(op_add_int);
                emitRegs(iReg, iReg, stepReg);

                if (enableRegReclaim) freeRegsTo(loopSavedReg);
                emitJumpTo(loopStartIdx);
                // Patch break jumps to after loop
                for (u32 bj : loopStack_.back().breakJumps) {
                    patchJump(bj);
                }
                loopStack_.pop_back();
            }
            return;
        }
        if (rangeExpr && rangeType->elemType_ == compiler_.fractionType()) {
            // Inline Fraction range loop — no RangeObj allocation.
            // Phase 4f: Fraction is inline 2 words [numer, denom]; allocate
            // 2-reg slots and use emitMoveN for copies.
            Type* fracType = compiler_.fractionType();
            u16 startReg = genExpr(static_cast<Expr*>(rangeExpr->start.get()));

            // Compute end first (needed for step inference)
            u16 endReg = 0;
            if (!rangeExpr->isInfinite) {
                endReg = genExpr(static_cast<Expr*>(rangeExpr->end.get()));
            }

            // Compute step
            u16 stepReg;
            if (rangeExpr->next) {
                u16 nextReg = genExpr(static_cast<Expr*>(rangeExpr->next.get()));
                stepReg = allocSlot(fracType);
                emitOp(op_sub_fraction);
                emitRegs(stepReg, nextReg, startReg);
            } else if (!rangeExpr->isInfinite) {
                // Infer step from direction: step = (start <= end) ? 1/1 : -1/1
                u16 cmpReg = allocReg();
                emitOp(op_cmp_le_fraction);
                emitRegs(cmpReg, startReg, endReg);

                u16 twoReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(twoReg);
                emitInt(2);

                u16 t = allocReg();
                emitOp(op_mul_int);
                emitRegs(t, cmpReg, twoReg);

                u16 oneReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(oneReg);
                emitInt(1);

                u16 intStepReg = allocReg();
                emitOp(op_sub_int);
                emitRegs(intStepReg, t, oneReg);

                stepReg = allocSlot(fracType);
                emitOp(op_int_to_fraction);
                emitRegs(stepReg, intStepReg);
            } else {
                // Infinite range with no next: default step = 1/1.
                // Inline literal: numer=1, denom=1 in 2 consecutive words.
                stepReg = allocSlot(fracType);
                emitOp(op_load_int_const);
                emitRegs(stepReg);
                emitInt(1);
                emitOp(op_load_int_const);
                emitRegs((u16)(stepReg + 1));
                emitInt(1);
            }

            // iReg = copy of startReg (2 words for inline Fraction)
            u16 iReg = allocSlot(fracType);
            emitMoveN(iReg, startReg, 2);

            // For direction check: stepNonNeg = step >= 0/1
            // Inline zero = (numer=0, denom=1).
            u16 zeroReg = allocSlot(fracType);
            emitOp(op_load_int_const);
            emitRegs(zeroReg);
            emitInt(0);
            emitOp(op_load_int_const);
            emitRegs((u16)(zeroReg + 1));
            emitInt(1);

            u16 stepNonNegReg = allocReg();
            emitOp(op_cmp_ge_fraction);
            emitRegs(stepNonNegReg, stepReg, zeroReg);

            // loopStart:
            u16 loopSavedReg2 = nextReg_;
            u32 loopStartIdx = (u32)currentBlock_->code.size();

            if (!rangeExpr->isInfinite) {
                u16 condAscReg = allocReg();
                emitOp(op_cmp_le_fraction);
                emitRegs(condAscReg, iReg, endReg);

                u16 condDescReg = allocReg();
                emitOp(op_cmp_ge_fraction);
                emitRegs(condDescReg, iReg, endReg);

                u16 t1 = allocReg();
                emitOp(op_mul_int);
                emitRegs(t1, stepNonNegReg, condAscReg);

                u16 oneReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(oneReg);
                emitInt(1);

                u16 notStep = allocReg();
                emitOp(op_sub_int);
                emitRegs(notStep, oneReg, stepNonNegReg);

                u16 t2 = allocReg();
                emitOp(op_mul_int);
                emitRegs(t2, notStep, condDescReg);

                u16 condReg = allocReg();
                emitOp(op_add_int);
                emitRegs(condReg, t1, t2);

                u32 exitJump = emitJump(op_jump_if_false, condReg);

                // body
                loopStack_.push_back({loopSavedReg2, {}, {}});
                pushScope();
                declareLocal(stmt->varName, iReg, rangeType->elemType_, false);
                clearConstsForMutableLocals();
                genNode(stmt->body.get());
                popScope();

                // Patch continue jumps to increment code
                for (u32 cj : loopStack_.back().continueJumps) {
                    patchJump(cj);
                }

                // increment: i = i + step
                emitOp(op_add_fraction);
                emitRegs(iReg, iReg, stepReg);

                if (enableRegReclaim) freeRegsTo(loopSavedReg2);
                emitJumpTo(loopStartIdx);
                patchJump(exitJump);
                // Patch break jumps to after loop
                for (u32 bj : loopStack_.back().breakJumps) {
                    patchJump(bj);
                }
                loopStack_.pop_back();
            } else {
                // Infinite range — no exit condition
                loopStack_.push_back({loopSavedReg2, {}, {}});
                pushScope();
                declareLocal(stmt->varName, iReg, rangeType->elemType_, false);
                clearConstsForMutableLocals();
                genNode(stmt->body.get());
                popScope();

                // Patch continue jumps to increment code
                for (u32 cj : loopStack_.back().continueJumps) {
                    patchJump(cj);
                }

                emitOp(op_add_fraction);
                emitRegs(iReg, iReg, stepReg);

                if (enableRegReclaim) freeRegsTo(loopSavedReg2);
                emitJumpTo(loopStartIdx);
                // Patch break jumps to after loop
                for (u32 bj : loopStack_.back().breakJumps) {
                    patchJump(bj);
                }
                loopStack_.pop_back();
            }
            return;
        }
        // Non-inline range (e.g., range stored in variable) — fall through to general approach below
    }

    // Strategy 2: For-loop over Array
    if (auto* arrayType = dynamic_cast<ArrayType*>(iterType)) {
        u16 arrReg = genExpr(static_cast<Expr*>(stmt->iterable.get()));

        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrayType->elemType_));
        emitRegs(lenReg, arrReg);
        emitPtr(arrayType);

        u16 idxReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(idxReg);
        emitInt(0);

        // loopStart:
        u16 loopSavedReg = nextReg_;
        u32 loopStartIdx = (u32)currentBlock_->code.size();

        u16 condReg = allocReg();
        emitOp(op_cmp_lt_int);
        emitRegs(condReg, idxReg, lenReg);

        u32 exitJump = emitJump(op_jump_if_false, condReg);

        // Phase 4e: inline-element arrays land 2-word values directly.
        u16 elemReg = allocSlot(arrayType->elemType_);
        emitOp(opArrayGetDynFor(arrayType->elemType_));
        emitRegs(elemReg, arrReg, idxReg);
        emitPtr(arrayType);

        loopStack_.push_back({loopSavedReg, {}, {}});
        pushScope();
        declareLocal(stmt->varName, elemReg, arrayType->elemType_, false);
        clearConstsForMutableLocals();
        genNode(stmt->body.get());
        popScope();

        // Patch continue jumps to increment code
        for (u32 cj : loopStack_.back().continueJumps) {
            patchJump(cj);
        }

        // idxReg += 1
        u16 oneReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(oneReg);
        emitInt(1);
        emitOp(op_add_int);
        emitRegs(idxReg, idxReg, oneReg);

        if (enableRegReclaim) freeRegsTo(loopSavedReg);
        emitJumpTo(loopStartIdx);
        patchJump(exitJump);
        // Patch break jumps to after loop
        for (u32 bj : loopStack_.back().breakJumps) {
            patchJump(bj);
        }
        loopStack_.pop_back();
        return;
    }

    // Strategy 3: For-loop over List
    if (auto* listType = dynamic_cast<ListType*>(iterType)) {
        u16 listReg = genExpr(static_cast<Expr*>(stmt->iterable.get()));

        // loopStart:
        u16 loopSavedReg = nextReg_;
        u32 loopStartIdx = (u32)currentBlock_->code.size();

        u16 isNilReg = allocReg();
        emitOp(op_list_is_nil);
        emitRegs(isNilReg, listReg);

        u32 exitJump = emitJump(op_jump_if_true, isNilReg);

        u16 headReg = allocReg();
        emitOp(op_list_head);
        emitRegs(headReg, listReg);

        loopStack_.push_back({loopSavedReg, {}, {}});
        pushScope();
        declareLocal(stmt->varName, headReg, listType->elemType_, false);
        clearConstsForMutableLocals();
        genNode(stmt->body.get());
        popScope();

        // Patch continue jumps to advance code
        for (u32 cj : loopStack_.back().continueJumps) {
            patchJump(cj);
        }

        // listReg = tail(listReg)
        emitOp(op_list_tail);
        emitRegs(listReg, listReg);

        if (enableRegReclaim) freeRegsTo(loopSavedReg);
        emitJumpTo(loopStartIdx);
        patchJump(exitJump);
        // Patch break jumps to after loop
        for (u32 bj : loopStack_.back().breakJumps) {
            patchJump(bj);
        }
        loopStack_.pop_back();
        return;
    }

    // Strategy 4: For-loop over Coroutine
    if (auto* coroType = dynamic_cast<CoroutineType*>(iterType)) {
        u16 coroReg = genExpr(static_cast<Expr*>(stmt->iterable.get()));

        // loopStart:
        u16 loopSavedReg = nextReg_;
        u32 loopStartIdx = (u32)currentBlock_->code.size();

        // op_coro_resume -> value directly (no Enum wrapper).
        // Phase 4g.12: yield type may be multi-word (inline composite); allocate
        // a slot wide enough to hold it.
        u16 elemReg = allocSlot(coroType->yieldType_);
        emitOp(op_coro_resume);
        emitRegs(elemReg, coroReg);
        emitReturnPcStackMap(elemReg, coroType->yieldType_);

        // Check if coroutine is done
        u16 doneReg = allocReg();
        emitOp(op_coro_is_done);
        emitRegs(doneReg, coroReg);
        u32 exitJump = emitJump(op_jump_if_true, doneReg);

        loopStack_.push_back({loopSavedReg, {}, {}});
        pushScope();
        declareLocal(stmt->varName, elemReg, coroType->yieldType_, false);
        clearConstsForMutableLocals();
        genNode(stmt->body.get());
        popScope();

        // Patch continue jumps to loop top
        for (u32 cj : loopStack_.back().continueJumps) {
            patchJump(cj);
        }

        if (enableRegReclaim) freeRegsTo(loopSavedReg);
        emitJumpTo(loopStartIdx);
        patchJump(exitJump);
        // Patch break jumps to after loop
        for (u32 bj : loopStack_.back().breakJumps) {
            patchJump(bj);
        }
        loopStack_.pop_back();
        return;
    }

    error(stmt->loc, "For-loop over unsupported iterable type");
}

void CodeGen::genBreakStmt(BreakStmtNode* stmt) {
    if (loopStack_.empty()) {
        error(stmt->loc, "'break' outside of loop");
        return;
    }
    auto& loop = loopStack_.back();
    if (enableRegReclaim) freeRegsTo(loop.savedReg);
    u32 jumpPos = emitJump(op_jump);
    loop.breakJumps.push_back(jumpPos);
}

void CodeGen::genContinueStmt(ContinueStmtNode* stmt) {
    if (loopStack_.empty()) {
        error(stmt->loc, "'continue' outside of loop");
        return;
    }
    auto& loop = loopStack_.back();
    if (enableRegReclaim) freeRegsTo(loop.savedReg);
    u32 jumpPos = emitJump(op_jump);
    loop.continueJumps.push_back(jumpPos);
}

void CodeGen::genSwitchStmt(SwitchStmtNode* stmt) {
    // Evaluate subject once
    u16 subjReg = genExpr(static_cast<Expr*>(stmt->subject.get()));
    Type* subjType = stmt->subject->resolvedType;
    u16 caseSavedReg = nextReg_;

    // Track all end-of-case jumps (patch to after the switch)
    std::vector<u32> endJumps;

    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        auto& clause = stmt->cases[i];

        if (enableRegReclaim) freeRegsTo(caseSavedReg);
        pushScope();

        // Generate pattern match tests
        std::vector<u32> failJumps;
        genPatternMatch(clause.pattern.get(), subjReg, subjType, failJumps);

        // Generate body
        genNode(clause.body.get());

        // Jump to end of switch (skip remaining cases)
        u32 endJump = emitJump(op_jump);
        endJumps.push_back(endJump);

        popScope();

        // Patch fail jumps to here (start of next case)
        for (u32 fj : failJumps) {
            patchJump(fj);
        }
    }

    if (enableRegReclaim) freeRegsTo(caseSavedReg);

    // Patch all end jumps to here (after the switch)
    for (u32 ej : endJumps) {
        patchJump(ej);
    }
}

void CodeGen::emitGlobalStoreIfNeeded(const std::string& name, u16 reg) {
    auto it = typeChecker_.globalVars().find(name);
    if (it != typeChecker_.globalVars().end() && localScopes_.size() <= 1) {
        emitGlobalStore(reg, it->second.globalIndex, it->second.type, /*init=*/true);
    }
}

void CodeGen::genPatternMatch(Pattern* pat, u16 subjReg, Type* subjType,
                               std::vector<u32>& failJumps, bool isMutable) {
    switch (pat->kind) {
        case Pattern::LiteralPat: {
            auto* lit = static_cast<LiteralPattern*>(pat);

            // Handle nil pattern: check if list is empty (null pointer)
            if (lit->literal->kind == ASTNode::NilLiteral) {
                u16 isNilReg = allocReg();
                emitOp(op_list_is_nil);
                emitRegs(isNilReg, subjReg);
                u32 failJump = emitJump(op_jump_if_false, isNilReg);
                failJumps.push_back(failJump);
                break;
            }

            u16 litReg = genExpr(static_cast<Expr*>(lit->literal.get()));
            Type* litType = lit->literal->resolvedType;

            u16 cmpReg = allocReg();

            if (subjType == compiler_.intType() || subjType == compiler_.boolType()) {
                emitOp(op_cmp_eq_int);
                emitRegs(cmpReg, subjReg, litReg);
            } else if (subjType == compiler_.floatType()) {
                u16 promotedLit = ensureFloat(litReg, litType);
                emitOp(op_cmp_eq_float);
                emitRegs(cmpReg, subjReg, promotedLit);
            } else if (subjType == compiler_.stringType()) {
                emitOp(op_cmp_eq_str);
                emitRegs(cmpReg, subjReg, litReg);
            } else if (subjType == compiler_.symbolType()) {
                // Symbols are interned; compare pointers as integers
                emitOp(op_cmp_eq_int);
                emitRegs(cmpReg, subjReg, litReg);
            } else {
                error(pat->loc, "Unsupported literal pattern type for switch");
                break;
            }

            u32 failJump = emitJump(op_jump_if_false, cmpReg);
            failJumps.push_back(failJump);
            break;
        }

        case Pattern::WildcardPat:
            // Always matches — no code needed
            break;

        case Pattern::BindingPat: {
            auto* bp = static_cast<BindingPattern*>(pat);
            if (pat->enumCaseIndex >= 0) {
                // Phase 3: NullablePtrEnum -- compare subject against null.
                if (subjType && subjType->repr_ == ts::Type::Repr::NullablePtrEnum) {
                    auto* etype = dynamic_cast<EnumType*>(subjType);
                    Type* caseType = (etype && (size_t)pat->enumCaseIndex < etype->cases_.size())
                        ? etype->cases_[pat->enumCaseIndex].type
                        : nullptr;
                    bool isVoidCase = (caseType == compiler_.voidType());
                    u16 nullReg = allocReg();
                    emitOp(op_load_nil);
                    emitRegs(nullReg);
                    u16 cmpReg = allocReg();
                    emitOp(op_cmp_eq_obj);
                    emitRegs(cmpReg, subjReg, nullReg);
                    emitPtr(etype);
                    u32 failJump = isVoidCase
                        ? emitJump(op_jump_if_false, cmpReg)
                        : emitJump(op_jump_if_true,  cmpReg);
                    failJumps.push_back(failJump);
                    break;
                }
                // Unqualified no-data enum case match.
                // Phase 2: DiscriminantEnum -- subject IS the i64 tag.
                // Phase 4g.4: Inline enum -- subject's word 0 IS the i64 tag.
                u16 whichReg;
                if (subjType
                    && (subjType->repr_ == ts::Type::Repr::DiscriminantEnum
                        || subjType->repr_ == ts::Type::Repr::Inline)) {
                    whichReg = subjReg;
                } else {
                    whichReg = allocReg();
                    emitOp(op_enum_get_which);
                    emitRegs(whichReg, subjReg);
                }

                u16 expectedReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(expectedReg);
                emitInt((i64)pat->enumCaseIndex);

                u16 matchReg = allocReg();
                emitOp(op_cmp_eq_int);
                emitRegs(matchReg, whichReg, expectedReg);

                u32 failJump = emitJump(op_jump_if_false, matchReg);
                failJumps.push_back(failJump);
                break;
            }
            // Always matches — bind subject to a local variable.
            // Phase 4f: inline value-type bindings need 2-word slots.
            u16 bindReg = allocSlot(subjType);
            emitMoveN(bindReg, subjReg, typeSlotWords(subjType));
            declareLocal(bp->name, bindReg, subjType, isMutable);
            emitGlobalStoreIfNeeded(bp->name, bindReg);
            break;
        }

        case Pattern::EnumPat: {
            auto* ep = static_cast<EnumPattern*>(pat);
            auto* etype = dynamic_cast<EnumType*>(subjType);
            if (!etype) {
                error(pat->loc, "Enum pattern on non-enum type");
                break;
            }

            // Find case index and type
            int caseIdx = -1;
            Type* caseType = nullptr;
            for (size_t i = 0; i < etype->cases_.size(); ++i) {
                if (etype->cases_[i].name->str() == ep->caseName) {
                    caseIdx = (int)i;
                    caseType = etype->cases_[i].type;
                    break;
                }
            }

            if (caseIdx < 0) {
                error(pat->loc, "Unknown enum case '" + ep->caseName + "'");
                break;
            }

            // Phase 3: NullablePtrEnum -- compare subject against null pointer.
            if (etype->repr_ == ts::Type::Repr::NullablePtrEnum) {
                bool isVoidCase = (caseType == compiler_.voidType());
                u16 nullReg = allocReg();
                emitOp(op_load_nil);
                emitRegs(nullReg);
                u16 cmpReg = allocReg();
                emitOp(op_cmp_eq_obj);
                emitRegs(cmpReg, subjReg, nullReg);
                emitPtr(etype);
                // For the void (None) case, match when subj == null.
                // For the data (Some) case, match when subj != null.
                u32 failJump = isVoidCase
                    ? emitJump(op_jump_if_false, cmpReg)
                    : emitJump(op_jump_if_true,  cmpReg);
                failJumps.push_back(failJump);
                if (ep->innerPattern && caseType && caseType != compiler_.voidType()) {
                    // The Some payload IS the subject register itself.
                    genPatternMatch(ep->innerPattern.get(), subjReg, caseType, failJumps, isMutable);
                }
                break;
            }

            // Get which_ field from enum.
            // - DiscriminantEnum: subject IS the i64 tag.
            // - Inline (Phase 4g.4): subject's word 0 IS the i64 tag.
            u16 whichReg;
            if (etype->repr_ == ts::Type::Repr::DiscriminantEnum
                || etype->repr_ == ts::Type::Repr::Inline) {
                whichReg = subjReg;
            } else {
                whichReg = allocReg();
                emitOp(op_enum_get_which);
                emitRegs(whichReg, subjReg);
            }

            // Compare to expected case index
            u16 expectedReg = allocReg();
            emitOp(op_load_int_const);
            emitRegs(expectedReg);
            emitInt((i64)caseIdx);

            u16 matchReg = allocReg();
            emitOp(op_cmp_eq_int);
            emitRegs(matchReg, whichReg, expectedReg);

            u32 failJump = emitJump(op_jump_if_false, matchReg);
            failJumps.push_back(failJump);

            // If matched and inner pattern exists, extract value and recurse.
            if (ep->innerPattern && caseType && caseType != compiler_.voidType()) {
                u16 valReg;
                if (etype->repr_ == ts::Type::Repr::Inline) {
                    // Phase 4g.4: payload lives in-place at subjReg+1; bind
                    // the alias directly so multi-word reads work.
                    valReg = (u16)(subjReg + 1);
                } else {
                    // Phase 4g.15: heap Enum payload is native multi-word.
                    // Allocate a slot sized for caseType and copy directly.
                    valReg = allocSlot(caseType);
                    emitOp(op_enum_get_value);
                    emitRegs(valReg, subjReg);
                    emitPtr(caseType);
                }
                genPatternMatch(ep->innerPattern.get(), valReg, caseType, failJumps, isMutable);
            }
            break;
        }

        case Pattern::StructPat: {
            auto* sp = static_cast<StructPattern*>(pat);
            auto* stype = dynamic_cast<StructType*>(subjType);
            if (!stype) {
                error(pat->loc, "Struct pattern on non-struct type");
                break;
            }

            for (auto& field : sp->fields) {
                // Find field index
                int fieldIdx = -1;
                Type* fieldType = nullptr;
                for (size_t j = 0; j < stype->fields_.size(); ++j) {
                    if (stype->fields_[j].name->str() == field.name) {
                        fieldIdx = (int)j;
                        fieldType = stype->fields_[j].type;
                        break;
                    }
                }

                if (fieldIdx < 0) {
                    error(field.loc, "Unknown field '" + field.name + "'");
                    continue;
                }

                u16 fieldReg = emitFieldGet(stype, subjReg, (u16)fieldIdx, fieldType);
                genPatternMatch(field.pattern.get(), fieldReg, fieldType, failJumps, isMutable);
            }
            break;
        }

        case Pattern::TuplePat: {
            auto* tp = static_cast<TuplePattern*>(pat);

            // Unqualified enum case pattern with data
            if (pat->enumCaseIndex >= 0) {
                // Phase 3: NullablePtrEnum -- compare against null; payload IS the subject.
                if (subjType && subjType->repr_ == ts::Type::Repr::NullablePtrEnum) {
                    auto* etype = dynamic_cast<EnumType*>(subjType);
                    Type* caseType = (etype && (size_t)pat->enumCaseIndex < etype->cases_.size())
                        ? etype->cases_[pat->enumCaseIndex].type
                        : nullptr;
                    bool isVoidCase = (caseType == compiler_.voidType());
                    u16 nullReg = allocReg();
                    emitOp(op_load_nil);
                    emitRegs(nullReg);
                    u16 cmpReg = allocReg();
                    emitOp(op_cmp_eq_obj);
                    emitRegs(cmpReg, subjReg, nullReg);
                    emitPtr(etype);
                    u32 failJump = isVoidCase
                        ? emitJump(op_jump_if_false, cmpReg)
                        : emitJump(op_jump_if_true,  cmpReg);
                    failJumps.push_back(failJump);
                    if (pat->enumCaseDataType && pat->enumCaseDataType != compiler_.voidType()
                        && tp->elements.size() == 1) {
                        genPatternMatch(tp->elements[0].get(), subjReg, pat->enumCaseDataType, failJumps, isMutable);
                    }
                    break;
                }

                u16 whichReg;
                if (subjType
                    && (subjType->repr_ == ts::Type::Repr::DiscriminantEnum
                        || subjType->repr_ == ts::Type::Repr::Inline)) {
                    whichReg = subjReg;
                } else {
                    whichReg = allocReg();
                    emitOp(op_enum_get_which);
                    emitRegs(whichReg, subjReg);
                }

                u16 expectedReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(expectedReg);
                emitInt((i64)pat->enumCaseIndex);

                u16 matchReg = allocReg();
                emitOp(op_cmp_eq_int);
                emitRegs(matchReg, whichReg, expectedReg);

                u32 failJump = emitJump(op_jump_if_false, matchReg);
                failJumps.push_back(failJump);

                // Extract and match inner data.
                if (pat->enumCaseDataType && pat->enumCaseDataType != compiler_.voidType()) {
                    u16 valReg;
                    if (subjType && subjType->repr_ == ts::Type::Repr::Inline) {
                        // Phase 4g.4: payload lives in-place at subjReg+1.
                        valReg = (u16)(subjReg + 1);
                    } else {
                        // Phase 4g.15: heap Enum payload is native multi-word.
                        valReg = allocSlot(pat->enumCaseDataType);
                        emitOp(op_enum_get_value);
                        emitRegs(valReg, subjReg);
                        emitPtr(pat->enumCaseDataType);
                    }

                    if (tp->elements.size() == 1) {
                        genPatternMatch(tp->elements[0].get(), valReg, pat->enumCaseDataType, failJumps, isMutable);
                    } else {
                        auto* ttype = dynamic_cast<TupleType*>(pat->enumCaseDataType);
                        if (ttype) {
                            for (size_t i = 0; i < tp->elements.size() && i < ttype->fields_.size(); ++i) {
                                Type* ft = ttype->fields_[i];
                                u16 elemReg = emitFieldGet(ttype, valReg, (u16)i, ft);
                                genPatternMatch(tp->elements[i].get(), elemReg, ft, failJumps, isMutable);
                            }
                        }
                    }
                }
                break;
            }

            // Tuple struct pattern: Name(pat, pat, ...)
            if (!tp->structName.empty()) {
                auto* stype = dynamic_cast<StructType*>(subjType);
                if (!stype) {
                    error(pat->loc, "Tuple struct pattern on non-struct type");
                    break;
                }
                // Phase 1: UnwrappedTupleStruct -- subject IS the inner value;
                // bind the single sub-pattern directly to it.
                if (stype->repr_ == ts::Type::Repr::UnwrappedTupleStruct
                    && stype->fields_.size() == 1 && tp->elements.size() == 1) {
                    genPatternMatch(tp->elements[0].get(), subjReg, stype->fields_[0].type, failJumps, isMutable);
                    break;
                }
                for (size_t i = 0; i < tp->elements.size() && i < stype->fields_.size(); ++i) {
                    u16 fieldReg = emitFieldGet(stype, subjReg, (u16)i, stype->fields_[i].type);
                    genPatternMatch(tp->elements[i].get(), fieldReg, stype->fields_[i].type, failJumps, isMutable);
                }
                if (tp->hasRest && !tp->restName.empty()) {
                    Vec<Type*> restFields(rt::STLAllocator<Type*>(nullptr));
                    for (size_t i = tp->elements.size(); i < stype->fields_.size(); ++i) {
                        restFields.push_back(stype->fields_[i].type);
                    }
                    TupleType* restType = compiler_.tupleType(restFields);
                    bool inlineRest = restType->repr_ == ts::Type::Repr::Inline;
                    u16 elemBase = nextReg_;
                    u16 cursor = elemBase;
                    for (size_t i = tp->elements.size(); i < stype->fields_.size(); ++i) {
                        Type* ft = stype->fields_[i].type;
                        u16 fieldReg = emitFieldGet(stype, subjReg, (u16)i, ft);
                        u16 fieldSlotWords = inlineRest ? (u16)typeSlotWords(ft) : 1;
                        emitArgPlacement(cursor, fieldReg, inlineRest ? ft : nullptr);
                        cursor = (u16)(cursor + fieldSlotWords);
                        if (nextReg_ < cursor) { nextReg_ = cursor; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
                    }
                    u16 restReg = inlineRest ? allocSlot(restType) : allocReg();
                    emitOp(op_make_tuple);
                    emitRegs(restReg, elemBase, (u16)(stype->fields_.size() - tp->elements.size()));
                    emitPtr(restType);
                    declareLocal(tp->restName, restReg, restType, isMutable);
                    emitGlobalStoreIfNeeded(tp->restName, restReg);
                }
                break;
            }

            auto* ttype = dynamic_cast<TupleType*>(subjType);
            if (!ttype) {
                error(pat->loc, "Tuple pattern on non-tuple type");
                break;
            }

            for (size_t i = 0; i < tp->elements.size() && i < ttype->fields_.size(); ++i) {
                Type* ft = ttype->fields_[i];
                u16 elemReg = emitFieldGet(ttype, subjReg, (u16)i, ft);
                genPatternMatch(tp->elements[i].get(), elemReg, ft, failJumps, isMutable);
            }

            // Rest binding: create sub-tuple from remaining fields
            if (tp->hasRest && !tp->restName.empty()) {
                Vec<Type*> restFields(rt::STLAllocator<Type*>(nullptr));
                for (size_t i = tp->elements.size(); i < ttype->fields_.size(); ++i) {
                    restFields.push_back(ttype->fields_[i]);
                }
                TupleType* restType = compiler_.tupleType(restFields);
                // Phase 4g.2: build the rest tuple from individual field reads
                // so we transparently handle Inline parents and Inline results.
                bool inlineRest = restType->repr_ == ts::Type::Repr::Inline;
                u16 elemBase = nextReg_;
                u16 cursor = elemBase;
                for (size_t i = tp->elements.size(); i < ttype->fields_.size(); ++i) {
                    Type* ft = ttype->fields_[i];
                    u16 fieldReg = emitFieldGet(ttype, subjReg, (u16)i, ft);
                    u16 fieldSlotWords = inlineRest ? (u16)typeSlotWords(ft) : 1;
                    emitArgPlacement(cursor, fieldReg, inlineRest ? ft : nullptr);
                    cursor = (u16)(cursor + fieldSlotWords);
                    if (nextReg_ < cursor) { nextReg_ = cursor; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
                }
                u16 restReg = inlineRest ? allocSlot(restType) : allocReg();
                emitOp(op_make_tuple);
                emitRegs(restReg, elemBase, (u16)(ttype->fields_.size() - tp->elements.size()));
                emitPtr(restType);
                declareLocal(tp->restName, restReg, restType, isMutable);
                emitGlobalStoreIfNeeded(tp->restName, restReg);
            }
            break;
        }

        case Pattern::ArrayPat: {
            auto* ap = static_cast<ArrayPattern*>(pat);
            auto* atype = dynamic_cast<ArrayType*>(subjType);
            if (!atype) {
                error(pat->loc, "Array pattern on non-array type");
                break;
            }

            // If there are fixed elements and this is in switch (failJumps may be used),
            // check that array has enough elements
            if (!ap->elements.empty()) {
                u16 lenReg = allocReg();
                emitOp(opArrayLengthFor(atype->elemType_));
                emitRegs(lenReg, subjReg);
                emitPtr(atype);

                u16 minLenReg = allocReg();
                emitOp(op_load_int_const);
                emitRegs(minLenReg);
                emitInt((i64)ap->elements.size());

                u16 cmpReg = allocReg();
                if (ap->hasRest) {
                    // Need at least ap->elements.size() elements
                    emitOp(op_cmp_ge_int);
                } else {
                    // Need exactly ap->elements.size() elements
                    emitOp(op_cmp_eq_int);
                }
                emitRegs(cmpReg, lenReg, minLenReg);

                u32 failJump = emitJump(op_jump_if_false, cmpReg);
                failJumps.push_back(failJump);
            }

            // Extract each fixed element
            Type* elemType = atype->elemType_;
            for (size_t i = 0; i < ap->elements.size(); ++i) {
                u16 elemReg = allocReg();
                emitOp(op_array_get);
                emitRegs(elemReg, subjReg, (u16)i);
                emitPtr(atype);
                genPatternMatch(ap->elements[i].get(), elemReg, elemType, failJumps, isMutable);
            }

            // Handle rest binding
            if (ap->hasRest && !ap->restName.empty()) {
                u16 restReg = allocReg();
                emitOp(op_array_slice);
                emitRegs(restReg, subjReg, (u16)ap->elements.size());
                emitPtr(atype);
                declareLocal(ap->restName, restReg, atype, isMutable);
                emitGlobalStoreIfNeeded(ap->restName, restReg);
            }
            break;
        }

        case Pattern::GuardedPat: {
            auto* gp = static_cast<GuardedPattern*>(pat);
            // First test the inner pattern
            genPatternMatch(gp->pattern.get(), subjReg, subjType, failJumps, isMutable);
            // Then evaluate the guard expression
            u16 guardReg = genExpr(static_cast<Expr*>(gp->guard.get()));
            u32 guardFail = emitJump(op_jump_if_false, guardReg);
            failJumps.push_back(guardFail);
            break;
        }

        case Pattern::ConsPat: {
            auto* cp = static_cast<ConsPattern*>(pat);
            auto* ltype = dynamic_cast<ListType*>(subjType);
            if (!ltype) {
                error(pat->loc, "Cons pattern on non-list type");
                break;
            }

            // Check if list is non-nil (fail if nil)
            u16 isNilReg = allocReg();
            emitOp(op_list_is_nil);
            emitRegs(isNilReg, subjReg);
            u32 nilFail = emitJump(op_jump_if_true, isNilReg);
            failJumps.push_back(nilFail);

            // Extract head and recurse on head pattern
            u16 headReg = allocReg();
            emitOp(op_list_head);
            emitRegs(headReg, subjReg);
            genPatternMatch(cp->head.get(), headReg, ltype->elemType_, failJumps, isMutable);

            // Extract tail and recurse on tail pattern
            u16 tailReg = allocReg();
            emitOp(op_list_tail);
            emitRegs(tailReg, subjReg);
            genPatternMatch(cp->tail.get(), tailReg, subjType, failJumps, isMutable);
            break;
        }
        case Pattern::TypeTestPat: {
            auto* tp = static_cast<TypeTestPattern*>(pat);
            Type* targetType = tp->resolvedTargetType;

            // Get wrapped type pointer from AnyObj
            u16 typePtrReg = allocReg();
            emitOp(op_any_get_type_ptr);
            emitRegs(typePtrReg, subjReg);

            // Load target type pointer as int for comparison
            u16 targetReg = allocReg();
            emitOp(op_load_int_const);
            emitRegs(targetReg);
            emitInt(reinterpret_cast<i64>(targetType));

            // Compare type pointers
            u16 matchReg = allocReg();
            emitOp(op_cmp_eq_int);
            emitRegs(matchReg, typePtrReg, targetReg);

            // If no match, jump to fail
            u32 failJump = emitJump(op_jump_if_false, matchReg);
            failJumps.push_back(failJump);

            // Extract value and bind to name
            u16 valReg = allocReg();
            emitOp(op_any_get_value);
            emitRegs(valReg, subjReg);

            declareLocal(tp->bindingName, valReg, targetType, isMutable);
            emitGlobalStoreIfNeeded(tp->bindingName, valReg);
            break;
        }
    }
}

void CodeGen::genReturnStmt(ReturnStmtNode* stmt) {
    if (stmt->value) {
        inTailPosition_ = true;
        u16 resultReg = genExpr(static_cast<Expr*>(stmt->value.get()));
        inTailPosition_ = false;
        emitReturn(resultReg);
    } else {
        emitOp(op_return_void);
    }
}

void CodeGen::genAssignStmt(AssignStmtNode* stmt) {
    u32 sizeBeforeRhs = (u32)currentBlock_->code.size();
    u16 valReg = genExpr(static_cast<Expr*>(stmt->value.get()));
    Type* valType = stmt->value->resolvedType;

    // Dynamic scope variable assignment: `name = expr;
    if (stmt->isDynamic) {
        auto it = typeChecker_.dynamicVars().find(stmt->target);
        emitDynStore(valReg, it->second.dynIndex, it->second.type, DynStore);
        return;
    }

    // Check local first
    LocalVar* local = lookupLocal(stmt->target);
    if (local) {
        u32 nWords = typeSlotWords(local->type);
        if (local->isUpvar) {
            // Store through the UpVar's location_ pointer. Cannot use the
            // arith->mov fuse path here -- the producer wrote into a real
            // register slot, but the target slot is whatever location_
            // points at, which the codegen has no static handle on.
            u16 mask = computeWordGCMask(local->type);
            emitOp(op_store_upvar_n);
            emitRegs(local->reg, valReg, (u16)nWords);
            emitInt((i64)mask);
            return;
        }
        // Fuse: if the RHS's last emission wrote into a fresh temp `valReg`,
        // patch its destination to be `local->reg` directly and drop the MOV.
        // Falls back to MOV when the RHS produced a borrowed reg (identifier)
        // or its producer isn't safely redirectable.
        if (local->reg != valReg
            && tryFuseRedirect(valReg, local->reg, nWords, sizeBeforeRhs)) {
            // Producer now writes into local->reg directly; no MOV needed.
        } else if (local->reg != valReg) {
            emitMoveN(local->reg, valReg, nWords);
        }
        // Reassignment invalidates any const tracking on the target. The const
        // folder's view is built from the initializer's RHS and is only valid
        // until the next write.
        clearConst(local->reg);
        return;
    }

    // Check global
    auto it = typeChecker_.globalVars().find(stmt->target);
    if (it != typeChecker_.globalVars().end()) {
        emitGlobalStore(valReg, it->second.globalIndex, it->second.type, /*init=*/false);
        return;
    }

    (void)valType;  // unused for non-dyn/local/global paths
    error(stmt->loc, "Codegen: undeclared variable '" + stmt->target + "'");
}

void CodeGen::genIndexAssignStmt(IndexAssignStmtNode* stmt) {
    // Evaluate object first, then index, then value.
    u16 objReg = genExpr(static_cast<Expr*>(stmt->object.get()));
    u16 idxReg = genExpr(static_cast<Expr*>(stmt->index.get()));
    u16 valReg = genExpr(static_cast<Expr*>(stmt->value.get()));

    if (auto* mt = dynamic_cast<MapType*>(stmt->containerType)) {
        idxReg = ensureType(idxReg, stmt->index->resolvedType, mt->keyType_);
        valReg = ensureType(valReg, stmt->value->resolvedType, mt->valueType_);
        emitOp(op_map_set);
        emitRegs(objReg, idxReg, valReg);
        emitPtr(mt);
        return;
    }

    auto* at = dynamic_cast<ArrayType*>(stmt->containerType);
    if (!at) {
        error(stmt->loc, "Codegen: indexed assignment target is not Array or Map");
        return;
    }
    // Reuse the existing op_array_set_* family used by literal/auto-mapped
    // construction. They already use cyclicIndex and apply SATB barriers
    // through ObjArray::set on Obj-backed arrays.
    valReg = ensureType(valReg, stmt->value->resolvedType, at->elemType_);
    emitOp(opArraySetFor(at->elemType_));
    emitRegs(objReg, idxReg, valReg);
    emitPtr(at);
}

void CodeGen::genExprStmt(ExprStmtNode* stmt) {
    if (enableRegReclaim && !stmt->isTrailing) {
        u16 savedReg = nextReg_;
        genExpr(static_cast<Expr*>(stmt->expr.get()));
        freeRegsTo(savedReg);
    } else {
        genExpr(static_cast<Expr*>(stmt->expr.get()));
    }
}

// --- Generate expressions ---

u16 CodeGen::genExpr(Expr* expr) {
    u16 reg = genExprDispatch(expr);
    // Phase 5.2: every value computed by codegen has a static type. Recording
    // it against its destination register lets stack-map emitters (function
    // entry, backward jumps, call-site returns) find every live Obj* slot
    // without separately walking localScopes_ or tracking an in-flight-temp
    // stack. The marker skips non-Obj and inline-multiword types itself.
    if (expr && expr->resolvedType) setRegType(reg, expr->resolvedType);
    return reg;
}

u16 CodeGen::genExprDispatch(Expr* expr) {
    // Only CallExpr can consume the tail position flag; clear for all others
    if (expr->kind != ASTNode::CallExpr) {
        inTailPosition_ = false;
    }
    switch (expr->kind) {
        case ASTNode::IntLiteral:      return genIntLiteral(static_cast<IntLiteralExpr*>(expr));
        case ASTNode::FloatLiteral:    return genFloatLiteral(static_cast<FloatLiteralExpr*>(expr));
        case ASTNode::ImaginaryLiteral:return genImaginaryLiteral(static_cast<ImaginaryLiteralExpr*>(expr));
        case ASTNode::FractionLiteral:return genFractionLiteral(static_cast<FractionLiteralExpr*>(expr));
        case ASTNode::BoolLiteral:     return genBoolLiteral(static_cast<BoolLiteralExpr*>(expr));
        case ASTNode::StringLiteral:   return genStringLiteral(static_cast<StringLiteralExpr*>(expr));
        case ASTNode::SymbolLiteral:   return genSymbolLiteral(static_cast<SymbolLiteralExpr*>(expr));
        case ASTNode::Identifier:      return genIdentifier(static_cast<IdentifierExpr*>(expr));
        case ASTNode::DynamicVar: {
            auto* dv = static_cast<DynamicVarExpr*>(expr);
            auto it = typeChecker_.dynamicVars().find(dv->name);
            return emitDynLoad(it->second.dynIndex, it->second.type);
        }
        case ASTNode::BinaryOp:        return genBinaryOp(static_cast<BinaryOpExpr*>(expr));
        case ASTNode::UnaryOp:         return genUnaryOp(static_cast<UnaryOpExpr*>(expr));
        case ASTNode::CallExpr:        return genCall(static_cast<CallExpr_*>(expr));
        case ASTNode::ArrayLiteral:    return genArrayLiteral(static_cast<ArrayLiteralExpr*>(expr));
        case ASTNode::TupleLiteral:    return genTupleLiteral(static_cast<TupleLiteralExpr*>(expr));
        case ASTNode::StructLiteral:   return genStructLiteral(static_cast<StructLiteralExpr*>(expr));
        case ASTNode::IndexExpr:       return genIndexExpr(static_cast<IndexExpr_*>(expr));
        case ASTNode::FieldExpr:       return genFieldExpr(static_cast<FieldExpr_*>(expr));
        case ASTNode::EnumConstructor: return genEnumConstruct(expr);
        case ASTNode::LambdaExpr:      return genLambdaExpr(static_cast<LambdaExprNode*>(expr));
        case ASTNode::IfExpr:          return genIfExpr(static_cast<IfExprNode*>(expr));
        case ASTNode::BlockExpr: {
            auto* be = static_cast<BlockExprNode*>(expr);
            // Phase 4g.25: size resultReg to the block's result type.
            u16 resultReg = allocSlot(be->resolvedType);
            genBlockForValue(static_cast<BlockStmt*>(be->body.get()), resultReg);
            return resultReg;
        }
        case ASTNode::NilLiteral:      return genNilLiteral();
        case ASTNode::ListLiteral:     return genListLiteral(static_cast<ListLiteralExpr*>(expr));
        case ASTNode::MapLiteral:      return genMapLiteral(static_cast<MapLiteralExpr*>(expr));
        case ASTNode::SetLiteral:      return genSetLiteral(static_cast<SetLiteralExpr*>(expr));
        case ASTNode::RangeExpr:    return genRangeExpr(static_cast<RangeExprNode*>(expr));
        case ASTNode::AsTypeExpr:   return genAsTypeExpr(static_cast<AsTypeExprNode*>(expr));
        case ASTNode::AutoMap: {
            // Auto-map wrapper: generate the inner expression
            auto* am = static_cast<AutoMapExpr*>(expr);
            return genExpr(static_cast<Expr*>(am->inner.get()));
        }
        default:
            error(expr->loc, "Codegen: unsupported expression type");
            return allocReg();
    }
}

u16 CodeGen::genIntLiteral(IntLiteralExpr* expr) {
    u16 dst = allocReg();
    emitOp(op_load_int_const);
    emitRegs(dst);
    emitInt(expr->value);
    markConstInt(dst, expr->value);
    return dst;
}

u16 CodeGen::genFloatLiteral(FloatLiteralExpr* expr) {
    u16 dst = allocReg();
    emitOp(op_load_float_const);
    emitRegs(dst);
    emitFloat(expr->value);
    markConstFloat(dst, expr->value);
    return dst;
}

u16 CodeGen::genBoolLiteral(BoolLiteralExpr* expr) {
    u16 dst = allocReg();
    emitOp(expr->value ? op_load_bool_true : op_load_bool_false);
    emitRegs(dst);
    markConstBool(dst, expr->value);
    return dst;
}

u16 CodeGen::genStringLiteral(StringLiteralExpr* expr) {
    // Allocate a StringObj and store it in the CodeBlock's objConstants
    auto* strObj = new StringObj(expr->value);
    u16 constIdx = currentBlock_->addObjConstant(strObj);

    u16 dst = allocReg();
    emitOp(op_load_obj);
    emitRegs(dst, constIdx);
    return dst;
}

u16 CodeGen::genSymbolLiteral(SymbolLiteralExpr* expr) {
    SymbolPtr sym = compiler_.intern(expr->value);
    u16 dst = allocReg();
    emitOp(op_load_int_const);
    emitRegs(dst);
    emitInt(reinterpret_cast<i64>(sym));
    return dst;
}

u16 CodeGen::genImaginaryLiteral(ImaginaryLiteralExpr* expr) {
    // Phase 4f: Complex is inline 2 words [real=0.0, imag=value].
    u16 dst = allocSlot(compiler_.complexType());
    emitOp(op_load_float_const);
    emitRegs(dst);
    emitFloat(0.0);
    emitOp(op_load_float_const);
    emitRegs((u16)(dst + 1));
    emitFloat(expr->value);
    return dst;
}

u16 CodeGen::genFractionLiteral(FractionLiteralExpr* expr) {
    // Phase 4f: Fraction is inline 2 words [numer, denom].
    // Reduce at compile time via r64 constructor for canonical form.
    r64 r(expr->numerator, expr->denominator);
    u16 dst = allocSlot(compiler_.fractionType());
    emitOp(op_load_int_const);
    emitRegs(dst);
    emitInt(r.numer());
    emitOp(op_load_int_const);
    emitRegs((u16)(dst + 1));
    emitInt(r.denom());
    return dst;
}

u16 CodeGen::genNilLiteral() {
    u16 dst = allocReg();
    emitOp(op_load_nil);
    emitRegs(dst);
    return dst;
}

u16 CodeGen::genListLiteral(ListLiteralExpr* expr) {
    auto* listType = dynamic_cast<ListType*>(expr->resolvedType);
    Type* elemType = listType->elemType_;
    // Phase 4g.20: ListNode stores all Inline composite heads natively,
    // including Complex/Fraction (2-word x64/r64) -- no more 1-word boxed
    // boundary for those.
    bool elemInline = elemType
        && elemType->repr_ == ts::Type::Repr::Inline;
    u32 stride = elemInline ? typeSlotWords(elemType) : 1;
    u16 firstSrc = nextReg_;
    for (size_t i = 0; i < expr->elements.size(); ++i) {
        auto& elem = expr->elements[i];
        u16 elemReg = genExpr(static_cast<Expr*>(elem.get()));
        u16 promoted = ensureType(elemReg, elem->resolvedType, elemType);
        u16 expectedReg = (u16)(firstSrc + (u16)i * stride);
        if (elemInline) {
            emitArgPlacement(expectedReg, promoted, elemType);
        } else if (promoted != expectedReg) {
            emitMov(expectedReg, promoted);
        }
    }
    u16 count = (u16)expr->elements.size();
    u16 endReg = (u16)(firstSrc + count * stride);
    if (nextReg_ < endReg) {
        nextReg_ = endReg;
        if (nextReg_ > maxReg_) maxReg_ = nextReg_;
    }
    u16 dst = allocReg();
    emitOp(op_make_list);
    emitRegs(dst, firstSrc, count);
    emitPtr(listType);
    return dst;
}

u16 CodeGen::genMapLiteral(MapLiteralExpr* expr) {
    auto* mapType = dynamic_cast<MapType*>(expr->resolvedType);
    if (!mapType) {
        error(expr->loc, "Map literal has non-map resolved type");
        return allocReg();
    }
    Type* keyType = mapType->keyType_;
    Type* valType = mapType->valueType_;
    usize numPairs = expr->entries.size();

    // Phase 4g.11: MapObj stores keys/values natively. Each pair occupies
    // (keyStride + valueStride) consecutive registers; multi-word inline
    // composites are written directly without boxing.
    u32 keyStride = typeSlotWords(keyType);
    u32 valStride = typeSlotWords(valType);
    u32 pairStride = keyStride + valStride;
    u16 kvBase = nextReg_;
    for (size_t i = 0; i < numPairs; ++i) {
        u16 keyReg = genExpr(static_cast<Expr*>(expr->entries[i].key.get()));
        keyReg = ensureType(keyReg, expr->entries[i].key->resolvedType, keyType);
        u16 keyPos = (u16)(kvBase + i * pairStride);
        emitArgPlacement(keyPos, keyReg, keyType);

        u16 valReg = genExpr(static_cast<Expr*>(expr->entries[i].value.get()));
        valReg = ensureType(valReg, expr->entries[i].value->resolvedType, valType);
        u16 valPos = (u16)(keyPos + keyStride);
        emitArgPlacement(valPos, valReg, valType);
    }

    u16 dst = allocReg();
    emitOp(op_make_map);
    emitRegs(dst, kvBase, (u16)numPairs);
    emitPtr(mapType);
    return dst;
}

u16 CodeGen::genSetLiteral(SetLiteralExpr* expr) {
    auto* setType = dynamic_cast<SetType*>(expr->resolvedType);
    if (!setType) {
        error(expr->loc, "Set literal has non-set resolved type");
        return allocReg();
    }
    Type* elemType = setType->elemType_;
    usize count = expr->elements.size();

    // Phase 4g.11: SetObj stores elements natively. Each element occupies
    // elemStride consecutive registers; multi-word inline composites are
    // written directly without boxing.
    u32 elemStride = typeSlotWords(elemType);
    u16 elemBase = nextReg_;
    for (size_t i = 0; i < count; ++i) {
        u16 elemReg = genExpr(static_cast<Expr*>(expr->elements[i].get()));
        elemReg = ensureType(elemReg, expr->elements[i]->resolvedType, elemType);
        u16 pos = (u16)(elemBase + i * elemStride);
        emitArgPlacement(pos, elemReg, elemType);
    }

    u16 dst = allocReg();
    emitOp(op_make_set);
    emitRegs(dst, elemBase, (u16)count);
    emitPtr(setType);
    return dst;
}

u16 CodeGen::genRangeExpr(RangeExprNode* expr) {
    auto* rangeType = dynamic_cast<RangeType*>(expr->resolvedType);
    Type* elemType = rangeType->elemType_;
    bool isInt = (elemType == compiler_.intType());

    // Generate start and promote to elemType if needed
    u16 startReg = genExpr(static_cast<Expr*>(expr->start.get()));
    startReg = ensureType(startReg, expr->start->resolvedType, elemType);

    // Generate step: if next is present, step = next - start; else step = 1
    u16 stepReg;
    if (expr->next) {
        u16 nextReg = genExpr(static_cast<Expr*>(expr->next.get()));
        nextReg = ensureType(nextReg, expr->next->resolvedType, elemType);
        // Phase 4f: step slot must be sized for elemType (2 words for Fraction).
        stepReg = allocSlot(elemType);
        emitOp(isInt ? op_sub_int : op_sub_fraction);
        emitRegs(stepReg, nextReg, startReg);
    } else if (!expr->isInfinite) {
        // Defer step computation until after end is generated (need endReg)
        stepReg = 0; // placeholder
    } else {
        // Infinite range with no next: default step = 1.
        stepReg = allocSlot(elemType);
        if (isInt) {
            emitOp(op_load_int_const);
            emitRegs(stepReg);
            emitInt(1);
        } else {
            // Phase 4f: Fraction inline = (numer=1, denom=1) in 2 words.
            emitOp(op_load_int_const);
            emitRegs(stepReg);
            emitInt(1);
            emitOp(op_load_int_const);
            emitRegs((u16)(stepReg + 1));
            emitInt(1);
        }
    }

    // Generate end (or use a dummy reg for infinite ranges)
    u16 endReg;
    if (!expr->isInfinite) {
        endReg = genExpr(static_cast<Expr*>(expr->end.get()));
        endReg = ensureType(endReg, expr->end->resolvedType, elemType);
    } else {
        endReg = allocSlot(elemType);
        if (isInt) {
            emitOp(op_load_int_const);
            emitRegs(endReg);
            emitInt(0);
        } else {
            // Phase 4f: Fraction inline = (numer=0, denom=1) in 2 words.
            emitOp(op_load_int_const);
            emitRegs(endReg);
            emitInt(0);
            emitOp(op_load_int_const);
            emitRegs((u16)(endReg + 1));
            emitInt(1);
        }
    }

    // Deferred step inference: if no next and finite, compute step from direction
    if (!expr->next && !expr->isInfinite) {
        // step = (start <= end) ? 1 : -1
        // cmp = (start <= end) → 0 or 1 (always Int even for Fraction ranges)
        // step = cmp * 2 - 1  → -1 or 1
        u16 cmpReg = allocReg();
        emitOp(isInt ? op_cmp_le_int : op_cmp_le_fraction);
        emitRegs(cmpReg, startReg, endReg);

        u16 twoReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(twoReg);
        emitInt(2);

        u16 t = allocReg();
        emitOp(op_mul_int);
        emitRegs(t, cmpReg, twoReg);

        u16 oneReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(oneReg);
        emitInt(1);

        stepReg = allocReg();
        emitOp(op_sub_int);
        emitRegs(stepReg, t, oneReg);

        // For Fraction ranges, convert the Int step to Fraction (2-word slot).
        if (!isInt) {
            u16 fracStepReg = allocSlot(compiler_.fractionType());
            emitOp(op_int_to_fraction);
            emitRegs(fracStepReg, stepReg);
            stepReg = fracStepReg;
        }
    }

    // Phase 4g.14: RangeObj stores endpoints natively per the element type's
    // footprint (1 word for Int, 2 for Fraction). No boundary box needed.
    u16 dst = allocReg();
    emitOp(op_make_range);
    emitRegs(dst, startReg, endReg, stepReg);
    emitPtr(rangeType);
    i64 flags = (expr->isInfinite ? 1 : 0);
    emitInt(flags);
    return dst;
}

u16 CodeGen::genAsTypeExpr(AsTypeExprNode* expr) {
    u16 subjReg = genExpr(static_cast<Expr*>(expr->subject.get()));

    Type* subjType = expr->subject->resolvedType;
    Type* targetType = expr->resolvedTargetType;

    // Numeric downcast (possibly auto-mapped)
    if (!dynamic_cast<AnyType*>(subjType)) {
        if (expr->autoMapDepth > 0) {
            return emitAutoMapDowncast(subjReg, expr);
        }
        return emitNumericDowncast(subjReg, subjType, targetType);
    }

    // Any -> concrete type: fallible, returns Option
    auto* optType = static_cast<EnumType*>(expr->resolvedType);

    // Get wrapped type pointer from AnyObj
    u16 typePtrReg = allocReg();
    emitOp(op_any_get_type_ptr);
    emitRegs(typePtrReg, subjReg);

    // Load target type pointer as int for comparison
    u16 targetReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(targetReg);
    emitInt(reinterpret_cast<i64>(targetType));

    // Compare type pointers
    u16 matchReg = allocReg();
    emitOp(op_cmp_eq_int);
    emitRegs(matchReg, typePtrReg, targetReg);

    // Branch: if no match -> none
    u32 noneJump = emitJump(op_jump_if_false, matchReg);

    // Match: extract value, wrap in Option.some
    u16 valReg = allocReg();
    emitOp(op_any_get_value);
    emitRegs(valReg, subjReg);

    // Phase 4g.15: AnyObj stores its value as a single Word (boxed for Inline
    // composites). op_make_enum / op_make_inline_enum now expect a native
    // multi-word source slot for Inline payloads, so unbox into the right
    // sized slot here.
    if (targetType && targetType->repr_ == ts::Type::Repr::Inline
        && optType->repr_ != ts::Type::Repr::NullablePtrEnum) {
        valReg = emitUnboxIfInline(valReg, targetType);
    }

    u16 dstReg = allocSlot(optType);
    if (optType->repr_ == ts::Type::Repr::NullablePtrEnum) {
        // Phase 3: Some(p) is just the pointer.
        emitMov(dstReg, valReg);
    } else if (optType->repr_ == ts::Type::Repr::Inline) {
        // Phase 4g.4: inline enum -- write tag + payload directly into dst.
        emitOp(op_make_inline_enum);
        emitRegs(dstReg, valReg, 0);  // case 0 = some
        emitPtr(optType);
    } else {
        emitOp(op_make_enum);
        emitRegs(dstReg, valReg, 0);  // case 0 = some
        emitPtr(optType);
    }

    u32 endJump = emitJump(op_jump);

    // None branch
    patchJump(noneJump);
    if (optType->repr_ == ts::Type::Repr::NullablePtrEnum) {
        emitOp(op_load_nil);
        emitRegs(dstReg);
    } else if (optType->repr_ == ts::Type::Repr::Inline) {
        emitOp(op_make_inline_enum_nodata);
        emitRegs(dstReg, 1);  // case 1 = none
        emitPtr(optType);
    } else {
        emitOp(op_make_enum_nodata);
        emitRegs(dstReg, 1);  // case 1 = none
        emitPtr(optType);
    }

    patchJump(endJump);
    return dstReg;
}

u16 CodeGen::emitNumericDowncast(u16 reg, Type* fromType, Type* toType) {
    // Phase 4f: dst slot must be sized for toType (Fraction/Complex = 2 words).
    u16 dst = allocSlot(toType);

    if (fromType == compiler_.complexType()) {
        if (toType == compiler_.floatType()) {
            emitOp(op_complex_to_float);
            emitRegs(dst, reg);
        } else if (toType == compiler_.fractionType()) {
            emitOp(op_complex_to_fraction);
            emitRegs(dst, reg);
        } else {
            // Complex -> Int
            emitOp(op_complex_to_int);
            emitRegs(dst, reg);
        }
    } else if (fromType == compiler_.floatType()) {
        if (toType == compiler_.intType()) {
            emitOp(op_float_to_int);
            emitRegs(dst, reg);
        } else {
            // Float -> Fraction: go through Int (truncation)
            u16 intReg = allocReg();
            emitOp(op_float_to_int);
            emitRegs(intReg, reg);
            emitOp(op_int_to_fraction);
            emitRegs(dst, intReg);
        }
    } else if (fromType == compiler_.fractionType()) {
        // Fraction -> Int
        emitOp(op_fraction_to_int);
        emitRegs(dst, reg);
    } else {
        return reg;
    }
    return dst;
}

u16 CodeGen::emitAutoMapDowncast(u16 subjReg, AsTypeExprNode* expr) {
    int depth = expr->autoMapDepth;
    Type* targetType = expr->resolvedTargetType;

    // Find the element type at the innermost level
    Type* elemType = expr->subject->resolvedType;
    for (int i = 0; i < depth; ++i) {
        if (auto* arrT = dynamic_cast<ArrayType*>(elemType))
            elemType = arrT->elemType_;
        else if (auto* lstT = dynamic_cast<ListType*>(elemType))
            elemType = lstT->elemType_;
    }

    // Build result type from inside out: targetType, then wrap in Array layers
    // e.g. for depth=2, [[Float]] as(Int) -> [[Int]]
    std::vector<Type*> resultTypes(depth + 1);
    resultTypes[0] = targetType;
    for (int i = 1; i <= depth; ++i) {
        resultTypes[i] = compiler_.arrayType(resultTypes[i - 1]);
    }

    // Build source array types from inside out for op_array_length/get_dyn
    Type* srcType = expr->subject->resolvedType;
    std::vector<Type*> srcArrayTypes(depth);
    for (int i = depth - 1; i >= 0; --i) {
        srcArrayTypes[i] = srcType;
        if (auto* arrT = dynamic_cast<ArrayType*>(srcType))
            srcType = arrT->elemType_;
        else if (auto* lstT = dynamic_cast<ListType*>(srcType))
            srcType = lstT->elemType_;
    }

    // For depth=1 (the common case), generate a simple loop
    // For deeper nesting, generate nested loops recursively
    return emitAutoMapDowncastLoop(subjReg, srcArrayTypes, resultTypes, 0, depth,
                                   elemType, targetType);
}

u16 CodeGen::emitAutoMapDowncastLoop(u16 arrReg, std::vector<Type*>& srcArrayTypes,
                                      std::vector<Type*>& resultTypes,
                                      int level, int depth,
                                      Type* elemType, Type* targetType) {
    auto* srcArrType = dynamic_cast<ArrayType*>(srcArrayTypes[level]);

    // Get length
    u16 lenReg = allocReg();
    emitOp(opArrayLengthFor(srcArrType->elemType_));
    emitRegs(lenReg, arrReg);
    emitPtr(srcArrType);

    // Allocate result array
    auto* resultArrType = dynamic_cast<ArrayType*>(resultTypes[depth - level]);
    u16 resultArrReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(resultArrReg, lenReg);
    emitPtr(resultArrType);

    // Loop counter
    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    u16 condReg = allocReg();

    // Loop start
    u32 loopStart = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, lenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // Extract element. Phase 4e: inline-element arrays land 2-word values
    // straight into a 2-word slot; no per-element box/unbox.
    u16 elemReg = allocSlot(srcArrType->elemType_);
    emitOp(opArrayGetDynFor(srcArrType->elemType_));
    emitRegs(elemReg, arrReg, iReg);
    emitPtr(srcArrType);

    // Either recurse for deeper levels or apply the downcast
    u16 convertedReg;
    if (level + 1 < depth) {
        convertedReg = emitAutoMapDowncastLoop(elemReg, srcArrayTypes, resultTypes,
                                                level + 1, depth, elemType, targetType);
    } else {
        convertedReg = emitNumericDowncast(elemReg, elemType, targetType);
    }

    // Store in result array
    emitOp(opArraySetFor(resultArrType->elemType_));
    emitRegs(resultArrReg, iReg, convertedReg);
    emitPtr(resultArrType);

    // Increment and loop
    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);
    emitJumpTo(loopStart);

    patchJump(exitJump);
    return resultArrReg;
}

u16 CodeGen::genIdentifier(IdentifierExpr* expr) {
    // Check template lambda specialization first (e.g., passing template lambda to higher-order fn)
    // Must come before local/global checks which would return the unspecialized Lambda.
    if (expr->templateLambdaSpecType) {
        auto* concreteLT = expr->templateLambdaSpecType;
        // Find the template lambda's register
        u16 srcReg;
        LocalVar* tmplLocal = lookupLocal(expr->name);
        if (tmplLocal) {
            srcReg = tmplLocal->reg;
            // Compile the body if needed
            auto* tmplType = dynamic_cast<TemplateLambdaType*>(tmplLocal->type);
            if (tmplType && tmplType->astNode_) {
                compileTemplateLambdaBody(tmplType->astNode_, concreteLT);
            }
        } else {
            auto gvIt = typeChecker_.globalVars().find(expr->name);
            if (gvIt != typeChecker_.globalVars().end()) {
                srcReg = allocReg();
                emitOp(op_load_global);
                emitRegs(srcReg);
                emitInt(gvIt->second.globalIndex);
                auto* tmplType = dynamic_cast<TemplateLambdaType*>(gvIt->second.type);
                if (tmplType && tmplType->astNode_) {
                    compileTemplateLambdaBody(tmplType->astNode_, concreteLT);
                }
            } else {
                error(expr->loc, "Cannot find template lambda '" + expr->name + "'");
                return allocReg();
            }
        }
        // Emit op_specialize_lambda
        u16 dst = allocReg();
        emitOp(op_specialize_lambda);
        emitRegs(dst, srcReg);
        emitPtr(concreteLT);
        return dst;
    }

    // Check local
    LocalVar* local = lookupLocal(expr->name);
    if (local) {
        if (local->isUpvar) {
            // The register holds an UpVar*. Emit op_load_upvar_n to copy
            // sizeWords words from *upvar->location_ into a fresh slot
            // sized to the captured value's type.
            u16 sw = (u16)typeSlotWords(local->type);
            u16 dst = allocSlot(local->type);
            emitOp(op_load_upvar_n);
            emitRegs(dst, local->reg, sw);
            return dst;
        }
        return local->reg;
    }

    // Check global variable
    auto it = typeChecker_.globalVars().find(expr->name);
    if (it != typeChecker_.globalVars().end()) {
        return emitGlobalLoad(it->second.globalIndex, it->second.type);
    }

    // Check function reference (set by type checker for function-as-value)
    if (expr->resolvedFuncGlobalIndex >= 0 && expr->funcRefLambdaType) {
        // Set codeBlock_ on the LambdaType so Lambda constructor can read it
        expr->funcRefLambdaType->codeBlock_ = static_cast<CodeBlock*>(compiler_.global(expr->resolvedFuncGlobalIndex).p);
        u16 dst = allocReg();
        emitOp(op_func_ref);
        emitRegs(dst);
        emitPtr(expr->funcRefLambdaType);
        return dst;
    }

    error(expr->loc, "Codegen: undeclared identifier '" + expr->name + "'");
    return allocReg();
}

u16 CodeGen::genBinaryOp(BinaryOpExpr* expr) {
    // AST-level constant folding
    if (canFoldBinaryOp(expr)) {
        u16 result = foldBinaryOp(expr);
        if (result != UINT16_MAX) return result;
    }

    // Check for auto-mapped binary ops (@ on operands)
    if (expr->leftAutoMap || expr->rightAutoMap) {
        bool hasCartesian = (expr->leftAutoMap.cartesianIndex > 0) ||
                            (expr->rightAutoMap.cartesianIndex > 0);
        int maxDepth = std::max(expr->leftAutoMap.depth, expr->rightAutoMap.depth);
        if (hasCartesian)          return genCartesianBinaryOp(expr);
        else if (maxDepth > 1)     return genDeepMapBinaryOp(expr, maxDepth);
        else {
            bool anyList = expr->leftAutoMap.isList || expr->rightAutoMap.isList;
            return anyList ? genAutoMapBinaryOpList(expr) : genAutoMapBinaryOp(expr);
        }
    }

    // Check for operator overload (resolved by type checker)
    if (expr->resolvedFuncGlobalIndex >= 0) {
        // Emit as a function call to the overloaded operator
        u16 argBase = nextReg_;
        u16 leftReg = genExpr(static_cast<Expr*>(expr->left.get()));
        Type* leftT = expr->left->resolvedType;
        Type* rightT = expr->right->resolvedType;
        u32 leftWords = emitArgPlacementForCall(argBase, leftReg, leftT, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        u16 rightDst = (u16)(argBase + leftWords);
        u16 rightReg = genExpr(static_cast<Expr*>(expr->right.get()));
        u32 rightWords = emitArgPlacementForCall(rightDst, rightReg, rightT, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        u16 next = (u16)(rightDst + rightWords);
        if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        bool builtinReturnsInlineComposite = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs && expr->resolvedType
            && expr->resolvedType->repr_ == ts::Type::Repr::Inline
            && expr->resolvedType != compiler_.complexType()
            && expr->resolvedType != compiler_.fractionType();
        u16 resultReg = builtinReturnsInlineComposite ? allocReg() : allocSlot(expr->resolvedType);
        // Phase 5.2: see the comment in genCall's main path -- arg slots
        // become callee-owned and a stack map at returnPC roots the rest.
        clearArgRegTypes(argBase, resultReg);
        emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
        emitRegs(resultReg, 2, argBase);
        emitInt(expr->resolvedFuncGlobalIndex);
        emitReturnPcStackMap(resultReg, expr->resolvedType);
        if (builtinReturnsInlineComposite) {
            return emitUnboxIfInline(resultReg, expr->resolvedType);
        }
        return resultReg;
    }

    // Peephole: Int binary op where one side is an IntLiteral that fits in
    // a 16-bit signed immediate. Skip emitting the literal into a register and
    // use the immediate-form opcode. Saves one dispatch per such op and shrinks
    // the bytecode. Disabled when --no-const-fold is off (the optimization is
    // semantically transparent but parallels other folder-driven rewrites).
    if (enableConstFold) {
        bool intArith = expr->resolvedType == compiler_.intType()
                     && expr->left->resolvedType == compiler_.intType()
                     && expr->right->resolvedType == compiler_.intType();
        bool intCmp   = expr->resolvedType == compiler_.boolType()
                     && expr->left->resolvedType == compiler_.intType()
                     && expr->right->resolvedType == compiler_.intType();
        if (intArith || intCmp) {
            auto fitsImm16 = [](i64 v) { return v >= -32768 && v <= 32767; };
            ASTNode* L = expr->left.get();
            ASTNode* R = expr->right.get();
            bool leftIsLit  = L->kind == ASTNode::IntLiteral &&
                              fitsImm16(static_cast<IntLiteralExpr*>(L)->value);
            bool rightIsLit = R->kind == ASTNode::IntLiteral &&
                              fitsImm16(static_cast<IntLiteralExpr*>(R)->value);

            // Pick which side holds the literal and which operation form to emit.
            // For commutative ops, prefer RHS-literal but fall back to LHS.
            // For ordering compares, flipping operands also flips the relation.
            Operation immOp = nullptr;
            i64 imm = 0;
            Expr* nonLit = nullptr;
            switch (expr->op) {
                case BinaryOpExpr::Add:
                    if (rightIsLit) { immOp = op_add_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_add_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                case BinaryOpExpr::Sub:
                    if (rightIsLit) { immOp = op_sub_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    // (lit - reg) is rare; skip for v1.
                    break;
                case BinaryOpExpr::Mul:
                    if (rightIsLit) { immOp = op_mul_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_mul_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                case BinaryOpExpr::Eq:
                    if (rightIsLit) { immOp = op_cmp_eq_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_cmp_eq_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                case BinaryOpExpr::Ne:
                    if (rightIsLit) { immOp = op_cmp_ne_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_cmp_ne_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                case BinaryOpExpr::Lt:
                    if (rightIsLit) { immOp = op_cmp_lt_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_cmp_gt_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                case BinaryOpExpr::Le:
                    if (rightIsLit) { immOp = op_cmp_le_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_cmp_ge_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                case BinaryOpExpr::Gt:
                    if (rightIsLit) { immOp = op_cmp_gt_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_cmp_lt_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                case BinaryOpExpr::Ge:
                    if (rightIsLit) { immOp = op_cmp_ge_int_imm; imm = static_cast<IntLiteralExpr*>(R)->value; nonLit = static_cast<Expr*>(L); }
                    else if (leftIsLit) { immOp = op_cmp_le_int_imm; imm = static_cast<IntLiteralExpr*>(L)->value; nonLit = static_cast<Expr*>(R); }
                    break;
                default: break;
            }
            if (immOp) {
                u16 srcReg = genExpr(nonLit);
                u16 dst = allocReg();
                emitOp(immOp);
                emitRegs(dst, srcReg, (u16)(i16)imm);
                return dst;
            }
        }
    }

    u16 leftReg = genExpr(static_cast<Expr*>(expr->left.get()));
    u16 rightReg = genExpr(static_cast<Expr*>(expr->right.get()));

    Type* leftType = expr->left->resolvedType;
    Type* rightType = expr->right->resolvedType;
    Type* resultType = expr->resolvedType;

    // String operations
    if (leftType == compiler_.stringType() && rightType == compiler_.stringType()) {
        u16 dst = allocReg();
        switch (expr->op) {
            case BinaryOpExpr::Concat:
                emitOp(op_concat_str);
                emitRegs(dst, leftReg, rightReg);
                return dst;
            case BinaryOpExpr::Eq:
            case BinaryOpExpr::Ne:
            case BinaryOpExpr::Lt:
            case BinaryOpExpr::Le:
            case BinaryOpExpr::Gt:
            case BinaryOpExpr::Ge:
                emitOp(getCmpOp(expr->op, compiler_.stringType()));
                emitRegs(dst, leftReg, rightReg);
                return dst;
            default:
                error(expr->loc, "Unsupported operator for strings");
                return dst;
        }
    }

    // Array/List/Tuple concatenation
    if (expr->op == BinaryOpExpr::Concat) {
        if (auto* arrType = dynamic_cast<ArrayType*>(resultType)) {
            u16 dst = allocReg();
            emitOp(op_concat_array);
            emitRegs(dst, leftReg, rightReg);
            emitPtr(arrType);
            return dst;
        }
        if (auto* listType = dynamic_cast<ListType*>(resultType)) {
            u16 dst = allocReg();
            emitOp(op_concat_list);
            emitRegs(dst, leftReg, rightReg);
            emitPtr(listType);
            return dst;
        }
        if (auto* tupType = dynamic_cast<TupleType*>(resultType)) {
            auto* leftTupType = dynamic_cast<TupleType*>(leftType);
            auto* rightTupType = dynamic_cast<TupleType*>(rightType);
            // Phase 4g.16: op_concat_tuple accepts Inline operands natively
            // (no boxing). Result is built directly into the multi-word dst
            // slot when resultType is Inline, or a heap Tuple* otherwise.
            u16 outReg = allocSlot(tupType);
            emitOp(op_concat_tuple);
            emitRegs(outReg, leftReg, rightReg);
            emitPtr(tupType);
            emitPtr(leftTupType);
            emitPtr(rightTupType);
            return outReg;
        }
    }

    // Phase 4f: result slot must be sized for the type (Complex/Fraction = 2 words).
    u16 dst = allocSlot(resultType);

    // Composite (Array/Tuple) arithmetic
    if (isCompositeNumeric(resultType)) {
        switch (expr->op) {
            case BinaryOpExpr::Add:
            case BinaryOpExpr::Sub:
            case BinaryOpExpr::Mul:
            case BinaryOpExpr::Div: {
                auto isInlineComposite = [&](Type* t) {
                    return t && t->repr_ == ts::Type::Repr::Inline
                        && t != compiler_.complexType()
                        && t != compiler_.fractionType();
                };
                // Phase 4g.7: emit inline variant when the result is an Inline
                // composite; operands stay in their multi-word slots (or scalar).
                if (isInlineComposite(resultType)) {
                    emitOp(getCompositeArithOpInline(expr->op));
                    emitRegs(dst, leftReg, rightReg);
                    emitPtr(resultType);
                    emitPtr(leftType);
                    emitPtr(rightType);
                    return dst;
                }
                // Phase 4g.16: heap-result composite arith now reads operand
                // fields natively via base pointers; no operand boxing.
                emitOp(getCompositeArithOp(expr->op));
                emitRegs(dst, leftReg, rightReg);
                emitPtr(resultType);
                emitPtr(leftType);
                emitPtr(rightType);
                return dst;
            }
            default:
                break;
        }
    }

    switch (expr->op) {
        case BinaryOpExpr::Add:
        case BinaryOpExpr::Sub:
        case BinaryOpExpr::Mul: {
            // Promote both operands to result type
            leftReg = ensureType(leftReg, leftType, resultType);
            rightReg = ensureType(rightReg, rightType, resultType);
            emitOp(getArithOp(expr->op, resultType));
            emitRegs(dst, leftReg, rightReg);
            break;
        }

        case BinaryOpExpr::Div: {
            // Int / Int -> Fraction: promote both to fraction first
            // Otherwise promote to common type
            leftReg = ensureType(leftReg, leftType, resultType);
            rightReg = ensureType(rightReg, rightType, resultType);
            emitOp(getArithOp(expr->op, resultType));
            emitRegs(dst, leftReg, rightReg);
            break;
        }

        case BinaryOpExpr::Mod:
            emitOp(op_mod_int);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::IntDiv:
            emitOp(op_int_div);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::Eq:
        case BinaryOpExpr::Ne:
        case BinaryOpExpr::Lt:
        case BinaryOpExpr::Le:
        case BinaryOpExpr::Gt:
        case BinaryOpExpr::Ge: {
            // Composite comparison (e.g. Tuple > Scalar)
            if (isCompositeNumeric(resultType)) {
                auto isInlineComposite = [&](Type* t) {
                    return t && t->repr_ == ts::Type::Repr::Inline
                        && t != compiler_.complexType()
                        && t != compiler_.fractionType();
                };
                // Phase 4g.7: emit inline variant when result is Inline composite.
                if (isInlineComposite(resultType)) {
                    emitOp(getCompositeCmpOpInline(expr->op));
                    emitRegs(dst, leftReg, rightReg);
                    emitPtr(resultType);
                    emitPtr(leftType);
                    emitPtr(rightType);
                    return dst;
                }
                // Phase 4g.16: heap-result composite cmp now reads operand
                // fields natively via base pointers; no operand boxing.
                emitOp(getCompositeCmpOp(expr->op));
                emitRegs(dst, leftReg, rightReg);
                emitPtr(resultType);
                emitPtr(leftType);
                emitPtr(rightType);
                return dst;
            }
            // For comparison, determine common type from operands (result is always bool)
            Type* cmpType = leftType;  // default: use operand type directly
            auto isScalarNumeric = [&](Type* t) {
                return t == compiler_.intType() || t == compiler_.floatType()
                    || t == compiler_.boolType() || t == compiler_.fractionType()
                    || t == compiler_.complexType();
            };
            if (isScalarNumeric(leftType) && isScalarNumeric(rightType)) {
                cmpType = compiler_.intType();
                if (leftType == compiler_.complexType() || rightType == compiler_.complexType())
                    cmpType = compiler_.complexType();
                else if (leftType == compiler_.floatType() || rightType == compiler_.floatType())
                    cmpType = compiler_.floatType();
                else if (leftType == compiler_.fractionType() || rightType == compiler_.fractionType())
                    cmpType = compiler_.fractionType();
                leftReg = ensureType(leftReg, leftType, cmpType);
                rightReg = ensureType(rightReg, rightType, cmpType);
            }
            // Phase 4g.16: op_cmp_eq_obj / op_cmp_ne_obj now take an operand
            // type ptr and compare via wordsEqual natively; no boxing needed
            // for Inline composite operands.
            Operation cmpOp = getCmpOp(expr->op, cmpType);
            emitOp(cmpOp);
            emitRegs(dst, leftReg, rightReg);
            if (cmpOp == op_cmp_eq_obj || cmpOp == op_cmp_ne_obj) {
                emitPtr(cmpType);
            }
            break;
        }

        case BinaryOpExpr::And:
            emitOp(op_and_bool);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::Or:
            emitOp(op_or_bool);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::BitAnd:
            emitOp(op_bitand_int);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::BitOr:
            emitOp(op_bitor_int);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::BitXor:
            emitOp(op_bitxor_int);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::ShiftL:
            emitOp(op_shl_int);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::ShiftR:
            emitOp(op_shr_int);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::UShiftR:
            emitOp(op_ushr_int);
            emitRegs(dst, leftReg, rightReg);
            break;

        case BinaryOpExpr::Cons: {
            auto* listT = dynamic_cast<ListType*>(resultType);
            Type* et = listT->elemType_;
            // Promote head to list element type if needed.
            leftReg = ensureType(leftReg, leftType, et);
            // Phase 4g.9: ListNode stores Inline composite heads natively
            // (op_cons reads stride words). Complex/Fraction stay boxed at
            // the head boundary (1-word slot); box first.
            bool inlineMW = et && et->repr_ == ts::Type::Repr::Inline
                && et != compiler_.complexType()
                && et != compiler_.fractionType();
            if (!inlineMW && et && et->repr_ == ts::Type::Repr::Inline) {
                leftReg = emitBoxIfInline(leftReg, et);
            }
            emitOp(op_cons);
            emitRegs(dst, leftReg, rightReg);
            emitPtr(listT);
            break;
        }

        case BinaryOpExpr::LeftArrow: {
            // ref <- value: set the ref's value with write barrier.
            // Phase 4g.5: inline-composite Refs hold the payload in an
            // InlineRef flex array; mutate in place via op_ref_set_inline.
            auto* refType = static_cast<RefType*>(leftType);
            rightReg = ensureType(rightReg, rightType, refType->elemType_);
            if (isInlineMultiword(refType->elemType_)) {
                u16 sz = (u16)typeSlotWords(refType->elemType_);
                u16 dstSlot = allocRegs(sz);
                emitOp(op_ref_set_inline);
                emitRegs(dstSlot, leftReg, rightReg);
                emitPtr(refType);
                return dstSlot;
            }
            emitOp(op_ref_set);
            emitRegs(dst, leftReg, rightReg);
            emitPtr(refType);
            break;
        }

        case BinaryOpExpr::RightArrow: {
            // value -> ref: set the ref's value with write barrier.
            auto* refType = static_cast<RefType*>(rightType);
            leftReg = ensureType(leftReg, leftType, refType->elemType_);
            if (isInlineMultiword(refType->elemType_)) {
                u16 sz = (u16)typeSlotWords(refType->elemType_);
                u16 dstSlot = allocRegs(sz);
                emitOp(op_ref_set_inline);
                emitRegs(dstSlot, rightReg, leftReg);
                emitPtr(refType);
                return dstSlot;
            }
            emitOp(op_ref_set);
            emitRegs(dst, rightReg, leftReg);
            emitPtr(refType);
            break;
        }

        default:
            error(expr->loc, "Codegen: unsupported binary operator");
            break;
    }

    return dst;
}

u16 CodeGen::genUnaryOp(UnaryOpExpr* expr) {
    // AST-level constant folding
    if (canFoldUnaryOp(expr)) {
        u16 result = foldUnaryOp(expr);
        if (result != UINT16_MAX) return result;
    }

    // Overloaded unary operator — emit as function call
    if (expr->resolvedFuncGlobalIndex >= 0) {
        u16 argBase = nextReg_;
        u16 operandReg = genExpr(static_cast<Expr*>(expr->operand.get()));
        Type* opT = expr->operand->resolvedType;
        u32 sw = emitArgPlacementForCall(argBase, operandReg, opT, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        u16 next = (u16)(argBase + sw);
        if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        bool builtinReturnsInlineComposite = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs && expr->resolvedType
            && expr->resolvedType->repr_ == ts::Type::Repr::Inline
            && expr->resolvedType != compiler_.complexType()
            && expr->resolvedType != compiler_.fractionType();
        u16 resultReg = builtinReturnsInlineComposite ? allocReg() : allocSlot(expr->resolvedType);
        clearArgRegTypes(argBase, resultReg);
        emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
        emitRegs(resultReg, 1, argBase);
        emitInt(expr->resolvedFuncGlobalIndex);
        emitReturnPcStackMap(resultReg, expr->resolvedType);
        if (builtinReturnsInlineComposite) {
            return emitUnboxIfInline(resultReg, expr->resolvedType);
        }
        return resultReg;
    }

    u16 operandReg = genExpr(static_cast<Expr*>(expr->operand.get()));
    // Phase 4f: result slot must be sized for the type (Complex/Fraction = 2 words).
    u16 dst = allocSlot(expr->resolvedType);

    switch (expr->op) {
        case UnaryOpExpr::Neg:
            if (isCompositeNumeric(expr->resolvedType)) {
                Type* opT = expr->operand->resolvedType;
                Type* rT  = expr->resolvedType;
                auto isInlineComposite = [&](Type* t) {
                    return t && t->repr_ == ts::Type::Repr::Inline
                        && t != compiler_.complexType()
                        && t != compiler_.fractionType();
                };
                if (isInlineComposite(rT)) {
                    emitOp(op_neg_composite_inline);
                    emitRegs(dst, operandReg);
                    emitPtr(rT);
                    emitPtr(opT);
                    return dst;
                }
                // Phase 4g.16: heap-result composite unop reads operand
                // natively; no boxing.
                emitOp(op_neg_composite);
                emitRegs(dst, operandReg);
                emitPtr(rT);
                emitPtr(opT);
                return dst;
            }
            if (expr->resolvedType == compiler_.complexType()) {
                emitOp(op_neg_complex);
            } else if (expr->resolvedType == compiler_.fractionType()) {
                emitOp(op_neg_fraction);
            } else if (expr->resolvedType == compiler_.floatType()) {
                emitOp(op_neg_float);
            } else {
                emitOp(op_neg_int);
            }
            emitRegs(dst, operandReg);
            break;

        case UnaryOpExpr::Not:
            if (isCompositeNumeric(expr->resolvedType)) {
                Type* opT = expr->operand->resolvedType;
                Type* rT  = expr->resolvedType;
                auto isInlineComposite = [&](Type* t) {
                    return t && t->repr_ == ts::Type::Repr::Inline
                        && t != compiler_.complexType()
                        && t != compiler_.fractionType();
                };
                if (isInlineComposite(rT)) {
                    emitOp(op_not_composite_inline);
                    emitRegs(dst, operandReg);
                    emitPtr(rT);
                    emitPtr(opT);
                    return dst;
                }
                // Phase 4g.16: heap-result composite unop reads operand
                // natively; no boxing.
                emitOp(op_not_composite);
                emitRegs(dst, operandReg);
                emitPtr(rT);
                emitPtr(opT);
                return dst;
            }
            emitOp(op_not_bool);
            emitRegs(dst, operandReg);
            break;

        case UnaryOpExpr::BitNot:
            if (isCompositeNumeric(expr->resolvedType)) {
                Type* opT = expr->operand->resolvedType;
                Type* rT  = expr->resolvedType;
                auto isInlineComposite = [&](Type* t) {
                    return t && t->repr_ == ts::Type::Repr::Inline
                        && t != compiler_.complexType()
                        && t != compiler_.fractionType();
                };
                if (isInlineComposite(rT)) {
                    emitOp(op_bitnot_composite_inline);
                    emitRegs(dst, operandReg);
                    emitPtr(rT);
                    emitPtr(opT);
                    return dst;
                }
                // Phase 4g.16: heap-result composite unop reads operand
                // natively; no boxing.
                emitOp(op_bitnot_composite);
                emitRegs(dst, operandReg);
                emitPtr(rT);
                emitPtr(opT);
                return dst;
            }
            emitOp(op_bitnot_int);
            emitRegs(dst, operandReg);
            break;

        case UnaryOpExpr::Ref: {
            // &expr — create a Ref<T>.
            // Phase 4g.5: inline composites travel inline through InlineRef::v[].
            auto* refType = static_cast<RefType*>(expr->resolvedType);
            if (isInlineMultiword(refType->elemType_)) {
                emitOp(op_make_ref_inline);
                emitRegs(dst, operandReg);
                emitPtr(refType);
                break;
            }
            emitOp(op_make_ref);
            emitRegs(dst, operandReg);
            emitPtr(refType);
            break;
        }

        case UnaryOpExpr::Deref: {
            // *expr — dereference a Ref<T>.
            auto* refType = dynamic_cast<RefType*>(expr->operand->resolvedType);
            if (refType && isInlineMultiword(refType->elemType_)) {
                // Phase 4g.5: read the inline payload into a fresh multi-word
                // slot. `dst` was sized 1-word by the unary op preamble; alloc
                // the right-sized slot here instead.
                u16 sz = (u16)typeSlotWords(refType->elemType_);
                u16 dstSlot = allocRegs(sz);
                emitOp(op_ref_get_inline);
                emitRegs(dstSlot, operandReg);
                emitPtr(refType);
                return dstSlot;
            }
            emitOp(op_ref_get);
            emitRegs(dst, operandReg);
            break;
        }
    }

    return dst;
}

u16 CodeGen::genCall(CallExpr_* expr) {
    // Consume tail position flag — only this top-level call can use it
    bool isTailCall = inTailPosition_ && enableTailCalls;
    inTailPosition_ = false;  // Clear for sub-expression generation

    // Tuple struct construction: resolvedFuncGlobalIndex == -3
    if (expr->resolvedFuncGlobalIndex == -3) {
        if (!expr->autoMapArgs.empty()) {
            // Check for cartesian (@1/@2) vs zip (@)
            int maxCartesian = 0;
            for (auto& am : expr->autoMapArgs) {
                if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
            }
            if (maxCartesian > 0) return genCartesianTupleStruct(expr);
            return genAutoMapTupleStruct(expr);
        }
        auto* stype = dynamic_cast<StructType*>(expr->resolvedType);
        if (!stype) {
            error(expr->loc, "Tuple struct construction has non-struct resolved type");
            return allocReg();
        }
        // Phase 1: single-field tuple struct over a Pointer-Repr inner is
        // an UnwrappedTupleStruct -- skip the boxing Struct allocation and
        // just return the inner value. Runtime treats both as a 1-word Obj*.
        if (stype->repr_ == ts::Type::Repr::UnwrappedTupleStruct
            && stype->fields_.size() == 1 && expr->args.size() == 1) {
            u16 valReg = genExpr(static_cast<Expr*>(expr->args[0].get()));
            Type* valType = expr->args[0]->resolvedType;
            Type* declType = stype->fields_[0].type;
            return ensureType(valReg, valType, declType);
        }
        // Phase 4f/4g.2: Inline tuple-structs lay out fields at multi-word
        // stride and land directly in a multi-word dst. Heap tuple-structs
        // use 1 Word per field with box-at-boundary for Inline-typed fields.
        usize numFields = stype->fields_.size();
        bool inlineStruct = stype->repr_ == ts::Type::Repr::Inline;
        u16 fieldBase = nextReg_;
        u16 cursor = fieldBase;
        for (size_t i = 0; i < numFields; ++i) {
            u16 valReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
            Type* valType = expr->args[i]->resolvedType;
            Type* declType = stype->fields_[i].type;
            valReg = ensureType(valReg, valType, declType);
            if (!inlineStruct && isInlineMultiword(declType)) {
                valReg = emitBoxIfInline(valReg, declType);
            }
            u16 fieldSlotWords = inlineStruct ? (u16)typeSlotWords(declType) : 1;
            emitArgPlacement(cursor, valReg, inlineStruct ? declType : nullptr);
            cursor = (u16)(cursor + fieldSlotWords);
            if (nextReg_ < cursor) { nextReg_ = cursor; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        }
        u16 dst = inlineStruct ? allocSlot(stype) : allocReg();
        emitOp(op_make_struct);
        emitRegs(dst, fieldBase, (u16)numFields);
        emitPtr(stype);
        return dst;
    }

    // Check for module-qualified or std-qualified call: module.func(args) or std.func(args)
    // The type checker sets resolvedFuncGlobalIndex for these.
    if (expr->callee->kind == ASTNode::FieldExpr && expr->resolvedFuncGlobalIndex >= 0) {
        auto* fe = static_cast<FieldExpr_*>(expr->callee.get());
        if (fe->object->kind == ASTNode::Identifier) {
            const auto& importedModules = typeChecker_.importedModules();
            auto* ident = static_cast<IdentifierExpr*>(fe->object.get());
            if (importedModules.count(ident->name)) {
                // Look up resolved FuncInfo for argument type promotion
                const std::vector<Type*>* paramTypes = nullptr;
                {
                    auto modIt2 = importedModules.find(ident->name);
                    if (modIt2 != importedModules.end()) {
                        auto expIt = modIt2->second->exports.find(fe->field);
                        if (expIt != modIt2->second->exports.end()) {
                            for (const auto& fi : expIt->second.funcOverloads) {
                                if ((i32)fi.globalIndex == expr->resolvedFuncGlobalIndex) {
                                    paramTypes = &fi.paramTypes;
                                    break;
                                }
                            }
                        }
                    }
                }
                // Generate args into consecutive registers (Phase 4f: multi-word).
                u16 argBase = nextReg_;
                u32 cumOffset = 0;
                for (size_t i = 0; i < expr->args.size(); ++i) {
                    u16 argReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
                    Type* paramType = expr->args[i]->resolvedType;
                    if (paramTypes && i < paramTypes->size()) {
                        argReg = ensureType(argReg, expr->args[i]->resolvedType, (*paramTypes)[i]);
                        paramType = (*paramTypes)[i];
                    }
                    u16 dstReg = (u16)(argBase + cumOffset);
                    cumOffset += emitArgPlacementForCall(dstReg, argReg, paramType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
                }
                if (isTailCall && !expr->isBuiltinCall) {
                    emitOp(op_tail_call);
                    emitRegs(0, (u16)expr->args.size(), argBase);
                    emitInt(expr->resolvedFuncGlobalIndex);
                    return allocReg();
                }
                u16 resultReg = allocSlot(expr->resolvedType);
                clearArgRegTypes(argBase, resultReg);
                emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
                emitRegs(resultReg, (u16)expr->args.size(), argBase);
                emitInt(expr->resolvedFuncGlobalIndex);
                emitReturnPcStackMap(resultReg, expr->resolvedType);
                return resultReg;
            }
        }
    }

    if (expr->callee->kind != ASTNode::Identifier) {
        // Template lambda through indirect callee (e.g., (*r)(x) where r = &fn(x){...})
        if (expr->resolvedTemplateLambdaType) {
            auto* concreteLT = expr->resolvedTemplateLambdaType;
            u16 calleeReg = genExpr(static_cast<Expr*>(expr->callee.get()));

            // Compile the CodeBlock for this instantiation if needed
            auto* tmplType = dynamic_cast<TemplateLambdaType*>(expr->callee->resolvedType);
            if (tmplType && tmplType->astNode_) {
                compileTemplateLambdaBody(tmplType->astNode_, concreteLT);
            }

            // Generate args into consecutive registers (Phase 4f: multi-word).
            u16 argBase = nextReg_;
            u32 cumOffset = 0;
            for (size_t i = 0; i < expr->args.size(); ++i) {
                u16 argReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
                Type* paramType = (i < concreteLT->argTypes_.size())
                                ? concreteLT->argTypes_[i] : expr->args[i]->resolvedType;
                u16 dstReg = (u16)(argBase + cumOffset);
                cumOffset += emitArgPlacementForCall(dstReg, argReg, paramType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
            }

            if (isTailCall) {
                emitOp(op_tail_call_template_lambda);
                emitRegs(0, (u16)expr->args.size(), argBase, calleeReg);
                emitPtr(concreteLT->codeBlock_);
                return allocReg();
            }
            u16 resultReg = allocSlot(expr->resolvedType);
            clearArgRegTypes(argBase, resultReg);
            emitOp(op_call_template_lambda);
            emitRegs(resultReg, (u16)expr->args.size(), argBase, calleeReg);
            emitPtr(concreteLT->codeBlock_);
            emitReturnPcStackMap(resultReg, expr->resolvedType);
            return resultReg;
        }

        // General expression callee (e.g., a[i](x, y))
        u16 calleeReg = genExpr(static_cast<Expr*>(expr->callee.get()));
        auto* funcType = dynamic_cast<FunctionType*>(expr->callee->resolvedType);

        // Auto-mapped lambda call
        if (!expr->autoMapArgs.empty()) {
            if (funcType) return genAutoMapLambdaCall(expr, calleeReg, funcType);
        }

        // Generate args into consecutive registers (Phase 4f: multi-word).
        u16 argBase = nextReg_;
        u32 cumOffset = 0;
        for (size_t i = 0; i < expr->args.size(); ++i) {
            u16 argReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
            Type* paramType = (funcType && i < funcType->argTypes_.size())
                            ? funcType->argTypes_[i] : expr->args[i]->resolvedType;
            u16 dstReg = (u16)(argBase + cumOffset);
            cumOffset += emitArgPlacementForCall(dstReg, argReg, paramType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        }

        // Coroutine lambda call
        if (expr->isCoroCall) {
            auto* coroType = dynamic_cast<CoroutineType*>(expr->resolvedType);
            u16 resultReg = allocReg();
            emitOp(op_coro_create_lambda);
            emitRegs(resultReg, argBase, (u16)expr->args.size(), calleeReg);
            emitPtr(coroType);
            return resultReg;
        }

        if (isTailCall) {
            emitOp(op_tail_call_lambda);
            emitRegs(0, (u16)expr->args.size(), argBase, calleeReg);
            return allocReg();
        }
        u16 resultReg = allocSlot(expr->resolvedType);
        clearArgRegTypes(argBase, resultReg);
        emitOp(op_call_lambda);
        emitRegs(resultReg, (u16)expr->args.size(), argBase, calleeReg);
        emitReturnPcStackMap(resultReg, expr->resolvedType);
        return resultReg;
    }

    auto* ident = static_cast<IdentifierExpr*>(expr->callee.get());

    // Handle getListPrintLimit()
    if (ident->name == "getListPrintLimit") {
        u16 dst = allocReg();
        emitOp(op_get_list_print_limit);
        emitRegs(dst);
        return dst;
    }

    // Handle setListPrintLimit(n)
    if (ident->name == "setListPrintLimit") {
        u16 argReg = genExpr(static_cast<Expr*>(expr->args[0].get()));
        u16 dst = allocReg();
        emitOp(op_set_list_print_limit);
        emitRegs(dst, argReg);
        return dst;
    }

    // Handle Complex() constructor
    if (ident->name == "Complex") {
        // Complex(real, imag) - both args promoted to float
        u16 realReg = genExpr(static_cast<Expr*>(expr->args[0].get()));
        u16 imagReg = genExpr(static_cast<Expr*>(expr->args[1].get()));
        realReg = ensureFloat(realReg, expr->args[0]->resolvedType);
        imagReg = ensureFloat(imagReg, expr->args[1]->resolvedType);

        // Phase 4f: Complex is inline 2 words.
        u16 dst = allocSlot(compiler_.complexType());
        emitOp(op_make_complex);
        emitRegs(dst, realReg, imagReg);
        return dst;
    }

    // Check for template lambda call (resolvedTemplateLambdaType set by type checker)
    if (expr->resolvedTemplateLambdaType) {
        auto* concreteLT = expr->resolvedTemplateLambdaType;

        // Find the template lambda's AST node from the TemplateLambdaType
        // The callee must be a local or global variable holding a template lambda
        LocalVar* tmplLocal = lookupLocal(ident->name);
        u16 calleeReg;
        LambdaExprNode* tmplExpr = nullptr;

        if (tmplLocal) {
            calleeReg = tmplLocal->reg;
            // Get the TemplateLambdaType from the local's type
            auto* tmplType = dynamic_cast<TemplateLambdaType*>(tmplLocal->type);
            if (tmplType) tmplExpr = tmplType->astNode_;
        } else {
            // Global template lambda variable
            auto gvIt = typeChecker_.globalVars().find(ident->name);
            if (gvIt != typeChecker_.globalVars().end()) {
                calleeReg = allocReg();
                emitOp(op_load_global);
                emitRegs(calleeReg);
                emitInt(gvIt->second.globalIndex);
                auto* tmplType = dynamic_cast<TemplateLambdaType*>(gvIt->second.type);
                if (tmplType) tmplExpr = tmplType->astNode_;
            } else {
                error(expr->loc, "Cannot find template lambda '" + ident->name + "'");
                return allocReg();
            }
        }

        // Compile the CodeBlock for this instantiation if needed
        if (tmplExpr) {
            compileTemplateLambdaBody(tmplExpr, concreteLT);
        }

        // Generate args into consecutive registers (Phase 4f: multi-word).
        u16 argBase = nextReg_;
        u32 cumOffset = 0;
        for (size_t i = 0; i < expr->args.size(); ++i) {
            u16 argReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
            Type* paramType = (i < concreteLT->argTypes_.size())
                            ? concreteLT->argTypes_[i] : expr->args[i]->resolvedType;
            u16 dstReg = (u16)(argBase + cumOffset);
            cumOffset += emitArgPlacementForCall(dstReg, argReg, paramType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        }

        if (isTailCall) {
            emitOp(op_tail_call_template_lambda);
            emitRegs(0, (u16)expr->args.size(), argBase, calleeReg);
            emitPtr(concreteLT->codeBlock_);
            return allocReg();
        }
        u16 resultReg = allocSlot(expr->resolvedType);
        clearArgRegTypes(argBase, resultReg);
        emitOp(op_call_template_lambda);
        emitRegs(resultReg, (u16)expr->args.size(), argBase, calleeReg);
        emitPtr(concreteLT->codeBlock_);
        emitReturnPcStackMap(resultReg, expr->resolvedType);
        return resultReg;
    }

    // Check if callee is a local variable holding a lambda/function type
    LocalVar* calleeLocal = lookupLocal(ident->name);
    if (calleeLocal && dynamic_cast<FunctionType*>(calleeLocal->type)) {
        u16 calleeReg = calleeLocal->reg;
        auto* funcType = static_cast<FunctionType*>(calleeLocal->type);

        // Auto-mapped lambda call
        if (!expr->autoMapArgs.empty()) {
            return genAutoMapLambdaCall(expr, calleeReg, funcType);
        }

        // Generate args into consecutive registers (Phase 4f: multi-word aware).
        u16 argBase = nextReg_;
        u32 cumOffset = 0;
        for (size_t i = 0; i < expr->args.size(); ++i) {
            u16 argReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
            Type* paramType = (i < funcType->argTypes_.size())
                            ? funcType->argTypes_[i] : expr->args[i]->resolvedType;
            u16 dstReg = (u16)(argBase + cumOffset);
            cumOffset += emitArgPlacementForCall(dstReg, argReg, paramType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        }

        if (isTailCall) {
            emitOp(op_tail_call_lambda);
            emitRegs(0, (u16)expr->args.size(), argBase, calleeReg);
            return allocReg();
        }
        u16 resultReg = allocSlot(expr->resolvedType);
        clearArgRegTypes(argBase, resultReg);
        emitOp(op_call_lambda);
        emitRegs(resultReg, (u16)expr->args.size(), argBase, calleeReg);
        emitReturnPcStackMap(resultReg, expr->resolvedType);
        return resultReg;
    }

    // Check for global lambda variable
    {
        auto gvIt = typeChecker_.globalVars().find(ident->name);
        if (gvIt != typeChecker_.globalVars().end()) {
            auto* funcType = dynamic_cast<FunctionType*>(gvIt->second.type);
            if (funcType) {
                // Load global lambda into a register
                u16 calleeReg = allocReg();
                emitOp(op_load_global);
                emitRegs(calleeReg);
                emitInt(gvIt->second.globalIndex);

                // Auto-mapped lambda call
                if (!expr->autoMapArgs.empty()) {
                    return genAutoMapLambdaCall(expr, calleeReg, funcType);
                }

                // Generate args into consecutive registers (Phase 4f: multi-word).
                u16 argBase = nextReg_;
                u32 cumOffset = 0;
                for (size_t i = 0; i < expr->args.size(); ++i) {
                    u16 argReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
                    Type* paramType = (i < funcType->argTypes_.size())
                                    ? funcType->argTypes_[i] : expr->args[i]->resolvedType;
                    u16 dstReg = (u16)(argBase + cumOffset);
                    cumOffset += emitArgPlacementForCall(dstReg, argReg, paramType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
                }

                if (isTailCall) {
                    emitOp(op_tail_call_lambda);
                    emitRegs(0, (u16)expr->args.size(), argBase, calleeReg);
                    return allocReg();
                }
                u16 resultReg = allocSlot(expr->resolvedType);
                clearArgRegTypes(argBase, resultReg);
                emitOp(op_call_lambda);
                emitRegs(resultReg, (u16)expr->args.size(), argBase, calleeReg);
                emitReturnPcStackMap(resultReg, expr->resolvedType);
                return resultReg;
            }
        }
    }

    // Check for auto-mapped call
    if (!expr->autoMapArgs.empty()) {
        // Check for combined explicit @ + implicit auto-mapping
        if (!expr->innerAutoMapArgs.empty()) {
            return genExplicitImplicitAutoMapCall(expr);
        }

        // Check if any arg has cartesian index > 0
        bool hasCartesian = false;
        int maxDepth = 0;
        for (auto& am : expr->autoMapArgs) {
            if (am.cartesianIndex > 0) hasCartesian = true;
            if (am.depth > maxDepth) maxDepth = am.depth;
        }

        if (hasCartesian) {
            return genCartesianCall(expr);
        } else if (maxDepth > 1) {
            return genDeepMapCall(expr, maxDepth);
        } else {
            return genAutoMapCall(expr);
        }
    }

    // Use resolved overload index from type checker
    if (expr->resolvedFuncGlobalIndex < 0) {
        error(expr->loc, "Codegen: unresolved function '" + ident->name + "'");
        return allocReg();
    }

    // Look up function info for parameter types (needed for numeric promotion)
    const FuncInfo* funcInfo = nullptr;
    auto funcIt = typeChecker_.functions().find(ident->name);
    if (funcIt != typeChecker_.functions().end()) {
        for (const auto& fi : funcIt->second) {
            if ((i32)fi.globalIndex == expr->resolvedFuncGlobalIndex) {
                funcInfo = &fi;
                break;
            }
        }
    }

    // Generate arguments into consecutive registers.
    // Phase 4f: each arg occupies sizeWords_ regs (1 for atom/pointer; 2 for
    // inline Complex/Fraction). cumOffset tracks the running word offset.
    // Variadic args going into a Tuple/Array pack must be boxed to 1 Word
    // each (the pack opcode stores 1 Word per field/element).
    u16 argBase = nextReg_;
    u32 cumOffset = 0;
    for (size_t i = 0; i < expr->args.size(); ++i) {
        u16 argReg = genExpr(static_cast<Expr*>(expr->args[i].get()));
        // Promote argument type if needed (e.g. Int -> Float)
        Type* targetType = expr->args[i]->resolvedType;
        bool isVariadic = (expr->variadicPackStart >= 0 && (int)i >= expr->variadicPackStart);
        if (!isVariadic && funcInfo && i < funcInfo->paramTypes.size()) {
            argReg = ensureType(argReg, expr->args[i]->resolvedType, funcInfo->paramTypes[i]);
            targetType = funcInfo->paramTypes[i];
        } else if (isVariadic) {
            if (auto* arrType = dynamic_cast<ArrayType*>(expr->variadicPackType)) {
                argReg = ensureType(argReg, expr->args[i]->resolvedType, arrType->elemType_);
                targetType = arrType->elemType_;
            }
            // Tuple-pack variadics keep the arg's own type.
        }
        // Phase 4g.13: variadic args are packed into a heap Tuple/Array
        // whose storage is layout-aware (multi-word per Inline composite
        // field). Place at the arg's natural footprint; no boundary box.
        u32 placeSw = typeSlotWords(targetType);
        if (isVariadic && dynamic_cast<ArrayType*>(expr->variadicPackType)) {
            // Array-pack variadics still want a 1-word Obj* per element.
            if (isInlineMultiword(targetType)) {
                argReg = emitBoxIfInline(argReg, targetType);
                placeSw = 1;
            }
        }
        // Phase 4g.2 / 4g.6: legacy builtins (op_call_primitive) receive Inline
        // structs/tuples as a 1-Word boxed Obj*; Complex/Fraction stay multi-
        // word per Phase 4f. Builtins migrated under Phase 4g.6 set
        // FuncInfo::acceptsInlineArgs (propagated to expr->builtinAcceptsInline
        // Args) and receive multi-word inline slots directly. Skip for yield /
        // coro_resume / coro_yieldAll which have their own specialized
        // lowering further down in this function.
        bool specialCoroOp = expr->isCoroYield || expr->isCoroResume
                          || expr->isCoroYieldAll;
        if (!isVariadic && expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs
            && !specialCoroOp && targetType
            && targetType->repr_ == ts::Type::Repr::Inline
            && targetType != compiler_.complexType()
            && targetType != compiler_.fractionType()) {
            argReg = emitBoxIfInline(argReg, targetType);
            placeSw = 1;
        }
        u16 dstReg = (u16)(argBase + cumOffset);
        if (argReg != dstReg) {
            if (placeSw <= 1) {
                emitMov(dstReg, argReg);
            } else {
                emitMoveN(dstReg, argReg, placeSw);
            }
            if (enableConstFold && placeSw == 1) {
                auto* srcConst = getConst(argReg);
                if (srcConst) constRegs_[dstReg] = *srcConst;
                else          constRegs_.erase(dstReg);
            }
        }
        u16 next = (u16)(dstReg + placeSw);
        if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        cumOffset += placeSw;
    }

    // Variadic packing: pack excess args into a Tuple or Array
    u16 callArgc = (u16)expr->args.size();
    if (expr->variadicPackStart >= 0) {
        u16 packBase = argBase + (u16)expr->variadicPackStart;
        u16 packCount = (u16)expr->args.size() - (u16)expr->variadicPackStart;

        // If there are zero variadic args, we still need to allocate a register for the pack
        if (packCount == 0) {
            // Ensure packBase register is allocated
            if (nextReg_ <= packBase) { nextReg_ = packBase + 1; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        }

        if (auto* tupType = dynamic_cast<TupleType*>(expr->variadicPackType)) {
            // Phase 4g.2: builtin variadic-pack tuples must always land as
            // a heap Tuple* (the builtin reads tup->v[i] for each value),
            // even when the TupleType was promoted to Inline. For non-
            // builtin functions the callee expects the pack as a multi-word
            // inline value, which op_make_tuple Inline produces directly.
            emitOp((expr->isBuiltinCall && tupType->repr_ == ts::Type::Repr::Inline)
                   ? op_make_tuple_heap : op_make_tuple);
            emitRegs(packBase, packBase, packCount);
            emitPtr(tupType);
        } else if (auto* arrType = dynamic_cast<ArrayType*>(expr->variadicPackType)) {
            emitOp(op_make_array);
            emitRegs(packBase, packBase, packCount);
            emitPtr(arrType);
        }

        // The packed value is now in packBase; adjust register state
        nextReg_ = packBase + 1;
        if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        callArgc = (u16)expr->variadicPackStart + 1;
    }

    // Try folding builtin calls with all-constant arguments
    if (enableConstFold && expr->isBuiltinCall && expr->autoMapArgs.empty()) {
        u16 folded = tryFoldBuiltinCall(expr, argBase, callArgc);
        if (folded != UINT16_MAX) return folded;
    }

    // Coroutine resume: emit op_coro_resume + op_coro_wrap_option for next() calls
    if (expr->isCoroResume) {
        u16 coroReg = argBase;  // first (and only) argument is the coroutine
        // Phase 4g.12: yield value lands in a slot wide enough for the yield
        // type; op_coro_wrap_option writes the Inline Option directly into a
        // multi-word dst (no heap Enum* round-trip).
        auto* coroType = dynamic_cast<CoroutineType*>(expr->args[0]->resolvedType);
        Type* yieldType = coroType ? coroType->yieldType_ : nullptr;
        Type* optType = expr->resolvedType;
        u16 valueReg = allocSlot(yieldType);
        emitOp(op_coro_resume);
        emitRegs(valueReg, coroReg);
        emitReturnPcStackMap(valueReg, yieldType);
        u16 resultReg = allocSlot(optType);
        emitOp(op_coro_wrap_option);
        emitRegs(resultReg, valueReg, coroReg);
        emitPtr(optType);  // Option<T> EnumType*
        return resultReg;
    }

    // Coroutine yield: emit op_yield (same logic as old genYieldStmt)
    if (expr->isCoroYield) {
        // Build GC map: collect registers that hold Obj* values.
        // Phase 4g.4: skip Inline composite locals -- their base word is
        // a payload word (tag for enums, field 0 for structs/tuples), not
        // an Obj*; treating it as one would crash the GC walker. Embedded
        // Obj* fields inside inline composites that survive across a yield
        // are not yet traced; revisit when we promote container backends.
        std::vector<u16> gcMap;
        for (auto& scope : localScopes_) {
            for (auto& entry : scope) {
                Type* t = entry.second.type;
                if (!t) continue;
                if (isInlineMultiword(t)) continue;
                if (storesObjPtr(t)) {
                    gcMap.push_back(entry.second.reg);
                }
            }
        }

        // Store GC map in current code block
        u16 gcMapIndex = currentYieldCount_++;
        currentBlock_->coroGCMaps_.push_back(std::move(gcMap));

        // Phase 4g.12: yield transfers sizeWords_ words from src to the caller
        // -- no boxing of inline composites at the yield boundary.
        u16 srcReg = argBase;  // first (and only) argument is the yielded value

        // Emit yield: op, regs{src, gcMapIdx}
        emitOp(op_yield);
        emitRegs(srcReg, gcMapIndex);

        return allocReg();  // void result
    }

    // Coroutine yieldAll: drain inner coroutine, yielding each value
    if (expr->isCoroYieldAll) {
        u16 coroReg = argBase;  // first (and only) argument is the inner coroutine

        // Get the inner coroutine's yield type for GC map decisions
        auto* innerCoroType = dynamic_cast<CoroutineType*>(expr->args[0]->resolvedType);

        // Loop top
        u32 loopStart = (u32)currentBlock_->code.size();

        // op_coro_resume -> value directly (no Enum wrapper).
        // Phase 4g.12: value slot is sized for the yield type (multi-word for
        // Inline composites).
        u16 valueReg = allocSlot(innerCoroType ? innerCoroType->yieldType_ : nullptr);
        emitOp(op_coro_resume);
        emitRegs(valueReg, coroReg);
        emitReturnPcStackMap(valueReg, innerCoroType ? innerCoroType->yieldType_ : nullptr);

        // Check if coroutine is done
        u16 doneReg = allocReg();
        emitOp(op_coro_is_done);
        emitRegs(doneReg, coroReg);
        u32 exitJump = emitJump(op_jump_if_true, doneReg);

        // Build GC map: collect registers that hold Obj* values.
        // Phase 4g.4: skip Inline composite locals (see yield path comment).
        std::vector<u16> gcMap;
        for (auto& scope : localScopes_) {
            for (auto& entry : scope) {
                Type* t = entry.second.type;
                if (!t) continue;
                if (isInlineMultiword(t)) continue;
                if (storesObjPtr(t)) {
                    gcMap.push_back(entry.second.reg);
                }
            }
        }
        // coroReg always holds an Obj*
        gcMap.push_back(coroReg);
        // valueReg only holds an Obj* if the yield type is an object type
        if (innerCoroType && storesObjPtr(innerCoroType->yieldType_)) {
            gcMap.push_back(valueReg);
        }

        u16 gcMapIndex = currentYieldCount_++;
        currentBlock_->coroGCMaps_.push_back(std::move(gcMap));

        // Yield the value
        emitOp(op_yield);
        emitRegs(valueReg, gcMapIndex);

        // Jump back to loop
        emitJumpTo(loopStart);

        // Patch exit
        patchJump(exitJump);

        return allocReg();  // void result
    }

    // Coroutine creation: emit op_coro_create instead of op_call
    if (expr->isCoroCall) {
        auto* coroType = dynamic_cast<CoroutineType*>(expr->resolvedType);
        u16 resultReg = allocReg();
        emitOp(op_coro_create);
        emitRegs(resultReg, argBase, callArgc);
        emitInt(expr->resolvedFuncGlobalIndex);  // global index for CodeBlock lookup
        emitPtr(coroType);
        return resultReg;
    }

    // Tail call: reuse current frame instead of pushing a new one
    if (isTailCall && !expr->isBuiltinCall) {
        emitOp(op_tail_call);
        emitRegs(0, callArgc, argBase);
        emitInt(expr->resolvedFuncGlobalIndex);
        return allocReg();  // Dead register — caller emits dead op_return
    }

    // Result register. Phase 4f: builtins writing inline Complex/Fraction
    // need a 2-word slot. Phase 4g.2: builtins return Inline structs/
    // tuples as a 1-Word boxed pointer; allocate a 1-word target reg and
    // unbox after the call.
    bool builtinReturnsInlineComposite = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs && expr->resolvedType
        && expr->resolvedType->repr_ == ts::Type::Repr::Inline
        && expr->resolvedType != compiler_.complexType()
        && expr->resolvedType != compiler_.fractionType();
    u16 callDst = builtinReturnsInlineComposite ? allocReg() : allocSlot(expr->resolvedType);

    // Phase 5.2: arg slots become callee-owned the moment the call
    // dispatches; the caller's stack map at the returnPC must NOT name
    // them as live refs (a GC walker visiting mid-call would otherwise
    // read callee data as Obj* pointers).
    clearArgRegTypes(argBase, callDst);

    // Emit CALL: op, regs{resultReg, argc, argBase}, callee_global_idx
    emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
    emitRegs(callDst, callArgc, argBase);
    emitInt(expr->resolvedFuncGlobalIndex);

    // Phase 5.2: record live refs at the returnPC so an in-callee GC
    // cycle can root the caller's frame precisely. callDst is delivered
    // by op_return BEFORE the caller resumes here, so it IS a live Obj*
    // slot at this PC if the result type stores one -- we seed it here
    // because genExpr's wrapper only setRegType-s it after we return.
    emitReturnPcStackMap(callDst, expr->resolvedType);

    if (builtinReturnsInlineComposite) {
        return emitUnboxIfInline(callDst, expr->resolvedType);
    }
    return callDst;
}

// Helper: get the parameter type for argument i, handling variadic functions
// where i may exceed paramTypes.size().
Type* CodeGen::getParamType(const FuncInfo* funcInfo, const CallExpr_* expr, size_t i) {
    if (i < funcInfo->paramTypes.size()) {
        return funcInfo->paramTypes[i];
    }
    // Beyond fixed params — use variadic element type
    if (expr->variadicPackStart >= 0) {
        if (auto* arrType = dynamic_cast<ArrayType*>(expr->variadicPackType)) {
            return arrType->elemType_;
        }
        // For TupleType variadics, no promotion needed
    }
    return nullptr;
}

// Helper: emit variadic packing after arg setup in an auto-map loop.
// Returns the adjusted argc for the call.
u16 CodeGen::emitVariadicPack(CallExpr_* expr, u16 callArgBase, u16 argc) {
    if (expr->variadicPackStart < 0) return argc;

    u16 packBase = callArgBase + (u16)expr->variadicPackStart;
    u16 packCount = argc - (u16)expr->variadicPackStart;

    if (packCount == 0) {
        if (nextReg_ <= packBase) { nextReg_ = packBase + 1; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
    }

    if (auto* tupType = dynamic_cast<TupleType*>(expr->variadicPackType)) {
        emitOp(op_make_tuple);
        emitRegs(packBase, packBase, packCount);
        emitPtr(tupType);
    } else if (auto* arrType = dynamic_cast<ArrayType*>(expr->variadicPackType)) {
        emitOp(op_make_array);
        emitRegs(packBase, packBase, packCount);
        emitPtr(arrType);
    }

    nextReg_ = packBase + 1;
    if (nextReg_ > maxReg_) maxReg_ = nextReg_;
    return (u16)expr->variadicPackStart + 1;
}

u16 CodeGen::genAutoMapBinaryOp(BinaryOpExpr* expr) {
    // --- Phase 1: Evaluate both operands ---
    u16 leftReg = genExpr(static_cast<Expr*>(expr->left.get()));
    u16 rightReg = genExpr(static_cast<Expr*>(expr->right.get()));

    Type* leftType = expr->left->resolvedType;
    Type* rightType = expr->right->resolvedType;

    // Determine element types by unwrapping
    Type* leftElemType = leftType;
    Type* rightElemType = rightType;
    if (expr->leftAutoMap) {
        for (int d = 0; d < expr->leftAutoMap.depth; ++d) {
            if (auto* arrT = dynamic_cast<ArrayType*>(leftElemType))
                leftElemType = arrT->elemType_;
        }
    }
    if (expr->rightAutoMap) {
        for (int d = 0; d < expr->rightAutoMap.depth; ++d) {
            if (auto* arrT = dynamic_cast<ArrayType*>(rightElemType))
                rightElemType = arrT->elemType_;
        }
    }

    // --- Phase 2: Compute min length of @-tagged arrays ---
    u16 minLenReg = 0;
    bool firstAutoMap = true;

    auto computeLen = [&](u16 arrReg, Type* arrType) {
        auto* at = dynamic_cast<ArrayType*>(arrType);
        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(at->elemType_));
        emitRegs(lenReg, arrReg);
        emitPtr(at);
        if (firstAutoMap) {
            minLenReg = lenReg;
            firstAutoMap = false;
        } else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skipJump);
        }
    };

    if (expr->leftAutoMap) computeLen(leftReg, leftType);
    if (expr->rightAutoMap) computeLen(rightReg, rightType);

    // --- Phase 3: Allocate result array ---
    auto* resultArrayType = dynamic_cast<ArrayType*>(expr->resolvedType);
    u16 resultArrReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(resultArrReg, minLenReg);
    emitPtr(resultArrayType);

    // --- Phase 4: Loop counter setup ---
    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    u16 condReg = allocReg();

    // --- Phase 5: Loop start ---
    u32 loopStartIdx = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, minLenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // --- Phase 6: Extract elements ---
    auto needsBoxAuto = [&](Type* t) {
        return t && t->repr_ == ts::Type::Repr::Inline
            && t != compiler_.complexType()
            && t != compiler_.fractionType();
    };
    // Phase 4g.8: InlineArray returns multi-word inline slots directly via
    // op_array_get_dyn -- no unboxing needed.
    auto allocElemSlot = [&](Type* t) {
        u16 nw = typeSlotWords(t);
        u16 r = nextReg_;
        nextReg_ = (u16)(nextReg_ + nw);
        if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        return r;
    };
    u16 leftElemReg = leftReg;
    if (expr->leftAutoMap) {
        auto* arrType = dynamic_cast<ArrayType*>(leftType);
        leftElemReg = allocElemSlot(leftElemType);
        emitOp(opArrayGetDynFor(arrType->elemType_));
        emitRegs(leftElemReg, leftReg, iReg);
        emitPtr(arrType);
    }

    u16 rightElemReg = rightReg;
    if (expr->rightAutoMap) {
        auto* arrType = dynamic_cast<ArrayType*>(rightType);
        rightElemReg = allocElemSlot(rightElemType);
        emitOp(opArrayGetDynFor(arrType->elemType_));
        emitRegs(rightElemReg, rightReg, iReg);
        emitPtr(arrType);
    }

    // --- Phase 7: Compute per-element result ---
    Type* scalarResultType = resultArrayType->elemType_;
    u16 elemResultReg;

    if (expr->resolvedFuncGlobalIndex >= 0) {
        // Operator overload: call the function. Phase 4g.2: place each
        // operand at its multi-word slot offset for non-builtin calls; for
        // builtins box Inline composite operands first.
        u16 argBase = nextReg_;
        u32 lWords = emitArgPlacementForCall(argBase, leftElemReg, leftElemType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        u16 rDst = (u16)(argBase + lWords);
        u32 rWords = emitArgPlacementForCall(rDst, rightElemReg, rightElemType, expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        u16 next = (u16)(rDst + rWords);
        if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        bool builtinReturnsInline = expr->isBuiltinCall && needsBoxAuto(scalarResultType);
        elemResultReg = builtinReturnsInline ? allocReg() : allocSlot(scalarResultType);
        clearArgRegTypes(argBase, elemResultReg);
        emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
        emitRegs(elemResultReg, 2, argBase);
        emitInt(expr->resolvedFuncGlobalIndex);
        emitReturnPcStackMap(elemResultReg, scalarResultType);
        if (builtinReturnsInline) {
            elemResultReg = emitUnboxIfInline(elemResultReg, scalarResultType);
        }
    } else if (isCompositeNumeric(scalarResultType)) {
        // Composite numeric per-element (e.g. Tuple + Scalar). Phase 4g.8:
        // when the result is Inline, use the inline composite arith op so
        // the multi-word result lives in the destination slot and can be
        // copied straight into the InlineArray.
        if (needsBoxAuto(scalarResultType)) {
            elemResultReg = allocSlot(scalarResultType);
            emitOp(getCompositeArithOpInline(expr->op));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
            emitPtr(scalarResultType);
            emitPtr(leftElemType);
            emitPtr(rightElemType);
        } else {
            // Phase 4g.16: heap-result composite arith reads Inline operands
            // natively via base pointers; no operand boxing needed here.
            elemResultReg = allocReg();
            emitOp(getCompositeArithOp(expr->op));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
            emitPtr(scalarResultType);
            emitPtr(leftElemType);
            emitPtr(rightElemType);
        }
        emitOp(opArraySetFor(resultArrayType->elemType_));
        emitRegs(resultArrReg, iReg, elemResultReg);
        emitPtr(resultArrayType);
        emitOp(op_add_int);
        emitRegs(iReg, iReg, oneReg);
        emitJumpTo(loopStartIdx);
        patchJump(exitJump);
        return resultArrReg;
    } else {
        // Scalar numeric op
        bool isCmp = (expr->op >= BinaryOpExpr::Eq && expr->op <= BinaryOpExpr::Ge);
        if (isCmp) {
            Type* cmpType = compiler_.intType();
            if (leftElemType == compiler_.complexType() || rightElemType == compiler_.complexType())
                cmpType = compiler_.complexType();
            else if (leftElemType == compiler_.floatType() || rightElemType == compiler_.floatType())
                cmpType = compiler_.floatType();
            else if (leftElemType == compiler_.fractionType() || rightElemType == compiler_.fractionType())
                cmpType = compiler_.fractionType();
            leftElemReg = ensureType(leftElemReg, leftElemType, cmpType);
            rightElemReg = ensureType(rightElemReg, rightElemType, cmpType);
            elemResultReg = allocReg();
            emitOp(getCmpOp(expr->op, cmpType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        } else {
            leftElemReg = ensureType(leftElemReg, leftElemType, scalarResultType);
            rightElemReg = ensureType(rightElemReg, rightElemType, scalarResultType);
            elemResultReg = allocReg();
            emitOp(getArithOp(expr->op, scalarResultType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        }
    }

    // --- Phase 8: Store in result array ---
    // Phase 4g.8: InlineArray takes the multi-word inline element directly.
    emitOp(opArraySetFor(resultArrayType->elemType_));
    emitRegs(resultArrReg, iReg, elemResultReg);
    emitPtr(resultArrayType);

    // --- Phase 9: Increment and loop ---
    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);
    emitJumpTo(loopStartIdx);
    patchJump(exitJump);

    return resultArrReg;
}

u16 CodeGen::genAutoMapBinaryOpList(BinaryOpExpr* expr) {
    // List auto-mapped binary op: iterate lists/arrays, build result list
    u16 leftReg = genExpr(static_cast<Expr*>(expr->left.get()));
    u16 rightReg = genExpr(static_cast<Expr*>(expr->right.get()));

    Type* leftType = expr->left->resolvedType;
    Type* rightType = expr->right->resolvedType;

    // Determine element types
    Type* leftElemType = leftType;
    Type* rightElemType = rightType;
    if (expr->leftAutoMap) {
        if (auto* arrT = dynamic_cast<ArrayType*>(leftElemType))
            leftElemType = arrT->elemType_;
        else if (auto* listT = dynamic_cast<ListType*>(leftElemType))
            leftElemType = listT->elemType_;
    }
    if (expr->rightAutoMap) {
        if (auto* arrT = dynamic_cast<ArrayType*>(rightElemType))
            rightElemType = arrT->elemType_;
        else if (auto* listT = dynamic_cast<ListType*>(rightElemType))
            rightElemType = listT->elemType_;
    }

    auto* resultListType = dynamic_cast<ListType*>(expr->resolvedType);
    Type* scalarResultType = resultListType->elemType_;

    // Track list cursors and array index/length for each @-tagged operand
    u16 leftCurReg = 0, rightCurReg = 0;
    u16 leftIdxReg = 0, leftLenReg = 0;
    u16 rightIdxReg = 0, rightLenReg = 0;
    bool leftIsList = expr->leftAutoMap.isList;
    bool rightIsList = expr->rightAutoMap.isList;

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    // Initialize iterators for @-tagged operands
    if (expr->leftAutoMap) {
        if (leftIsList) {
            leftCurReg = allocReg();
            emitMov(leftCurReg, leftReg);
        } else {
            leftIdxReg = allocReg();
            emitOp(op_load_int_const);
            emitRegs(leftIdxReg);
            emitInt(0);
            leftLenReg = allocReg();
            emitOp(opArrayLengthFor(dynamic_cast<ArrayType*>(leftType)->elemType_));
            emitRegs(leftLenReg, leftReg);
            emitPtr(dynamic_cast<ArrayType*>(leftType));
        }
    }
    if (expr->rightAutoMap) {
        if (rightIsList) {
            rightCurReg = allocReg();
            emitMov(rightCurReg, rightReg);
        } else {
            rightIdxReg = allocReg();
            emitOp(op_load_int_const);
            emitRegs(rightIdxReg);
            emitInt(0);
            rightLenReg = allocReg();
            emitOp(opArrayLengthFor(dynamic_cast<ArrayType*>(rightType)->elemType_));
            emitRegs(rightLenReg, rightReg);
            emitPtr(dynamic_cast<ArrayType*>(rightType));
        }
    }

    // Accumulator (reversed result list)
    u16 accReg = allocReg();
    emitOp(op_load_nil);
    emitRegs(accReg);

    // Loop start
    u32 loopStartIdx = (u32)currentBlock_->code.size();

    // Check exhaustion of any @-tagged operand
    u16 nilCheckReg = allocReg();
    u16 anyDoneReg = allocReg();
    bool firstCheck = true;

    auto emitExhaustionCheck = [&](bool isList, u16 curReg, u16 idxReg, u16 lenReg) {
        if (isList) {
            emitOp(op_list_is_nil);
            emitRegs(nilCheckReg, curReg);
        } else {
            // idx >= len means done
            emitOp(op_cmp_lt_int);
            emitRegs(nilCheckReg, idxReg, lenReg);
            emitOp(op_not_bool);
            emitRegs(nilCheckReg, nilCheckReg);
        }
        if (firstCheck) {
            emitMov(anyDoneReg, nilCheckReg);
            firstCheck = false;
        } else {
            emitOp(op_or_bool);
            emitRegs(anyDoneReg, anyDoneReg, nilCheckReg);
        }
    };

    if (expr->leftAutoMap) emitExhaustionCheck(leftIsList, leftCurReg, leftIdxReg, leftLenReg);
    if (expr->rightAutoMap) emitExhaustionCheck(rightIsList, rightCurReg, rightIdxReg, rightLenReg);

    u32 exitJump = emitJump(op_jump_if_true, anyDoneReg);

    // Extract elements
    u16 leftElemReg = leftReg;
    if (expr->leftAutoMap) {
        leftElemReg = allocReg();
        if (leftIsList) {
            emitOp(op_list_head);
            emitRegs(leftElemReg, leftCurReg);
        } else {
            emitOp(opArrayGetDynFor(dynamic_cast<ArrayType*>(leftType)->elemType_));
            emitRegs(leftElemReg, leftReg, leftIdxReg);
            emitPtr(dynamic_cast<ArrayType*>(leftType));
        }
    }

    u16 rightElemReg = rightReg;
    if (expr->rightAutoMap) {
        rightElemReg = allocReg();
        if (rightIsList) {
            emitOp(op_list_head);
            emitRegs(rightElemReg, rightCurReg);
        } else {
            emitOp(opArrayGetDynFor(dynamic_cast<ArrayType*>(rightType)->elemType_));
            emitRegs(rightElemReg, rightReg, rightIdxReg);
            emitPtr(dynamic_cast<ArrayType*>(rightType));
        }
    }

    // Compute per-element result (same logic as genAutoMapBinaryOp Phase 7)
    u16 elemResultReg;
    if (expr->resolvedFuncGlobalIndex >= 0) {
        u16 argBase = nextReg_;
        if (leftElemReg != argBase) { emitMov(argBase, leftElemReg); }
        if (nextReg_ <= argBase) { nextReg_ = argBase + 1; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        if (rightElemReg != argBase + 1) { emitMov(argBase + 1, rightElemReg); }
        if (nextReg_ <= argBase + 1) { nextReg_ = argBase + 2; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        elemResultReg = allocReg();
        clearArgRegTypes(argBase, elemResultReg);
        emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
        emitRegs(elemResultReg, 2, argBase);
        emitInt(expr->resolvedFuncGlobalIndex);
        emitReturnPcStackMap(elemResultReg, scalarResultType);
    } else if (isCompositeNumeric(scalarResultType)) {
        elemResultReg = allocReg();
        emitOp(getCompositeArithOp(expr->op));
        emitRegs(elemResultReg, leftElemReg, rightElemReg);
        emitPtr(scalarResultType);
        emitPtr(leftElemType);
        emitPtr(rightElemType);
    } else {
        bool isCmp = (expr->op >= BinaryOpExpr::Eq && expr->op <= BinaryOpExpr::Ge);
        if (isCmp) {
            Type* cmpType = compiler_.intType();
            if (leftElemType == compiler_.complexType() || rightElemType == compiler_.complexType())
                cmpType = compiler_.complexType();
            else if (leftElemType == compiler_.floatType() || rightElemType == compiler_.floatType())
                cmpType = compiler_.floatType();
            else if (leftElemType == compiler_.fractionType() || rightElemType == compiler_.fractionType())
                cmpType = compiler_.fractionType();
            leftElemReg = ensureType(leftElemReg, leftElemType, cmpType);
            rightElemReg = ensureType(rightElemReg, rightElemType, cmpType);
            elemResultReg = allocReg();
            emitOp(getCmpOp(expr->op, cmpType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        } else {
            leftElemReg = ensureType(leftElemReg, leftElemType, scalarResultType);
            rightElemReg = ensureType(rightElemReg, rightElemType, scalarResultType);
            elemResultReg = allocReg();
            emitOp(getArithOp(expr->op, scalarResultType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        }
    }

    // Cons onto accumulator
    emitOp(op_cons);
    emitRegs(accReg, elemResultReg, accReg);
    emitPtr(resultListType);

    // Advance iterators
    if (expr->leftAutoMap) {
        if (leftIsList) {
            emitOp(op_list_tail);
            emitRegs(leftCurReg, leftCurReg);
        } else {
            emitOp(op_add_int);
            emitRegs(leftIdxReg, leftIdxReg, oneReg);
        }
    }
    if (expr->rightAutoMap) {
        if (rightIsList) {
            emitOp(op_list_tail);
            emitRegs(rightCurReg, rightCurReg);
        } else {
            emitOp(op_add_int);
            emitRegs(rightIdxReg, rightIdxReg, oneReg);
        }
    }

    // Loop back
    emitJumpTo(loopStartIdx);
    patchJump(exitJump);

    // Reverse the accumulated list
    u16 resultReg = allocReg();
    emitOp(op_load_nil);
    emitRegs(resultReg);

    u32 revLoopStart = (u32)currentBlock_->code.size();
    u16 revNilCheck = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(revNilCheck, accReg);
    u32 revExitJump = emitJump(op_jump_if_true, revNilCheck);

    u16 revHead = allocReg();
    emitOp(op_list_head);
    emitRegs(revHead, accReg);
    emitOp(op_cons);
    emitRegs(resultReg, revHead, resultReg);
    emitPtr(resultListType);
    emitOp(op_list_tail);
    emitRegs(accReg, accReg);
    emitJumpTo(revLoopStart);
    patchJump(revExitJump);

    return resultReg;
}

u16 CodeGen::genCartesianBinaryOp(BinaryOpExpr* expr) {
    // Cartesian product: @1 and @2 on operands create nested loops
    u16 leftReg = genExpr(static_cast<Expr*>(expr->left.get()));
    u16 rightReg = genExpr(static_cast<Expr*>(expr->right.get()));

    Type* leftType = expr->left->resolvedType;
    Type* rightType = expr->right->resolvedType;

    // Determine element types
    Type* leftElemType = leftType;
    Type* rightElemType = rightType;
    if (expr->leftAutoMap) {
        if (auto* arrT = dynamic_cast<ArrayType*>(leftElemType))
            leftElemType = arrT->elemType_;
    }
    if (expr->rightAutoMap) {
        if (auto* arrT = dynamic_cast<ArrayType*>(rightElemType))
            rightElemType = arrT->elemType_;
    }

    int maxCartesian = std::max(expr->leftAutoMap.cartesianIndex, expr->rightAutoMap.cartesianIndex);

    // Get lengths for each cartesian level
    std::vector<u16> lenRegs(maxCartesian + 1, 0);
    auto assignLen = [&](int ci, u16 arrReg, Type* arrType) {
        if (ci > 0 && lenRegs[ci] == 0) {
            auto* at = dynamic_cast<ArrayType*>(arrType);
            u16 lenReg = allocReg();
            emitOp(opArrayLengthFor(at->elemType_));
            emitRegs(lenReg, arrReg);
            emitPtr(at);
            lenRegs[ci] = lenReg;
        }
    };
    if (expr->leftAutoMap.cartesianIndex > 0) assignLen(expr->leftAutoMap.cartesianIndex, leftReg, leftType);
    if (expr->rightAutoMap.cartesianIndex > 0) assignLen(expr->rightAutoMap.cartesianIndex, rightReg, rightType);

    // Loop counters
    std::vector<u16> iRegs(maxCartesian + 1);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    for (int level = 1; level <= maxCartesian; ++level) {
        iRegs[level] = allocReg();
        emitOp(op_load_int_const);
        emitRegs(iRegs[level]);
        emitInt(0);
    }

    std::vector<u16> condRegs(maxCartesian + 1);
    for (int level = 1; level <= maxCartesian; ++level) {
        condRegs[level] = allocReg();
    }

    // Allocate outer result array
    auto* outerResultType = dynamic_cast<ArrayType*>(expr->resolvedType);
    u16 outerResultReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(outerResultReg, lenRegs[1]);
    emitPtr(outerResultType);

    // Level 1 (outer) loop
    u32 loop1StartIdx = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(condRegs[1], iRegs[1], lenRegs[1]);
    u32 loop1ExitJump = emitJump(op_jump_if_false, condRegs[1]);

    u16 innerResultReg = 0;
    ArrayType* innerResultType = nullptr;
    u32 loop2StartIdx = 0, loop2ExitJump = 0;

    if (maxCartesian >= 2) {
        innerResultType = dynamic_cast<ArrayType*>(outerResultType->elemType_);
        innerResultReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(innerResultReg, lenRegs[2]);
        emitPtr(innerResultType);

        emitOp(op_load_int_const);
        emitRegs(iRegs[2]);
        emitInt(0);

        loop2StartIdx = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int);
        emitRegs(condRegs[2], iRegs[2], lenRegs[2]);
        loop2ExitJump = emitJump(op_jump_if_false, condRegs[2]);
    }

    // Phase 4g.19: Inline composite elements (e.g. Tuple(Int,Int)) occupy
    // multiple register words. allocReg() reserves only one Word and would
    // overlap left/right slots, so use allocElemSlot for inline composites.
    auto needsBoxAuto = [&](Type* t) {
        return t && t->repr_ == ts::Type::Repr::Inline
            && t != compiler_.complexType()
            && t != compiler_.fractionType();
    };
    auto allocElemSlot = [&](Type* t) {
        u16 nw = typeSlotWords(t);
        u16 r = nextReg_;
        nextReg_ = (u16)(nextReg_ + nw);
        if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        return r;
    };

    // Extract elements
    u16 leftElemReg = leftReg;
    if (expr->leftAutoMap.cartesianIndex > 0) {
        leftElemReg = allocElemSlot(leftElemType);
        emitOp(opArrayGetDynFor(dynamic_cast<ArrayType*>(leftType)->elemType_));
        emitRegs(leftElemReg, leftReg, iRegs[expr->leftAutoMap.cartesianIndex]);
        emitPtr(dynamic_cast<ArrayType*>(leftType));
    } else if (expr->leftAutoMap.depth > 0) {
        // Plain @ with cartesian context — zip with level 1
        leftElemReg = allocElemSlot(leftElemType);
        emitOp(opArrayGetDynFor(dynamic_cast<ArrayType*>(leftType)->elemType_));
        emitRegs(leftElemReg, leftReg, iRegs[1]);
        emitPtr(dynamic_cast<ArrayType*>(leftType));
    }

    u16 rightElemReg = rightReg;
    if (expr->rightAutoMap.cartesianIndex > 0) {
        rightElemReg = allocElemSlot(rightElemType);
        emitOp(opArrayGetDynFor(dynamic_cast<ArrayType*>(rightType)->elemType_));
        emitRegs(rightElemReg, rightReg, iRegs[expr->rightAutoMap.cartesianIndex]);
        emitPtr(dynamic_cast<ArrayType*>(rightType));
    } else if (expr->rightAutoMap.depth > 0) {
        rightElemReg = allocElemSlot(rightElemType);
        emitOp(opArrayGetDynFor(dynamic_cast<ArrayType*>(rightType)->elemType_));
        emitRegs(rightElemReg, rightReg, iRegs[1]);
        emitPtr(dynamic_cast<ArrayType*>(rightType));
    }

    // Compute per-element result
    Type* scalarResultType = (maxCartesian >= 2 && innerResultType)
        ? innerResultType->elemType_
        : outerResultType->elemType_;
    u16 elemResultReg;

    if (expr->resolvedFuncGlobalIndex >= 0) {
        // Operator overload call. Place each operand at its multi-word slot
        // offset for non-builtin calls; for builtins box Inline composite
        // operands first so the legacy 1-Word ABI sees a heap pointer.
        u16 argBase = nextReg_;
        u32 lWords = emitArgPlacementForCall(argBase, leftElemReg, leftElemType,
                                             expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        u16 rDst = (u16)(argBase + lWords);
        u32 rWords = emitArgPlacementForCall(rDst, rightElemReg, rightElemType,
                                             expr->isBuiltinCall, expr->builtinAcceptsInlineArgs);
        u16 next = (u16)(rDst + rWords);
        if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        bool builtinReturnsInline = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs
                                     && needsBoxAuto(scalarResultType);
        elemResultReg = builtinReturnsInline ? allocReg() : allocSlot(scalarResultType);
        clearArgRegTypes(argBase, elemResultReg);
        emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
        emitRegs(elemResultReg, 2, argBase);
        emitInt(expr->resolvedFuncGlobalIndex);
        emitReturnPcStackMap(elemResultReg, scalarResultType);
        if (builtinReturnsInline) {
            elemResultReg = emitUnboxIfInline(elemResultReg, scalarResultType);
        }
    } else {
        bool isCmp = (expr->op >= BinaryOpExpr::Eq && expr->op <= BinaryOpExpr::Ge);
        if (isCmp) {
            Type* cmpType = compiler_.intType();
            if (leftElemType == compiler_.complexType() || rightElemType == compiler_.complexType())
                cmpType = compiler_.complexType();
            else if (leftElemType == compiler_.floatType() || rightElemType == compiler_.floatType())
                cmpType = compiler_.floatType();
            else if (leftElemType == compiler_.fractionType() || rightElemType == compiler_.fractionType())
                cmpType = compiler_.fractionType();
            leftElemReg = ensureType(leftElemReg, leftElemType, cmpType);
            rightElemReg = ensureType(rightElemReg, rightElemType, cmpType);
            elemResultReg = allocReg();
            emitOp(getCmpOp(expr->op, cmpType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        } else if (isCompositeNumeric(scalarResultType)) {
            if (needsBoxAuto(scalarResultType)) {
                // Inline-result composite arith writes directly into the
                // multi-word destination slot (no heap allocation per elem).
                elemResultReg = allocSlot(scalarResultType);
                emitOp(getCompositeArithOpInline(expr->op));
                emitRegs(elemResultReg, leftElemReg, rightElemReg);
                emitPtr(scalarResultType);
                emitPtr(leftElemType);
                emitPtr(rightElemType);
            } else {
                elemResultReg = allocReg();
                emitOp(getCompositeArithOp(expr->op));
                emitRegs(elemResultReg, leftElemReg, rightElemReg);
                emitPtr(scalarResultType);
                emitPtr(leftElemType);
                emitPtr(rightElemType);
            }
        } else {
            leftElemReg = ensureType(leftElemReg, leftElemType, scalarResultType);
            rightElemReg = ensureType(rightElemReg, rightElemType, scalarResultType);
            elemResultReg = allocReg();
            emitOp(getArithOp(expr->op, scalarResultType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        }
    }

    // Store and close loops
    if (maxCartesian >= 2) {
        emitOp(opArraySetFor(innerResultType->elemType_));
        emitRegs(innerResultReg, iRegs[2], elemResultReg);
        emitPtr(innerResultType);

        emitOp(op_add_int);
        emitRegs(iRegs[2], iRegs[2], oneReg);
        emitJumpTo(loop2StartIdx);
        patchJump(loop2ExitJump);

        emitOp(opArraySetFor(outerResultType->elemType_));
        emitRegs(outerResultReg, iRegs[1], innerResultReg);
        emitPtr(outerResultType);
    } else {
        emitOp(opArraySetFor(outerResultType->elemType_));
        emitRegs(outerResultReg, iRegs[1], elemResultReg);
        emitPtr(outerResultType);
    }

    emitOp(op_add_int);
    emitRegs(iRegs[1], iRegs[1], oneReg);
    emitJumpTo(loop1StartIdx);
    patchJump(loop1ExitJump);

    return outerResultReg;
}

u16 CodeGen::genDeepMapBinaryOp(BinaryOpExpr* expr, int depth) {
    // Deep map (@@, @@@, etc.) for binary ops
    u16 leftReg = genExpr(static_cast<Expr*>(expr->left.get()));
    u16 rightReg = genExpr(static_cast<Expr*>(expr->right.get()));

    Type* leftType = expr->left->resolvedType;
    Type* rightType = expr->right->resolvedType;

    // Determine which operands are deep-mapped
    bool leftDeep = (expr->leftAutoMap.depth > 1);
    bool rightDeep = (expr->rightAutoMap.depth > 1);

    // Determine the primary deep operand (to drive loop lengths)
    bool primaryIsLeft = leftDeep;

    // Determine element types at the innermost level
    Type* leftElemType = leftType;
    Type* rightElemType = rightType;
    if (expr->leftAutoMap) {
        Type* t = leftType;
        for (int d = 0; d < expr->leftAutoMap.depth; ++d) {
            if (auto* arrT = dynamic_cast<ArrayType*>(t)) t = arrT->elemType_;
        }
        leftElemType = t;
    }
    if (expr->rightAutoMap) {
        Type* t = rightType;
        for (int d = 0; d < expr->rightAutoMap.depth; ++d) {
            if (auto* arrT = dynamic_cast<ArrayType*>(t)) t = arrT->elemType_;
        }
        rightElemType = t;
    }

    // Constants
    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    // Build nested loops for outer depth levels (depth-1 loops)
    struct LoopLevel {
        u16 lenReg, iReg, condReg, resultReg;
        u32 loopStartIdx, exitJump;
        ArrayType* resultType;
    };

    std::vector<LoopLevel> loops(depth - 1);
    u16 currentLeftReg = leftReg;
    u16 currentRightReg = rightReg;
    Type* curResultType = expr->resolvedType;

    for (int d = 0; d < depth - 1; ++d) {
        auto& loop = loops[d];

        // Get array type at this depth for the primary operand
        Type* primaryType = primaryIsLeft ? leftType : rightType;
        for (int level = 0; level < d; ++level) {
            primaryType = dynamic_cast<ArrayType*>(primaryType)->elemType_;
        }
        auto* curArrType = dynamic_cast<ArrayType*>(primaryType);
        u16 primaryReg = primaryIsLeft ? currentLeftReg : currentRightReg;

        loop.lenReg = allocReg();
        emitOp(opArrayLengthFor(curArrType->elemType_));
        emitRegs(loop.lenReg, primaryReg);
        emitPtr(curArrType);

        loop.resultType = dynamic_cast<ArrayType*>(curResultType);
        loop.resultReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(loop.resultReg, loop.lenReg);
        emitPtr(loop.resultType);

        loop.iReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(loop.iReg);
        emitInt(0);

        loop.condReg = allocReg();

        loop.loopStartIdx = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int);
        emitRegs(loop.condReg, loop.iReg, loop.lenReg);
        loop.exitJump = emitJump(op_jump_if_false, loop.condReg);

        // Extract sub-arrays from deep-mapped operands
        if (leftDeep) {
            Type* lt = leftType;
            for (int level = 0; level < d; ++level) lt = dynamic_cast<ArrayType*>(lt)->elemType_;
            auto* lArrT = dynamic_cast<ArrayType*>(lt);
            u16 subReg = allocReg();
            emitOp(opArrayGetDynFor(lArrT->elemType_));
            emitRegs(subReg, currentLeftReg, loop.iReg);
            emitPtr(lArrT);
            currentLeftReg = subReg;
        }
        if (rightDeep) {
            Type* rt = rightType;
            for (int level = 0; level < d; ++level) rt = dynamic_cast<ArrayType*>(rt)->elemType_;
            auto* rArrT = dynamic_cast<ArrayType*>(rt);
            u16 subReg = allocReg();
            emitOp(opArrayGetDynFor(rArrT->elemType_));
            emitRegs(subReg, currentRightReg, loop.iReg);
            emitPtr(rArrT);
            currentRightReg = subReg;
        }

        curResultType = loop.resultType->elemType_;
    }

    // Innermost auto-map loop (standard depth=1)
    auto* innerResultArrayType = dynamic_cast<ArrayType*>(curResultType);

    // Compute min length at innermost level
    u16 minLenReg = 0;
    bool firstAutoMap = true;

    auto addMinLen = [&](u16 arrReg, Type* fullType, int amDepth) {
        Type* curArgType = fullType;
        if (amDepth > 1) {
            for (int level = 0; level < depth - 1; ++level)
                curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
        }
        auto* arrType = dynamic_cast<ArrayType*>(curArgType);
        if (!arrType) return;
        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrType->elemType_));
        emitRegs(lenReg, arrReg);
        emitPtr(arrType);
        if (firstAutoMap) {
            minLenReg = lenReg;
            firstAutoMap = false;
        } else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skipJump);
        }
    };

    if (expr->leftAutoMap) addMinLen(currentLeftReg, leftType, expr->leftAutoMap.depth);
    if (expr->rightAutoMap) addMinLen(currentRightReg, rightType, expr->rightAutoMap.depth);

    u16 innerResultReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(innerResultReg, minLenReg);
    emitPtr(innerResultArrayType);

    u16 innerIReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(innerIReg);
    emitInt(0);

    u16 innerCondReg = allocReg();

    u32 innerLoopStart = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(innerCondReg, innerIReg, minLenReg);
    u32 innerExitJump = emitJump(op_jump_if_false, innerCondReg);

    // Extract innermost elements
    u16 leftElemReg = currentLeftReg;
    if (expr->leftAutoMap) {
        Type* curArgType = leftType;
        if (expr->leftAutoMap.depth > 1) {
            for (int level = 0; level < depth - 1; ++level)
                curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
        }
        auto* arrType = dynamic_cast<ArrayType*>(curArgType);
        leftElemReg = allocReg();
        emitOp(opArrayGetDynFor(arrType->elemType_));
        emitRegs(leftElemReg, currentLeftReg, innerIReg);
        emitPtr(arrType);
    }

    u16 rightElemReg = currentRightReg;
    if (expr->rightAutoMap) {
        Type* curArgType = rightType;
        if (expr->rightAutoMap.depth > 1) {
            for (int level = 0; level < depth - 1; ++level)
                curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
        }
        auto* arrType = dynamic_cast<ArrayType*>(curArgType);
        rightElemReg = allocReg();
        emitOp(opArrayGetDynFor(arrType->elemType_));
        emitRegs(rightElemReg, currentRightReg, innerIReg);
        emitPtr(arrType);
    }

    // Per-element op
    Type* scalarResultType = innerResultArrayType->elemType_;
    u16 elemResultReg;

    if (expr->resolvedFuncGlobalIndex >= 0) {
        u16 argBase = nextReg_;
        if (leftElemReg != argBase) { emitMov(argBase, leftElemReg); }
        if (nextReg_ <= argBase) { nextReg_ = argBase + 1; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        if (rightElemReg != argBase + 1) { emitMov(argBase + 1, rightElemReg); }
        if (nextReg_ <= argBase + 1) { nextReg_ = argBase + 2; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
        elemResultReg = allocReg();
        clearArgRegTypes(argBase, elemResultReg);
        emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
        emitRegs(elemResultReg, 2, argBase);
        emitInt(expr->resolvedFuncGlobalIndex);
        emitReturnPcStackMap(elemResultReg, scalarResultType);
    } else {
        bool isCmp = (expr->op >= BinaryOpExpr::Eq && expr->op <= BinaryOpExpr::Ge);
        if (isCmp) {
            Type* cmpType = compiler_.intType();
            if (leftElemType == compiler_.complexType() || rightElemType == compiler_.complexType())
                cmpType = compiler_.complexType();
            else if (leftElemType == compiler_.floatType() || rightElemType == compiler_.floatType())
                cmpType = compiler_.floatType();
            else if (leftElemType == compiler_.fractionType() || rightElemType == compiler_.fractionType())
                cmpType = compiler_.fractionType();
            leftElemReg = ensureType(leftElemReg, leftElemType, cmpType);
            rightElemReg = ensureType(rightElemReg, rightElemType, cmpType);
            elemResultReg = allocReg();
            emitOp(getCmpOp(expr->op, cmpType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        } else if (isCompositeNumeric(scalarResultType)) {
            elemResultReg = allocReg();
            emitOp(getCompositeArithOp(expr->op));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
            emitPtr(scalarResultType);
            emitPtr(leftElemType);
            emitPtr(rightElemType);
        } else {
            leftElemReg = ensureType(leftElemReg, leftElemType, scalarResultType);
            rightElemReg = ensureType(rightElemReg, rightElemType, scalarResultType);
            elemResultReg = allocReg();
            emitOp(getArithOp(expr->op, scalarResultType));
            emitRegs(elemResultReg, leftElemReg, rightElemReg);
        }
    }

    // Store in innermost result
    emitOp(opArraySetFor(innerResultArrayType->elemType_));
    emitRegs(innerResultReg, innerIReg, elemResultReg);
    emitPtr(innerResultArrayType);

    emitOp(op_add_int);
    emitRegs(innerIReg, innerIReg, oneReg);
    emitJumpTo(innerLoopStart);
    patchJump(innerExitJump);

    // Close outer loops
    u16 prevResultReg = innerResultReg;
    for (int d = depth - 2; d >= 0; --d) {
        auto& loop = loops[d];
        emitOp(opArraySetFor(loop.resultType->elemType_));
        emitRegs(loop.resultReg, loop.iReg, prevResultReg);
        emitPtr(loop.resultType);

        emitOp(op_add_int);
        emitRegs(loop.iReg, loop.iReg, oneReg);
        emitJumpTo(loop.loopStartIdx);
        patchJump(loop.exitJump);

        prevResultReg = loop.resultReg;
    }

    return prevResultReg;
}

u16 CodeGen::genAutoMapCall(CallExpr_* expr) {
    auto* ident = static_cast<IdentifierExpr*>(expr->callee.get());

    // Look up function info for parameter types (needed for element type promotion)
    const FuncInfo* funcInfo = nullptr;
    auto funcIt = typeChecker_.functions().find(ident->name);
    if (funcIt != typeChecker_.functions().end()) {
        for (const auto& fi : funcIt->second) {
            if ((i32)fi.globalIndex == expr->resolvedFuncGlobalIndex) {
                funcInfo = &fi;
                break;
            }
        }
    }
    if (!funcInfo) {
        error(expr->loc, "Codegen: auto-map function not found");
        return allocReg();
    }

    // Check if any auto-mapped arg is a List
    for (size_t i = 0; i < expr->args.size(); ++i) {
        if (expr->autoMapArgs[i] && expr->autoMapArgs[i].isList) {
            // If the function returns Void, eagerly iterate the list for side effects
            if (funcInfo->returnType == compiler_.voidType()) {
                return genAutoMapCallListVoid(expr, funcInfo);
            }
            return genAutoMapCallList(expr, funcInfo);
        }
    }

    u16 argc = (u16)expr->args.size();

    // --- Phase 1: Evaluate all argument expressions (before the loop) ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    // --- Phase 2: Compute min length of auto-mapped arrays ---
    u16 minLenReg = 0;
    bool firstAutoMap = true;

    for (size_t i = 0; i < argc; ++i) {
        if (!expr->autoMapArgs[i]) continue;

        // For explicit @, the arg may be an AutoMapExpr wrapping the real expression.
        // The actual array is already generated in argRegs[i].
        auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);

        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrType->elemType_));
        emitRegs(lenReg, argRegs[i]);
        emitPtr(arrType);

        if (firstAutoMap) {
            minLenReg = lenReg;
            firstAutoMap = false;
        } else {
            // minLenReg = min(minLenReg, lenReg)
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skipJump);
        }
    }

    // Check if the function returns Void (side-effects only, no result collection)
    bool isVoidReturn = (funcInfo->returnType == compiler_.voidType());

    // --- Phase 3: Allocate result array (skip for Void) ---
    auto* resultArrayType = isVoidReturn ? nullptr : dynamic_cast<ArrayType*>(expr->resolvedType);
    u16 resultArrReg = 0;
    if (!isVoidReturn) {
        resultArrReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(resultArrReg, minLenReg);
        emitPtr(resultArrayType);
    }

    // --- Phase 4: Loop counter setup ---
    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    u16 condReg = allocReg();

    // --- Phase 5: Loop start ---
    // All registers allocated above (argRegs, minLenReg, resultArrReg, iReg,
    // oneReg, condReg) are BEFORE callArgBase, so they are safe from being
    // clobbered by the callee's register window.
    u32 loopStartIdx = (u32)currentBlock_->code.size();

    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, minLenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // --- Phase 6: Set up call arguments ---
    // callArgBase starts at nextReg_ — callee's frame begins here
    u16 callArgBase = nextReg_;

    // Phase 4g.2: track per-arg slot widths so multi-word inline args don't
    // collide with following args. For builtin calls, Inline composites are
    // boxed (1 word) per the builtin calling convention; for non-builtin
    // calls Inline composites pass multi-word inline.
    std::vector<u32> argSlotWords;
    argSlotWords.reserve(argc);
    auto callerSlotWords = [&](Type* paramType, u16 i) -> u32 {
        // Variadic-packed args are placed 1-word per arg pre-pack;
        // op_make_tuple/op_make_array bundles them.
        if (expr->variadicPackStart >= 0 && i >= (u16)expr->variadicPackStart) {
            return 1;
        }
        if (!paramType) return 1;
        if (paramType->repr_ == ts::Type::Repr::Inline
            && paramType != compiler_.complexType()
            && paramType != compiler_.fractionType()) {
            // Phase 4g.6: legacy builtins receive a 1-word boxed Obj* for
            // inline composites; migrated ones receive multi-word inline.
            bool boxedAtBoundary = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs;
            return boxedAtBoundary ? 1u : (u32)paramType->sizeWords_;
        }
        return typeSlotWords(paramType);
    };
    u32 cumOffset = 0;
    std::vector<u32> argOffsets;
    argOffsets.reserve(argc);
    for (u16 i = 0; i < argc; ++i) {
        argOffsets.push_back(cumOffset);
        cumOffset += callerSlotWords(getParamType(funcInfo, expr, i), i);
    }
    for (u16 i = 0; i < argc; ++i) {
        u16 targetReg = (u16)(callArgBase + argOffsets[i]);
        Type* paramType = getParamType(funcInfo, expr, i);
        u32 slotW = callerSlotWords(paramType, i);
        // Ensure registers are tracked
        if (nextReg_ <= (u16)(targetReg + slotW)) {
            nextReg_ = (u16)(targetReg + slotW);
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        if (expr->autoMapArgs[i]) {
            auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            Type* elemType = arrType->elemType_;
            bool elemInlineComposite = elemType
                && elemType->repr_ == ts::Type::Repr::Inline
                && elemType != compiler_.complexType()
                && elemType != compiler_.fractionType();
            // Phase 4g.8: InlineArray returns the inline element multi-word
            // directly into a sizeWords_-wide slot. For legacy builtins that
            // still take a 1-word boxed pointer, box the unboxed element.
            u16 elemReg = elemInlineComposite ? allocSlot(elemType) : targetReg;
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(elemReg, argRegs[i], iReg);
            emitPtr(arrType);
            if (elemInlineComposite) {
                bool boxedAtBoundary = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs;
                if (boxedAtBoundary) {
                    u16 boxed = emitBoxIfInline(elemReg, elemType);
                    if (boxed != targetReg) { emitMov(targetReg, boxed); }
                } else {
                    if (elemReg != targetReg) {
                        emitMoveN(targetReg, elemReg, slotW);
                    }
                }
            }

            // Promote element type to parameter type if needed
            if (paramType && elemType != paramType) {
                u16 promoted = ensureType(targetReg, elemType, paramType);
                if (promoted != targetReg) {
                    emitMoveN(targetReg, promoted, slotW);
                }
            }
        } else {
            // Non-auto-mapped: copy scalar/inline value to call arg position
            Type* argType = expr->args[i]->resolvedType;
            u16 srcReg = argRegs[i];
            if (paramType && argType != paramType) {
                srcReg = ensureType(srcReg, argType, paramType);
            }
            if (paramType
                && paramType->repr_ == ts::Type::Repr::Inline
                && paramType != compiler_.complexType()
                && paramType != compiler_.fractionType()
                && expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs) {
                srcReg = emitBoxIfInline(srcReg, paramType);
            }
            if (srcReg != targetReg) {
                if (slotW <= 1) {
                    emitMov(targetReg, srcReg);
                } else {
                    emitMoveN(targetReg, srcReg, slotW);
                }
            }
        }
    }

    // --- Phase 7: Variadic packing + Call function ---
    u16 callArgc = emitVariadicPack(expr, callArgBase, argc);
    // The per-call return type. funcInfo->returnType may be null for some
    // untyped-variadic monomorphizations; in that case fall back to the
    // resolved result-array's element type.
    Type* returnT = funcInfo->returnType;
    if (!returnT && resultArrayType) returnT = resultArrayType->elemType_;
    bool builtinReturnsInlineComposite = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs && returnT
        && returnT->repr_ == ts::Type::Repr::Inline
        && returnT != compiler_.complexType()
        && returnT != compiler_.fractionType();
    u16 callResultReg = builtinReturnsInlineComposite ? allocReg() : allocSlot(returnT);
    clearArgRegTypes(callArgBase, callResultReg);
    emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
    emitRegs(callResultReg, callArgc, callArgBase);
    emitInt(expr->resolvedFuncGlobalIndex);
    emitReturnPcStackMap(callResultReg, returnT);
    if (builtinReturnsInlineComposite) {
        callResultReg = emitUnboxIfInline(callResultReg, returnT);
    }

    // --- Phase 8: Store result in result array (skip for Void) ---
    // Phase 4g.8: InlineArray takes the multi-word inline result directly.
    if (!isVoidReturn) {
        emitOp(opArraySetFor(resultArrayType->elemType_));
        emitRegs(resultArrReg, iReg, callResultReg);
        emitPtr(resultArrayType);
    }

    // --- Phase 9: Increment and loop ---
    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);

    emitJumpTo(loopStartIdx);

    patchJump(exitJump);

    return isVoidReturn ? callResultReg : resultArrReg;
}

u16 CodeGen::genAutoMapLambdaCall(CallExpr_* expr, u16 calleeReg, FunctionType* funcType) {
    u16 argc = (u16)expr->args.size();
    bool isVoidReturn = (funcType->returnType_ == compiler_.voidType());

    // Phase 4g.21: if any auto-mapped arg is a List, the rest of this function
    // (which only handles Array auto-map) would crash at codegen. Route to a
    // list-aware eager loop instead. Multi-arg lambdas with List args fall
    // back to this path too.
    bool anyListArg = false;
    for (size_t i = 0; i < argc; ++i) {
        if (expr->autoMapArgs[i] && expr->autoMapArgs[i].isList) {
            anyListArg = true;
            break;
        }
    }
    if (anyListArg) {
        return genAutoMapLambdaCallList(expr, calleeReg, funcType);
    }

    // Determine the max auto-map depth
    int depth = 0;
    for (auto& am : expr->autoMapArgs) {
        if (am.depth > depth) depth = am.depth;
    }

    // --- Phase 1: Evaluate all argument expressions (before any loops) ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    // Shared constant for incrementing loop counters
    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    // --- Outer loops: peel (depth - 1) array layers ---
    // Find which args are deep-mapped (depth > 1)
    std::vector<int> deepArgs;
    for (size_t i = 0; i < argc; ++i) {
        if (expr->autoMapArgs[i].depth > 1) {
            deepArgs.push_back((int)i);
        }
    }
    int primaryArg = deepArgs.empty() ? 0 : deepArgs[0];

    struct LoopLevel {
        u16 lenReg, iReg, condReg, resultReg;
        u32 loopStartIdx, exitJump;
        ArrayType* resultType;
    };

    std::vector<LoopLevel> outerLoops(depth > 1 ? depth - 1 : 0);
    std::vector<u16> currentArgRegs = argRegs;
    Type* curResultType = expr->resolvedType;

    for (int d = 0; d < depth - 1; ++d) {
        auto& loop = outerLoops[d];

        // Compute the array type at this depth level for the primary arg
        Type* curArgType = expr->args[primaryArg]->resolvedType;
        for (int level = 0; level < d; ++level) {
            curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
        }
        auto* curArrType = dynamic_cast<ArrayType*>(curArgType);

        loop.lenReg = allocReg();
        emitOp(opArrayLengthFor(curArrType->elemType_));
        emitRegs(loop.lenReg, currentArgRegs[primaryArg]);
        emitPtr(curArrType);

        // Allocate result array for this level (skip for Void)
        if (!isVoidReturn) {
            loop.resultType = dynamic_cast<ArrayType*>(curResultType);
            loop.resultReg = allocReg();
            emitOp(op_array_alloc);
            emitRegs(loop.resultReg, loop.lenReg);
            emitPtr(loop.resultType);
        }

        loop.iReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(loop.iReg);
        emitInt(0);

        loop.condReg = allocReg();

        loop.loopStartIdx = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int);
        emitRegs(loop.condReg, loop.iReg, loop.lenReg);
        loop.exitJump = emitJump(op_jump_if_false, loop.condReg);

        // Extract sub-arrays from each deep-mapped arg
        for (int ai : deepArgs) {
            Type* argType = expr->args[ai]->resolvedType;
            for (int level = 0; level < d; ++level) {
                argType = dynamic_cast<ArrayType*>(argType)->elemType_;
            }
            auto* argArrType = dynamic_cast<ArrayType*>(argType);

            u16 subReg = allocReg();
            emitOp(opArrayGetDynFor(argArrType->elemType_));
            emitRegs(subReg, currentArgRegs[ai], loop.iReg);
            emitPtr(argArrType);
            currentArgRegs[ai] = subReg;
        }

        if (!isVoidReturn) {
            curResultType = loop.resultType->elemType_;
        }
    }

    // --- Innermost loop: standard auto-map with op_call_lambda ---

    // Compute min length of innermost auto-mapped arrays
    u16 minLenReg = 0;
    bool firstAutoMap = true;

    for (size_t i = 0; i < argc; ++i) {
        if (!expr->autoMapArgs[i]) continue;

        // Determine the array type at the innermost level
        Type* curArgType = expr->args[i]->resolvedType;
        if (expr->autoMapArgs[i].depth > 1) {
            for (int level = 0; level < depth - 1; ++level) {
                curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
            }
        }
        auto* arrType = dynamic_cast<ArrayType*>(curArgType);
        if (!arrType) continue;

        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrType->elemType_));
        emitRegs(lenReg, currentArgRegs[i]);
        emitPtr(arrType);

        if (firstAutoMap) {
            minLenReg = lenReg;
            firstAutoMap = false;
        } else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skipJump);
        }
    }

    // Allocate innermost result array (skip for Void)
    auto* innerResultArrayType = isVoidReturn ? nullptr : dynamic_cast<ArrayType*>(curResultType);
    u16 innerResultReg = 0;
    if (!isVoidReturn) {
        innerResultReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(innerResultReg, minLenReg);
        emitPtr(innerResultArrayType);
    }

    // Inner loop counter
    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 condReg = allocReg();

    // Inner loop start
    u32 loopStartIdx = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, minLenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // Set up call arguments
    u16 callArgBase = nextReg_;

    for (u16 i = 0; i < argc; ++i) {
        u16 targetReg = callArgBase + i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        if (expr->autoMapArgs[i]) {
            Type* curArgType = expr->args[i]->resolvedType;
            if (expr->autoMapArgs[i].depth > 1) {
                for (int level = 0; level < depth - 1; ++level) {
                    curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
                }
            }
            auto* arrType = dynamic_cast<ArrayType*>(curArgType);

            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(targetReg, currentArgRegs[i], iReg);
            emitPtr(arrType);

            // Promote element type to parameter type if needed
            Type* elemType = arrType->elemType_;
            Type* paramType = (i < funcType->argTypes_.size()) ? funcType->argTypes_[i] : nullptr;
            if (paramType && elemType != paramType) {
                u16 promoted = ensureType(targetReg, elemType, paramType);
                if (promoted != targetReg) {
                    emitMov(targetReg, promoted);
                }
            }
        } else {
            Type* argType = expr->args[i]->resolvedType;
            Type* paramType = (i < funcType->argTypes_.size()) ? funcType->argTypes_[i] : nullptr;
            u16 srcReg = currentArgRegs[i];
            if (paramType && argType != paramType) {
                srcReg = ensureType(srcReg, argType, paramType);
            }
            if (srcReg != targetReg) {
                emitMov(targetReg, srcReg);
            }
        }
    }

    // Call lambda
    u16 callResultReg = allocReg();
    clearArgRegTypes(callArgBase, callResultReg);
    emitOp(op_call_lambda);
    emitRegs(callResultReg, argc, callArgBase, calleeReg);
    emitReturnPcStackMap(callResultReg,
                        innerResultArrayType ? innerResultArrayType->elemType_ : nullptr);

    // Store result (skip for Void)
    if (!isVoidReturn) {
        emitOp(opArraySetFor(innerResultArrayType->elemType_));
        emitRegs(innerResultReg, iReg, callResultReg);
        emitPtr(innerResultArrayType);
    }

    // Increment inner loop counter and loop back
    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);
    emitJumpTo(loopStartIdx);
    patchJump(exitJump);

    // --- Close outer loops (innermost to outermost) ---
    u16 prevResultReg = isVoidReturn ? callResultReg : innerResultReg;

    for (int d = depth - 2; d >= 0; --d) {
        auto& loop = outerLoops[d];

        if (!isVoidReturn) {
            emitOp(opArraySetFor(loop.resultType->elemType_));
            emitRegs(loop.resultReg, loop.iReg, prevResultReg);
            emitPtr(loop.resultType);
        }

        emitOp(op_add_int);
        emitRegs(loop.iReg, loop.iReg, oneReg);
        emitJumpTo(loop.loopStartIdx);
        patchJump(loop.exitJump);

        if (!isVoidReturn) {
            prevResultReg = loop.resultReg;
        }
    }

    return prevResultReg;
}

// Phase 4g.21: eager List auto-map for lambda values (locals/captures).
// Mirrors the structure of genAutoMapCallListVoid but uses op_call_lambda and
// collects into a result List (via cons-from-right-to-left) for non-Void
// returns.
u16 CodeGen::genAutoMapLambdaCallList(CallExpr_* expr, u16 calleeReg, FunctionType* funcType) {
    u16 argc = (u16)expr->args.size();
    bool isVoidReturn = (funcType->returnType_ == compiler_.voidType());

    // --- Phase 1: Evaluate all argument expressions ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    // --- Phase 2: Find the list arg (assumes a single one, like the
    // genAutoMapCallListVoid path). ---
    u16 listArgIndex = 0;
    for (size_t i = 0; i < argc; ++i) {
        if (expr->autoMapArgs[i] && expr->autoMapArgs[i].isList) {
            listArgIndex = (u16)i;
            break;
        }
    }

    auto* srcListType = dynamic_cast<ListType*>(expr->args[listArgIndex]->resolvedType);
    Type* listElemType = srcListType->elemType_;
    bool elemInlineComposite = listElemType
        && listElemType->repr_ == ts::Type::Repr::Inline;
    u32 elemSlotW = elemInlineComposite ? typeSlotWords(listElemType) : 1;

    // For non-Void return: build the result as a reversed accumulator
    // (cons-from-the-right), then reverse it at the end -- same pattern as
    // genAutoMapBinaryOpList.
    auto* resultListType = isVoidReturn
        ? static_cast<ListType*>(nullptr)
        : dynamic_cast<ListType*>(expr->resolvedType);
    Type* resultElemType = resultListType ? resultListType->elemType_ : nullptr;

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    // --- Phase 3: Iterate the list eagerly ---
    u16 curListReg = allocReg();
    emitMov(curListReg, argRegs[listArgIndex]);

    u16 accReg = 0;
    if (!isVoidReturn) {
        accReg = allocReg();
        emitOp(op_load_nil);
        emitRegs(accReg);
    }

    u32 loopStart = (u32)currentBlock_->code.size();
    u16 nilCheckReg = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(nilCheckReg, curListReg);
    u32 exitJump = emitJump(op_jump_if_true, nilCheckReg);

    // Extract head element (multi-word for Inline composites).
    u16 headReg = nextReg_;
    nextReg_ = (u16)(nextReg_ + elemSlotW);
    if (nextReg_ > maxReg_) maxReg_ = nextReg_;
    emitOp(op_list_head);
    emitRegs(headReg, curListReg);

    // --- Phase 4: Set up call arguments ---
    u16 callArgBase = nextReg_;

    auto perArgSlotW = [&](Type* paramType) -> u32 {
        if (!paramType) return 1;
        if (paramType->repr_ == ts::Type::Repr::Inline)
            return (u32)paramType->sizeWords_;
        return typeSlotWords(paramType);
    };
    u32 cumOffset = 0;
    std::vector<u32> argOffsets(argc, 0);
    for (u16 i = 0; i < argc; ++i) {
        argOffsets[i] = cumOffset;
        Type* pT = (i < funcType->argTypes_.size()) ? funcType->argTypes_[i] : nullptr;
        cumOffset += perArgSlotW(pT);
    }

    for (u16 i = 0; i < argc; ++i) {
        u16 targetReg = (u16)(callArgBase + argOffsets[i]);
        Type* paramType = (i < funcType->argTypes_.size()) ? funcType->argTypes_[i] : nullptr;
        u32 slotW = perArgSlotW(paramType);
        if (nextReg_ <= (u16)(targetReg + slotW)) {
            nextReg_ = (u16)(targetReg + slotW);
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        if (i == listArgIndex) {
            u16 srcReg = headReg;
            if (elemInlineComposite) {
                if (srcReg != targetReg) emitMoveN(targetReg, srcReg, slotW);
            } else {
                if (paramType && listElemType != paramType) {
                    srcReg = ensureType(srcReg, listElemType, paramType);
                }
                if (srcReg != targetReg) {
                    emitMov(targetReg, srcReg);
                }
            }
        } else {
            Type* argType = expr->args[i]->resolvedType;
            u16 srcReg = argRegs[i];
            if (paramType && argType != paramType) {
                srcReg = ensureType(srcReg, argType, paramType);
            }
            if (srcReg != targetReg) {
                if (slotW <= 1) { emitMov(targetReg, srcReg); }
                else { emitMoveN(targetReg, srcReg, slotW); }
            }
        }
    }

    // --- Phase 5: Call the lambda ---
    u16 callResultReg = isVoidReturn ? allocReg() : allocSlot(resultElemType);
    clearArgRegTypes(callArgBase, callResultReg);
    emitOp(op_call_lambda);
    emitRegs(callResultReg, argc, callArgBase, calleeReg);
    emitReturnPcStackMap(callResultReg, isVoidReturn ? nullptr : resultElemType);

    // Cons the result onto the accumulator (reversed result list).
    if (!isVoidReturn) {
        u16 newAcc = allocReg();
        emitOp(op_cons);
        emitRegs(newAcc, callResultReg, accReg);
        emitPtr(resultListType);
        emitMov(accReg, newAcc);
    }

    // --- Phase 6: Advance to tail and loop ---
    emitOp(op_list_tail);
    emitRegs(curListReg, curListReg);
    emitJumpTo(loopStart);
    patchJump(exitJump);

    if (isVoidReturn) {
        return callResultReg;
    }

    // Reverse the accumulator to produce the result in source order.
    u16 resultReg = allocReg();
    emitOp(op_load_nil);
    emitRegs(resultReg);

    u32 revLoopStart = (u32)currentBlock_->code.size();
    u16 revNilReg = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(revNilReg, accReg);
    u32 revExitJump = emitJump(op_jump_if_true, revNilReg);

    u16 revHeadReg = allocSlot(resultElemType);
    emitOp(op_list_head);
    emitRegs(revHeadReg, accReg);

    u16 newResult = allocReg();
    emitOp(op_cons);
    emitRegs(newResult, revHeadReg, resultReg);
    emitPtr(resultListType);
    emitMov(resultReg, newResult);

    emitOp(op_list_tail);
    emitRegs(accReg, accReg);
    emitJumpTo(revLoopStart);
    patchJump(revExitJump);

    return resultReg;
}

u16 CodeGen::genAutoMapCallList(CallExpr_* expr, const FuncInfo* funcInfo) {
    // Lazy list auto-mapping: create an AutoMapListGen that produces elements
    // on demand instead of eagerly iterating the entire (possibly infinite) list.
    u16 argc = (u16)expr->args.size();

    auto* resultListType = dynamic_cast<ListType*>(expr->resolvedType);
    if (!resultListType) {
        error(expr->loc, "Codegen: expected List result type for list auto-map");
        return allocReg();
    }

    // --- Phase 1: Evaluate all argument expressions ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    // --- Phase 2: Find the list arg and collect broadcast args ---
    u16 listArgIndex = 0;
    for (size_t i = 0; i < argc; ++i) {
        if (expr->autoMapArgs[i] && expr->autoMapArgs[i].isList) {
            listArgIndex = (u16)i;
            break;
        }
    }

    auto* srcListType = dynamic_cast<ListType*>(expr->args[listArgIndex]->resolvedType);
    Type* listElemType = srcListType->elemType_;
    Type* listParamType = getParamType(funcInfo, expr, listArgIndex);
    if (!listParamType) listParamType = listElemType;

    // Build the AutoMapCallInfo descriptor
    auto* info = new AutoMapCallInfo();
    info->funcGlobalIndex = expr->resolvedFuncGlobalIndex;
    info->isBuiltin       = expr->isBuiltinCall;
    info->argc            = argc;
    info->listArgIndex    = listArgIndex;
    info->listElemType    = listElemType;
    info->listParamType   = listParamType;
    info->resultElemType  = resultListType->elemType_;
    info->resultListType  = resultListType;

    // --- Phase 3: Place broadcast scalars in consecutive registers ---
    // Promote scalar types at compile time (ensureType emits conversion opcodes).
    u16 broadcastBase = nextReg_;
    u16 numBroadcast = 0;

    for (u16 i = 0; i < argc; ++i) {
        if (i == listArgIndex) continue;  // skip list arg

        Type* argType = expr->args[i]->resolvedType;
        Type* paramType = getParamType(funcInfo, expr, i);
        if (!paramType) paramType = argType;
        u16 srcReg = argRegs[i];

        // Check if this arg is an auto-mapped array (iterated in parallel with the list)
        bool isAutoMappedArray = expr->autoMapArgs[i] && expr->autoMapArgs[i].depth > 0
                                 && !expr->autoMapArgs[i].isList;

        // Promote scalar at compile time if needed (skip for arrays - elements promoted at runtime)
        if (!isAutoMappedArray && argType != paramType) {
            srcReg = ensureType(srcReg, argType, paramType);
        }

        u16 bcastReg = allocReg();
        if (srcReg != bcastReg) {
            emitMov(bcastReg, srcReg);
        }

        AutoMapCallInfo::BroadcastArg ba;
        ba.argIndex = i;
        ba.isArray  = isAutoMappedArray;
        ba.isObj    = isAutoMappedArray ? true : storesObjPtr(paramType);
        ba.srcType  = (argType != paramType) ? argType : nullptr;
        ba.dstType  = (argType != paramType) ? paramType : nullptr;
        ba.elemType = isAutoMappedArray ? static_cast<ArrayType*>(argType)->elemType_ : nullptr;
        info->broadcastArgs.push_back(ba);
        numBroadcast++;
    }

    // Store the info descriptor as an object constant in the CodeBlock
    currentBlock_->addObjConstant(info);

    // --- Phase 4: Emit op_make_lazy_automap ---
    u16 resultReg = allocReg();
    emitOp(op_make_lazy_automap);
    emitRegs(resultReg, argRegs[listArgIndex], broadcastBase, numBroadcast);
    emitPtr(info);

    return resultReg;
}

u16 CodeGen::genAutoMapCallListVoid(CallExpr_* expr, const FuncInfo* funcInfo) {
    // Eager list iteration for auto-mapping a Void-returning function.
    // Since there's no result to collect, we iterate the list and call the
    // function for side effects only.
    u16 argc = (u16)expr->args.size();

    // --- Phase 1: Evaluate all argument expressions ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    // --- Phase 2: Find the list arg ---
    u16 listArgIndex = 0;
    for (size_t i = 0; i < argc; ++i) {
        if (expr->autoMapArgs[i] && expr->autoMapArgs[i].isList) {
            listArgIndex = (u16)i;
            break;
        }
    }

    auto* srcListType = dynamic_cast<ListType*>(expr->args[listArgIndex]->resolvedType);
    Type* listElemType = srcListType->elemType_;
    bool elemInlineComposite = listElemType
        && listElemType->repr_ == ts::Type::Repr::Inline;
    u32 elemSlotW = elemInlineComposite ? typeSlotWords(listElemType) : 1;
    bool boxAtBoundary = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs;

    // --- Phase 3: Iterate the list eagerly ---
    u16 curListReg = allocReg();
    emitMov(curListReg, argRegs[listArgIndex]);

    u32 loopStart = (u32)currentBlock_->code.size();
    u16 nilCheckReg = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(nilCheckReg, curListReg);
    u32 exitJump = emitJump(op_jump_if_true, nilCheckReg);

    // Extract head element. Phase 4g.21: op_list_head writes payloadWords_
    // words, so reserve a slot wide enough for Inline composite elements
    // (Tuple/Struct/Enum and Complex/Fraction).
    u16 headReg = nextReg_;
    nextReg_ = (u16)(nextReg_ + elemSlotW);
    if (nextReg_ > maxReg_) maxReg_ = nextReg_;
    emitOp(op_list_head);
    emitRegs(headReg, curListReg);

    // --- Phase 4: Set up call arguments ---
    u16 callArgBase = nextReg_;

    auto perArgSlotW = [&](Type* paramType, u16 i) -> u32 {
        if (expr->variadicPackStart >= 0 && i >= (u16)expr->variadicPackStart) {
            return 1;
        }
        if (!paramType) return 1;
        if (paramType->repr_ == ts::Type::Repr::Inline)
            return boxAtBoundary ? 1u : (u32)paramType->sizeWords_;
        return typeSlotWords(paramType);
    };
    u32 cumOffset = 0;
    std::vector<u32> argOffsets(argc, 0);
    for (u16 i = 0; i < argc; ++i) {
        argOffsets[i] = cumOffset;
        cumOffset += perArgSlotW(getParamType(funcInfo, expr, i), i);
    }

    for (u16 i = 0; i < argc; ++i) {
        u16 targetReg = (u16)(callArgBase + argOffsets[i]);
        Type* paramType = getParamType(funcInfo, expr, i);
        u32 slotW = perArgSlotW(paramType, i);
        if (nextReg_ <= (u16)(targetReg + slotW)) {
            nextReg_ = (u16)(targetReg + slotW);
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        if (i == listArgIndex) {
            // List element. For Inline composite elements the head is a
            // multi-word slot at headReg..headReg+elemSlotW.
            u16 srcReg = headReg;
            if (elemInlineComposite) {
                if (boxAtBoundary) {
                    srcReg = emitBoxIfInline(headReg, listElemType);
                    if (srcReg != targetReg) {
                        emitMov(targetReg, srcReg);
                    }
                } else {
                    if (srcReg != targetReg) {
                        emitMoveN(targetReg, srcReg, slotW);
                    }
                }
            } else {
                if (paramType && listElemType != paramType) {
                    srcReg = ensureType(srcReg, listElemType, paramType);
                }
                if (srcReg != targetReg) {
                    emitMov(targetReg, srcReg);
                }
            }
        } else {
            // Broadcast scalar argument
            Type* argType = expr->args[i]->resolvedType;
            u16 srcReg = argRegs[i];
            if (paramType && argType != paramType) {
                srcReg = ensureType(srcReg, argType, paramType);
            }
            if (paramType
                && paramType->repr_ == ts::Type::Repr::Inline
                && boxAtBoundary) {
                srcReg = emitBoxIfInline(srcReg, paramType);
            }
            if (srcReg != targetReg) {
                if (slotW <= 1) { emitMov(targetReg, srcReg); }
                else { emitMoveN(targetReg, srcReg, slotW); }
            }
        }
    }

    // --- Phase 5: Variadic packing + Call function (discard result) ---
    u16 callArgc = emitVariadicPack(expr, callArgBase, argc);
    u16 callResultReg = allocReg();
    clearArgRegTypes(callArgBase, callResultReg);
    emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
    emitRegs(callResultReg, callArgc, callArgBase);
    emitInt(expr->resolvedFuncGlobalIndex);
    emitReturnPcStackMap();  // result discarded -- no result reg to seed

    // --- Phase 6: Advance to tail and loop ---
    emitOp(op_list_tail);
    emitRegs(curListReg, curListReg);
    emitJumpTo(loopStart);
    patchJump(exitJump);

    return callResultReg;
}

u16 CodeGen::genExplicitImplicitAutoMapCall(CallExpr_* expr) {
    // Handles calls with explicit @ on some args and implicit auto-mapping on
    // other args.  The explicit @ generates outer loops, and the implicit
    // auto-mapping generates an inner loop.  The two are independent.
    auto* ident = static_cast<IdentifierExpr*>(expr->callee.get());

    // Look up function info
    const FuncInfo* funcInfo = nullptr;
    auto funcIt = typeChecker_.functions().find(ident->name);
    if (funcIt != typeChecker_.functions().end()) {
        for (const auto& fi : funcIt->second) {
            if ((i32)fi.globalIndex == expr->resolvedFuncGlobalIndex) {
                funcInfo = &fi;
                break;
            }
        }
    }
    if (!funcInfo) {
        error(expr->loc, "Codegen: explicit+implicit auto-map function not found");
        return allocReg();
    }

    bool isVoidReturn = (funcInfo->returnType == compiler_.voidType());

    u16 argc = (u16)expr->args.size();

    // Determine explicit depth (max depth among explicit @ args)
    int explicitDepth = 0;
    for (auto& am : expr->autoMapArgs) {
        if (am.depth > explicitDepth) explicitDepth = am.depth;
    }

    // Determine implicit depth (max depth among inner auto-map args)
    int implicitDepth = 0;
    for (auto& iam : expr->innerAutoMapArgs) {
        if (iam.depth > implicitDepth) implicitDepth = iam.depth;
    }

    // --- Phase 1: Evaluate all argument expressions ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    // Constants
    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    // Compute min length of implicit auto-mapped arrays (before outer loops,
    // since these arrays don't change across outer iterations)
    u16 implicitMinLenReg = 0;
    bool firstImplicit = true;

    for (size_t i = 0; i < argc; ++i) {
        if (i >= expr->innerAutoMapArgs.size() || !expr->innerAutoMapArgs[i]) continue;
        auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);

        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrType->elemType_));
        emitRegs(lenReg, argRegs[i]);
        emitPtr(arrType);

        if (firstImplicit) {
            implicitMinLenReg = lenReg;
            firstImplicit = false;
        } else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, implicitMinLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(implicitMinLenReg, lenReg);
            patchJump(skipJump);
        }
    }

    // --- Phase 2: Explicit @ outer loops ---
    // For explicit depth D, we generate D nested loops.
    // Each loop peels one Array layer from explicit @ args.

    struct LoopLevel {
        u16 lenReg;
        u16 iReg;
        u16 condReg;
        u16 resultReg;
        u32 loopStartIdx;
        u32 exitJump;
        ArrayType* resultType;
    };

    std::vector<LoopLevel> outerLoops(explicitDepth);
    std::vector<u16> currentExplicitRegs = argRegs;

    // Get result type structure
    Type* curResultType = expr->resolvedType;

    // Find the primary explicit arg (first one with depth > 0) for lengths
    int primaryExplicit = -1;
    for (size_t i = 0; i < argc; ++i) {
        if (expr->autoMapArgs[i].depth > 0) {
            primaryExplicit = (int)i;
            break;
        }
    }

    for (int d = 0; d < explicitDepth; ++d) {
        auto& loop = outerLoops[d];

        // Compute the type at this depth level for the primary explicit arg
        Type* curArgType = expr->args[primaryExplicit]->resolvedType;
        for (int level = 0; level < d; ++level) {
            curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
        }
        auto* curArrType = dynamic_cast<ArrayType*>(curArgType);

        // Get length
        loop.lenReg = allocReg();
        emitOp(opArrayLengthFor(curArrType->elemType_));
        emitRegs(loop.lenReg, currentExplicitRegs[primaryExplicit]);
        emitPtr(curArrType);

        // Allocate result array (skip for Void)
        if (!isVoidReturn) {
            loop.resultType = dynamic_cast<ArrayType*>(curResultType);
            loop.resultReg = allocReg();
            emitOp(op_array_alloc);
            emitRegs(loop.resultReg, loop.lenReg);
            emitPtr(loop.resultType);
        }

        // Loop counter
        loop.iReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(loop.iReg);
        emitInt(0);

        loop.condReg = allocReg();

        // Loop start
        loop.loopStartIdx = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int);
        emitRegs(loop.condReg, loop.iReg, loop.lenReg);
        loop.exitJump = emitJump(op_jump_if_false, loop.condReg);

        // Extract sub-arrays/elements from each explicit @ arg. Phase 4g.8:
        // peeled elements may be multi-word inline composites.
        for (size_t i = 0; i < argc; ++i) {
            if (expr->autoMapArgs[i].depth == 0) continue;

            Type* argType = expr->args[i]->resolvedType;
            for (int level = 0; level < d; ++level) {
                argType = dynamic_cast<ArrayType*>(argType)->elemType_;
            }
            auto* argArrType = dynamic_cast<ArrayType*>(argType);
            Type* peeledElem = argArrType->elemType_;

            u16 subReg = allocSlot(peeledElem);
            emitOp(opArrayGetDynFor(argArrType->elemType_));
            emitRegs(subReg, currentExplicitRegs[i], loop.iReg);
            emitPtr(argArrType);
            currentExplicitRegs[i] = subReg;
        }

        if (!isVoidReturn) {
            curResultType = loop.resultType->elemType_;
        }
    }

    // At this point, currentExplicitRegs[i] holds the fully-peeled scalar
    // for explicit @ args, and argRegs[i] still holds the original array
    // for implicit args.

    // --- Phase 3: Inner implicit auto-map loop ---
    auto* innerResultType = isVoidReturn ? nullptr : dynamic_cast<ArrayType*>(curResultType);

    u16 innerResultReg = 0;
    if (!isVoidReturn) {
        innerResultReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(innerResultReg, implicitMinLenReg);
        emitPtr(innerResultType);
    }

    // Inner loop counter
    u16 innerIReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(innerIReg);
    emitInt(0);

    u16 innerCondReg = allocReg();

    // Inner loop start
    u32 innerLoopStart = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(innerCondReg, innerIReg, implicitMinLenReg);
    u32 innerExitJump = emitJump(op_jump_if_false, innerCondReg);

    // --- Phase 4: Set up call arguments ---
    u16 callArgBase = nextReg_;

    auto inlineCompositeT = [&](Type* t) {
        return t && t->repr_ == ts::Type::Repr::Inline
            && t != compiler_.complexType()
            && t != compiler_.fractionType();
    };
    // Legacy (unmigrated) builtins still expect 1-Word boxed args; migrated
    // builtins (`acceptsInlineArgs=true`) take inline-composite args natively.
    bool boxAtBoundary = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs;
    auto callerSlotW = [&](Type* paramType, u16 i) -> u32 {
        if (expr->variadicPackStart >= 0 && i >= (u16)expr->variadicPackStart) {
            return 1;
        }
        if (!paramType) return 1;
        if (inlineCompositeT(paramType))
            return boxAtBoundary ? 1u : (u32)paramType->sizeWords_;
        return typeSlotWords(paramType);
    };
    u32 cumOffset = 0;
    std::vector<u32> argOffsets(argc, 0);
    for (u16 i = 0; i < argc; ++i) {
        argOffsets[i] = cumOffset;
        cumOffset += callerSlotW(getParamType(funcInfo, expr, i), i);
    }

    for (u16 i = 0; i < argc; ++i) {
        u16 targetReg = (u16)(callArgBase + argOffsets[i]);
        Type* paramType = getParamType(funcInfo, expr, i);
        u32 slotW = callerSlotW(paramType, i);
        if (nextReg_ <= (u16)(targetReg + slotW)) {
            nextReg_ = (u16)(targetReg + slotW);
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        if (expr->autoMapArgs[i].depth > 0) {
            // Explicit @ arg: already peeled to scalar by outer loops.
            // Phase 4g.8: peeled element from InlineArray is multi-word.
            u16 srcReg = currentExplicitRegs[i];
            Type* elemType = expr->args[i]->resolvedType;
            for (int d = 0; d < expr->autoMapArgs[i].depth; ++d) {
                elemType = dynamic_cast<ArrayType*>(elemType)->elemType_;
            }
            if (inlineCompositeT(elemType) && boxAtBoundary) {
                srcReg = emitBoxIfInline(srcReg, elemType);
                if (srcReg != targetReg) { emitMov(targetReg, srcReg); }
            } else {
                if (paramType && elemType != paramType) {
                    srcReg = ensureType(srcReg, elemType, paramType);
                }
                if (srcReg != targetReg) {
                    if (slotW <= 1) { emitMov(targetReg, srcReg); }
                    else { emitMoveN(targetReg, srcReg, slotW); }
                }
            }
        } else if (i < expr->innerAutoMapArgs.size() && expr->innerAutoMapArgs[i].depth > 0) {
            // Implicit auto-map arg: extract from inner loop. Phase 4g.8:
            // InlineArray returns multi-word inline element direct.
            auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            Type* elemType = arrType->elemType_;
            bool elemInlineComposite = inlineCompositeT(elemType);
            u16 elemReg = elemInlineComposite ? allocSlot(elemType) : targetReg;
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(elemReg, argRegs[i], innerIReg);
            emitPtr(arrType);
            if (elemInlineComposite) {
                if (boxAtBoundary) {
                    u16 boxed = emitBoxIfInline(elemReg, elemType);
                    if (boxed != targetReg) { emitMov(targetReg, boxed); }
                } else {
                    if (elemReg != targetReg) emitMoveN(targetReg, elemReg, slotW);
                }
            } else if (paramType && elemType != paramType) {
                u16 promoted = ensureType(targetReg, elemType, paramType);
                if (promoted != targetReg) {
                    emitMov(targetReg, promoted);
                }
            }
        } else {
            // Non-auto-mapped: copy scalar value
            Type* argType = expr->args[i]->resolvedType;
            u16 srcReg = argRegs[i];
            if (paramType && argType != paramType) {
                srcReg = ensureType(srcReg, argType, paramType);
            }
            if (inlineCompositeT(paramType) && boxAtBoundary) {
                srcReg = emitBoxIfInline(srcReg, paramType);
            }
            if (srcReg != targetReg) {
                if (slotW <= 1) { emitMov(targetReg, srcReg); }
                else { emitMoveN(targetReg, srcReg, slotW); }
            }
        }
    }

    // --- Phase 5: Variadic packing + Call function ---
    u16 callArgc = emitVariadicPack(expr, callArgBase, argc);
    Type* returnT = funcInfo->returnType;
    bool retInlineComposite = returnT
        && returnT->repr_ == ts::Type::Repr::Inline
        && returnT != compiler_.complexType()
        && returnT != compiler_.fractionType();
    bool builtinReturnsInline = boxAtBoundary && retInlineComposite;
    u16 callResultReg = builtinReturnsInline
        ? allocReg()
        : (retInlineComposite ? allocSlot(returnT) : allocReg());
    clearArgRegTypes(callArgBase, callResultReg);
    emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
    emitRegs(callResultReg, callArgc, callArgBase);
    emitInt(expr->resolvedFuncGlobalIndex);
    emitReturnPcStackMap(callResultReg, returnT);
    if (builtinReturnsInline) {
        callResultReg = emitUnboxIfInline(callResultReg, returnT);
    }
    // Phase 4g.8: InlineArray takes the multi-word inline result directly.
    (void)retInlineComposite;
    u16 storeReg = callResultReg;

    // Store in inner result array (skip for Void)
    if (!isVoidReturn) {
        emitOp(opArraySetFor(innerResultType->elemType_));
        emitRegs(innerResultReg, innerIReg, storeReg);
        emitPtr(innerResultType);
    }

    // Increment inner loop counter
    emitOp(op_add_int);
    emitRegs(innerIReg, innerIReg, oneReg);
    emitJumpTo(innerLoopStart);
    patchJump(innerExitJump);

    // --- Phase 6: Close outer loops (innermost to outermost) ---
    u16 prevResultReg = isVoidReturn ? callResultReg : innerResultReg;

    for (int d = explicitDepth - 1; d >= 0; --d) {
        auto& loop = outerLoops[d];

        // Store result in current level's result array (skip for Void)
        if (!isVoidReturn) {
            emitOp(opArraySetFor(loop.resultType->elemType_));
            emitRegs(loop.resultReg, loop.iReg, prevResultReg);
            emitPtr(loop.resultType);
        }

        // Increment counter
        emitOp(op_add_int);
        emitRegs(loop.iReg, loop.iReg, oneReg);
        emitJumpTo(loop.loopStartIdx);
        patchJump(loop.exitJump);

        if (!isVoidReturn) {
            prevResultReg = loop.resultReg;
        }
    }

    return prevResultReg;
}

u16 CodeGen::genCartesianCall(CallExpr_* expr) {
    auto* ident = static_cast<IdentifierExpr*>(expr->callee.get());

    // Look up function info
    const FuncInfo* funcInfo = nullptr;
    auto funcIt = typeChecker_.functions().find(ident->name);
    if (funcIt != typeChecker_.functions().end()) {
        for (const auto& fi : funcIt->second) {
            if ((i32)fi.globalIndex == expr->resolvedFuncGlobalIndex) {
                funcInfo = &fi;
                break;
            }
        }
    }
    if (!funcInfo) {
        error(expr->loc, "Codegen: cartesian function not found");
        return allocReg();
    }

    u16 argc = (u16)expr->args.size();

    // Determine max cartesian index
    int maxCartesian = 0;
    for (auto& am : expr->autoMapArgs) {
        if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
    }

    // --- Phase 1: Evaluate all argument expressions ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    bool isVoidReturn = (funcInfo->returnType == compiler_.voidType());

    // --- Phase 2: Get lengths for each cartesian level ---
    // lenRegs[level] = length of the array for @level (1-based)
    std::vector<u16> lenRegs(maxCartesian + 1, 0);
    for (size_t i = 0; i < argc; ++i) {
        int ci = expr->autoMapArgs[i].cartesianIndex;
        if (ci > 0 && lenRegs[ci] == 0) {
            auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            u16 lenReg = allocReg();
            emitOp(opArrayLengthFor(arrType->elemType_));
            emitRegs(lenReg, argRegs[i]);
            emitPtr(arrType);
            lenRegs[ci] = lenReg;
        }
    }

    // --- Phase 3: Allocate result arrays and loop counters ---
    // We need maxCartesian nested loops.
    // For @1/@2: result is [[R]]
    // Outer loop (@1): allocate outer array of len(@1)
    // Inner loop (@2): allocate inner array of len(@2)

    // Loop counter registers
    std::vector<u16> iRegs(maxCartesian + 1);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    for (int level = 1; level <= maxCartesian; ++level) {
        iRegs[level] = allocReg();
        emitOp(op_load_int_const);
        emitRegs(iRegs[level]);
        emitInt(0);
    }

    // Condition registers for each level
    std::vector<u16> condRegs(maxCartesian + 1);
    for (int level = 1; level <= maxCartesian; ++level) {
        condRegs[level] = allocReg();
    }

    // Allocate outer result array (for @1 level) - skip for Void
    auto* outerResultType = isVoidReturn ? nullptr : dynamic_cast<ArrayType*>(expr->resolvedType);
    u16 outerResultReg = 0;
    if (!isVoidReturn) {
        outerResultReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(outerResultReg, lenRegs[1]);
        emitPtr(outerResultType);
    }

    // --- Phase 4: Generate nested loops ---
    // Level 1 (outer) loop
    u32 loop1StartIdx = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(condRegs[1], iRegs[1], lenRegs[1]);
    u32 loop1ExitJump = emitJump(op_jump_if_false, condRegs[1]);

    // Allocate inner result array (for @2 level) inside outer loop
    u16 innerResultReg = 0;
    ArrayType* innerResultType = nullptr;
    u32 loop2StartIdx = 0;
    u32 loop2ExitJump = 0;

    if (maxCartesian >= 2) {
        if (!isVoidReturn) {
            innerResultType = dynamic_cast<ArrayType*>(outerResultType->elemType_);
            innerResultReg = allocReg();
            emitOp(op_array_alloc);
            emitRegs(innerResultReg, lenRegs[2]);
            emitPtr(innerResultType);
        }

        // Reset inner counter
        emitOp(op_load_int_const);
        emitRegs(iRegs[2]);
        emitInt(0);

        // Level 2 (inner) loop
        loop2StartIdx = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int);
        emitRegs(condRegs[2], iRegs[2], lenRegs[2]);
        loop2ExitJump = emitJump(op_jump_if_false, condRegs[2]);
    }

    // --- Phase 5: Set up call arguments ---
    u16 callArgBase = nextReg_;

    auto inlineCompositeT = [&](Type* t) {
        return t && t->repr_ == ts::Type::Repr::Inline
            && t != compiler_.complexType()
            && t != compiler_.fractionType();
    };
    // Legacy (unmigrated) builtins still expect 1-Word boxed args.
    bool boxAtBoundary = expr->isBuiltinCall && !expr->builtinAcceptsInlineArgs;
    auto callerSlotW = [&](Type* paramType, u16 i) -> u32 {
        if (expr->variadicPackStart >= 0 && i >= (u16)expr->variadicPackStart) {
            return 1;
        }
        if (!paramType) return 1;
        if (inlineCompositeT(paramType))
            return boxAtBoundary ? 1u : (u32)paramType->sizeWords_;
        return typeSlotWords(paramType);
    };
    u32 cumOffset = 0;
    std::vector<u32> argOffsets(argc, 0);
    for (u16 i = 0; i < argc; ++i) {
        argOffsets[i] = cumOffset;
        cumOffset += callerSlotW(getParamType(funcInfo, expr, i), i);
    }
    for (u16 i = 0; i < argc; ++i) {
        u16 targetReg = (u16)(callArgBase + argOffsets[i]);
        Type* paramType = getParamType(funcInfo, expr, i);
        u32 slotW = callerSlotW(paramType, i);
        if (nextReg_ <= (u16)(targetReg + slotW)) {
            nextReg_ = (u16)(targetReg + slotW);
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        int ci = expr->autoMapArgs[i].cartesianIndex;
        if (ci > 0 || expr->autoMapArgs[i].depth > 0) {
            auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            Type* elemType = arrType->elemType_;
            bool elemInlineComposite = inlineCompositeT(elemType);
            // Phase 4g.8: InlineArray returns multi-word inline element direct.
            u16 elemReg = elemInlineComposite ? allocSlot(elemType) : targetReg;
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(elemReg, argRegs[i], (ci > 0 ? iRegs[ci] : iRegs[1]));
            emitPtr(arrType);
            if (elemInlineComposite) {
                if (boxAtBoundary) {
                    u16 boxed = emitBoxIfInline(elemReg, elemType);
                    if (boxed != targetReg) { emitMov(targetReg, boxed); }
                } else {
                    if (elemReg != targetReg) emitMoveN(targetReg, elemReg, slotW);
                }
            }
            if (paramType && elemType != paramType) {
                u16 promoted = ensureType(targetReg, elemType, paramType);
                if (promoted != targetReg) emitMoveN(targetReg, promoted, slotW);
            }
        } else {
            Type* argType = expr->args[i]->resolvedType;
            u16 srcReg = argRegs[i];
            if (paramType && argType != paramType) {
                srcReg = ensureType(srcReg, argType, paramType);
            }
            if (inlineCompositeT(paramType) && boxAtBoundary) {
                srcReg = emitBoxIfInline(srcReg, paramType);
            }
            if (srcReg != targetReg) {
                if (slotW <= 1) { emitMov(targetReg, srcReg); }
                else { emitMoveN(targetReg, srcReg, slotW); }
            }
        }
    }

    // --- Phase 6: Variadic packing + Call function ---
    u16 callArgc = emitVariadicPack(expr, callArgBase, argc);
    Type* returnT = funcInfo->returnType;
    bool retInlineComposite = returnT
        && returnT->repr_ == ts::Type::Repr::Inline
        && returnT != compiler_.complexType()
        && returnT != compiler_.fractionType();
    bool builtinReturnsInline = boxAtBoundary && retInlineComposite;
    u16 callResultReg = builtinReturnsInline
        ? allocReg()
        : (retInlineComposite ? allocSlot(returnT) : allocReg());
    clearArgRegTypes(callArgBase, callResultReg);
    emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
    emitRegs(callResultReg, callArgc, callArgBase);
    emitInt(expr->resolvedFuncGlobalIndex);
    emitReturnPcStackMap(callResultReg, returnT);
    if (builtinReturnsInline) {
        callResultReg = emitUnboxIfInline(callResultReg, returnT);
    }
    // Phase 4g.8: InlineArray takes the multi-word inline result directly.
    u16 storeReg = callResultReg;
    (void)retInlineComposite;

    // --- Phase 7: Store result and close loops ---
    if (maxCartesian >= 2) {
        // Store in inner array (skip for Void)
        if (!isVoidReturn) {
            emitOp(opArraySetFor(innerResultType->elemType_));
            emitRegs(innerResultReg, iRegs[2], storeReg);
            emitPtr(innerResultType);
        }

        // Increment inner counter
        emitOp(op_add_int);
        emitRegs(iRegs[2], iRegs[2], oneReg);
        emitJumpTo(loop2StartIdx);
        patchJump(loop2ExitJump);

        // Store inner array in outer array (skip for Void)
        if (!isVoidReturn) {
            emitOp(opArraySetFor(outerResultType->elemType_));
            emitRegs(outerResultReg, iRegs[1], innerResultReg);
            emitPtr(outerResultType);
        }
    } else {
        // maxCartesian == 1: store directly in outer array (skip for Void)
        if (!isVoidReturn) {
            emitOp(opArraySetFor(outerResultType->elemType_));
            emitRegs(outerResultReg, iRegs[1], storeReg);
            emitPtr(outerResultType);
        }
    }

    // Increment outer counter
    emitOp(op_add_int);
    emitRegs(iRegs[1], iRegs[1], oneReg);
    emitJumpTo(loop1StartIdx);
    patchJump(loop1ExitJump);

    return isVoidReturn ? callResultReg : outerResultReg;
}

u16 CodeGen::genDeepMapCall(CallExpr_* expr, int depth) {
    auto* ident = static_cast<IdentifierExpr*>(expr->callee.get());

    // Look up function info
    const FuncInfo* funcInfo = nullptr;
    auto funcIt = typeChecker_.functions().find(ident->name);
    if (funcIt != typeChecker_.functions().end()) {
        for (const auto& fi : funcIt->second) {
            if ((i32)fi.globalIndex == expr->resolvedFuncGlobalIndex) {
                funcInfo = &fi;
                break;
            }
        }
    }
    if (!funcInfo) {
        error(expr->loc, "Codegen: deep-map function not found");
        return allocReg();
    }

    bool isVoidReturn = (funcInfo->returnType == compiler_.voidType());

    u16 argc = (u16)expr->args.size();

    // --- Phase 1: Evaluate all argument expressions ---
    std::vector<u16> argRegs;
    for (auto& arg : expr->args) {
        argRegs.push_back(genExpr(static_cast<Expr*>(arg.get())));
    }

    // For deep mapping, we generate (depth - 1) outer loops that peel array layers,
    // then at the innermost level, do a standard auto-map loop.
    // This handles @@, @@@, etc.

    // We build this recursively via nested loop generation.
    // For each depth level d (from 0 to depth-2):
    //   - Get length of current-level array
    //   - Allocate result array for this level
    //   - Loop over elements
    //     - Extract sub-array element from each deep-mapped arg
    //     - At the innermost level (d == depth-1), do the auto-map loop

    // Find which args are deep-mapped
    std::vector<int> deepArgs; // indices of args with depth > 1
    for (size_t i = 0; i < argc; ++i) {
        if (expr->autoMapArgs[i].depth > 1) {
            deepArgs.push_back((int)i);
        }
    }

    // We'll use the first deep-mapped arg to determine the array structure lengths
    int primaryArg = deepArgs.empty() ? 0 : deepArgs[0];

    // Constants
    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    // Build nested loops
    struct LoopLevel {
        u16 arrReg;     // Current level's array for the primary arg
        u16 lenReg;
        u16 iReg;
        u16 condReg;
        u16 resultReg;
        u32 loopStartIdx;
        u32 exitJump;
        ArrayType* resultType;
    };

    std::vector<LoopLevel> loops(depth - 1);

    // Current array registers for each deep arg (peel one level per loop)
    std::vector<u16> currentArgRegs = argRegs;

    // Get result type structure
    Type* curResultType = expr->resolvedType;

    for (int d = 0; d < depth - 1; ++d) {
        auto& loop = loops[d];

        // Get length from primary deep arg at this level
        // Compute the type at this depth level
        Type* curArgType = expr->args[primaryArg]->resolvedType;
        for (int level = 0; level < d; ++level) {
            curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
        }
        auto* curArrType = dynamic_cast<ArrayType*>(curArgType);

        loop.arrReg = currentArgRegs[primaryArg];
        loop.lenReg = allocReg();
        emitOp(opArrayLengthFor(curArrType->elemType_));
        emitRegs(loop.lenReg, loop.arrReg);
        emitPtr(curArrType);

        // Allocate result array (skip for Void)
        if (!isVoidReturn) {
            loop.resultType = dynamic_cast<ArrayType*>(curResultType);
            loop.resultReg = allocReg();
            emitOp(op_array_alloc);
            emitRegs(loop.resultReg, loop.lenReg);
            emitPtr(loop.resultType);
        }

        // Loop counter
        loop.iReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(loop.iReg);
        emitInt(0);

        loop.condReg = allocReg();

        // Loop start
        loop.loopStartIdx = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int);
        emitRegs(loop.condReg, loop.iReg, loop.lenReg);
        loop.exitJump = emitJump(op_jump_if_false, loop.condReg);

        // Extract sub-arrays from each deep-mapped arg
        for (int ai : deepArgs) {
            Type* argType = expr->args[ai]->resolvedType;
            for (int level = 0; level < d; ++level) {
                argType = dynamic_cast<ArrayType*>(argType)->elemType_;
            }
            auto* argArrType = dynamic_cast<ArrayType*>(argType);

            u16 subReg = allocReg();
            emitOp(opArrayGetDynFor(argArrType->elemType_));
            emitRegs(subReg, currentArgRegs[ai], loop.iReg);
            emitPtr(argArrType);
            currentArgRegs[ai] = subReg;
        }

        if (!isVoidReturn) {
            curResultType = loop.resultType->elemType_;
        }
    }

    // At the innermost level, do the standard auto-map loop
    // Build a temporary autoMapArgs that represents depth=1 for the deep args
    // and copy the call structure.

    // Get the innermost array type and length
    // The currentArgRegs now hold the depth-1 peeled arrays
    auto* innerResultArrayType = isVoidReturn ? nullptr : dynamic_cast<ArrayType*>(curResultType);

    // Compute min length of innermost arrays
    u16 minLenReg = 0;
    bool firstAutoMap = true;

    for (size_t i = 0; i < argc; ++i) {
        if (!expr->autoMapArgs[i]) continue;

        // For deep args, we've peeled (depth-1) levels, so current is [T]
        // For non-deep args with depth=1, they're still the original arrays
        Type* curArgType = expr->args[i]->resolvedType;
        if (expr->autoMapArgs[i].depth > 1) {
            for (int level = 0; level < depth - 1; ++level) {
                curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
            }
        }
        auto* arrType = dynamic_cast<ArrayType*>(curArgType);
        if (!arrType) continue;

        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrType->elemType_));
        emitRegs(lenReg, currentArgRegs[i]);
        emitPtr(arrType);

        if (firstAutoMap) {
            minLenReg = lenReg;
            firstAutoMap = false;
        } else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skipJump);
        }
    }

    // Allocate innermost result array (skip for Void)
    u16 innerResultReg = 0;
    if (!isVoidReturn) {
        innerResultReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(innerResultReg, minLenReg);
        emitPtr(innerResultArrayType);
    }

    // Inner loop counter
    u16 innerIReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(innerIReg);
    emitInt(0);

    u16 innerCondReg = allocReg();

    // Inner loop start
    u32 innerLoopStart = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(innerCondReg, innerIReg, minLenReg);
    u32 innerExitJump = emitJump(op_jump_if_false, innerCondReg);

    // Set up call arguments
    u16 callArgBase = nextReg_;

    for (u16 i = 0; i < argc; ++i) {
        u16 targetReg = callArgBase + i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        if (expr->autoMapArgs[i]) {
            Type* curArgType = expr->args[i]->resolvedType;
            if (expr->autoMapArgs[i].depth > 1) {
                for (int level = 0; level < depth - 1; ++level) {
                    curArgType = dynamic_cast<ArrayType*>(curArgType)->elemType_;
                }
            }
            auto* arrType = dynamic_cast<ArrayType*>(curArgType);

            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(targetReg, currentArgRegs[i], innerIReg);
            emitPtr(arrType);

            Type* elemType = arrType->elemType_;
            Type* paramType = getParamType(funcInfo, expr, i);
            if (paramType && elemType != paramType) {
                u16 promoted = ensureType(targetReg, elemType, paramType);
                if (promoted != targetReg) {
                    emitMov(targetReg, promoted);
                }
            }
        } else {
            // Promote scalar type to parameter type if needed (e.g. Int -> Float)
            Type* argType = expr->args[i]->resolvedType;
            Type* paramType = getParamType(funcInfo, expr, i);
            u16 srcReg = currentArgRegs[i];
            if (paramType && argType != paramType) {
                srcReg = ensureType(srcReg, argType, paramType);
            }
            if (srcReg != targetReg) {
                emitMov(targetReg, srcReg);
            }
        }
    }

    // Variadic packing + Call function
    u16 callArgc = emitVariadicPack(expr, callArgBase, argc);
    u16 callResultReg = allocReg();
    clearArgRegTypes(callArgBase, callResultReg);
    emitOp(expr->isBuiltinCall ? op_call_primitive : op_call);
    emitRegs(callResultReg, callArgc, callArgBase);
    emitInt(expr->resolvedFuncGlobalIndex);
    emitReturnPcStackMap(callResultReg,
                        isVoidReturn ? nullptr :
                            (innerResultArrayType ? innerResultArrayType->elemType_ : nullptr));

    // Store in innermost result array (skip for Void)
    if (!isVoidReturn) {
        emitOp(opArraySetFor(innerResultArrayType->elemType_));
        emitRegs(innerResultReg, innerIReg, callResultReg);
        emitPtr(innerResultArrayType);
    }

    // Increment inner loop counter
    emitOp(op_add_int);
    emitRegs(innerIReg, innerIReg, oneReg);
    emitJumpTo(innerLoopStart);
    patchJump(innerExitJump);

    // Close outer loops (from innermost to outermost)
    u16 prevResultReg = isVoidReturn ? callResultReg : innerResultReg;

    for (int d = depth - 2; d >= 0; --d) {
        auto& loop = loops[d];

        // Store previous result in current level's result array (skip for Void)
        if (!isVoidReturn) {
            emitOp(opArraySetFor(loop.resultType->elemType_));
            emitRegs(loop.resultReg, loop.iReg, prevResultReg);
            emitPtr(loop.resultType);
        }

        // Increment counter
        emitOp(op_add_int);
        emitRegs(loop.iReg, loop.iReg, oneReg);
        emitJumpTo(loop.loopStartIdx);
        patchJump(loop.exitJump);

        if (!isVoidReturn) {
            prevResultReg = loop.resultReg;
        }
    }

    return prevResultReg;
}

u16 CodeGen::genArrayLiteral(ArrayLiteralExpr* expr) {
    // Dispatch to auto-map variants if any elements have @ annotation
    if (!expr->autoMapElements.empty()) {
        // Check for cartesian (@1/@2) vs zip (@)
        int maxCartesian = 0;
        for (auto& am : expr->autoMapElements) {
            if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
        }
        if (maxCartesian > 0) return genCartesianArrayLiteral(expr);
        return genAutoMapArrayLiteral(expr);
    }

    auto* arrType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!arrType) {
        error(expr->loc, "Array literal has non-array resolved type");
        return allocReg();
    }
    Type* elemType = arrType->elemType_;
    usize count = expr->elements.size();

    // Generate all element values into consecutive registers.
    // Phase 4e: inline-element arrays (Array[Complex] / Array[Fraction])
    // use PodArray<x64>/<r64> backends and read 2 consecutive Words per
    // element; lay them out at multi-word stride. Phase 4g.8: Inline
    // structs/tuples/enums now ride the InlineArray backend and live as
    // sizeWords_ consecutive Words per element -- no boxing.
    u32 sw = typeSlotWords(elemType);
    u16 elemBase = nextReg_;
    for (size_t i = 0; i < count; ++i) {
        u16 elemReg = genExpr(static_cast<Expr*>(expr->elements[i].get()));
        Type* elemExprType = expr->elements[i]->resolvedType;
        elemReg = ensureType(elemReg, elemExprType, elemType);
        u16 dstSlot = (u16)(elemBase + (u16)i * sw);
        emitArgPlacement(dstSlot, elemReg, elemType);
        u16 next = (u16)(dstSlot + sw);
        if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
    }

    u16 dst = allocReg();
    emitOp(op_make_array);
    emitRegs(dst, elemBase, (u16)count);
    emitPtr(arrType);
    return dst;
}

u16 CodeGen::genTupleLiteral(TupleLiteralExpr* expr) {
    // Dispatch to auto-map variants if any elements have @ annotation
    if (!expr->autoMapElements.empty()) {
        int maxCartesian = 0;
        for (auto& am : expr->autoMapElements) {
            if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
        }
        if (maxCartesian > 0) return genCartesianTupleLiteral(expr);
        return genAutoMapTupleLiteral(expr);
    }

    auto* tupType = dynamic_cast<TupleType*>(expr->resolvedType);
    if (!tupType) {
        error(expr->loc, "Tuple literal has non-tuple resolved type");
        return allocReg();
    }
    usize count = expr->elements.size();

    // Phase 4g.13: place each field at its natural footprint (multi-word
    // for Inline composite fields). Heap tuples now store fields natively
    // just like Inline tuples -- no boundary box.
    bool inlineTuple = tupType->repr_ == ts::Type::Repr::Inline;
    u16 elemBase = nextReg_;
    u16 cursor = elemBase;
    for (size_t i = 0; i < count; ++i) {
        u16 elemReg = genExpr(static_cast<Expr*>(expr->elements[i].get()));
        Type* elemType = expr->elements[i]->resolvedType;
        Type* fieldType = tupType->fields_[i];
        elemReg = ensureType(elemReg, elemType, fieldType);
        u16 fieldSlotWords = (u16)typeSlotWords(fieldType);
        emitArgPlacement(cursor, elemReg, fieldType);
        cursor = (u16)(cursor + fieldSlotWords);
        if (nextReg_ < cursor) { nextReg_ = cursor; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
    }

    u16 dst = inlineTuple ? allocSlot(tupType) : allocReg();
    emitOp(op_make_tuple);
    emitRegs(dst, elemBase, (u16)count);
    emitPtr(tupType);
    return dst;
}

u16 CodeGen::genStructLiteral(StructLiteralExpr* expr) {
    // Check for auto-mapped struct literal
    if (!expr->autoMapFields.empty()) {
        // Check for cartesian (@1/@2) vs zip (@)
        int maxCartesian = 0;
        for (auto& am : expr->autoMapFields) {
            if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
        }
        if (maxCartesian > 0) return genCartesianStructLiteral(expr);
        return genAutoMapStructLiteral(expr);
    }

    auto* stype = dynamic_cast<StructType*>(expr->resolvedType);
    if (!stype) {
        error(expr->loc, "Struct literal has non-struct resolved type");
        return allocReg();
    }

    usize numFields = stype->fields_.size();

    // Generate spread expression first if present
    u16 spreadReg = 0;
    if (expr->spreadExpr) {
        spreadReg = genExpr(static_cast<Expr*>(expr->spreadExpr.get()));
    }

    // We need to emit field values in the order of the struct declaration,
    // not the order they appear in the literal.
    // Build a mapping from field name to literal field index.
    std::unordered_map<std::string, size_t> litFieldMap;
    for (size_t i = 0; i < expr->fields.size(); ++i) {
        litFieldMap[expr->fields[i].name] = i;
    }

    // Phase 4g.13: each field occupies its natural footprint (multi-word
    // for Inline composite fields). Heap structs now also store fields
    // natively per layout -- no boundary box.
    bool inlineStruct = stype->repr_ == ts::Type::Repr::Inline;
    u16 fieldBase = nextReg_;
    u16 cursor = fieldBase;
    for (size_t i = 0; i < numFields; ++i) {
        std::string fieldName(stype->fields_[i].name->str());
        auto it = litFieldMap.find(fieldName);
        Type* declType = stype->fields_[i].type;
        u16 fieldSlotWords = (u16)typeSlotWords(declType);
        if (it != litFieldMap.end()) {
            size_t litIdx = it->second;
            u16 valReg = genExpr(static_cast<Expr*>(expr->fields[litIdx].value.get()));
            Type* valType = expr->fields[litIdx].value->resolvedType;
            valReg = ensureType(valReg, valType, declType);
            emitArgPlacement(cursor, valReg, declType);
        } else if (expr->spreadExpr) {
            // Copy field from spread source struct (multi-word for Inline
            // composite fields; Phase 4g.13).
            if (inlineStruct) {
                emitOp(op_inline_struct_get);
            } else {
                emitOp(op_struct_get);
            }
            emitRegs(cursor, spreadReg, (u16)i);
            emitPtr(stype);
        } else {
            u16 reg = allocReg();
            emitOp(op_load_nil);
            emitRegs(reg);
            if (reg != cursor) { emitMov(cursor, reg); }
        }
        cursor = (u16)(cursor + fieldSlotWords);
        if (nextReg_ < cursor) { nextReg_ = cursor; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
    }

    u16 dst = inlineStruct ? allocSlot(stype) : allocReg();
    emitOp(op_make_struct);
    emitRegs(dst, fieldBase, (u16)numFields);
    emitPtr(stype);
    return dst;
}

u16 CodeGen::genAutoMapStructLiteral(StructLiteralExpr* expr) {
    auto* resultArrayType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!resultArrayType) {
        error(expr->loc, "Auto-mapped struct literal has non-array resolved type");
        return allocReg();
    }
    auto* stype = dynamic_cast<StructType*>(resultArrayType->elemType_);
    if (!stype) {
        error(expr->loc, "Auto-mapped struct literal element type is not a struct");
        return allocReg();
    }

    usize numFields = stype->fields_.size();

    // Build mapping from field name to literal index
    std::unordered_map<std::string, size_t> litFieldMap;
    for (size_t i = 0; i < expr->fields.size(); ++i) {
        litFieldMap[expr->fields[i].name] = i;
    }

    // Build declaration-order mapping: declOrder[i] = literal index for struct field i
    // SIZE_MAX sentinel means field comes from spread source
    std::vector<size_t> declOrder(numFields, SIZE_MAX);
    for (size_t i = 0; i < numFields; ++i) {
        std::string fieldName(stype->fields_[i].name->str());
        auto it = litFieldMap.find(fieldName);
        if (it != litFieldMap.end()) {
            declOrder[i] = it->second;
        }
    }

    // --- Phase 1: Evaluate all field expressions (before the loop) ---
    std::vector<u16> fieldRegs(expr->fields.size());
    for (size_t i = 0; i < expr->fields.size(); ++i) {
        fieldRegs[i] = genExpr(static_cast<Expr*>(expr->fields[i].value.get()));
    }

    // Generate spread expression if present
    u16 spreadReg = 0;
    if (expr->spreadExpr) {
        spreadReg = genExpr(static_cast<Expr*>(expr->spreadExpr.get()));
    }

    // --- Phase 2: Compute min length of auto-mapped arrays ---
    u16 minLenReg = 0;
    bool firstAutoMap = true;

    for (size_t i = 0; i < expr->fields.size(); ++i) {
        if (!expr->autoMapFields[i]) continue;

        auto* arrType = dynamic_cast<ArrayType*>(expr->fields[i].value->resolvedType);

        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrType->elemType_));
        emitRegs(lenReg, fieldRegs[i]);
        emitPtr(arrType);

        if (firstAutoMap) {
            minLenReg = lenReg;
            firstAutoMap = false;
        } else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skipJump);
        }
    }

    // --- Phase 3: Allocate result array ---
    u16 resultArrReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(resultArrReg, minLenReg);
    emitPtr(resultArrayType);

    // --- Phase 4: Loop counter setup ---
    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    u16 condReg = allocReg();

    // --- Phase 5: Loop start ---
    u32 loopStartIdx = (u32)currentBlock_->code.size();

    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, minLenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // --- Phase 6: Build struct fields in declaration order ---
    u16 structFieldBase = nextReg_;

    for (size_t i = 0; i < numFields; ++i) {
        size_t litIdx = declOrder[i];
        u16 targetReg = structFieldBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        Type* declType = stype->fields_[i].type;

        if (litIdx == SIZE_MAX) {
            // Field from spread source
            emitOp(op_struct_get);
            emitRegs(targetReg, spreadReg, (u16)i);
            emitPtr(stype);
        } else if (expr->autoMapFields[litIdx]) {
            auto* arrType = dynamic_cast<ArrayType*>(expr->fields[litIdx].value->resolvedType);
            // Extract element at runtime index
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(targetReg, fieldRegs[litIdx], iReg);
            emitPtr(arrType);

            // Promote element type to declared field type if needed
            Type* elemType = arrType->elemType_;
            if (elemType != declType) {
                u16 promoted = ensureType(targetReg, elemType, declType);
                if (promoted != targetReg) {
                    emitMov(targetReg, promoted);
                }
            }
        } else {
            // Non-auto-mapped: copy scalar value
            u16 valReg = fieldRegs[litIdx];
            Type* valType = expr->fields[litIdx].value->resolvedType;
            valReg = ensureType(valReg, valType, declType);
            if (valReg != targetReg) {
                emitMov(targetReg, valReg);
            }
        }
    }

    // --- Phase 7: Construct struct ---
    // Phase 4g.2: Inline struct lands as multi-word; reserve sizeWords slot.
    bool inlineStruct = stype->repr_ == ts::Type::Repr::Inline;
    u16 structReg = inlineStruct ? allocSlot(stype) : allocReg();
    emitOp(op_make_struct);
    emitRegs(structReg, structFieldBase, (u16)numFields);
    emitPtr(stype);

    // --- Phase 8: Store struct in result array ---
    // Phase 4g.8: InlineArray takes the multi-word inline struct directly.
    emitOp(opArraySetFor(resultArrayType->elemType_));
    emitRegs(resultArrReg, iReg, structReg);
    emitPtr(resultArrayType);

    // --- Phase 9: Increment and loop ---
    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);

    emitJumpTo(loopStartIdx);

    patchJump(exitJump);

    return resultArrReg;
}

u16 CodeGen::genAutoMapTupleStruct(CallExpr_* expr) {
    auto* resultArrayType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!resultArrayType) {
        error(expr->loc, "Auto-mapped tuple struct has non-array resolved type");
        return allocReg();
    }
    auto* stype = dynamic_cast<StructType*>(resultArrayType->elemType_);
    if (!stype) {
        error(expr->loc, "Auto-mapped tuple struct element type is not a struct");
        return allocReg();
    }

    usize numFields = stype->fields_.size();

    // --- Phase 1: Evaluate all arg expressions (before the loop) ---
    std::vector<u16> argRegs(expr->args.size());
    for (size_t i = 0; i < expr->args.size(); ++i) {
        argRegs[i] = genExpr(static_cast<Expr*>(expr->args[i].get()));
    }

    // --- Phase 2: Compute min length of auto-mapped arrays ---
    u16 minLenReg = 0;
    bool firstAutoMap = true;

    for (size_t i = 0; i < expr->args.size(); ++i) {
        if (!expr->autoMapArgs[i]) continue;

        auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);

        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrType->elemType_));
        emitRegs(lenReg, argRegs[i]);
        emitPtr(arrType);

        if (firstAutoMap) {
            minLenReg = lenReg;
            firstAutoMap = false;
        } else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skipJump = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skipJump);
        }
    }

    // --- Phase 3: Allocate result array ---
    u16 resultArrReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(resultArrReg, minLenReg);
    emitPtr(resultArrayType);

    // --- Phase 4: Loop counter setup ---
    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    u16 condReg = allocReg();

    // --- Phase 5: Loop start ---
    u32 loopStartIdx = (u32)currentBlock_->code.size();

    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, minLenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // --- Phase 6: Build struct fields ---
    u16 structFieldBase = nextReg_;

    for (size_t i = 0; i < numFields; ++i) {
        u16 targetReg = structFieldBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }

        Type* declType = stype->fields_[i].type;

        if (expr->autoMapArgs[i]) {
            auto* arrType = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            // Extract element at runtime index
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(targetReg, argRegs[i], iReg);
            emitPtr(arrType);

            // Promote element type to declared field type if needed
            Type* elemType = arrType->elemType_;
            if (elemType != declType) {
                u16 promoted = ensureType(targetReg, elemType, declType);
                if (promoted != targetReg) {
                    emitMov(targetReg, promoted);
                }
            }
        } else {
            // Non-auto-mapped: copy scalar value
            u16 valReg = argRegs[i];
            Type* valType = expr->args[i]->resolvedType;
            valReg = ensureType(valReg, valType, declType);
            if (valReg != targetReg) {
                emitMov(targetReg, valReg);
            }
        }
    }

    // --- Phase 7: Construct struct (or skip allocation for UnwrappedTupleStruct) ---
    u16 structReg;
    bool inlineStruct = stype->repr_ == ts::Type::Repr::Inline;
    if (stype->repr_ == ts::Type::Repr::UnwrappedTupleStruct && numFields == 1) {
        structReg = structFieldBase;
    } else {
        structReg = inlineStruct ? allocSlot(stype) : allocReg();
        emitOp(op_make_struct);
        emitRegs(structReg, structFieldBase, (u16)numFields);
        emitPtr(stype);
    }

    // --- Phase 8: Store struct in result array ---
    // Phase 4g.8: InlineArray takes the multi-word inline struct directly.
    (void)inlineStruct;
    emitOp(opArraySetFor(resultArrayType->elemType_));
    emitRegs(resultArrReg, iReg, structReg);
    emitPtr(resultArrayType);

    // --- Phase 9: Increment and loop ---
    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);

    emitJumpTo(loopStartIdx);

    patchJump(exitJump);

    return resultArrReg;
}

// ============================================================
// Auto-map (zip @) for array literals: [[1,2,3]@, 4, 5] → [[1,4,5],[2,4,5],[3,4,5]]
// ============================================================
u16 CodeGen::genAutoMapArrayLiteral(ArrayLiteralExpr* expr) {
    auto* resultArrayType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!resultArrayType) {
        error(expr->loc, "Auto-mapped array literal has non-array resolved type");
        return allocReg();
    }
    auto* innerArrayType = dynamic_cast<ArrayType*>(resultArrayType->elemType_);
    if (!innerArrayType) {
        error(expr->loc, "Auto-mapped array literal inner type is not an array");
        return allocReg();
    }
    Type* elemType = innerArrayType->elemType_;
    usize count = expr->elements.size();

    // --- Phase 1: Evaluate all element expressions ---
    std::vector<u16> elemRegs(count);
    for (size_t i = 0; i < count; ++i) {
        elemRegs[i] = genExpr(static_cast<Expr*>(expr->elements[i].get()));
    }

    // --- Phase 2: Compute min length of @-tagged arrays ---
    u16 minLenReg = 0;
    bool firstAM = true;
    for (size_t i = 0; i < count; ++i) {
        if (!expr->autoMapElements[i]) continue;
        auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrT->elemType_));
        emitRegs(lenReg, elemRegs[i]);
        emitPtr(arrT);
        if (firstAM) { minLenReg = lenReg; firstAM = false; }
        else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int);
            emitRegs(cmpReg, lenReg, minLenReg);
            u32 skip = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skip);
        }
    }

    // --- Phase 3: Allocate result array ---
    u16 resultArrReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(resultArrReg, minLenReg);
    emitPtr(resultArrayType);

    // --- Phase 4: Loop counter ---
    u16 iReg = allocReg();
    emitOp(op_load_int_const); emitRegs(iReg); emitInt(0);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const); emitRegs(oneReg); emitInt(1);
    u16 condReg = allocReg();

    // --- Phase 5: Loop start ---
    u32 loopStart = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int); emitRegs(condReg, iReg, minLenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // --- Phase 6: Build inner array elements ---
    u16 innerBase = nextReg_;
    for (size_t i = 0; i < count; ++i) {
        u16 targetReg = innerBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }
        if (expr->autoMapElements[i]) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_));
            emitRegs(targetReg, elemRegs[i], iReg);
            emitPtr(arrT);
            Type* srcElemType = arrT->elemType_;
            if (srcElemType != elemType) {
                u16 promoted = ensureType(targetReg, srcElemType, elemType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else {
            // Scalar element — copy and promote if needed
            Type* valType = expr->elements[i]->resolvedType;
            u16 srcReg = elemRegs[i];
            srcReg = ensureType(srcReg, valType, elemType);
            if (srcReg != targetReg) { emitMov(targetReg, srcReg); }
        }
    }

    // --- Phase 7: Construct inner array ---
    u16 innerArrReg = allocReg();
    emitOp(op_make_array);
    emitRegs(innerArrReg, innerBase, (u16)count);
    emitPtr(innerArrayType);

    // --- Phase 8: Store in result array ---
    emitOp(opArraySetFor(resultArrayType->elemType_));
    emitRegs(resultArrReg, iReg, innerArrReg);
    emitPtr(resultArrayType);

    // --- Phase 9: Increment and loop ---
    emitOp(op_add_int); emitRegs(iReg, iReg, oneReg);
    emitJumpTo(loopStart);
    patchJump(exitJump);

    return resultArrReg;
}

// ============================================================
// Cartesian (@1/@2) for array literals
// ============================================================
u16 CodeGen::genCartesianArrayLiteral(ArrayLiteralExpr* expr) {
    auto* outerResultType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!outerResultType) {
        error(expr->loc, "Cartesian array literal has non-array resolved type");
        return allocReg();
    }

    usize count = expr->elements.size();

    int maxCartesian = 0;
    for (auto& am : expr->autoMapElements) {
        if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
    }

    // Peel array layers: outerResultType has maxCartesian+1 Array layers
    // constructedArrayType is the type of each innermost array we construct ([elemType])
    // innerResultType is the @2 loop result type (only for maxCartesian >= 2)
    Type* peeled = outerResultType;
    for (int i = 0; i < maxCartesian; ++i) {
        peeled = dynamic_cast<ArrayType*>(peeled)->elemType_;
    }
    auto* constructedArrayType = dynamic_cast<ArrayType*>(peeled);
    if (!constructedArrayType) {
        error(expr->loc, "Cartesian array literal constructed type is not an array");
        return allocReg();
    }
    Type* elemType = constructedArrayType->elemType_;
    // Compute result types for each loop level
    std::vector<ArrayType*> resultTypes(maxCartesian + 1, nullptr);
    {
        ArrayType* rt = outerResultType;
        for (int level = 1; level <= maxCartesian; ++level) {
            resultTypes[level] = rt;
            if (level < maxCartesian)
                rt = dynamic_cast<ArrayType*>(rt->elemType_);
        }
    }

    // --- Phase 1: Evaluate all element expressions ---
    std::vector<u16> elemRegs(count);
    for (size_t i = 0; i < count; ++i) {
        elemRegs[i] = genExpr(static_cast<Expr*>(expr->elements[i].get()));
    }

    // --- Phase 2: Get lengths for each cartesian level ---
    std::vector<u16> lenRegs(maxCartesian + 1, 0);
    for (size_t i = 0; i < count; ++i) {
        int ci = expr->autoMapElements[i].cartesianIndex;
        if (ci > 0 && lenRegs[ci] == 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            u16 lenReg = allocReg();
            emitOp(opArrayLengthFor(arrT->elemType_)); emitRegs(lenReg, elemRegs[i]); emitPtr(arrT);
            lenRegs[ci] = lenReg;
        }
    }

    // --- Phase 3: Loop counters ---
    std::vector<u16> iRegs(maxCartesian + 1);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const); emitRegs(oneReg); emitInt(1);
    for (int level = 1; level <= maxCartesian; ++level) {
        iRegs[level] = allocReg();
        emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
    }
    std::vector<u16> condRegs(maxCartesian + 1);
    for (int level = 1; level <= maxCartesian; ++level) {
        condRegs[level] = allocReg();
    }

    // Allocate outer result array
    u16 outerResultReg = allocReg();
    emitOp(op_array_alloc); emitRegs(outerResultReg, lenRegs[1]); emitPtr(outerResultType);

    // --- Phase 4: Open all loops ---
    std::vector<u16> resultRegs(maxCartesian + 1, 0);
    std::vector<u32> loopStarts(maxCartesian + 1, 0);
    std::vector<u32> loopExits(maxCartesian + 1, 0);
    resultRegs[1] = outerResultReg;
    for (int level = 1; level <= maxCartesian; ++level) {
        if (level > 1) {
            resultRegs[level] = allocReg();
            emitOp(op_array_alloc); emitRegs(resultRegs[level], lenRegs[level]); emitPtr(resultTypes[level]);
            emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
        }
        loopStarts[level] = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int); emitRegs(condRegs[level], iRegs[level], lenRegs[level]);
        loopExits[level] = emitJump(op_jump_if_false, condRegs[level]);
    }

    // --- Phase 5: Build inner array elements ---
    u16 innerBase = nextReg_;
    for (size_t i = 0; i < count; ++i) {
        u16 targetReg = innerBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }
        int ci = expr->autoMapElements[i].cartesianIndex;
        if (ci > 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, elemRegs[i], iRegs[ci]); emitPtr(arrT);
            Type* srcElem = arrT->elemType_;
            if (srcElem != elemType) {
                u16 promoted = ensureType(targetReg, srcElem, elemType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else if (expr->autoMapElements[i].depth > 0) {
            // Plain @ in cartesian context — zip with level 1
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, elemRegs[i], iRegs[1]); emitPtr(arrT);
            Type* srcElem = arrT->elemType_;
            if (srcElem != elemType) {
                u16 promoted = ensureType(targetReg, srcElem, elemType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else {
            // Scalar
            Type* valType = expr->elements[i]->resolvedType;
            u16 srcReg = ensureType(elemRegs[i], valType, elemType);
            if (srcReg != targetReg) { emitMov(targetReg, srcReg); }
        }
    }

    // Construct inner array (type = constructedArrayType = [elemType])
    u16 innerArrReg = allocReg();
    emitOp(op_make_array); emitRegs(innerArrReg, innerBase, (u16)count);
    emitPtr(constructedArrayType);

    // --- Phase 6: Close loops (innermost to outermost) ---
    {
        u16 prevReg = innerArrReg;
        for (int level = maxCartesian; level >= 1; --level) {
            emitOp(opArraySetFor(resultTypes[level]->elemType_)); emitRegs(resultRegs[level], iRegs[level], prevReg); emitPtr(resultTypes[level]);
            emitOp(op_add_int); emitRegs(iRegs[level], iRegs[level], oneReg);
            emitJumpTo(loopStarts[level]);
            patchJump(loopExits[level]);
            prevReg = resultRegs[level];
        }
    }

    return outerResultReg;
}

// ============================================================
// Auto-map (zip @) for tuple literals: ([1,2,3]@, 'b, 'c) → [(1,'b,'c),(2,'b,'c),(3,'b,'c)]
// ============================================================
u16 CodeGen::genAutoMapTupleLiteral(TupleLiteralExpr* expr) {
    auto* resultArrayType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!resultArrayType) {
        error(expr->loc, "Auto-mapped tuple literal has non-array resolved type");
        return allocReg();
    }
    auto* tupType = dynamic_cast<TupleType*>(resultArrayType->elemType_);
    if (!tupType) {
        error(expr->loc, "Auto-mapped tuple literal inner type is not a tuple");
        return allocReg();
    }
    usize count = expr->elements.size();

    // --- Phase 1: Evaluate all element expressions ---
    std::vector<u16> elemRegs(count);
    for (size_t i = 0; i < count; ++i) {
        elemRegs[i] = genExpr(static_cast<Expr*>(expr->elements[i].get()));
    }

    // --- Phase 2: Compute min length of @-tagged arrays ---
    u16 minLenReg = 0;
    bool firstAM = true;
    for (size_t i = 0; i < count; ++i) {
        if (!expr->autoMapElements[i]) continue;
        auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
        u16 lenReg = allocReg();
        emitOp(opArrayLengthFor(arrT->elemType_)); emitRegs(lenReg, elemRegs[i]); emitPtr(arrT);
        if (firstAM) { minLenReg = lenReg; firstAM = false; }
        else {
            u16 cmpReg = allocReg();
            emitOp(op_cmp_lt_int); emitRegs(cmpReg, lenReg, minLenReg);
            u32 skip = emitJump(op_jump_if_false, cmpReg);
            emitMov(minLenReg, lenReg);
            patchJump(skip);
        }
    }

    // --- Phase 3: Allocate result array ---
    u16 resultArrReg = allocReg();
    emitOp(op_array_alloc); emitRegs(resultArrReg, minLenReg); emitPtr(resultArrayType);

    // --- Phase 4: Loop counter ---
    u16 iReg = allocReg();
    emitOp(op_load_int_const); emitRegs(iReg); emitInt(0);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const); emitRegs(oneReg); emitInt(1);
    u16 condReg = allocReg();

    // --- Phase 5: Loop start ---
    u32 loopStart = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int); emitRegs(condReg, iReg, minLenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // --- Phase 6: Build tuple fields ---
    u16 tupleFieldBase = nextReg_;
    for (size_t i = 0; i < count; ++i) {
        u16 targetReg = tupleFieldBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }
        Type* fieldType = tupType->fields_[i];
        if (expr->autoMapElements[i]) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, elemRegs[i], iReg); emitPtr(arrT);
            Type* srcElemType = arrT->elemType_;
            if (srcElemType != fieldType) {
                u16 promoted = ensureType(targetReg, srcElemType, fieldType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else {
            Type* valType = expr->elements[i]->resolvedType;
            u16 srcReg = ensureType(elemRegs[i], valType, fieldType);
            if (srcReg != targetReg) { emitMov(targetReg, srcReg); }
        }
    }

    // --- Phase 7: Construct tuple ---
    bool inlineTuple = tupType->repr_ == ts::Type::Repr::Inline;
    u16 tupReg = inlineTuple ? allocSlot(tupType) : allocReg();
    emitOp(op_make_tuple);
    emitRegs(tupReg, tupleFieldBase, (u16)count);
    emitPtr(tupType);

    // --- Phase 8: Store in result array ---
    // Phase 4g.8: InlineArray takes the multi-word inline tuple directly.
    (void)inlineTuple;
    emitOp(opArraySetFor(resultArrayType->elemType_));
    emitRegs(resultArrReg, iReg, tupReg);
    emitPtr(resultArrayType);

    // --- Phase 9: Increment and loop ---
    emitOp(op_add_int); emitRegs(iReg, iReg, oneReg);
    emitJumpTo(loopStart);
    patchJump(exitJump);

    return resultArrReg;
}

// ============================================================
// Cartesian (@1/@2) for tuple literals
// ============================================================
u16 CodeGen::genCartesianTupleLiteral(TupleLiteralExpr* expr) {
    auto* outerResultType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!outerResultType) {
        error(expr->loc, "Cartesian tuple literal has non-array resolved type");
        return allocReg();
    }
    usize count = expr->elements.size();

    int maxCartesian = 0;
    for (auto& am : expr->autoMapElements) {
        if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
    }

    // Peel array layers to get the tuple type
    Type* inner = outerResultType;
    for (int i = 0; i < maxCartesian; ++i) {
        auto* arrT = dynamic_cast<ArrayType*>(inner);
        if (!arrT) { error(expr->loc, "Cartesian tuple type nesting error"); return allocReg(); }
        inner = arrT->elemType_;
    }
    auto* tupType = dynamic_cast<TupleType*>(inner);
    if (!tupType) {
        error(expr->loc, "Cartesian tuple literal element type is not a tuple");
        return allocReg();
    }

    // Compute result types for each loop level
    std::vector<ArrayType*> resultTypes(maxCartesian + 1, nullptr);
    {
        ArrayType* rt = outerResultType;
        for (int level = 1; level <= maxCartesian; ++level) {
            resultTypes[level] = rt;
            if (level < maxCartesian)
                rt = dynamic_cast<ArrayType*>(rt->elemType_);
        }
    }

    // --- Phase 1: Evaluate all element expressions ---
    std::vector<u16> elemRegs(count);
    for (size_t i = 0; i < count; ++i) {
        elemRegs[i] = genExpr(static_cast<Expr*>(expr->elements[i].get()));
    }

    // --- Phase 2: Get lengths for each cartesian level ---
    std::vector<u16> lenRegs(maxCartesian + 1, 0);
    for (size_t i = 0; i < count; ++i) {
        int ci = expr->autoMapElements[i].cartesianIndex;
        if (ci > 0 && lenRegs[ci] == 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            u16 lenReg = allocReg();
            emitOp(opArrayLengthFor(arrT->elemType_)); emitRegs(lenReg, elemRegs[i]); emitPtr(arrT);
            lenRegs[ci] = lenReg;
        }
    }

    // --- Phase 3: Loop counters ---
    std::vector<u16> iRegs(maxCartesian + 1);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const); emitRegs(oneReg); emitInt(1);
    for (int level = 1; level <= maxCartesian; ++level) {
        iRegs[level] = allocReg();
        emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
    }
    std::vector<u16> condRegs(maxCartesian + 1);
    for (int level = 1; level <= maxCartesian; ++level) {
        condRegs[level] = allocReg();
    }

    // Allocate outer result array
    u16 outerResultReg = allocReg();
    emitOp(op_array_alloc); emitRegs(outerResultReg, lenRegs[1]); emitPtr(outerResultType);

    // --- Phase 4: Open all loops ---
    std::vector<u16> resultRegs(maxCartesian + 1, 0);
    std::vector<u32> loopStarts(maxCartesian + 1, 0);
    std::vector<u32> loopExits(maxCartesian + 1, 0);
    resultRegs[1] = outerResultReg;
    for (int level = 1; level <= maxCartesian; ++level) {
        if (level > 1) {
            resultRegs[level] = allocReg();
            emitOp(op_array_alloc); emitRegs(resultRegs[level], lenRegs[level]); emitPtr(resultTypes[level]);
            emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
        }
        loopStarts[level] = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int); emitRegs(condRegs[level], iRegs[level], lenRegs[level]);
        loopExits[level] = emitJump(op_jump_if_false, condRegs[level]);
    }

    // --- Phase 5: Build tuple fields ---
    u16 tupleFieldBase = nextReg_;
    for (size_t i = 0; i < count; ++i) {
        u16 targetReg = tupleFieldBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }
        Type* fieldType = tupType->fields_[i];
        int ci = expr->autoMapElements[i].cartesianIndex;
        if (ci > 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, elemRegs[i], iRegs[ci]); emitPtr(arrT);
            Type* srcElem = arrT->elemType_;
            if (srcElem != fieldType) {
                u16 promoted = ensureType(targetReg, srcElem, fieldType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else if (expr->autoMapElements[i].depth > 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->elements[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, elemRegs[i], iRegs[1]); emitPtr(arrT);
            Type* srcElem = arrT->elemType_;
            if (srcElem != fieldType) {
                u16 promoted = ensureType(targetReg, srcElem, fieldType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else {
            Type* valType = expr->elements[i]->resolvedType;
            u16 srcReg = ensureType(elemRegs[i], valType, fieldType);
            if (srcReg != targetReg) { emitMov(targetReg, srcReg); }
        }
    }

    // Construct tuple. Phase 4g.2: Inline tuple lands multi-word.
    bool inlineTuple = tupType->repr_ == ts::Type::Repr::Inline;
    u16 tupReg = inlineTuple ? allocSlot(tupType) : allocReg();
    emitOp(op_make_tuple); emitRegs(tupReg, tupleFieldBase, (u16)count); emitPtr(tupType);

    // --- Phase 6: Close loops (innermost to outermost) ---
    {
        // Phase 4g.8: InlineArray takes the multi-word inline tuple directly.
        (void)inlineTuple;
        u16 prevReg = tupReg;
        for (int level = maxCartesian; level >= 1; --level) {
            emitOp(opArraySetFor(resultTypes[level]->elemType_)); emitRegs(resultRegs[level], iRegs[level], prevReg); emitPtr(resultTypes[level]);
            emitOp(op_add_int); emitRegs(iRegs[level], iRegs[level], oneReg);
            emitJumpTo(loopStarts[level]);
            patchJump(loopExits[level]);
            prevReg = resultRegs[level];
        }
    }

    return outerResultReg;
}

// ============================================================
// Cartesian (@1/@2) for struct literals
// ============================================================
u16 CodeGen::genCartesianStructLiteral(StructLiteralExpr* expr) {
    auto* outerResultType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!outerResultType) {
        error(expr->loc, "Cartesian struct literal has non-array resolved type");
        return allocReg();
    }

    int maxCartesian = 0;
    for (auto& am : expr->autoMapFields) {
        if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
    }

    // Peel array layers to get the struct type
    Type* inner = outerResultType;
    for (int i = 0; i < maxCartesian; ++i) {
        auto* arrT = dynamic_cast<ArrayType*>(inner);
        if (!arrT) { error(expr->loc, "Cartesian struct type nesting error"); return allocReg(); }
        inner = arrT->elemType_;
    }
    auto* stype = dynamic_cast<StructType*>(inner);
    if (!stype) {
        error(expr->loc, "Cartesian struct literal inner type is not a struct");
        return allocReg();
    }

    // Compute result types for each loop level
    std::vector<ArrayType*> resultTypes(maxCartesian + 1, nullptr);
    {
        ArrayType* rt = outerResultType;
        for (int level = 1; level <= maxCartesian; ++level) {
            resultTypes[level] = rt;
            if (level < maxCartesian)
                rt = dynamic_cast<ArrayType*>(rt->elemType_);
        }
    }
    usize numFields = stype->fields_.size();

    // Build mapping from field name to literal index
    std::unordered_map<std::string, size_t> litFieldMap;
    for (size_t i = 0; i < expr->fields.size(); ++i) {
        litFieldMap[expr->fields[i].name] = i;
    }
    std::vector<size_t> declOrder(numFields, SIZE_MAX);
    for (size_t i = 0; i < numFields; ++i) {
        std::string fieldName(stype->fields_[i].name->str());
        auto it = litFieldMap.find(fieldName);
        if (it != litFieldMap.end()) declOrder[i] = it->second;
    }

    // --- Phase 1: Evaluate all field expressions ---
    std::vector<u16> fieldRegs(expr->fields.size());
    for (size_t i = 0; i < expr->fields.size(); ++i) {
        fieldRegs[i] = genExpr(static_cast<Expr*>(expr->fields[i].value.get()));
    }

    // --- Phase 2: Get lengths for each cartesian level ---
    std::vector<u16> lenRegs(maxCartesian + 1, 0);
    for (size_t i = 0; i < expr->fields.size(); ++i) {
        int ci = expr->autoMapFields[i].cartesianIndex;
        if (ci > 0 && lenRegs[ci] == 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->fields[i].value->resolvedType);
            u16 lenReg = allocReg();
            emitOp(opArrayLengthFor(arrT->elemType_)); emitRegs(lenReg, fieldRegs[i]); emitPtr(arrT);
            lenRegs[ci] = lenReg;
        }
    }

    // --- Phase 3: Loop counters ---
    std::vector<u16> iRegs(maxCartesian + 1);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const); emitRegs(oneReg); emitInt(1);
    for (int level = 1; level <= maxCartesian; ++level) {
        iRegs[level] = allocReg();
        emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
    }
    std::vector<u16> condRegs(maxCartesian + 1);
    for (int level = 1; level <= maxCartesian; ++level) {
        condRegs[level] = allocReg();
    }

    // Allocate outer result array
    u16 outerResultReg = allocReg();
    emitOp(op_array_alloc); emitRegs(outerResultReg, lenRegs[1]); emitPtr(outerResultType);

    // --- Phase 4: Open all loops ---
    std::vector<u16> resultRegs(maxCartesian + 1, 0);
    std::vector<u32> loopStarts(maxCartesian + 1, 0);
    std::vector<u32> loopExits(maxCartesian + 1, 0);
    resultRegs[1] = outerResultReg;
    for (int level = 1; level <= maxCartesian; ++level) {
        if (level > 1) {
            resultRegs[level] = allocReg();
            emitOp(op_array_alloc); emitRegs(resultRegs[level], lenRegs[level]); emitPtr(resultTypes[level]);
            emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
        }
        loopStarts[level] = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int); emitRegs(condRegs[level], iRegs[level], lenRegs[level]);
        loopExits[level] = emitJump(op_jump_if_false, condRegs[level]);
    }

    // --- Phase 5: Build struct fields in declaration order ---
    u16 structFieldBase = nextReg_;
    for (size_t i = 0; i < numFields; ++i) {
        size_t litIdx = declOrder[i];
        u16 targetReg = structFieldBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }
        Type* declType = stype->fields_[i].type;
        if (litIdx == SIZE_MAX) {
            // Missing field — zero
            emitOp(op_load_nil); emitRegs(targetReg);
        } else {
            int ci = expr->autoMapFields[litIdx].cartesianIndex;
            if (ci > 0) {
                auto* arrT = dynamic_cast<ArrayType*>(expr->fields[litIdx].value->resolvedType);
                emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, fieldRegs[litIdx], iRegs[ci]); emitPtr(arrT);
                Type* srcElem = arrT->elemType_;
                if (srcElem != declType) {
                    u16 promoted = ensureType(targetReg, srcElem, declType);
                    if (promoted != targetReg) { emitMov(targetReg, promoted); }
                }
            } else if (expr->autoMapFields[litIdx].depth > 0) {
                // Plain @ in cartesian context — zip with level 1
                auto* arrT = dynamic_cast<ArrayType*>(expr->fields[litIdx].value->resolvedType);
                emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, fieldRegs[litIdx], iRegs[1]); emitPtr(arrT);
                Type* srcElem = arrT->elemType_;
                if (srcElem != declType) {
                    u16 promoted = ensureType(targetReg, srcElem, declType);
                    if (promoted != targetReg) { emitMov(targetReg, promoted); }
                }
            } else {
                // Scalar
                u16 srcReg = ensureType(fieldRegs[litIdx], expr->fields[litIdx].value->resolvedType, declType);
                if (srcReg != targetReg) { emitMov(targetReg, srcReg); }
            }
        }
    }

    // Construct struct. Phase 4g.2: Inline struct lands multi-word.
    bool inlineStruct = stype->repr_ == ts::Type::Repr::Inline;
    u16 structReg = inlineStruct ? allocSlot(stype) : allocReg();
    emitOp(op_make_struct); emitRegs(structReg, structFieldBase, (u16)numFields); emitPtr(stype);

    // --- Phase 6: Close loops (innermost to outermost) ---
    {
        // ObjArray expects 1-Word per element; box Inline composite first.
        // Phase 4g.8: InlineArray takes the multi-word inline struct directly.
        u16 storeReg = structReg;
        u16 prevReg = storeReg;
        for (int level = maxCartesian; level >= 1; --level) {
            emitOp(opArraySetFor(resultTypes[level]->elemType_)); emitRegs(resultRegs[level], iRegs[level], prevReg); emitPtr(resultTypes[level]);
            emitOp(op_add_int); emitRegs(iRegs[level], iRegs[level], oneReg);
            emitJumpTo(loopStarts[level]);
            patchJump(loopExits[level]);
            prevReg = resultRegs[level];
        }
    }

    return outerResultReg;
}

// ============================================================
// Cartesian (@1/@2) for tuple struct construction
// ============================================================
u16 CodeGen::genCartesianTupleStruct(CallExpr_* expr) {
    auto* outerResultType = dynamic_cast<ArrayType*>(expr->resolvedType);
    if (!outerResultType) {
        error(expr->loc, "Cartesian tuple struct has non-array resolved type");
        return allocReg();
    }

    int maxCartesian = 0;
    for (auto& am : expr->autoMapArgs) {
        if (am.cartesianIndex > maxCartesian) maxCartesian = am.cartesianIndex;
    }

    // Peel array layers to get the struct type
    Type* inner = outerResultType;
    for (int i = 0; i < maxCartesian; ++i) {
        auto* arrT = dynamic_cast<ArrayType*>(inner);
        if (!arrT) { error(expr->loc, "Cartesian tuple struct type nesting error"); return allocReg(); }
        inner = arrT->elemType_;
    }
    auto* stype = dynamic_cast<StructType*>(inner);
    if (!stype) {
        error(expr->loc, "Cartesian tuple struct inner type is not a struct");
        return allocReg();
    }

    // Compute result types for each loop level
    std::vector<ArrayType*> resultTypes(maxCartesian + 1, nullptr);
    {
        ArrayType* rt = outerResultType;
        for (int level = 1; level <= maxCartesian; ++level) {
            resultTypes[level] = rt;
            if (level < maxCartesian)
                rt = dynamic_cast<ArrayType*>(rt->elemType_);
        }
    }
    usize numFields = stype->fields_.size();

    // --- Phase 1: Evaluate all arg expressions ---
    std::vector<u16> argRegs(expr->args.size());
    for (size_t i = 0; i < expr->args.size(); ++i) {
        argRegs[i] = genExpr(static_cast<Expr*>(expr->args[i].get()));
    }

    // --- Phase 2: Get lengths for each cartesian level ---
    std::vector<u16> lenRegs(maxCartesian + 1, 0);
    for (size_t i = 0; i < expr->args.size(); ++i) {
        int ci = expr->autoMapArgs[i].cartesianIndex;
        if (ci > 0 && lenRegs[ci] == 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            u16 lenReg = allocReg();
            emitOp(opArrayLengthFor(arrT->elemType_)); emitRegs(lenReg, argRegs[i]); emitPtr(arrT);
            lenRegs[ci] = lenReg;
        }
    }

    // --- Phase 3: Loop counters ---
    std::vector<u16> iRegs(maxCartesian + 1);
    u16 oneReg = allocReg();
    emitOp(op_load_int_const); emitRegs(oneReg); emitInt(1);
    for (int level = 1; level <= maxCartesian; ++level) {
        iRegs[level] = allocReg();
        emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
    }
    std::vector<u16> condRegs(maxCartesian + 1);
    for (int level = 1; level <= maxCartesian; ++level) {
        condRegs[level] = allocReg();
    }

    // Allocate outer result array
    u16 outerResultReg = allocReg();
    emitOp(op_array_alloc); emitRegs(outerResultReg, lenRegs[1]); emitPtr(outerResultType);

    // --- Phase 4: Open all loops ---
    std::vector<u16> resultRegs(maxCartesian + 1, 0);
    std::vector<u32> loopStarts(maxCartesian + 1, 0);
    std::vector<u32> loopExits(maxCartesian + 1, 0);
    resultRegs[1] = outerResultReg;
    for (int level = 1; level <= maxCartesian; ++level) {
        if (level > 1) {
            resultRegs[level] = allocReg();
            emitOp(op_array_alloc); emitRegs(resultRegs[level], lenRegs[level]); emitPtr(resultTypes[level]);
            emitOp(op_load_int_const); emitRegs(iRegs[level]); emitInt(0);
        }
        loopStarts[level] = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int); emitRegs(condRegs[level], iRegs[level], lenRegs[level]);
        loopExits[level] = emitJump(op_jump_if_false, condRegs[level]);
    }

    // --- Phase 5: Build struct fields ---
    u16 structFieldBase = nextReg_;
    for (size_t i = 0; i < numFields; ++i) {
        u16 targetReg = structFieldBase + (u16)i;
        if (nextReg_ <= targetReg) {
            nextReg_ = targetReg + 1;
            if (nextReg_ > maxReg_) maxReg_ = nextReg_;
        }
        Type* declType = stype->fields_[i].type;
        int ci = expr->autoMapArgs[i].cartesianIndex;
        if (ci > 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, argRegs[i], iRegs[ci]); emitPtr(arrT);
            Type* srcElem = arrT->elemType_;
            if (srcElem != declType) {
                u16 promoted = ensureType(targetReg, srcElem, declType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else if (expr->autoMapArgs[i].depth > 0) {
            auto* arrT = dynamic_cast<ArrayType*>(expr->args[i]->resolvedType);
            emitOp(opArrayGetDynFor(arrT->elemType_)); emitRegs(targetReg, argRegs[i], iRegs[1]); emitPtr(arrT);
            Type* srcElem = arrT->elemType_;
            if (srcElem != declType) {
                u16 promoted = ensureType(targetReg, srcElem, declType);
                if (promoted != targetReg) { emitMov(targetReg, promoted); }
            }
        } else {
            u16 srcReg = ensureType(argRegs[i], expr->args[i]->resolvedType, declType);
            if (srcReg != targetReg) { emitMov(targetReg, srcReg); }
        }
    }

    // Construct struct (skip allocation for UnwrappedTupleStruct).
    // Phase 4g.2: Inline struct lands multi-word.
    u16 structReg;
    bool inlineStruct = stype->repr_ == ts::Type::Repr::Inline;
    if (stype->repr_ == ts::Type::Repr::UnwrappedTupleStruct && numFields == 1) {
        structReg = structFieldBase;
    } else {
        structReg = inlineStruct ? allocSlot(stype) : allocReg();
        emitOp(op_make_struct); emitRegs(structReg, structFieldBase, (u16)numFields); emitPtr(stype);
    }

    // --- Phase 6: Close loops (innermost to outermost) ---
    {
        // Phase 4g.8: InlineArray takes the multi-word inline struct directly.
        u16 storeReg = structReg;
        u16 prevReg = storeReg;
        for (int level = maxCartesian; level >= 1; --level) {
            emitOp(opArraySetFor(resultTypes[level]->elemType_)); emitRegs(resultRegs[level], iRegs[level], prevReg); emitPtr(resultTypes[level]);
            emitOp(op_add_int); emitRegs(iRegs[level], iRegs[level], oneReg);
            emitJumpTo(loopStarts[level]);
            patchJump(loopExits[level]);
            prevReg = resultRegs[level];
        }
    }

    return outerResultReg;
}

u16 CodeGen::genIndexExpr(IndexExpr_* expr) {
    // Auto-mapped indexing dispatch
    if (expr->autoMap) {
        if (expr->autoMap.depth > 1)
            return genAutoMapIndexObjDeep(expr, expr->autoMap.depth);
        if (expr->autoMap.isList)
            return genAutoMapIndexObjList(expr);
        return genAutoMapIndexObjArray(expr);
    }
    if (expr->indexAutoMap) {
        if (expr->indexAutoMap.isList)
            return genAutoMapIndexIdxList(expr);
        return genAutoMapIndexIdxArray(expr);
    }

    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    u16 idxReg = genExpr(static_cast<Expr*>(expr->index.get()));

    if (auto* mapType = dynamic_cast<MapType*>(expr->object->resolvedType)) {
        // Map subscript: map[key] -> Option<V>.
        // Phase 4g.11: keys are stored natively in the map.
        // Phase 4g.24: op_map_get_option writes Inline Option results
        // directly into a multi-word dst slot, so allocSlot sizes the dst
        // for the Option's footprint and no follow-up unbox is needed.
        Type* optType = compiler_.optionType(mapType->valueType_);
        u16 dst = allocSlot(optType);
        idxReg = ensureType(idxReg, expr->index->resolvedType, mapType->keyType_);
        emitOp(op_map_get_option);
        emitRegs(dst, objReg, idxReg);
        emitPtr(optType);
        return dst;
    }
    if (expr->object->resolvedType == compiler_.stringType()) {
        // String subscript: string[index] -> byte as Int
        u16 dst = allocReg();
        emitOp(op_string_get_byte);
        emitRegs(dst, objReg, idxReg);
        return dst;
    }
    // Array subscript: array[index]. Phase 4g.8: Inline structs/tuples/enums
    // ride the InlineArray backend and land as multi-word inline values
    // straight into a sizeWords_ register slot.
    auto* arrType = dynamic_cast<ArrayType*>(expr->object->resolvedType);
    Type* elemT = arrType ? arrType->elemType_ : nullptr;
    u16 dst = allocSlot(elemT);
    emitOp(opArrayGetDynFor(arrType->elemType_));
    emitRegs(dst, objReg, idxReg);
    emitPtr(arrType);
    return dst;
}

// --- Auto-mapped index codegen ---

// Compositional helper: emit code to index srcReg by idxReg.
// If indexAutoMap is set, generates an auto-mapped loop over the index.
// Otherwise, generates a scalar index lookup.
u16 CodeGen::emitIndexLookup(u16 srcReg, Type* srcType, u16 idxReg, Type* idxType,
                             const AutoMapArg& indexAutoMap, Type* resultType) {
    if (indexAutoMap && !indexAutoMap.isList) {
        // Array of indices → Array result
        auto* idxArrType = dynamic_cast<ArrayType*>(idxType);

        u16 idxLenReg = allocReg();
        emitOp(opArrayLengthFor(idxArrType->elemType_));
        emitRegs(idxLenReg, idxReg);
        emitPtr(idxArrType);

        auto* resultArrayType = dynamic_cast<ArrayType*>(resultType);
        u16 resReg = allocReg();
        emitOp(op_array_alloc);
        emitRegs(resReg, idxLenReg);
        emitPtr(resultArrayType);

        u16 jReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(jReg);
        emitInt(0);

        u16 jOneReg = allocReg();
        emitOp(op_load_int_const);
        emitRegs(jOneReg);
        emitInt(1);

        u16 jCondReg = allocReg();

        u32 loopStart = (u32)currentBlock_->code.size();
        emitOp(op_cmp_lt_int);
        emitRegs(jCondReg, jReg, idxLenReg);
        u32 exitJump = emitJump(op_jump_if_false, jCondReg);

        u16 idxValReg = allocReg();
        emitOp(opArrayGetDynFor(idxArrType->elemType_));
        emitRegs(idxValReg, idxReg, jReg);
        emitPtr(idxArrType);

        // Scalar lookup. Phase 4g.24: op_map_get_option writes the Option
        // result natively into the multi-word valReg slot, so we just size
        // the slot to the result element type and op_array_set picks up the
        // stride words directly.
        u16 valReg;
        Type* elemResT = resultArrayType->elemType_;
        if (auto* mapType = dynamic_cast<MapType*>(srcType)) {
            Type* optType = compiler_.optionType(mapType->valueType_);
            valReg = allocSlot(optType);
            idxValReg = ensureType(idxValReg, idxArrType->elemType_, mapType->keyType_);
            emitOp(op_map_get_option);
            emitRegs(valReg, srcReg, idxValReg);
            emitPtr(optType);
        } else if (srcType == compiler_.stringType()) {
            valReg = allocReg();
            emitOp(op_string_get_byte);
            emitRegs(valReg, srcReg, idxValReg);
        } else {
            auto* arrType = dynamic_cast<ArrayType*>(srcType);
            valReg = allocSlot(elemResT);
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(valReg, srcReg, idxValReg);
            emitPtr(arrType);
        }

        emitOp(opArraySetFor(resultArrayType->elemType_));
        emitRegs(resReg, jReg, valReg);
        emitPtr(resultArrayType);

        emitOp(op_add_int);
        emitRegs(jReg, jReg, jOneReg);
        emitJumpTo(loopStart);
        patchJump(exitJump);

        return resReg;
    }

    if (indexAutoMap && indexAutoMap.isList) {
        // List of indices → List result
        auto* idxListType = dynamic_cast<ListType*>(idxType);
        auto* resultListType = dynamic_cast<ListType*>(resultType);

        u16 curReg = allocReg();
        emitMov(curReg, idxReg);

        u16 accReg = allocReg();
        emitOp(op_load_nil);
        emitRegs(accReg);

        u32 loopStart = (u32)currentBlock_->code.size();
        u16 nilCheckReg = allocReg();
        emitOp(op_list_is_nil);
        emitRegs(nilCheckReg, curReg);
        u32 exitJump = emitJump(op_jump_if_true, nilCheckReg);

        u16 idxValReg = allocReg();
        emitOp(op_list_head);
        emitRegs(idxValReg, curReg);

        // Scalar lookup. Phase 4g.24: op_map_get_option writes Inline Option
        // results natively into a multi-word slot; allocSlot sizes valReg so
        // op_cons reads the right stride.
        Type* elemResT = resultListType->elemType_;
        u16 valReg = allocSlot(elemResT);
        if (auto* mapType = dynamic_cast<MapType*>(srcType)) {
            idxValReg = ensureType(idxValReg, idxListType->elemType_, mapType->keyType_);
            emitOp(op_map_get_option);
            emitRegs(valReg, srcReg, idxValReg);
            emitPtr(compiler_.optionType(mapType->valueType_));
        } else if (srcType == compiler_.stringType()) {
            emitOp(op_string_get_byte);
            emitRegs(valReg, srcReg, idxValReg);
        } else {
            auto* arrType = dynamic_cast<ArrayType*>(srcType);
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(valReg, srcReg, idxValReg);
            emitPtr(arrType);
        }

        emitOp(op_cons);
        emitRegs(accReg, valReg, accReg);
        emitPtr(resultListType);

        emitOp(op_list_tail);
        emitRegs(curReg, curReg);
        emitJumpTo(loopStart);
        patchJump(exitJump);

        // Reverse
        u16 resReg = allocReg();
        emitOp(op_load_nil);
        emitRegs(resReg);

        u32 revLoopStart = (u32)currentBlock_->code.size();
        u16 revNilCheck = allocReg();
        emitOp(op_list_is_nil);
        emitRegs(revNilCheck, accReg);
        u32 revExitJump = emitJump(op_jump_if_true, revNilCheck);

        // revHead spans the result element's words so op_list_head can
        // write a multi-word Inline head into a contiguous slot window.
        u16 revHead = allocSlot(elemResT);
        emitOp(op_list_head);
        emitRegs(revHead, accReg);
        emitOp(op_cons);
        emitRegs(resReg, revHead, resReg);
        emitPtr(resultListType);
        emitOp(op_list_tail);
        emitRegs(accReg, accReg);
        emitJumpTo(revLoopStart);
        patchJump(revExitJump);

        return resReg;
    }

    // Scalar index lookup. Phase 4g.24: Inline Option results land natively
    // in a sizeWords_-wide dst window, no follow-up unbox needed.
    u16 dst;
    if (auto* mapType = dynamic_cast<MapType*>(srcType)) {
        u16 convertedIdx = ensureType(idxReg, idxType, mapType->keyType_);
        Type* optType = compiler_.optionType(mapType->valueType_);
        dst = allocSlot(optType);
        emitOp(op_map_get_option);
        emitRegs(dst, srcReg, convertedIdx);
        emitPtr(optType);
    } else if (srcType == compiler_.stringType()) {
        dst = allocReg();
        emitOp(op_string_get_byte);
        emitRegs(dst, srcReg, idxReg);
    } else {
        auto* arrType = dynamic_cast<ArrayType*>(srcType);
        dst = allocReg();
        emitOp(opArrayGetDynFor(arrType->elemType_));
        emitRegs(dst, srcReg, idxReg);
        emitPtr(arrType);
    }
    return dst;
}

u16 CodeGen::genAutoMapIndexObjArray(IndexExpr_* expr) {
    // @ on object (Array): map indexing over each element
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    u16 idxReg = genExpr(static_cast<Expr*>(expr->index.get()));
    auto* arrType = dynamic_cast<ArrayType*>(expr->object->resolvedType);
    Type* elemType = arrType->elemType_;

    u16 lenReg = allocReg();
    emitOp(opArrayLengthFor(arrType->elemType_));
    emitRegs(lenReg, objReg);
    emitPtr(arrType);

    auto* resultArrayType = dynamic_cast<ArrayType*>(expr->resolvedType);
    u16 resultReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(resultReg, lenReg);
    emitPtr(resultArrayType);

    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    u16 condReg = allocReg();

    u32 loopStartIdx = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, lenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    u16 elemReg = allocSlot(elemType);
    emitOp(opArrayGetDynFor(arrType->elemType_));
    emitRegs(elemReg, objReg, iReg);
    emitPtr(arrType);

    // Compositional: delegate inner indexing to emitIndexLookup,
    // which handles scalar, array-of-indices, or list-of-indices.
    Type* innerResultType = resultArrayType->elemType_;
    u16 valReg = emitIndexLookup(elemReg, elemType, idxReg,
                                 expr->index->resolvedType,
                                 expr->indexAutoMap, innerResultType);

    // Phase 4g.8: InlineArray accepts multi-word inline directly.
    emitOp(opArraySetFor(resultArrayType->elemType_));
    emitRegs(resultReg, iReg, valReg);
    emitPtr(resultArrayType);

    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);
    emitJumpTo(loopStartIdx);
    patchJump(exitJump);

    return resultReg;
}

u16 CodeGen::genAutoMapIndexObjList(IndexExpr_* expr) {
    // @ on object (List): map indexing over each element
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    u16 idxReg = genExpr(static_cast<Expr*>(expr->index.get()));
    auto* listType = dynamic_cast<ListType*>(expr->object->resolvedType);
    Type* elemType = listType->elemType_;

    auto* resultListType = dynamic_cast<ListType*>(expr->resolvedType);

    u16 curReg = allocReg();
    emitMov(curReg, objReg);

    u16 accReg = allocReg();
    emitOp(op_load_nil);
    emitRegs(accReg);

    u32 loopStartIdx = (u32)currentBlock_->code.size();

    u16 nilCheckReg = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(nilCheckReg, curReg);
    u32 exitJump = emitJump(op_jump_if_true, nilCheckReg);

    u16 headReg = allocReg();
    emitOp(op_list_head);
    emitRegs(headReg, curReg);

    // Compositional: delegate inner indexing to emitIndexLookup
    Type* innerResultType = resultListType->elemType_;
    u16 valReg = emitIndexLookup(headReg, elemType, idxReg,
                                 expr->index->resolvedType,
                                 expr->indexAutoMap, innerResultType);

    // Phase 4g.4: same boxing concern as the array path -- list nodes hold
    // 1-Word values, so an Inline composite result must be boxed first.
    if (isInlineMultiword(innerResultType)) {
        valReg = emitBoxIfInline(valReg, innerResultType);
    }

    emitOp(op_cons);
    emitRegs(accReg, valReg, accReg);
    emitPtr(resultListType);

    emitOp(op_list_tail);
    emitRegs(curReg, curReg);

    emitJumpTo(loopStartIdx);
    patchJump(exitJump);

    // Reverse the accumulated list
    u16 resultReg = allocReg();
    emitOp(op_load_nil);
    emitRegs(resultReg);

    u32 revLoopStart = (u32)currentBlock_->code.size();
    u16 revNilCheck = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(revNilCheck, accReg);
    u32 revExitJump = emitJump(op_jump_if_true, revNilCheck);

    u16 revHead = allocReg();
    emitOp(op_list_head);
    emitRegs(revHead, accReg);
    emitOp(op_cons);
    emitRegs(resultReg, revHead, resultReg);
    emitPtr(resultListType);
    emitOp(op_list_tail);
    emitRegs(accReg, accReg);
    emitJumpTo(revLoopStart);
    patchJump(revExitJump);

    return resultReg;
}

u16 CodeGen::genAutoMapIndexObjDeep(IndexExpr_* expr, int depth) {
    // Deep auto-map indexing (depth > 1): nested Array/List of indexable types
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    u16 idxReg = genExpr(static_cast<Expr*>(expr->index.get()));
    Type* objType = expr->object->resolvedType;

    struct LevelInfo {
        bool isList;
        Type* containerType;
    };
    std::vector<LevelInfo> levels(depth);
    Type* t = objType;
    for (int d = 0; d < depth; ++d) {
        if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
            levels[d] = {false, arrT};
            t = arrT->elemType_;
        } else if (auto* listT = dynamic_cast<ListType*>(t)) {
            levels[d] = {true, listT};
            t = listT->elemType_;
        }
    }
    Type* innerElemType = t;

    std::vector<Type*> resultTypes(depth);
    Type* rt = expr->resolvedType;
    for (int d = 0; d < depth; ++d) {
        resultTypes[d] = rt;
        if (auto* arrT = dynamic_cast<ArrayType*>(rt))
            rt = arrT->elemType_;
        else if (auto* listT = dynamic_cast<ListType*>(rt))
            rt = listT->elemType_;
    }
    // rt is now the innermost result type (for the indexing operation)

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    struct LoopLevel {
        bool isList;
        u16 lenReg, iReg, condReg, resultReg;
        u32 loopStartIdx, exitJump;
        u16 curReg, accReg, nilCheckReg;
    };

    std::vector<LoopLevel> loops(depth);
    u16 currentObjReg = objReg;

    for (int d = 0; d < depth; ++d) {
        auto& loop = loops[d];
        loop.isList = levels[d].isList;

        if (!loop.isList) {
            auto* arrType = dynamic_cast<ArrayType*>(levels[d].containerType);
            loop.lenReg = allocReg();
            emitOp(opArrayLengthFor(arrType->elemType_));
            emitRegs(loop.lenReg, currentObjReg);
            emitPtr(arrType);

            auto* resArrType = dynamic_cast<ArrayType*>(resultTypes[d]);
            loop.resultReg = allocReg();
            emitOp(op_array_alloc);
            emitRegs(loop.resultReg, loop.lenReg);
            emitPtr(resArrType);

            loop.iReg = allocReg();
            emitOp(op_load_int_const);
            emitRegs(loop.iReg);
            emitInt(0);

            loop.condReg = allocReg();

            loop.loopStartIdx = (u32)currentBlock_->code.size();
            emitOp(op_cmp_lt_int);
            emitRegs(loop.condReg, loop.iReg, loop.lenReg);
            loop.exitJump = emitJump(op_jump_if_false, loop.condReg);

            u16 subReg = allocReg();
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(subReg, currentObjReg, loop.iReg);
            emitPtr(arrType);
            currentObjReg = subReg;
        } else {
            loop.curReg = allocReg();
            emitMov(loop.curReg, currentObjReg);

            loop.accReg = allocReg();
            emitOp(op_load_nil);
            emitRegs(loop.accReg);

            loop.nilCheckReg = allocReg();

            loop.loopStartIdx = (u32)currentBlock_->code.size();
            emitOp(op_list_is_nil);
            emitRegs(loop.nilCheckReg, loop.curReg);
            loop.exitJump = emitJump(op_jump_if_true, loop.nilCheckReg);

            u16 headReg = allocReg();
            emitOp(op_list_head);
            emitRegs(headReg, loop.curReg);
            currentObjReg = headReg;
        }
    }

    // Innermost: delegate to emitIndexLookup (handles scalar or auto-mapped)
    u16 valReg = emitIndexLookup(currentObjReg, innerElemType, idxReg,
                                 expr->index->resolvedType,
                                 expr->indexAutoMap, rt);
    // Phase 4g.4: store as boxed 1-Word in container slots (see Phase 4g.4
    // notes in genAutoMapIndexObjArray).
    if (isInlineMultiword(rt)) {
        valReg = emitBoxIfInline(valReg, rt);
    }

    // Close loops from innermost to outermost
    u16 prevResultReg = valReg;
    for (int d = depth - 1; d >= 0; --d) {
        auto& loop = loops[d];
        if (!loop.isList) {
            auto* resArrType = dynamic_cast<ArrayType*>(resultTypes[d]);
            emitOp(opArraySetFor(resArrType->elemType_));
            emitRegs(loop.resultReg, loop.iReg, prevResultReg);
            emitPtr(resArrType);

            emitOp(op_add_int);
            emitRegs(loop.iReg, loop.iReg, oneReg);
            emitJumpTo(loop.loopStartIdx);
            patchJump(loop.exitJump);

            prevResultReg = loop.resultReg;
        } else {
            auto* resListType = dynamic_cast<ListType*>(resultTypes[d]);
            emitOp(op_cons);
            emitRegs(loop.accReg, prevResultReg, loop.accReg);
            emitPtr(resListType);

            emitOp(op_list_tail);
            emitRegs(loop.curReg, loop.curReg);
            emitJumpTo(loop.loopStartIdx);
            patchJump(loop.exitJump);

            // Reverse
            u16 revResultReg = allocReg();
            emitOp(op_load_nil);
            emitRegs(revResultReg);

            u32 revLoopStart = (u32)currentBlock_->code.size();
            u16 revNilCheck = allocReg();
            emitOp(op_list_is_nil);
            emitRegs(revNilCheck, loop.accReg);
            u32 revExitJump = emitJump(op_jump_if_true, revNilCheck);

            u16 revHead = allocReg();
            emitOp(op_list_head);
            emitRegs(revHead, loop.accReg);
            emitOp(op_cons);
            emitRegs(revResultReg, revHead, revResultReg);
            emitPtr(resListType);
            emitOp(op_list_tail);
            emitRegs(loop.accReg, loop.accReg);
            emitJumpTo(revLoopStart);
            patchJump(revExitJump);

            prevResultReg = revResultReg;
        }
    }

    return prevResultReg;
}

u16 CodeGen::genAutoMapIndexIdxArray(IndexExpr_* expr) {
    // Index is Array of indices (standalone, no @): delegate to emitIndexLookup
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    u16 idxReg = genExpr(static_cast<Expr*>(expr->index.get()));
    return emitIndexLookup(objReg, expr->object->resolvedType, idxReg,
                           expr->index->resolvedType,
                           expr->indexAutoMap, expr->resolvedType);
}

u16 CodeGen::genAutoMapIndexIdxList(IndexExpr_* expr) {
    // Index is List of indices (standalone, no @): delegate to emitIndexLookup
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    u16 idxReg = genExpr(static_cast<Expr*>(expr->index.get()));
    return emitIndexLookup(objReg, expr->object->resolvedType, idxReg,
                           expr->index->resolvedType,
                           expr->indexAutoMap, expr->resolvedType);
}

u16 CodeGen::genFieldExpr(FieldExpr_* expr) {
    // Check for module-qualified or std-qualified access: module.name or std.name -> load global directly
    if (expr->object->kind == ASTNode::Identifier) {
        auto* ident = static_cast<IdentifierExpr*>(expr->object.get());

        const auto& importedModules = typeChecker_.importedModules();
        auto modIt = importedModules.find(ident->name);
        if (modIt != importedModules.end()) {
            ModuleInfo* mod = modIt->second;
            auto expIt = mod->exports.find(expr->field);
            if (expIt != mod->exports.end()) {
                u16 dst = allocReg();
                emitOp(op_load_global);
                emitRegs(dst);
                emitInt(expIt->second.globalIndex);
                return dst;
            }
            error(expr->loc, "Module '" + ident->name +
                  "' does not export '" + expr->field + "'");
            return allocReg();
        }
    }

    // Auto-mapped field access
    if (expr->autoMap) {
        if (expr->autoMap.depth > 1)
            return genAutoMapFieldDeep(expr, expr->autoMap.depth);
        if (expr->autoMap.isList)
            return genAutoMapFieldList(expr);
        return genAutoMapFieldArray(expr);
    }

    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    Type* objType = expr->object->resolvedType;

    // Handle struct field access
    if (auto* stype = dynamic_cast<StructType*>(objType)) {
        // Phase 1: UnwrappedTupleStruct -- value already IS the inner; field
        // index must be 0 and access is a no-op.
        if (stype->repr_ == ts::Type::Repr::UnwrappedTupleStruct
            && stype->isTupleStruct_ && stype->fields_.size() == 1) {
            return objReg;
        }
        bool inlineParent = stype->repr_ == ts::Type::Repr::Inline;
        auto emitGet = [&](size_t i, Type* ft) -> u16 {
            // Phase 4g.2: parent dispatch -- inline parents read fields
            // directly out of the multi-word slot via op_inline_struct_get;
            // Heap parents read out of the boxed Struct's v[] then unbox
            // any inline-typed field (Phase 4f).
            if (inlineParent) {
                u16 dst = allocSlot(ft);
                emitOp(op_inline_struct_get);
                emitRegs(dst, objReg, (u16)i);
                emitPtr(stype);
                return dst;
            }
            // Phase 4g.13: heap Struct stores fields natively, so the field
            // value lands directly in a multi-word slot -- no unbox needed.
            u16 dst = allocSlot(ft);
            emitOp(op_struct_get);
            emitRegs(dst, objReg, (u16)i);
            emitPtr(stype);
            return dst;
        };
        // Find field index by name
        for (size_t i = 0; i < stype->fields_.size(); ++i) {
            if (stype->fields_[i].name->str() == expr->field) {
                return emitGet(i, stype->fields_[i].type);
            }
        }
        // For tuple structs, allow numeric field access
        if (stype->isTupleStruct_) {
            size_t idx = std::stoul(expr->field);
            Type* ft = (idx < stype->fields_.size()) ? stype->fields_[idx].type : nullptr;
            return emitGet(idx, ft);
        }
        error(expr->loc, "No field '" + expr->field + "' in struct");
        return allocReg();
    }

    // Handle tuple field access (by numeric index)
    if (auto* ttype = dynamic_cast<TupleType*>(objType)) {
        size_t idx = std::stoul(expr->field);
        Type* ft = (idx < ttype->fields_.size()) ? ttype->fields_[idx] : nullptr;
        bool inlineParent = ttype->repr_ == ts::Type::Repr::Inline;
        if (inlineParent) {
            u16 dst = allocSlot(ft);
            emitOp(op_inline_tuple_get);
            emitRegs(dst, objReg, (u16)idx);
            emitPtr(ttype);
            return dst;
        }
        // Phase 4g.13: heap Tuple stores fields natively.
        u16 dst = allocSlot(ft);
        emitOp(op_tuple_get);
        emitRegs(dst, objReg, (u16)idx);
        emitPtr(ttype);
        return dst;
    }

    error(expr->loc, "Codegen: field access not supported on this type");
    return allocReg();
}

u16 CodeGen::genAutoMapFieldArray(FieldExpr_* expr) {
    // Auto-map field access over an Array: [struct1, struct2, ...].field -> [f1, f2, ...]
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    Type* objType = expr->object->resolvedType;
    auto* arrType = dynamic_cast<ArrayType*>(objType);

    // Get element type (struct or tuple)
    Type* elemType = arrType->elemType_;

    // Get array length
    u16 lenReg = allocReg();
    emitOp(opArrayLengthFor(arrType->elemType_));
    emitRegs(lenReg, objReg);
    emitPtr(arrType);

    // Allocate result array
    auto* resultArrayType = dynamic_cast<ArrayType*>(expr->resolvedType);
    u16 resultReg = allocReg();
    emitOp(op_array_alloc);
    emitRegs(resultReg, lenReg);
    emitPtr(resultArrayType);

    // Loop counter
    u16 iReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(iReg);
    emitInt(0);

    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    u16 condReg = allocReg();

    // Loop start
    u32 loopStartIdx = (u32)currentBlock_->code.size();
    emitOp(op_cmp_lt_int);
    emitRegs(condReg, iReg, lenReg);
    u32 exitJump = emitJump(op_jump_if_false, condReg);

    // Extract element from array. Phase 4g.8: Inline elements occupy
    // sizeWords_ consecutive register slots.
    u16 elemReg = allocSlot(elemType);
    emitOp(opArrayGetDynFor(arrType->elemType_));
    emitRegs(elemReg, objReg, iReg);
    emitPtr(arrType);

    // Extract field from element via emitFieldGet (handles inline parents).
    u16 fieldReg = 0;
    if (auto* stype = dynamic_cast<StructType*>(elemType)) {
        for (size_t i = 0; i < stype->fields_.size(); ++i) {
            if (stype->fields_[i].name->str() == expr->field) {
                fieldReg = emitFieldGet(elemType, elemReg, (u16)i, stype->fields_[i].type);
                break;
            }
        }
    } else if (auto* ttype = dynamic_cast<TupleType*>(elemType)) {
        size_t idx = std::stoul(expr->field);
        fieldReg = emitFieldGet(elemType, elemReg, (u16)idx, ttype->fields_[idx]);
    }

    // Store in result array
    emitOp(opArraySetFor(resultArrayType->elemType_));
    emitRegs(resultReg, iReg, fieldReg);
    emitPtr(resultArrayType);

    // Increment and loop
    emitOp(op_add_int);
    emitRegs(iReg, iReg, oneReg);
    emitJumpTo(loopStartIdx);
    patchJump(exitJump);

    return resultReg;
}

u16 CodeGen::genAutoMapFieldList(FieldExpr_* expr) {
    // Auto-map field access over a List: List(s1, s2, ...).field -> List(f1, f2, ...)
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    Type* objType = expr->object->resolvedType;
    auto* listType = dynamic_cast<ListType*>(objType);

    // Get element type (struct or tuple)
    Type* elemType = listType->elemType_;

    auto* resultListType = dynamic_cast<ListType*>(expr->resolvedType);

    // Current list pointer
    u16 curReg = allocReg();
    emitMov(curReg, objReg);

    // Accumulator (reversed result)
    u16 accReg = allocReg();
    emitOp(op_load_nil);
    emitRegs(accReg);

    // Loop start
    u32 loopStartIdx = (u32)currentBlock_->code.size();

    // Check if list is nil
    u16 nilCheckReg = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(nilCheckReg, curReg);
    u32 exitJump = emitJump(op_jump_if_true, nilCheckReg);

    // Extract head. Phase 4g.9: op_list_head copies stride words directly
    // into a slot sized for elemType, including Inline composite heads.
    u16 headReg = allocSlot(elemType);
    emitOp(op_list_head);
    emitRegs(headReg, curReg);

    // Extract field from head
    u16 fieldReg = 0;
    if (auto* stype = dynamic_cast<StructType*>(elemType)) {
        for (size_t i = 0; i < stype->fields_.size(); ++i) {
            if (stype->fields_[i].name->str() == expr->field) {
                fieldReg = emitFieldGet(elemType, headReg, (u16)i, stype->fields_[i].type);
                break;
            }
        }
    } else if (auto* ttype = dynamic_cast<TupleType*>(elemType)) {
        size_t idx = std::stoul(expr->field);
        fieldReg = emitFieldGet(elemType, headReg, (u16)idx, ttype->fields_[idx]);
    }

    // Cons onto accumulator (builds reversed)
    emitOp(op_cons);
    emitRegs(accReg, fieldReg, accReg);
    emitPtr(resultListType);

    // Advance to tail
    emitOp(op_list_tail);
    emitRegs(curReg, curReg);

    // Loop back
    emitJumpTo(loopStartIdx);
    patchJump(exitJump);

    // Reverse the accumulated list
    u16 resultReg = allocReg();
    emitOp(op_load_nil);
    emitRegs(resultReg);

    u32 revLoopStart = (u32)currentBlock_->code.size();
    u16 revNilCheck = allocReg();
    emitOp(op_list_is_nil);
    emitRegs(revNilCheck, accReg);
    u32 revExitJump = emitJump(op_jump_if_true, revNilCheck);

    u16 revHead = allocReg();
    emitOp(op_list_head);
    emitRegs(revHead, accReg);
    emitOp(op_cons);
    emitRegs(resultReg, revHead, resultReg);
    emitPtr(resultListType);
    emitOp(op_list_tail);
    emitRegs(accReg, accReg);
    emitJumpTo(revLoopStart);
    patchJump(revExitJump);

    return resultReg;
}

u16 CodeGen::genAutoMapFieldDeep(FieldExpr_* expr, int depth) {
    // Deep auto-map field access (depth > 1): nested Array/List of structs/tuples
    // Derive container types at each level from expr->object->resolvedType
    u16 objReg = genExpr(static_cast<Expr*>(expr->object.get()));
    Type* objType = expr->object->resolvedType;

    // Collect container info at each level
    struct LevelInfo {
        bool isList;
        Type* containerType;  // ArrayType* or ListType* at this level
    };
    std::vector<LevelInfo> levels(depth);
    Type* t = objType;
    for (int d = 0; d < depth; ++d) {
        if (auto* arrT = dynamic_cast<ArrayType*>(t)) {
            levels[d] = {false, arrT};
            t = arrT->elemType_;
        } else if (auto* listT = dynamic_cast<ListType*>(t)) {
            levels[d] = {true, listT};
            t = listT->elemType_;
        }
    }
    // t is now the innermost element type (struct or tuple)
    Type* innerElemType = t;

    // Collect result container types at each level from expr->resolvedType
    std::vector<Type*> resultTypes(depth);
    Type* rt = expr->resolvedType;
    for (int d = 0; d < depth; ++d) {
        resultTypes[d] = rt;
        if (auto* arrT = dynamic_cast<ArrayType*>(rt))
            rt = arrT->elemType_;
        else if (auto* listT = dynamic_cast<ListType*>(rt))
            rt = listT->elemType_;
    }

    // Constants
    u16 oneReg = allocReg();
    emitOp(op_load_int_const);
    emitRegs(oneReg);
    emitInt(1);

    // Build nested loops
    struct LoopLevel {
        bool isList;
        // For Array loops:
        u16 lenReg, iReg, condReg, resultReg;
        u32 loopStartIdx, exitJump;
        // For List loops:
        u16 curReg, accReg, nilCheckReg;
    };

    std::vector<LoopLevel> loops(depth);
    u16 currentObjReg = objReg;

    for (int d = 0; d < depth; ++d) {
        auto& loop = loops[d];
        loop.isList = levels[d].isList;

        if (!loop.isList) {
            // Array loop
            auto* arrType = dynamic_cast<ArrayType*>(levels[d].containerType);
            loop.lenReg = allocReg();
            emitOp(opArrayLengthFor(arrType->elemType_));
            emitRegs(loop.lenReg, currentObjReg);
            emitPtr(arrType);

            auto* resArrType = dynamic_cast<ArrayType*>(resultTypes[d]);
            loop.resultReg = allocReg();
            emitOp(op_array_alloc);
            emitRegs(loop.resultReg, loop.lenReg);
            emitPtr(resArrType);

            loop.iReg = allocReg();
            emitOp(op_load_int_const);
            emitRegs(loop.iReg);
            emitInt(0);

            loop.condReg = allocReg();

            loop.loopStartIdx = (u32)currentBlock_->code.size();
            emitOp(op_cmp_lt_int);
            emitRegs(loop.condReg, loop.iReg, loop.lenReg);
            loop.exitJump = emitJump(op_jump_if_false, loop.condReg);

            // Extract sub-element. Phase 4g.8: peeled elements may be
            // multi-word inline composites.
            Type* peeled = arrType->elemType_;
            u16 subReg = allocSlot(peeled);
            emitOp(opArrayGetDynFor(arrType->elemType_));
            emitRegs(subReg, currentObjReg, loop.iReg);
            emitPtr(arrType);
            currentObjReg = subReg;
        } else {
            // List loop
            loop.curReg = allocReg();
            emitMov(loop.curReg, currentObjReg);

            loop.accReg = allocReg();
            emitOp(op_load_nil);
            emitRegs(loop.accReg);

            loop.nilCheckReg = allocReg();

            loop.loopStartIdx = (u32)currentBlock_->code.size();
            emitOp(op_list_is_nil);
            emitRegs(loop.nilCheckReg, loop.curReg);
            loop.exitJump = emitJump(op_jump_if_true, loop.nilCheckReg);

            // Extract head. Phase 4g.9: op_list_head now copies stride words
            // directly into the destination; reserve a slot sized to elemType.
            auto* curListT = dynamic_cast<ListType*>(levels[d].containerType);
            Type* peeled = curListT ? curListT->elemType_ : nullptr;
            u16 headReg = allocSlot(peeled);
            emitOp(op_list_head);
            emitRegs(headReg, loop.curReg);
            currentObjReg = headReg;
        }
    }

    // Innermost: extract field from the struct/tuple element.
    // Phase 4g.8: emitFieldGet handles inline parent types.
    u16 fieldReg = 0;
    if (auto* stype = dynamic_cast<StructType*>(innerElemType)) {
        for (size_t i = 0; i < stype->fields_.size(); ++i) {
            if (stype->fields_[i].name->str() == expr->field) {
                fieldReg = emitFieldGet(innerElemType, currentObjReg, (u16)i, stype->fields_[i].type);
                break;
            }
        }
    } else if (auto* ttype = dynamic_cast<TupleType*>(innerElemType)) {
        size_t idx = std::stoul(expr->field);
        fieldReg = emitFieldGet(innerElemType, currentObjReg, (u16)idx, ttype->fields_[idx]);
    }

    // Close loops from innermost to outermost
    u16 prevResultReg = fieldReg;
    for (int d = depth - 1; d >= 0; --d) {
        auto& loop = loops[d];
        if (!loop.isList) {
            // Array: store result, increment, loop back
            auto* resArrType = dynamic_cast<ArrayType*>(resultTypes[d]);
            emitOp(opArraySetFor(resArrType->elemType_));
            emitRegs(loop.resultReg, loop.iReg, prevResultReg);
            emitPtr(resArrType);

            emitOp(op_add_int);
            emitRegs(loop.iReg, loop.iReg, oneReg);
            emitJumpTo(loop.loopStartIdx);
            patchJump(loop.exitJump);

            prevResultReg = loop.resultReg;
        } else {
            // List: cons onto accumulator, advance tail, loop back, then reverse
            auto* resListType = dynamic_cast<ListType*>(resultTypes[d]);
            emitOp(op_cons);
            emitRegs(loop.accReg, prevResultReg, loop.accReg);
            emitPtr(resListType);

            emitOp(op_list_tail);
            emitRegs(loop.curReg, loop.curReg);
            emitJumpTo(loop.loopStartIdx);
            patchJump(loop.exitJump);

            // Reverse
            u16 revResultReg = allocReg();
            emitOp(op_load_nil);
            emitRegs(revResultReg);

            u32 revLoopStart = (u32)currentBlock_->code.size();
            u16 revNilCheck = allocReg();
            emitOp(op_list_is_nil);
            emitRegs(revNilCheck, loop.accReg);
            u32 revExitJump = emitJump(op_jump_if_true, revNilCheck);

            u16 revHead = allocReg();
            emitOp(op_list_head);
            emitRegs(revHead, loop.accReg);
            emitOp(op_cons);
            emitRegs(revResultReg, revHead, revResultReg);
            emitPtr(resListType);
            emitOp(op_list_tail);
            emitRegs(loop.accReg, loop.accReg);
            emitJumpTo(revLoopStart);
            patchJump(revExitJump);

            prevResultReg = revResultReg;
        }
    }

    return prevResultReg;
}

u16 CodeGen::genEnumConstruct(ASTNode* node) {
    auto* etype = dynamic_cast<EnumType*>(node->resolvedType);
    if (!etype) {
        error(node->loc, "Enum construct has non-enum resolved type");
        return allocReg();
    }

    // Determine enum name, case name, and optional argument
    std::string caseName;
    Expr* argExpr = nullptr;

    if (auto* fe = dynamic_cast<FieldExpr_*>(node)) {
        // No-data case: EnumName.caseName (re-tagged FieldExpr_)
        caseName = fe->field;
    } else if (auto* ce = dynamic_cast<CallExpr_*>(node)) {
        // Data case: EnumName.caseName(value) (re-tagged CallExpr_)
        auto* fe2 = static_cast<FieldExpr_*>(ce->callee.get());
        caseName = fe2->field;
        if (!ce->args.empty()) {
            argExpr = static_cast<Expr*>(ce->args[0].get());
        }
    } else if (auto* ec = dynamic_cast<EnumConstructExpr*>(node)) {
        // Direct EnumConstructExpr node
        caseName = ec->caseName;
        argExpr = ec->arg ? static_cast<Expr*>(ec->arg.get()) : nullptr;
    } else {
        error(node->loc, "Codegen: unexpected node in enum construct");
        return allocReg();
    }

    // Find case index
    int caseIdx = -1;
    for (size_t i = 0; i < etype->cases_.size(); ++i) {
        if (etype->cases_[i].name->str() == caseName) {
            caseIdx = (int)i;
            break;
        }
    }
    if (caseIdx < 0) {
        error(node->loc, "Codegen: unknown enum case '" + caseName + "'");
        return allocReg();
    }

    u16 dst = allocSlot(etype);

    if (argExpr) {
        // Data case
        u16 valReg = genExpr(argExpr);
        // Promote if needed
        Type* valType = argExpr->resolvedType;
        Type* caseType = etype->cases_[caseIdx].type;
        valReg = ensureType(valReg, valType, caseType);

        // Phase 3: NullablePtrEnum -- Some(p) is just the inner pointer.
        if (etype->repr_ == ts::Type::Repr::NullablePtrEnum) {
            emitMov(dst, valReg);
        } else if (etype->repr_ == ts::Type::Repr::Inline) {
            // Phase 4g.4: inline enum -- payload is laid out in-place at
            // dst[1..1+P]. The payload may itself be an inline composite
            // (multi-word) and needs no boxing.
            emitOp(op_make_inline_enum);
            emitRegs(dst, valReg, (u16)caseIdx);
            emitPtr(etype);
        } else {
            // Phase 4g.15: heap Enum payload stored natively in v[]. The
            // source slot is already multi-word for Inline composite payloads
            // (allocSlot/ensureType keeps it that way), so just hand the
            // (multi-word) valReg to op_make_enum.
            emitOp(op_make_enum);
            emitRegs(dst, valReg, (u16)caseIdx);
            emitPtr(etype);
        }
    } else {
        // No-data case. Phase 2: empty enums (DiscriminantEnum repr) are
        // just the i64 case index. Phase 3: NullablePtrEnum's None case is
        // a null pointer.
        if (etype->repr_ == ts::Type::Repr::DiscriminantEnum) {
            emitOp(op_load_int_const);
            emitRegs(dst);
            emitInt((i64)caseIdx);
        } else if (etype->repr_ == ts::Type::Repr::NullablePtrEnum) {
            emitOp(op_load_nil);
            emitRegs(dst);
        } else if (etype->repr_ == ts::Type::Repr::Inline) {
            emitOp(op_make_inline_enum_nodata);
            emitRegs(dst, (u16)caseIdx);
            emitPtr(etype);
        } else {
            emitOp(op_make_enum_nodata);
            emitRegs(dst, (u16)caseIdx);
            emitPtr(etype);
        }
    }

    return dst;
}

u16 CodeGen::genLambdaExpr(LambdaExprNode* expr) {
    // Template lambda: don't compile the body, just emit captures and op_make_lambda
    if (expr->templateLambdaType) {
        return genTemplateLambdaDef(expr);
    }

    auto* lambdaType = expr->lambdaType;
    if (!lambdaType) {
        error(expr->loc, "Cannot infer types for lambda — add type annotations to parameters");
        return allocReg();
    }

    // Save codegen state
    CodeBlock* savedBlock = currentBlock_;
    u16 savedNextReg = nextReg_;
    u16 savedMaxReg = maxReg_;
    bool savedTailPos = inTailPosition_;
    auto savedScopes = std::move(localScopes_);
    auto savedFixups = std::move(jumpFixups_);
    auto savedConsts = std::move(constRegs_);
    auto savedPinned = std::move(regPinned_);
    inTailPosition_ = false;
    bool savedInFunctionBody = inFunctionBody_;
    inFunctionBody_ = true;
    bool savedInCoroFn = inCoroutineFn_;
    u32 savedYieldCount = currentYieldCount_;
    Type* savedReturnType = currentReturnType_;
    currentReturnType_ = lambdaType ? lambdaType->returnType_ : nullptr;

    if (expr->isCoroutine) {
        inCoroutineFn_ = true;
        currentYieldCount_ = 0;
    }

    // Create new CodeBlock for lambda body
    currentBlock_ = new CodeBlock();
    currentBlock_->name = compiler_.intern(expr->isCoroutine ? "<coro-lambda>" : "<lambda>");
    currentBlock_->numArgs = (u16)expr->params.size();
    currentBlock_->funcType = lambdaType;
    nextReg_ = 0;
    maxReg_ = 0;
    regTypes_.clear();
    regPinned_.clear();
    jumpFixups_.clear();
    constRegs_.clear();

    // Set up parameter registers
    localScopes_.clear();
    pushScope();
    for (size_t i = 0; i < expr->params.size(); ++i) {
        u16 paramReg = allocSlot(lambdaType->argTypes_[i]);
        declareLocal(expr->params[i].name, paramReg, lambdaType->argTypes_[i], false);
    }

    // Allocate registers for free variables (right after params). Layout:
    //   byReference captures occupy 1 word (an UpVar* pointer);
    //   byValue captures occupy sizeWords words inline.
    // The order and widths must match the outer capture-loading sequence
    // below so op_call_lambda's word-by-word free-var copy lines up.
    for (size_t i = 0; i < expr->captures.size(); ++i) {
        auto& cap = expr->captures[i];
        if (cap.byReference) {
            u16 freeVarReg = allocReg();
            declareLocalUpvar(cap.name, freeVarReg, cap.type);
        } else {
            u16 freeVarReg = allocSlot(cap.type);
            declareLocal(cap.name, freeVarReg, cap.type, false);
        }
    }

    // Generate body
    if (expr->body->kind == ASTNode::Block) {
        auto* block = static_cast<BlockStmt*>(expr->body.get());
        bool emittedReturn = false;
        for (size_t i = 0; i < block->stmts.size(); ++i) {
            auto* stmt = block->stmts[i].get();
            if (stmt->kind == ASTNode::ExprStmt) {
                auto* exprStmt = static_cast<ExprStmtNode*>(stmt);
                if (exprStmt->isTrailing) {
                    inTailPosition_ = true;
                    u16 resultReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
                    inTailPosition_ = false;
                    emitReturn(resultReg);
                    emittedReturn = true;
                    break;
                }
            }
            // Check for trailing IfStmtNode with else (value-producing if-else)
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::IfStmt
                && currentReturnType_ != compiler_.voidType()) {
                auto* ifStmt = static_cast<IfStmtNode*>(stmt);
                if (ifStmt->elseBranch) {
                    genIfStmtAsReturn(ifStmt);
                    emittedReturn = true;
                    break;
                }
            }
            // Check for trailing SwitchStmt (value-producing match)
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::SwitchStmt
                && currentReturnType_ != compiler_.voidType()) {
                genSwitchStmtAsReturn(static_cast<SwitchStmtNode*>(stmt));
                emittedReturn = true;
                break;
            }
            genNode(stmt);
        }
        if (!emittedReturn) {
            if (currentBlock_->code.empty() ||
                currentBlock_->code.back().op != op_return) {
                if (inCoroutineFn_) {
                    emitOp(op_coro_done);
                } else {
                    emitOp(op_return_void);
                }
            }
        }
    } else {
        if (inCoroutineFn_) {
            emitOp(op_coro_done);
        } else {
            emitOp(op_return_void);
        }
    }

    popScope();
    currentBlock_->numRegs = maxReg_;

    // Resolve jumps for this lambda's CodeBlock
    resolveJumps(currentBlock_);

    CodeBlock* lambdaBlock = currentBlock_;

    // Restore codegen state
    currentBlock_ = savedBlock;
    nextReg_ = savedNextReg;
    maxReg_ = savedMaxReg;
    inTailPosition_ = savedTailPos;
    inFunctionBody_ = savedInFunctionBody;
    inCoroutineFn_ = savedInCoroFn;
    currentYieldCount_ = savedYieldCount;
    currentReturnType_ = savedReturnType;
    localScopes_ = std::move(savedScopes);
    jumpFixups_ = std::move(savedFixups);
    constRegs_ = std::move(savedConsts);
    regPinned_ = std::move(savedPinned);

    // Set codeBlock_ on LambdaType so Lambda constructor can read it
    lambdaType->codeBlock_ = lambdaBlock;

    // Load captured variable values into consecutive registers. Layout
    // mirrors the inner body's free-var area: each byRef capture takes
    // 1 word (an UpVar*); each byValue capture takes sizeWords words.
    // The total word count goes into op_make_lambda so the Lambda's flex
    // freeVars_ array is sized correctly. For byRef captures we synthesize
    // an UpVar via op_capture_upvar_local (or, when the source local is
    // itself an upvar inherited from an enclosing lambda, just copy its
    // pointer with a 1-word MOV).
    {
        std::vector<bool> isUpvarFlags;
        isUpvarFlags.reserve(expr->captures.size());
        for (auto& cap : expr->captures) isUpvarFlags.push_back(cap.byReference);
        lambdaType->setCaptureLayout(isUpvarFlags);
    }
    u16 captureBase = nextReg_;
    u16 totalCaptureWords = 0;
    for (size_t i = 0; i < expr->captures.size(); ++i) {
        auto& cap = expr->captures[i];
        LocalVar* local = lookupLocal(cap.name);
        if (!local) {
            error(expr->loc, "Cannot find captured variable '" + cap.name + "'");
            allocReg();
            totalCaptureWords += 1;
            continue;
        }
        if (cap.byReference) {
            u16 captureReg = allocReg();
            if (local->isUpvar) {
                // The captured variable is already an UpVar in the
                // enclosing closure's free-var slot -- just copy the
                // UpVar* pointer so all closures share the same cell.
                if (local->reg != captureReg) emitMov(captureReg, local->reg);
            } else {
                // Fresh local capture: synthesize/get an UpVar pointing
                // to the local's register slot.
                u16 sw = (u16)typeSlotWords(cap.type);
                u16 mask = computeWordGCMask(cap.type);
                emitOp(op_capture_upvar_local);
                emitRegs(captureReg, local->reg, sw);
                emitInt((i64)mask);
                emitPtr(cap.type);
            }
            totalCaptureWords += 1;
        } else {
            u16 sw = (u16)typeSlotWords(cap.type);
            u16 captureReg = allocRegs(sw);
            if (local->isUpvar) {
                // Capturing the value of an enclosing upvar by snapshot
                // (immutable inner reference to a mutable outer var).
                // Read through the UpVar into the capture slot.
                emitOp(op_load_upvar_n);
                emitRegs(captureReg, local->reg, sw);
            } else if (local->reg != captureReg) {
                emitMoveN(captureReg, local->reg, sw);
            }
            totalCaptureWords += sw;
        }
    }

    // Emit op_make_lambda: [op] [regs: Rd, captureBase, totalCaptureWords] [LambdaType*]
    u16 dst = allocReg();
    emitOp(op_make_lambda);
    emitRegs(dst, captureBase, totalCaptureWords);
    emitPtr(lambdaType);

    return dst;
}

// Generate template lambda definition site — captures only, no body compilation
u16 CodeGen::genTemplateLambdaDef(LambdaExprNode* expr) {
    auto* tmplType = expr->templateLambdaType;

    // Lock in the byRef-aware capture layout on the TemplateLambdaType so
    // Lambda objects built from this template trace UpVar captures and
    // multi-word value captures correctly.
    {
        std::vector<bool> isUpvarFlags;
        isUpvarFlags.reserve(expr->captures.size());
        for (auto& cap : expr->captures) isUpvarFlags.push_back(cap.byReference);
        tmplType->setCaptureLayout(isUpvarFlags);
    }

    // Load captured variable values into consecutive registers (see the
    // matching loop in genLambdaExpr for the layout contract).
    u16 captureBase = nextReg_;
    u16 totalCaptureWords = 0;
    for (size_t i = 0; i < expr->captures.size(); ++i) {
        auto& cap = expr->captures[i];
        LocalVar* local = lookupLocal(cap.name);
        if (!local) {
            error(expr->loc, "Cannot find captured variable '" + cap.name + "'");
            allocReg();
            totalCaptureWords += 1;
            continue;
        }
        if (cap.byReference) {
            u16 captureReg = allocReg();
            if (local->isUpvar) {
                if (local->reg != captureReg) emitMov(captureReg, local->reg);
            } else {
                u16 sw = (u16)typeSlotWords(cap.type);
                u16 mask = computeWordGCMask(cap.type);
                emitOp(op_capture_upvar_local);
                emitRegs(captureReg, local->reg, sw);
                emitInt((i64)mask);
                emitPtr(cap.type);
            }
            totalCaptureWords += 1;
        } else {
            u16 sw = (u16)typeSlotWords(cap.type);
            u16 captureReg = allocRegs(sw);
            if (local->isUpvar) {
                emitOp(op_load_upvar_n);
                emitRegs(captureReg, local->reg, sw);
            } else if (local->reg != captureReg) {
                emitMoveN(captureReg, local->reg, sw);
            }
            totalCaptureWords += sw;
        }
    }

    // Emit op_make_template_lambda with TemplateLambdaType (codeBlock_ will be null on Lambda)
    u16 dst = allocReg();
    emitOp(op_make_template_lambda);
    emitRegs(dst, captureBase, totalCaptureWords);
    emitPtr(tmplType);

    return dst;
}

// Compile a monomorphized CodeBlock for a template lambda instantiation
void CodeGen::compileTemplateLambdaBody(LambdaExprNode* expr, LambdaType* lambdaType) {
    if (lambdaType->codeBlock_) return;  // already compiled

    // Re-type-check the body with correct bindings (resolvedTypes on shared AST may have
    // been overwritten by subsequent monomorphizations)
    auto* tmplType = expr->templateLambdaType;
    if (tmplType) {
        typeChecker_.recheckTemplateLambdaBody(expr, lambdaType, tmplType);
    }

    // Save codegen state
    CodeBlock* savedBlock = currentBlock_;
    u16 savedNextReg = nextReg_;
    u16 savedMaxReg = maxReg_;
    bool savedTailPos = inTailPosition_;
    auto savedScopes = std::move(localScopes_);
    auto savedFixups = std::move(jumpFixups_);
    auto savedConsts = std::move(constRegs_);
    auto savedPinned = std::move(regPinned_);
    inTailPosition_ = false;

    // Create new CodeBlock for this instantiation
    currentBlock_ = new CodeBlock();
    currentBlock_->name = compiler_.intern("<template-lambda>");
    currentBlock_->numArgs = (u16)expr->params.size();
    currentBlock_->funcType = lambdaType;
    nextReg_ = 0;
    maxReg_ = 0;
    regTypes_.clear();
    regPinned_.clear();
    jumpFixups_.clear();
    constRegs_.clear();

    // Set up parameter registers
    localScopes_.clear();
    pushScope();
    for (size_t i = 0; i < expr->params.size(); ++i) {
        u16 paramReg = allocSlot(lambdaType->argTypes_[i]);
        declareLocal(expr->params[i].name, paramReg, lambdaType->argTypes_[i], false);
    }

    // Allocate registers for free variables (right after params). Layout:
    //   byReference captures occupy 1 word (an UpVar* pointer);
    //   byValue captures occupy sizeWords words inline.
    // The order and widths must match the outer capture-loading sequence
    // below so op_call_lambda's word-by-word free-var copy lines up.
    for (size_t i = 0; i < expr->captures.size(); ++i) {
        auto& cap = expr->captures[i];
        if (cap.byReference) {
            u16 freeVarReg = allocReg();
            declareLocalUpvar(cap.name, freeVarReg, cap.type);
        } else {
            u16 freeVarReg = allocSlot(cap.type);
            declareLocal(cap.name, freeVarReg, cap.type, false);
        }
    }

    // Generate body (same pattern as genLambdaExpr)
    if (expr->body->kind == ASTNode::Block) {
        auto* block = static_cast<BlockStmt*>(expr->body.get());
        bool emittedReturn = false;
        for (size_t i = 0; i < block->stmts.size(); ++i) {
            auto* stmt = block->stmts[i].get();
            if (stmt->kind == ASTNode::ExprStmt) {
                auto* exprStmt = static_cast<ExprStmtNode*>(stmt);
                if (exprStmt->isTrailing) {
                    inTailPosition_ = true;
                    u16 resultReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
                    inTailPosition_ = false;
                    emitReturn(resultReg);
                    emittedReturn = true;
                    break;
                }
            }
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::IfStmt
                && currentReturnType_ != compiler_.voidType()) {
                auto* ifStmt = static_cast<IfStmtNode*>(stmt);
                if (ifStmt->elseBranch) {
                    genIfStmtAsReturn(ifStmt);
                    emittedReturn = true;
                    break;
                }
            }
            if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::SwitchStmt
                && currentReturnType_ != compiler_.voidType()) {
                genSwitchStmtAsReturn(static_cast<SwitchStmtNode*>(stmt));
                emittedReturn = true;
                break;
            }
            genNode(stmt);
        }
        if (!emittedReturn) {
            if (currentBlock_->code.empty() ||
                currentBlock_->code.back().op != op_return) {
                emitOp(op_return_void);
            }
        }
    } else {
        emitOp(op_return_void);
    }

    popScope();
    currentBlock_->numRegs = maxReg_;
    resolveJumps(currentBlock_);

    CodeBlock* lambdaBlock = currentBlock_;

    // Restore codegen state
    currentBlock_ = savedBlock;
    nextReg_ = savedNextReg;
    maxReg_ = savedMaxReg;
    inTailPosition_ = savedTailPos;
    localScopes_ = std::move(savedScopes);
    jumpFixups_ = std::move(savedFixups);
    constRegs_ = std::move(savedConsts);
    regPinned_ = std::move(savedPinned);

    // Set codeBlock_ on the LambdaType
    lambdaType->codeBlock_ = lambdaBlock;
}

bool CodeGen::genBlockForValue(BlockStmt* block, u16 resultReg) {
    u16 savedReg = nextReg_;
    pushScope();
    for (size_t i = 0; i < block->stmts.size(); ++i) {
        auto* stmt = block->stmts[i].get();

        // Check for trailing expression
        if (stmt->kind == ASTNode::ExprStmt) {
            auto* exprStmt = static_cast<ExprStmtNode*>(stmt);
            if (exprStmt->isTrailing) {
                u16 valReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
                // Phase 4g.25: copy all sizeWords words of the trailing
                // expression's value, not just word 0. Otherwise multi-word
                // types (Complex/Fraction/Inline composites) lose their
                // upper words when used as the value of a block.
                emitMoveN(resultReg, valReg,
                          typeSlotWords(exprStmt->expr->resolvedType));
                popScope();
                if (enableRegReclaim) freeRegsTo(savedReg);
                return true;
            }
        }

        // Check for trailing IfStmtNode with else (value-producing if-else)
        if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::IfStmt) {
            auto* ifStmt = static_cast<IfStmtNode*>(stmt);
            if (ifStmt->elseBranch) {
                genIfStmtForValue(ifStmt, resultReg);
                popScope();
                if (enableRegReclaim) freeRegsTo(savedReg);
                return true;
            }
        }

        // Check for trailing SwitchStmt (value-producing match)
        if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::SwitchStmt) {
            genSwitchStmtForValue(static_cast<SwitchStmtNode*>(stmt), resultReg);
            popScope();
            if (enableRegReclaim) freeRegsTo(savedReg);
            return true;
        }

        genNode(stmt);
    }
    popScope();
    if (enableRegReclaim) freeRegsTo(savedReg);
    return false;
}

void CodeGen::genIfStmtForValue(IfStmtNode* stmt, u16 resultReg) {
    u16 savedReg = nextReg_;
    u16 condReg = genExpr(static_cast<Expr*>(stmt->condition.get()));
    u32 elseJump = emitJump(op_jump_if_false, condReg);

    // Then branch
    if (enableRegReclaim) freeRegsTo(savedReg);
    if (stmt->thenBranch->kind == ASTNode::Block) {
        genBlockForValue(static_cast<BlockStmt*>(stmt->thenBranch.get()), resultReg);
    }

    u32 endJump = emitJump(op_jump);
    patchJump(elseJump);

    // Else branch
    if (enableRegReclaim) freeRegsTo(savedReg);
    if (stmt->elseBranch) {
        if (stmt->elseBranch->kind == ASTNode::Block) {
            genBlockForValue(static_cast<BlockStmt*>(stmt->elseBranch.get()), resultReg);
        } else if (stmt->elseBranch->kind == ASTNode::IfStmt) {
            // else-if chain
            genIfStmtForValue(static_cast<IfStmtNode*>(stmt->elseBranch.get()), resultReg);
        }
    }

    patchJump(endJump);
    if (enableRegReclaim) freeRegsTo(savedReg);
}

void CodeGen::genSwitchStmtForValue(SwitchStmtNode* stmt, u16 resultReg) {
    u16 subjReg = genExpr(static_cast<Expr*>(stmt->subject.get()));
    Type* subjType = stmt->subject->resolvedType;
    u16 caseSavedReg = nextReg_;

    std::vector<u32> endJumps;

    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        auto& clause = stmt->cases[i];

        if (enableRegReclaim) freeRegsTo(caseSavedReg);
        pushScope();

        std::vector<u32> failJumps;
        genPatternMatch(clause.pattern.get(), subjReg, subjType, failJumps);

        // Generate body and capture value into resultReg
        auto* body = clause.body.get();
        if (body->kind == ASTNode::Block) {
            genBlockForValue(static_cast<BlockStmt*>(body), resultReg);
        } else if (body->kind == ASTNode::ExprStmt) {
            auto* exprStmt = static_cast<ExprStmtNode*>(body);
            u16 valReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
            // Phase 4g.25: multi-word case bodies copy all sizeWords words.
            emitMoveN(resultReg, valReg,
                      typeSlotWords(exprStmt->expr->resolvedType));
        } else if (body->kind == ASTNode::IfStmt) {
            auto* ifStmt = static_cast<IfStmtNode*>(body);
            if (ifStmt->elseBranch) {
                genIfStmtForValue(ifStmt, resultReg);
            } else {
                genNode(body);
            }
        } else if (body->kind == ASTNode::SwitchStmt) {
            genSwitchStmtForValue(static_cast<SwitchStmtNode*>(body), resultReg);
        } else {
            genNode(body);
        }

        u32 endJump = emitJump(op_jump);
        endJumps.push_back(endJump);

        popScope();

        for (u32 fj : failJumps) {
            patchJump(fj);
        }
    }

    if (enableRegReclaim) freeRegsTo(caseSavedReg);

    for (u32 ej : endJumps) {
        patchJump(ej);
    }
}

// --- Tail-position-as-return variants ---
//
// When a function body ends with a value-producing if-else / match, the
// canonical lowering is to allocate a fresh slot, write each branch's value
// into it, and emit op_return at the end. That produces one extra MOV per
// branch. These helpers instead push the return into each branch directly,
// so the producer of the value writes into the result register and op_return
// reads from there with no intermediate copy. There is also no end-jump out
// of each branch: emitReturn is terminal.

void CodeGen::genBlockAsReturn(BlockStmt* block) {
    u16 savedReg = nextReg_;
    pushScope();
    for (size_t i = 0; i < block->stmts.size(); ++i) {
        auto* stmt = block->stmts[i].get();

        // Trailing expression: compute and return.
        if (stmt->kind == ASTNode::ExprStmt) {
            auto* exprStmt = static_cast<ExprStmtNode*>(stmt);
            if (exprStmt->isTrailing) {
                inTailPosition_ = true;
                u16 valReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
                inTailPosition_ = false;
                emitReturn(valReg);
                popScope();
                if (enableRegReclaim) freeRegsTo(savedReg);
                return;
            }
        }

        // Trailing if-else: descend recursively so each leaf returns.
        if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::IfStmt) {
            auto* ifStmt = static_cast<IfStmtNode*>(stmt);
            if (ifStmt->elseBranch) {
                genIfStmtAsReturn(ifStmt);
                popScope();
                if (enableRegReclaim) freeRegsTo(savedReg);
                return;
            }
        }

        // Trailing match: same idea.
        if (i == block->stmts.size() - 1 && stmt->kind == ASTNode::SwitchStmt) {
            genSwitchStmtAsReturn(static_cast<SwitchStmtNode*>(stmt));
            popScope();
            if (enableRegReclaim) freeRegsTo(savedReg);
            return;
        }

        genNode(stmt);
    }
    popScope();
    if (enableRegReclaim) freeRegsTo(savedReg);
}

void CodeGen::genIfStmtAsReturn(IfStmtNode* stmt) {
    u16 savedReg = nextReg_;
    u16 condReg = genExpr(static_cast<Expr*>(stmt->condition.get()));
    u32 elseJump = emitJump(op_jump_if_false, condReg);

    // Then branch: emits its own op_return, no end-jump needed.
    if (enableRegReclaim) freeRegsTo(savedReg);
    if (stmt->thenBranch->kind == ASTNode::Block) {
        genBlockAsReturn(static_cast<BlockStmt*>(stmt->thenBranch.get()));
    }

    patchJump(elseJump);

    // Else branch (block or else-if chain).
    if (enableRegReclaim) freeRegsTo(savedReg);
    if (stmt->elseBranch) {
        if (stmt->elseBranch->kind == ASTNode::Block) {
            genBlockAsReturn(static_cast<BlockStmt*>(stmt->elseBranch.get()));
        } else if (stmt->elseBranch->kind == ASTNode::IfStmt) {
            genIfStmtAsReturn(static_cast<IfStmtNode*>(stmt->elseBranch.get()));
        }
    }
    if (enableRegReclaim) freeRegsTo(savedReg);
}

void CodeGen::genSwitchStmtAsReturn(SwitchStmtNode* stmt) {
    u16 subjReg = genExpr(static_cast<Expr*>(stmt->subject.get()));
    Type* subjType = stmt->subject->resolvedType;
    u16 caseSavedReg = nextReg_;

    for (size_t i = 0; i < stmt->cases.size(); ++i) {
        auto& clause = stmt->cases[i];

        if (enableRegReclaim) freeRegsTo(caseSavedReg);
        pushScope();

        std::vector<u32> failJumps;
        genPatternMatch(clause.pattern.get(), subjReg, subjType, failJumps);

        auto* body = clause.body.get();
        if (body->kind == ASTNode::Block) {
            genBlockAsReturn(static_cast<BlockStmt*>(body));
        } else if (body->kind == ASTNode::ExprStmt) {
            auto* exprStmt = static_cast<ExprStmtNode*>(body);
            inTailPosition_ = true;
            u16 valReg = genExpr(static_cast<Expr*>(exprStmt->expr.get()));
            inTailPosition_ = false;
            emitReturn(valReg);
        } else if (body->kind == ASTNode::IfStmt) {
            auto* ifStmt = static_cast<IfStmtNode*>(body);
            if (ifStmt->elseBranch) {
                genIfStmtAsReturn(ifStmt);
            } else {
                genNode(body);
            }
        } else if (body->kind == ASTNode::SwitchStmt) {
            genSwitchStmtAsReturn(static_cast<SwitchStmtNode*>(body));
        } else {
            genNode(body);
        }

        // No endJump: each case body terminates via emitReturn.

        popScope();

        for (u32 fj : failJumps) {
            patchJump(fj);
        }
    }

    if (enableRegReclaim) freeRegsTo(caseSavedReg);
}

u16 CodeGen::genIfExpr(IfExprNode* expr) {
    u16 condReg = genExpr(static_cast<Expr*>(expr->condition.get()));
    // Phase 4g.25: size resultReg to the expression's result type so
    // multi-word values (Complex/Fraction/Inline composites) get all
    // their words preserved across both branches.
    u16 resultReg = allocSlot(expr->resolvedType);

    u32 elseJump = emitJump(op_jump_if_false, condReg);

    // Then branch - generate and copy result
    if (expr->thenBranch->kind == ASTNode::Block) {
        genBlockForValue(static_cast<BlockStmt*>(expr->thenBranch.get()), resultReg);
    }

    u32 endJump = emitJump(op_jump);
    patchJump(elseJump);

    // Else branch
    if (expr->elseBranch && expr->elseBranch->kind == ASTNode::Block) {
        genBlockForValue(static_cast<BlockStmt*>(expr->elseBranch.get()), resultReg);
    }

    patchJump(endJump);
    return resultReg;
}

// --- Constant folding ---

bool CodeGen::canFoldBinaryOp(BinaryOpExpr* expr) const {
    if (!enableConstFold) return false;
    if (expr->leftAutoMap || expr->rightAutoMap) return false;
    if (expr->resolvedFuncGlobalIndex >= 0) return false;

    auto isLiteral = [](ASTNode* n) {
        return n->kind == ASTNode::IntLiteral ||
               n->kind == ASTNode::FloatLiteral ||
               n->kind == ASTNode::BoolLiteral;
    };
    if (!isLiteral(expr->left.get()) || !isLiteral(expr->right.get())) return false;

    switch (expr->op) {
        case BinaryOpExpr::Add: case BinaryOpExpr::Sub:
        case BinaryOpExpr::Mul: case BinaryOpExpr::Div:
        case BinaryOpExpr::IntDiv: case BinaryOpExpr::Mod:
        case BinaryOpExpr::BitAnd: case BinaryOpExpr::BitOr:
        case BinaryOpExpr::BitXor: case BinaryOpExpr::ShiftL:
        case BinaryOpExpr::ShiftR: case BinaryOpExpr::And:
        case BinaryOpExpr::Or: case BinaryOpExpr::Eq:
        case BinaryOpExpr::Ne: case BinaryOpExpr::Lt:
        case BinaryOpExpr::Le: case BinaryOpExpr::Gt:
        case BinaryOpExpr::Ge:
            return true;
        default:
            return false;
    }
}

u16 CodeGen::foldBinaryOp(BinaryOpExpr* expr) {
    // Extract left value
    ConstVal lv, rv;
    if (expr->left->kind == ASTNode::IntLiteral) {
        lv.kind = ConstVal::CInt; lv.intVal = static_cast<IntLiteralExpr*>(expr->left.get())->value;
    } else if (expr->left->kind == ASTNode::FloatLiteral) {
        lv.kind = ConstVal::CFloat; lv.floatVal = static_cast<FloatLiteralExpr*>(expr->left.get())->value;
    } else {
        lv.kind = ConstVal::CBool; lv.boolVal = static_cast<BoolLiteralExpr*>(expr->left.get())->value;
    }
    if (expr->right->kind == ASTNode::IntLiteral) {
        rv.kind = ConstVal::CInt; rv.intVal = static_cast<IntLiteralExpr*>(expr->right.get())->value;
    } else if (expr->right->kind == ASTNode::FloatLiteral) {
        rv.kind = ConstVal::CFloat; rv.floatVal = static_cast<FloatLiteralExpr*>(expr->right.get())->value;
    } else {
        rv.kind = ConstVal::CBool; rv.boolVal = static_cast<BoolLiteralExpr*>(expr->right.get())->value;
    }

    // Check for div/mod by zero
    if (expr->op == BinaryOpExpr::Div || expr->op == BinaryOpExpr::IntDiv || expr->op == BinaryOpExpr::Mod) {
        if ((rv.kind == ConstVal::CInt && rv.intVal == 0) ||
            (rv.kind == ConstVal::CFloat && rv.floatVal == 0.0)) {
            // Don't fold — let runtime handle div by zero
            goto fallback;
        }
    }

    // Bool op Bool
    if (lv.kind == ConstVal::CBool && rv.kind == ConstVal::CBool) {
        bool result;
        switch (expr->op) {
            case BinaryOpExpr::And: result = lv.boolVal && rv.boolVal; break;
            case BinaryOpExpr::Or:  result = lv.boolVal || rv.boolVal; break;
            case BinaryOpExpr::Eq:  result = lv.boolVal == rv.boolVal; break;
            case BinaryOpExpr::Ne:  result = lv.boolVal != rv.boolVal; break;
            default: goto fallback;
        }
        u16 dst = allocReg();
        emitOp(result ? op_load_bool_true : op_load_bool_false);
        emitRegs(dst);
        markConstBool(dst, result);
        return dst;
    }

    // Promote to common numeric type
    {
        bool useFloat = (lv.kind == ConstVal::CFloat || rv.kind == ConstVal::CFloat);
        if (useFloat) {
            f64 l = (lv.kind == ConstVal::CFloat) ? lv.floatVal :
                    (lv.kind == ConstVal::CInt) ? (f64)lv.intVal : (f64)lv.boolVal;
            f64 r = (rv.kind == ConstVal::CFloat) ? rv.floatVal :
                    (rv.kind == ConstVal::CInt) ? (f64)rv.intVal : (f64)rv.boolVal;

            // Float comparisons return bool
            bool isCmp = false;
            bool cmpResult = false;
            switch (expr->op) {
                case BinaryOpExpr::Eq: isCmp = true; cmpResult = l == r; break;
                case BinaryOpExpr::Ne: isCmp = true; cmpResult = l != r; break;
                case BinaryOpExpr::Lt: isCmp = true; cmpResult = l < r; break;
                case BinaryOpExpr::Le: isCmp = true; cmpResult = l <= r; break;
                case BinaryOpExpr::Gt: isCmp = true; cmpResult = l > r; break;
                case BinaryOpExpr::Ge: isCmp = true; cmpResult = l >= r; break;
                default: break;
            }
            if (isCmp) {
                u16 dst = allocReg();
                emitOp(cmpResult ? op_load_bool_true : op_load_bool_false);
                emitRegs(dst);
                markConstBool(dst, cmpResult);
                return dst;
            }

            f64 result;
            switch (expr->op) {
                case BinaryOpExpr::Add: result = l + r; break;
                case BinaryOpExpr::Sub: result = l - r; break;
                case BinaryOpExpr::Mul: result = l * r; break;
                case BinaryOpExpr::Div: result = l / r; break;
                default: goto fallback;
            }
            u16 dst = allocReg();
            emitOp(op_load_float_const);
            emitRegs(dst);
            emitFloat(result);
            markConstFloat(dst, result);
            return dst;
        }

        // Int op Int
        i64 l = (lv.kind == ConstVal::CInt) ? lv.intVal : (i64)lv.boolVal;
        i64 r = (rv.kind == ConstVal::CInt) ? rv.intVal : (i64)rv.boolVal;

        // Int comparisons return bool
        bool isCmp = false;
        bool cmpResult = false;
        switch (expr->op) {
            case BinaryOpExpr::Eq: isCmp = true; cmpResult = l == r; break;
            case BinaryOpExpr::Ne: isCmp = true; cmpResult = l != r; break;
            case BinaryOpExpr::Lt: isCmp = true; cmpResult = l < r; break;
            case BinaryOpExpr::Le: isCmp = true; cmpResult = l <= r; break;
            case BinaryOpExpr::Gt: isCmp = true; cmpResult = l > r; break;
            case BinaryOpExpr::Ge: isCmp = true; cmpResult = l >= r; break;
            default: break;
        }
        if (isCmp) {
            u16 dst = allocReg();
            emitOp(cmpResult ? op_load_bool_true : op_load_bool_false);
            emitRegs(dst);
            markConstBool(dst, cmpResult);
            return dst;
        }

        i64 result;
        switch (expr->op) {
            case BinaryOpExpr::Add: result = l + r; break;
            case BinaryOpExpr::Sub: result = l - r; break;
            case BinaryOpExpr::Mul: result = l * r; break;
            case BinaryOpExpr::IntDiv: result = l / r; break;
            case BinaryOpExpr::Mod: result = l % r; break;
            case BinaryOpExpr::BitAnd: result = l & r; break;
            case BinaryOpExpr::BitOr: result = l | r; break;
            case BinaryOpExpr::BitXor: result = l ^ r; break;
            case BinaryOpExpr::ShiftL: result = l << r; break;
            case BinaryOpExpr::ShiftR: result = l >> r; break;
            case BinaryOpExpr::UShiftR: result = (i64)((u64)l >> r); break;
            case BinaryOpExpr::And: result = (l && r) ? 1 : 0; break;
            case BinaryOpExpr::Or: result = (l || r) ? 1 : 0; break;
            default: goto fallback;
        }

        // Int / Int → Fraction at runtime, but we can still fold to a single int
        // if the result type is Int (e.g. IntDiv).
        // For Div: the type checker resolves Int/Int to Fraction, so we shouldn't fold it here
        // as a pure int. Let fallback handle it.
        if (expr->op == BinaryOpExpr::Div) goto fallback;

        u16 dst = allocReg();
        emitOp(op_load_int_const);
        emitRegs(dst);
        emitInt(result);
        markConstInt(dst, result);
        return dst;
    }

fallback:
    // Can't fold — generate normally. Since canFoldBinaryOp was true,
    // we need to fall through to the normal codegen. We use a sentinel.
    // But actually this shouldn't happen in practice (we guard div by zero above).
    // Just generate operands and let the normal path handle it.
    return UINT16_MAX;
}

bool CodeGen::canFoldUnaryOp(UnaryOpExpr* expr) const {
    if (!enableConstFold) return false;
    auto* op = expr->operand.get();
    if (op->kind != ASTNode::IntLiteral && op->kind != ASTNode::FloatLiteral &&
        op->kind != ASTNode::BoolLiteral) return false;
    return expr->op == UnaryOpExpr::Neg || expr->op == UnaryOpExpr::Not ||
           expr->op == UnaryOpExpr::BitNot;
}

u16 CodeGen::foldUnaryOp(UnaryOpExpr* expr) {
    auto* operand = expr->operand.get();

    if (expr->op == UnaryOpExpr::Neg) {
        if (operand->kind == ASTNode::IntLiteral) {
            i64 val = -static_cast<IntLiteralExpr*>(operand)->value;
            u16 dst = allocReg();
            emitOp(op_load_int_const);
            emitRegs(dst);
            emitInt(val);
            markConstInt(dst, val);
            return dst;
        }
        if (operand->kind == ASTNode::FloatLiteral) {
            f64 val = -static_cast<FloatLiteralExpr*>(operand)->value;
            u16 dst = allocReg();
            emitOp(op_load_float_const);
            emitRegs(dst);
            emitFloat(val);
            markConstFloat(dst, val);
            return dst;
        }
    }
    if (expr->op == UnaryOpExpr::Not && operand->kind == ASTNode::BoolLiteral) {
        bool val = !static_cast<BoolLiteralExpr*>(operand)->value;
        u16 dst = allocReg();
        emitOp(val ? op_load_bool_true : op_load_bool_false);
        emitRegs(dst);
        markConstBool(dst, val);
        return dst;
    }
    if (expr->op == UnaryOpExpr::BitNot && operand->kind == ASTNode::IntLiteral) {
        i64 val = ~static_cast<IntLiteralExpr*>(operand)->value;
        u16 dst = allocReg();
        emitOp(op_load_int_const);
        emitRegs(dst);
        emitInt(val);
        markConstInt(dst, val);
        return dst;
    }
    return UINT16_MAX;
}

u16 CodeGen::tryFoldBuiltinCall(CallExpr_* expr, u16 argBase, u16 callArgc) {
    if (!enableConstFold) return UINT16_MAX;
    if (callArgc == 0 || callArgc > 8) return UINT16_MAX;

    Type* retType = expr->resolvedType;
    // Only fold scalar results (Int, Float, Bool)
    if (retType != compiler_.intType() &&
        retType != compiler_.floatType() &&
        retType != compiler_.boolType()) return UINT16_MAX;

    // Only fold if all argument types are scalar (not Obj types like Array, List, etc.)
    for (size_t i = 0; i < expr->args.size(); i++) {
        Type* argType = expr->args[i]->resolvedType;
        if (argType != compiler_.intType() &&
            argType != compiler_.floatType() &&
            argType != compiler_.boolType()) return UINT16_MAX;
    }

    // Collect constant args
    Word args[8];
    for (u16 i = 0; i < callArgc; i++) {
        auto* cv = getConst(argBase + i);
        if (!cv) return UINT16_MAX;
        switch (cv->kind) {
            case ConstVal::CInt:   args[i] = Word(cv->intVal); break;
            case ConstVal::CFloat: args[i] = Word(cv->floatVal); break;
            case ConstVal::CBool:  args[i] = Word((i64)cv->boolVal); break;
        }
    }

    // Get the Primitive and call it at compile time
    auto* prim = static_cast<Primitive*>(compiler_.global(expr->resolvedFuncGlobalIndex).o);
    if (!prim->pure_) return UINT16_MAX;  // Impure functions (e.g. RNG) cannot be folded
    Word result;
    if (!compiler_.evalPrimitive(prim, args, callArgc, result))
        return UINT16_MAX;  // No VM available — skip constant folding

    // Emit single load constant
    u16 dst = allocReg();
    if (retType == compiler_.intType() || retType == compiler_.boolType()) {
        emitOp(op_load_int_const);
        emitRegs(dst);
        emitInt(result.i);
        if (retType == compiler_.boolType())
            markConstBool(dst, result.i != 0);
        else
            markConstInt(dst, result.i);
    } else {
        emitOp(op_load_float_const);
        emitRegs(dst);
        emitFloat(result.f);
        markConstFloat(dst, result.f);
    }
    return dst;
}

} // namespace ts
