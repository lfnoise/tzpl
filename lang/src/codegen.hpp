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
//  codegen.hpp
//  lang
//
//  Code generator: typed AST -> register-based CodeBlock
//

#ifndef codegen_hpp
#define codegen_hpp

#include "ast.hpp"
#include "compiler.hpp"
#include "value.hpp"
#include "type_checker.hpp"
#include "opcodes.hpp"
#include "function_ref.hpp"
#include <unordered_map>

namespace ts {

class CodeGen {
public:
    CodeGen(Compiler& compiler, TypeChecker& typeChecker);

    // Generate code for a full program (returns top-level CodeBlock)
    // If isModule is true, the block ends with op_return_void instead of op_halt,
    // so it can be called as a function from the importing code.
    CodeBlock* generate(Program& program, bool isModule = false);

    // Generate code for a REPL input. Like generate(), but if the last program
    // item is an ExprStmt, its result is moved to reg 0 so the caller can
    // read it from vm.execute()'s return value.
    CodeBlock* generateREPL(Program& program);

    // Error access
    const std::vector<CompileError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

    // Optimization flags (all enabled by default, can be disabled for debugging)
    bool enableRegReclaim = true;
    bool enableConstFold = true;
    bool enableTailCalls = true;

    // Source context for error diagnostics (set by caller)
    void setSourceFilePath(const std::string& path) { sourceFilePath_ = path; }
    void setSourceText(const std::string& src) { sourceText_ = src; }

private:
    Compiler& compiler_;
    TypeChecker& typeChecker_;
    std::vector<CompileError> errors_;
    std::string sourceFilePath_;
    std::string sourceText_;

    // Current code block being emitted to
    CodeBlock* currentBlock_;

    // Register allocator state
    u16 nextReg_;
    u16 maxReg_;

    // Per-register pin flag. Slots that hold a `var` captured by some
    // closure must stay alive for the entire function lifetime so the
    // closure's open UpVar (which points into the slot) keeps reading the
    // live value, even after the enclosing block scope ends. freeRegsTo
    // skips over pinned slots when reclaiming temps.
    std::vector<u8> regPinned_;
    void pinReg(u16 reg);  // sizes regPinned_ as needed

    // Phase 5.2 of tracing-GC project: per-register type table.
    // Indexed by register number, indexed up to nextReg_. Tracks the static
    // type stored in each register at the current emission point, so stack
    // map emitters can find every live Obj* slot in a function (named local
    // OR in-flight temp) without relying on localScopes_ alone. The base
    // register of a multi-word inline composite gets the type; continuation
    // registers stay null (the marker skips them via isInlineMultiword).
    // Slots that have been freed by freeRegsTo are reset to null.
    std::vector<Type*> regTypes_;
    void setRegType(u16 reg, Type* t);  // tracks type, sizes regTypes_ as needed

    // Local variable -> register mapping
    struct LocalVar {
        u16 reg;
        Type* type;
        bool isMutable;
        // True when this local refers to a Lua-style open upvalue: the
        // register holds an UpVar* (1 word) and reads/writes against it
        // must go through op_load_upvar_n / op_store_upvar_n instead of
        // plain register access. Set only inside a closure body for a
        // captured-by-reference `var`; never set in the declaring frame
        // (where the var lives natively in its register block and reads/
        // writes are plain ops).
        bool isUpvar = false;
    };
    std::vector<std::unordered_map<std::string, LocalVar>> localScopes_;

    // Register allocation
    u16 allocReg();
    u16 allocRegs(u16 count);
    void freeRegsTo(u16 reg);  // Free all regs >= reg

    // Scope management
    void pushScope();
    void popScope();
    void declareLocal(const std::string& name, u16 reg, Type* type, bool isMutable);
    // Variant that also marks the local as an open upvalue: the register
    // holds an UpVar* and reads/writes against the local must go through
    // op_load_upvar_n / op_store_upvar_n.
    void declareLocalUpvar(const std::string& name, u16 reg, Type* type);
    LocalVar* lookupLocal(const std::string& name);

    // Const tracking helpers for mutation barriers.
    void clearConst(u16 reg);
    void clearConstsForMutableLocals();

    // Code generation for nodes
    void genNode(ASTNode* node);
    void genBlock(BlockStmt* block);
    void genImportDecl(ImportDeclNode* decl);
    void genLetDecl(LetDeclNode* decl);
    void genVarDecl(VarDeclNode* decl);
    void genConstDecl(ConstDeclNode* decl);
    void genFnDecl(FnDeclNode* decl);
    void genMonoInstance(FuncInfo& monoInfo);
    void genIfStmt(IfStmtNode* stmt);
    void genWhileStmt(WhileStmtNode* stmt);
    void genForStmt(ForStmtNode* stmt);
    void genSwitchStmt(SwitchStmtNode* stmt);
    void genReturnStmt(ReturnStmtNode* stmt);
    void genBreakStmt(BreakStmtNode* stmt);
    void genContinueStmt(ContinueStmtNode* stmt);
    void genAssignStmt(AssignStmtNode* stmt);
    void genIndexAssignStmt(IndexAssignStmtNode* stmt);
    void genExprStmt(ExprStmtNode* stmt);

    // Expression codegen - returns register holding result
    u16 genExpr(Expr* expr);
    u16 genExprDispatch(Expr* expr);  // raw dispatch; genExpr wraps to record the destination's type
    u16 genIntLiteral(IntLiteralExpr* expr);
    u16 genFloatLiteral(FloatLiteralExpr* expr);
    u16 genBoolLiteral(BoolLiteralExpr* expr);
    u16 genStringLiteral(StringLiteralExpr* expr);
    u16 genSymbolLiteral(SymbolLiteralExpr* expr);
    u16 genImaginaryLiteral(ImaginaryLiteralExpr* expr);
    u16 genFractionLiteral(FractionLiteralExpr* expr);
    u16 genIdentifier(IdentifierExpr* expr);
    u16 genBinaryOp(BinaryOpExpr* expr);
    u16 genUnaryOp(UnaryOpExpr* expr);
    u16 genCall(CallExpr_* expr);
    u16 genAutoMapCall(CallExpr_* expr);
    // Recursive auto-map of a named/builtin call over persistent-vector arg(s)
    // to arbitrary depth. Outer levels peel a persistent-vector level and
    // recurse; the base level runs a single-level mapped call.
    u16 emitPVecCallLevel(CallExpr_* expr, const FuncInfo* funcInfo, Type* returnT,
                          std::vector<u16> argRegs, std::vector<Type*> argTypes,
                          const std::vector<bool>& argMapped, PersistentVectorType* resPV);
    u16 genAutoMapLambdaCall(CallExpr_* expr, u16 calleeReg, FunctionType* funcType);
    u16 genAutoMapLambdaCallList(CallExpr_* expr, u16 calleeReg, FunctionType* funcType);
    // Auto-map of a lambda-value call over persistent-vector arg(s) to arbitrary
    // depth (recurses via emitPVecLambdaCallLevel).
    u16 genAutoMapLambdaCallPVec(CallExpr_* expr, u16 calleeReg, FunctionType* funcType,
                                 PersistentVectorType* resultPVType);
    u16 emitPVecLambdaCallLevel(CallExpr_* expr, u16 calleeReg, FunctionType* funcType, Type* returnT,
                                std::vector<u16> argRegs, std::vector<Type*> argTypes,
                                const std::vector<bool>& argMapped, PersistentVectorType* resPV);
    u16 genAutoMapCallList(CallExpr_* expr, const FuncInfo* funcInfo);
    u16 genAutoMapCallListVoid(CallExpr_* expr, const FuncInfo* funcInfo);
    u16 genExplicitImplicitAutoMapCall(CallExpr_* expr);
    u16 genCartesianCall(CallExpr_* expr);
    u16 genDeepMapCall(CallExpr_* expr, int depth);
    // Per-argument descriptor for emitMappedCallElem: either extract element
    // `srcReg[idxReg]` of array `arrType` (mapped), or broadcast the scalar/value
    // in `srcReg` (mapped == false).
    struct MappedCallArg {
        bool mapped = false;
        u16 srcReg = 0;
        u16 idxReg = 0;
        ArrayType* arrType = nullptr;
    };
    // Emit one call of an auto-mapped function: populate the callee arg window at
    // nextReg_ from `args` (handling inline-composite slot widths, boxing at the
    // builtin boundary, and parameter-type promotion), pack variadics, emit the
    // call, and return the result register. Shared by the plain, deep, and
    // Cartesian array-call loops; `returnT` is the call's return type.
    u16 emitMappedCallElem(CallExpr_* expr, const FuncInfo* funcInfo, Type* returnT,
                           std::vector<MappedCallArg> const& args);
    u16 genAutoMapBinaryOp(BinaryOpExpr* expr);
    u16 genAutoMapBinaryOpList(BinaryOpExpr* expr);
    // Elementwise binary op producing a persistent vector (#[...]). Operands
    // may each be a persistent vector (mapped) or a scalar (broadcast).
    u16 genBinaryOpMapPVec(BinaryOpExpr* expr);
    u16 emitPVecBinopLevel(BinaryOpExpr* expr, u16 leftReg, Type* leftType,
                           u16 rightReg, Type* rightType, PersistentVectorType* resPV);
    u16 genCartesianBinaryOp(BinaryOpExpr* expr);
    // Recursive N-dimensional cartesian nest. `containerType` is the result
    // container at this output dimension (Array or persistent vector); the nest
    // descends one dimension per level (1..maxCart), recording each level's loop
    // index in iRegs, and calls emitLeaf at the innermost dimension. dimLen
    // yields the length of the operand(s) assigned to a given dimension. Handles
    // arbitrary depth and mixed array/persistent-vector results.
    u16 emitCartesianNest(int level, int maxCart, Type* containerType,
                          std::vector<u16>& iRegs,
                          function_ref<u16(std::vector<u16> const&)> emitLeaf,
                          function_ref<u16(int)> dimLen);
    u16 genDeepMapBinaryOp(BinaryOpExpr* expr, int depth);
    // Emit the per-element computation of an auto-mapped binary op given the two
    // operand element registers/types and the result element type: dispatches to
    // an operator-overload call, composite-numeric op, comparison, or scalar
    // arith, returning the register holding the result. Shared by the plain,
    // deep, and Cartesian binary-op auto-map loops.
    u16 emitBinaryOpElem(BinaryOpExpr* expr,
                         u16 leftElemReg, Type* leftElemType,
                         u16 rightElemReg, Type* rightElemType,
                         Type* scalarResultType);
    u16 genArrayLiteral(ArrayLiteralExpr* expr);
    u16 genAutoMapArrayLiteral(ArrayLiteralExpr* expr);
    u16 genCartesianArrayLiteral(ArrayLiteralExpr* expr);
    u16 genTupleLiteral(TupleLiteralExpr* expr);
    u16 genAutoMapTupleLiteral(TupleLiteralExpr* expr);
    u16 genCartesianTupleLiteral(TupleLiteralExpr* expr);
    u16 genStructLiteral(StructLiteralExpr* expr);
    u16 genAutoMapStructLiteral(StructLiteralExpr* expr);
    u16 genCartesianStructLiteral(StructLiteralExpr* expr);
    u16 genAutoMapTupleStruct(CallExpr_* expr);
    u16 genCartesianTupleStruct(CallExpr_* expr);
    u16 genIndexExpr(IndexExpr_* expr);
    u16 genAutoMapIndexObjArray(IndexExpr_* expr);
    u16 genAutoMapIndexObjList(IndexExpr_* expr);
    u16 genAutoMapIndexObjDeep(IndexExpr_* expr, int depth);
    u16 genAutoMapIndexIdxArray(IndexExpr_* expr);
    u16 genAutoMapIndexIdxList(IndexExpr_* expr);
    u16 emitIndexLookup(u16 srcReg, Type* srcType, u16 idxReg, Type* idxType,
                        const AutoMapArg& indexAutoMap, Type* resultType);
    u16 genFieldExpr(FieldExpr_* expr);
    u16 genAutoMapFieldArray(FieldExpr_* expr);
    u16 genAutoMapFieldList(FieldExpr_* expr);
    u16 genAutoMapFieldDeep(FieldExpr_* expr, int depth);

    // Auto-map loop scaffolds. Each emits the standard per-element loop and
    // calls `body` once to emit the body that maps one source element (in
    // `elemReg`, sized for `elemType`) to one result element, returning the
    // register holding it. The array form builds a counted loop into a freshly
    // allocated result array; the list form traverses, conses (boxing inline
    // multiword results, which list nodes cannot hold inline), then reverses.
    u16 emitArrayMapLoop(u16 srcReg, ArrayType* srcType, ArrayType* dstType,
                         function_ref<u16(u16 elemReg, Type* elemType)> body);
    // Emit the length of `arrays[0]`, or the running min when several arrays are
    // mapped in parallel, returning the register that holds it.
    u16 emitMinArrayLength(std::vector<std::pair<u16, ArrayType*>> const& arrays);
    // Emit a bare counted loop `for i in 0..lenReg`, calling `body(i)` each
    // iteration for its side effects only (no result array). Used by the Void
    // auto-map paths. `body` owns any call/element register window above
    // nextReg_, which already sits past the loop's bookkeeping registers.
    void emitCountedLoop(u16 lenReg, function_ref<void(u16 iReg)> body);
    // Like emitCountedLoop, but allocates a fresh result array of `dstType` and
    // stores `body(i)` into element i, returning the result array register.
    u16 emitArrayBuildLoop(u16 lenReg, ArrayType* dstType,
                           function_ref<u16(u16 iReg)> body);
    u16 emitListMapLoop(u16 srcReg, ListType* srcType, ListType* dstType,
                        function_ref<u16(u16 elemReg, Type* elemType)> body);
    // Emit the in-place reversal of an accumulated list, returning a new list reg.
    u16 emitListReverse(u16 listReg, ListType* listType);
    // Extract the field named by `expr` from one element (struct or tuple) held
    // in `elemReg`, returning the register holding it (0 if not found).
    u16 emitMapFieldGet(FieldExpr_* expr, u16 elemReg, Type* elemType);
    u16 genEnumConstruct(ASTNode* node);
    u16 genLambdaExpr(LambdaExprNode* expr);
    u16 genTemplateLambdaDef(LambdaExprNode* expr);
    void compileTemplateLambdaBody(LambdaExprNode* expr, LambdaType* lambdaType);
    u16 genIfExpr(IfExprNode* expr);
    u16 genNilLiteral();
    u16 genListLiteral(ListLiteralExpr* expr);
    u16 genMapLiteral(MapLiteralExpr* expr);
    u16 genSetLiteral(SetLiteralExpr* expr);
    u16 genRangeExpr(RangeExprNode* expr);
    u16 genAsTypeExpr(AsTypeExprNode* expr);
    u16 emitNumericDowncast(u16 reg, Type* fromType, Type* toType);
    u16 emitAutoMapDowncast(u16 subjReg, AsTypeExprNode* expr);
    u16 emitAutoMapDowncastLoop(u16 arrReg, std::vector<Type*>& srcArrayTypes,
                                 std::vector<Type*>& resultTypes,
                                 int level, int depth,
                                 Type* elemType, Type* targetType);

    // Emit helpers
    void emitOp(Operation op) { currentBlock_->emitOp(op); }
    void emitRegs(u16 r0, u16 r1 = 0, u16 r2 = 0, u16 r3 = 0) {
        currentBlock_->emitRegs(r0, r1, r2, r3);
        // Track the position of the regs slot just emitted so a subsequent
        // arith->MOV pattern can redirect the previous instruction's destination
        // instead of emitting the MOV. Whether redirection is safe is decided
        // at the redirection site by inspecting the opcode at slot-1.
        lastProducerSlot_ = (i32)(currentBlock_->code.size() - 1);
        lastProducerDst_  = r0;
    }
    void emitInt(i64 val) { currentBlock_->emitInt(val); }
    void emitFloat(f64 val) { currentBlock_->emitFloat(val); }
    void emitPtr(void* p) { currentBlock_->emitPtr(p); }

    // Producer tracking for arith->MOV fusion in genAssignStmt. Reset whenever
    // control flow can join (patchJump) or branch away (after emitJump).
    i32 lastProducerSlot_ = -1;
    u16 lastProducerDst_ = 0;
    void invalidateLastProducer() {
        lastProducerSlot_ = -1;
    }
    // If the last emitted instruction wrote a single-word value to `from`
    // and is in the redirectable-producer whitelist, patch its destination to
    // `to` and return true. The `producerEmittedAt` parameter is the code size
    // saved BEFORE the expression that yielded `from` was generated; this guards
    // against fusing with a producer emitted by an unrelated earlier statement
    // when the expression itself was a bare identifier (no emission).
    bool tryFuseRedirect(u16 from, u16 to, u32 nWords, u32 producerEmittedAt);

    // Multi-word slot move (Phase 4): falls back to op_mov when nWords == 1.
    // Phase 5.3: propagate per-register types from src to dst so stack maps
    // emitted at a later returnPC / safepoint see the destination as a live
    // ref when the source held one. Without this, a result-relocation MOV
    // (post-CALL "stash to lower reg") would leave regTypes_[dst] holding
    // its previous type, and the marker would either miss a live Obj* (UAF
    // on sweep) or read garbage as a pointer (SEGV in markRoots).
    void emitMoveN(u16 dst, u16 src, u32 nWords) {
        if (nWords <= 1) {
            emitOp(op_mov);
            emitRegs(dst, src);
        } else {
            emitOp(op_move_n);
            emitRegs(dst, src);
            emitInt((i64)nWords);
        }
        u32 n = nWords ? nWords : 1;
        for (u32 i = 0; i < n; ++i) {
            Type* t = ((src + i) < regTypes_.size()) ? regTypes_[src + i] : nullptr;
            setRegType((u16)(dst + i), t);
        }
    }

    // Phase 5.3: canonical 1-word op_mov emitter that also propagates the
    // per-register type. Call this instead of emitOp(op_mov)+emitRegs(...)
    // at every reg-to-reg copy site so the stack-map walker stays sound.
    void emitMov(u16 dst, u16 src) {
        emitOp(op_mov);
        emitRegs(dst, src);
        Type* t = (src < regTypes_.size()) ? regTypes_[src] : nullptr;
        setRegType(dst, t);
    }

    // Allocate a slot sized for type t (Phase 4): atoms/pointers get 1 reg,
    // inline value types get t->sizeWords_ consecutive regs.
    u16 allocSlot(Type* t) {
        u16 n = (t && t->sizeWords_ > 1) ? t->sizeWords_ : 1;
        return (n == 1) ? allocReg() : allocRegs(n);
    }

    // Copy `argReg` to `targetReg` as a multi-word slot for type `argType`.
    // For 1-word types this emits op_mov; for inline value types it emits
    // op_move_n. Advances nextReg_ past the copied region. (Phase 4f)
    void emitArgPlacement(u16 targetReg, u16 argReg, Type* argType) {
        u32 sw = (argType && argType->sizeWords_ > 0) ? argType->sizeWords_ : 1;
        if (argReg != targetReg) {
            emitMoveN(targetReg, argReg, sw);
        }
        u16 next = (u16)(targetReg + sw);
        if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
    }

    // Phase 4g.2 / 4g.6: at builtin-call boundaries, inline-composite args were
    // historically boxed to a 1-Word Obj* because most builtins read args as
    // single Words. Phase 4g.6 migrates builtins incrementally: each builtin's
    // FuncInfo sets `acceptsInlineArgs=true` once it has been updated to read
    // multi-word slots, and the type checker propagates that to the call
    // expression's `builtinAcceptsInlineArgs`. When that flag is set, callers
    // pass `acceptsInline=true` here and we skip the box. Complex/Fraction
    // stay multi-word -- their builtins were updated in Phase 4f.
    //
    // Returns the number of words placed at targetReg so the caller can
    // advance its cumulative arg offset correctly.
    u32 emitArgPlacementForCall(u16 targetReg, u16 argReg, Type* paramType,
                                bool isBuiltin, bool acceptsInline = false) {
        bool boxNeeded = isBuiltin && !acceptsInline && paramType
            && paramType->repr_ == ts::Type::Repr::Inline
            && paramType != compiler_.complexType()
            && paramType != compiler_.fractionType();
        if (boxNeeded) {
            argReg = emitBoxIfInline(argReg, paramType);
            if (argReg != targetReg) { emitOp(op_mov); emitRegs(targetReg, argReg); }
            u16 next = (u16)(targetReg + 1);
            if (nextReg_ < next) { nextReg_ = next; if (nextReg_ > maxReg_) maxReg_ = nextReg_; }
            return 1;
        }
        emitArgPlacement(targetReg, argReg, paramType);
        return typeSlotWords(paramType);
    }

    // Symmetric to the above: legacy builtin returning an inline composite
    // gives back a 1-word boxed pointer; unbox into a multi-word slot. When
    // the builtin has been migrated (acceptsInline=true), the result already
    // arrives as a multi-word slot, so skip the unbox.
    u16 emitBuiltinResultUnbox(u16 resultReg, Type* returnType,
                               bool acceptsInline = false) {
        if (!acceptsInline
            && returnType
            && returnType->repr_ == ts::Type::Repr::Inline
            && returnType != compiler_.complexType()
            && returnType != compiler_.fractionType()) {
            return emitUnboxIfInline(resultReg, returnType);
        }
        return resultReg;
    }

    // Number of slot words for type t (1 for atom/pointer, sizeWords_ for inline).
    static u32 typeSlotWords(Type const* t) {
        return (t && t->sizeWords_ > 0) ? t->sizeWords_ : 1;
    }

    // Compute the GC-mask bitmap (bit i set <=> word i of an inline value
    // of `t` holds an Obj*). Used for UpVar payload tracing. Conservative
    // for inline composites we haven't enumerated word-by-word yet: those
    // are reported as all-zero (no Obj* words), which matches today's
    // 1-word capture semantics. Atomic and 1-word object types both work
    // exactly; Complex and Fraction return 0 (their two inline words are
    // f64/i64 primitives, never Obj*).
    static u16 computeWordGCMask(Type const* t) {
        if (!t) return 0;
        u16 sw = (u16)((t->sizeWords_ > 0) ? t->sizeWords_ : 1);
        if (sw <= 1) {
            return storesObjPtr(t) ? (u16)1 : (u16)0;
        }
        return 0;
    }

    // Phase 4g.5: emit a global store. For inline multi-word composites this
    // emits an inline op that mem-copies the payload into the global's
    // sizeWords_ slots and ARC-walks the layout. `init=true` skips releasing
    // the old payload (declaration site). For everything else, falls back to
    // the existing 1-word store path (and boxes Complex/Fraction first).
    void emitGlobalStore(u16 srcReg, u32 globalIdx, Type* type, bool init) {
        if (isInlineMultiword(type)
            && type != compiler_.complexType()
            && type != compiler_.fractionType()) {
            emitOp(init ? op_init_global_inline : op_store_global_inline);
            emitRegs(srcReg);
            emitInt((i64)globalIdx);
            emitPtr(type);
            return;
        }
        u16 storeReg = (type && type->repr_ == ts::Type::Repr::Inline)
                     ? emitBoxIfInline(srcReg, type) : srcReg;
        emitOp(storesObjPtr(type)
               ? (init ? op_init_global_obj : op_store_global_obj)
               : op_store_global);
        emitRegs(storeReg);
        emitInt((i64)globalIdx);
    }

    // Phase 4g.5: emit a global load. For inline multi-word composites this
    // emits an inline op that mem-copies sizeWords_ slots into a fresh multi-
    // word reg slot and returns its base. For everything else, falls back to
    // op_load_global (and unboxes Complex/Fraction).
    u16 emitGlobalLoad(u32 globalIdx, Type* type) {
        if (isInlineMultiword(type)
            && type != compiler_.complexType()
            && type != compiler_.fractionType()) {
            u16 dst = allocSlot(type);
            emitOp(op_load_global_inline);
            emitRegs(dst);
            emitInt((i64)globalIdx);
            emitPtr(type);
            return dst;
        }
        u16 dst = allocReg();
        emitOp(op_load_global);
        emitRegs(dst);
        emitInt((i64)globalIdx);
        return emitUnboxIfInline(dst, type);
    }

    // Phase 4g.5: emit a dynvar store. Mirrors emitGlobalStore but with
    // a third mode (dynscope = save current + set new) for in-function
    // declarations. For inline multi-word composites, routes through the new
    // inline ops; Complex/Fraction keep going through the boxed 1-word path.
    enum DynStoreMode { DynInit, DynStore, DynScopePush };
    void emitDynStore(u16 srcReg, u32 dynIdx, Type* type, DynStoreMode mode) {
        if (isInlineMultiword(type)
            && type != compiler_.complexType()
            && type != compiler_.fractionType()) {
            switch (mode) {
                case DynInit:      emitOp(op_init_dynamic_inline);  break;
                case DynStore:     emitOp(op_store_dynamic_inline); break;
                case DynScopePush: emitOp(op_dynscope_push_inline); break;
            }
            emitRegs(srcReg);
            emitInt((i64)dynIdx);
            emitPtr(type);
            return;
        }
        u16 storeReg = (type && type->repr_ == ts::Type::Repr::Inline)
                     ? emitBoxIfInline(srcReg, type) : srcReg;
        switch (mode) {
            case DynInit:
                emitOp(storesObjPtr(type) ? op_init_dynamic_obj : op_store_dynamic);
                break;
            case DynStore:
                emitOp(storesObjPtr(type) ? op_store_dynamic_obj : op_store_dynamic);
                break;
            case DynScopePush:
                emitOp(op_dynscope_push);
                break;
        }
        emitRegs(storeReg);
        emitInt((i64)dynIdx);
    }

    // Phase 4g.5: emit a dynvar load. For inline multi-word composites,
    // allocates a fresh multi-word slot. For everything else, falls back to
    // op_load_dynamic (and unboxes Complex/Fraction).
    u16 emitDynLoad(u32 dynIdx, Type* type) {
        if (isInlineMultiword(type)
            && type != compiler_.complexType()
            && type != compiler_.fractionType()) {
            u16 dst = allocSlot(type);
            emitOp(op_load_dynamic_inline);
            emitRegs(dst);
            emitInt((i64)dynIdx);
            emitPtr(type);
            return dst;
        }
        u16 dst = allocReg();
        emitOp(op_load_dynamic);
        emitRegs(dst);
        emitInt((i64)dynIdx);
        return emitUnboxIfInline(dst, type);
    }

    // True iff t is an Inline value type that occupies more than one Word
    // (Phase 4f: Complex and Fraction; later: tuples/structs/enums).
    static bool isInlineMultiword(Type const* t) {
        return t && t->repr_ == ts::Type::Repr::Inline && t->sizeWords_ > 1;
    }

    // Phase 2 of tracing-GC project: emit op_safepoint and record a
    // CodeBlock::StackMap entry naming every live-reference register slot
    // at this PC. Mirrors the existing coroGCMaps_ pattern but applies to
    // every safepoint, not just yield points. Conservative: skips inline
    // multiword locals (their base word is a payload word, not an Obj*;
    // tracing through it would crash). Phase 3 will refine to walk inline
    // composite layouts precisely.
    void emitSafepointWithStackMap();

    // Phase 5.2: record a stack map at the current PC offset *without*
    // emitting an op_safepoint. Used right after the last operand of a
    // non-tail CALL has been emitted, so the offset equals the call's
    // returnPC. When a GC cycle fires inside a callee, the marker walks
    // the caller's frame at this PC and roots every named live register.
    void emitReturnPcStackMap(u16 resultReg = UINT16_MAX, Type* resultType = nullptr);

    // Phase 5.2: drop the per-register types for argument slots immediately
    // before emitting a call. Once the call dispatches, those register
    // slots are owned by the callee's frame -- the caller's stack map at
    // the returnPC must NOT root them, or a GC walker visiting mid-call
    // would read callee data as Obj* pointers.
    void clearArgRegTypes(u16 argBase, u16 argEnd);

    // BOX an inline value into a heap Obj* (returns reg holding the Obj*).
    // For non-inline types, returns srcReg unchanged.
    //
    // Phase 4g.2: extends Phase 4f boxing to generic inline structs/tuples.
    // Storage boundaries (globals, container elements, Map/Set keys/values)
    // continue to expect a 1-word Obj* slot; the inline payload travels
    // through op_box_struct / op_box_tuple into a heap Struct/Tuple whose
    // v[] memory is laid out exactly the same way as the inline form.
    u16 emitBoxIfInline(u16 srcReg, Type* type) {
        if (type == compiler_.complexType()) {
            u16 dst = allocReg();
            emitOp(op_box_complex);
            emitRegs(dst, srcReg);
            return dst;
        }
        if (type == compiler_.fractionType()) {
            u16 dst = allocReg();
            emitOp(op_box_fraction);
            emitRegs(dst, srcReg);
            return dst;
        }
        if (type && type->repr_ == ts::Type::Repr::Inline) {
            if (auto* st = dynamic_cast<ts::StructType*>(type)) {
                u16 dst = allocReg();
                emitOp(op_box_struct);
                emitRegs(dst, srcReg);
                emitPtr(st);
                return dst;
            }
            if (auto* tt = dynamic_cast<ts::TupleType*>(type)) {
                u16 dst = allocReg();
                emitOp(op_box_tuple);
                emitRegs(dst, srcReg);
                emitPtr(tt);
                return dst;
            }
            if (auto* en = dynamic_cast<ts::EnumType*>(type)) {
                u16 dst = allocReg();
                emitOp(op_box_enum);
                emitRegs(dst, srcReg);
                emitPtr(en);
                return dst;
            }
        }
        return srcReg;
    }

    // UNBOX a heap Obj* into an inline multi-word slot (returns base reg).
    // For non-inline types, returns srcReg unchanged.
    u16 emitUnboxIfInline(u16 srcReg, Type* type) {
        if (type == compiler_.complexType()) {
            u16 dst = allocSlot(type);
            emitOp(op_unbox_complex);
            emitRegs(dst, srcReg);
            return dst;
        }
        if (type == compiler_.fractionType()) {
            u16 dst = allocSlot(type);
            emitOp(op_unbox_fraction);
            emitRegs(dst, srcReg);
            return dst;
        }
        if (type && type->repr_ == ts::Type::Repr::Inline) {
            if (auto* st = dynamic_cast<ts::StructType*>(type)) {
                u16 dst = allocSlot(type);
                emitOp(op_unbox_struct);
                emitRegs(dst, srcReg);
                emitPtr(st);
                return dst;
            }
            if (auto* tt = dynamic_cast<ts::TupleType*>(type)) {
                u16 dst = allocSlot(type);
                emitOp(op_unbox_tuple);
                emitRegs(dst, srcReg);
                emitPtr(tt);
                return dst;
            }
            if (auto* en = dynamic_cast<ts::EnumType*>(type)) {
                u16 dst = allocSlot(type);
                emitOp(op_unbox_enum);
                emitRegs(dst, srcReg);
                emitPtr(en);
                return dst;
            }
        }
        return srcReg;
    }

    // Jump helpers - store target indices, resolve to pointers after emission
    // jumpFixups_ tracks Code positions that contain jump target indices
    std::vector<u32> jumpFixups_;

    u32 emitJump(Operation jumpOp, u16 condReg = 0);
    void patchJump(u32 jumpPos);
    void emitJumpTo(u32 targetIdx);  // Emit unconditional jump to known index

    // Resolve all jump target indices to Code* pointers (called after code emission)
    void resolveJumps(CodeBlock* block);

    // Variadic helpers for auto-map codegen
    Type* getParamType(const FuncInfo* funcInfo, const CallExpr_* expr, size_t i);
    u16 emitVariadicPack(CallExpr_* expr, u16 callArgBase, u16 argc);

    // Insert conversions for numeric tower promotions
    u16 ensureInt(u16 reg, Type* type);
    u16 ensureFloat(u16 reg, Type* type);
    u16 ensureFraction(u16 reg, Type* type);
    u16 ensureComplex(u16 reg, Type* type);
    u16 ensureType(u16 reg, Type* fromType, Type* toType);
    // Pack a concrete value into a `some C` existential (exType is an
    // ExistentialType*), emitting op_pack_existential with its witness dict.
    u16 ensureExistential(u16 reg, Type* fromType, Type* exType);
    // Lower a constraint-method call on a `some C` receiver to op_call_witness.
    u16 genWitnessCall(CallExpr_* expr);
    // Recursively map witness dispatch through `kinds[level..]` collection levels
    // (outer->inner), bottoming out in emitWitnessDispatch at the existential.
    u16 genWitnessMap(u16 srcReg, Type* srcType, Type* dstType,
                      std::vector<i32> const& kinds, size_t level, i32 slot);
    // Emit a single op_call_witness: dispatch method `slot` on the existential in
    // `exReg`, result of type `retType`. Shared by scalar and auto-mapped paths.
    u16 emitWitnessDispatch(u16 exReg, i32 slot, Type* retType);

    // Get the appropriate arithmetic opcode based on type
    Operation getArithOp(BinaryOpExpr::Op op, Type* type);
    Operation getCmpOp(BinaryOpExpr::Op op, Type* type);
    Operation getCompositeArithOp(BinaryOpExpr::Op op);
    Operation getCompositeArithOpInline(BinaryOpExpr::Op op);
    Operation getCompositeCmpOp(BinaryOpExpr::Op op);
    Operation getCompositeCmpOpInline(BinaryOpExpr::Op op);

    // Check if type is a composite numeric (array or tuple)
    bool isCompositeNumeric(Type* type) const;

    // Phase 4g.2/4g.13: emit a field-get for a struct/tuple parent. Inline
    // parents use op_inline_struct_get / op_inline_tuple_get; Heap parents
    // use op_struct_get / op_tuple_get. Both now copy multi-word natively
    // for Inline composite fields -- no boundary box/unbox is needed.
    u16 emitFieldGet(Type* parentType, u16 parentReg, u16 fieldIdx, Type* fieldType) {
        bool inlineParent = parentType && parentType->repr_ == ts::Type::Repr::Inline;
        u16 dst = allocSlot(fieldType);
        if (inlineParent) {
            if (dynamic_cast<ts::TupleType*>(parentType)) {
                emitOp(op_inline_tuple_get);
            } else {
                emitOp(op_inline_struct_get);
            }
        } else {
            if (dynamic_cast<ts::TupleType*>(parentType)) {
                emitOp(op_tuple_get);
            } else {
                emitOp(op_struct_get);
            }
        }
        emitRegs(dst, parentReg, fieldIdx);
        emitPtr(parentType);
        return dst;
    }

    // Pattern match code generation
    void genPatternMatch(Pattern* pat, u16 subjReg, Type* subjType,
                         std::vector<u32>& failJumps, bool isMutable = false);

    // Helper for global store in pattern bindings
    void emitGlobalStoreIfNeeded(const std::string& name, u16 reg);

    // Value-producing if-else helpers (for implicit returns)
    bool genBlockForValue(BlockStmt* block, u16 resultReg);
    void genIfStmtForValue(IfStmtNode* stmt, u16 resultReg);
    void genSwitchStmtForValue(SwitchStmtNode* stmt, u16 resultReg);

    // Tail-position-as-return helpers. Used when a function's body ends in a
    // value-producing if-else or match: instead of writing each branch's value
    // into a shared resultReg and then RETURN'ing that reg, each branch emits
    // its own op_return directly. Eliminates the intermediate MOV.
    void genBlockAsReturn(BlockStmt* block);
    void genIfStmtAsReturn(IfStmtNode* stmt);
    void genSwitchStmtAsReturn(SwitchStmtNode* stmt);

    void error(SourceRange loc, const std::string& msg);

    // --- Constant folding ---
    struct ConstVal {
        enum Kind { CInt, CFloat, CBool } kind;
        union { i64 intVal; f64 floatVal; bool boolVal; };
    };
    std::unordered_map<u16, ConstVal> constRegs_;

    void markConstInt(u16 reg, i64 val);
    void markConstFloat(u16 reg, f64 val);
    void markConstBool(u16 reg, bool val);
    void clearConsts(u16 fromReg);
    const ConstVal* getConst(u16 reg) const;

    bool canFoldBinaryOp(BinaryOpExpr* expr) const;
    u16 foldBinaryOp(BinaryOpExpr* expr);
    bool canFoldUnaryOp(UnaryOpExpr* expr) const;
    u16 foldUnaryOp(UnaryOpExpr* expr);
    u16 tryFoldBuiltinCall(CallExpr_* expr, u16 argBase, u16 callArgc);

    // --- Loop break/continue support ---
    struct LoopContext {
        u16 savedReg;                      // register level to reclaim before jumping
        std::vector<u32> breakJumps;       // jump positions to patch to after loop
        std::vector<u32> continueJumps;    // jump positions to patch to increment/advance
    };
    std::vector<LoopContext> loopStack_;

    // --- Tail call optimization ---
    bool inTailPosition_ = false;

    // --- Coroutine codegen state ---
    bool inCoroutineFn_ = false;

    // --- Async codegen state --- true while compiling an `async fn` body, so
    // returns lower to op_async_return (resolve the Future) and await suspension
    // points record GC maps (reusing the coroutine GC-map machinery).
    bool inAsyncFn_ = false;

    // Phase 4f: return type of the function currently being compiled.
    // Used to decide whether to box a multi-word inline value before op_return.
    Type* currentReturnType_ = nullptr;

    // Phase 4d: op_return is natively multi-word. Encode source register and
    // slot size; the handler copies nWords from src into the caller's result
    // slot. No boxing involved.
    void emitReturn(u16 reg) {
        if (inAsyncFn_) {
            // Inside an async fn, returning a value resolves its Future. Routes
            // every return path (trailing expr, explicit return, if/switch-as-
            // return) through op_async_return.
            emitOp(op_async_return);
            emitRegs(reg);
            return;
        }
        u32 nWords = typeSlotWords(currentReturnType_);
        emitOp(op_return);
        emitRegs(reg, (u16)nWords);
    }
    u16 currentYieldCount_ = 0;

    // --- Function body tracking (for dynamic scope push/pop) ---
    bool inFunctionBody_ = false;
};

} // namespace ts

#endif /* codegen_hpp */
