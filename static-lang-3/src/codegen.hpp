//
//  codegen.hpp
//  static-lang-3
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

    // Local variable -> register mapping
    struct LocalVar {
        u16 reg;
        Type* type;
        bool isMutable;
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
    LocalVar* lookupLocal(const std::string& name);

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
    void genExprStmt(ExprStmtNode* stmt);

    // Expression codegen - returns register holding result
    u16 genExpr(Expr* expr);
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
    u16 genAutoMapLambdaCall(CallExpr_* expr, u16 calleeReg, FunctionType* funcType);
    u16 genAutoMapCallList(CallExpr_* expr, const FuncInfo* funcInfo);
    u16 genAutoMapCallListVoid(CallExpr_* expr, const FuncInfo* funcInfo);
    u16 genExplicitImplicitAutoMapCall(CallExpr_* expr);
    u16 genCartesianCall(CallExpr_* expr);
    u16 genDeepMapCall(CallExpr_* expr, int depth);
    u16 genAutoMapBinaryOp(BinaryOpExpr* expr);
    u16 genAutoMapBinaryOpList(BinaryOpExpr* expr);
    u16 genCartesianBinaryOp(BinaryOpExpr* expr);
    u16 genDeepMapBinaryOp(BinaryOpExpr* expr, int depth);
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

    // Emit helpers
    void emitOp(Operation op) { currentBlock_->emitOp(op); }
    void emitRegs(u16 r0, u16 r1 = 0, u16 r2 = 0, u16 r3 = 0) {
        currentBlock_->emitRegs(r0, r1, r2, r3);
    }
    void emitInt(i64 val) { currentBlock_->emitInt(val); }
    void emitFloat(f64 val) { currentBlock_->emitFloat(val); }
    void emitPtr(void* p) { currentBlock_->emitPtr(p); }

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

    // Get the appropriate arithmetic opcode based on type
    Operation getArithOp(BinaryOpExpr::Op op, Type* type);
    Operation getCmpOp(BinaryOpExpr::Op op, Type* type);
    Operation getCompositeArithOp(BinaryOpExpr::Op op);
    Operation getCompositeCmpOp(BinaryOpExpr::Op op);

    // Check if type is a composite numeric (array or tuple)
    bool isCompositeNumeric(Type* type) const;

    // Pattern match code generation
    void genPatternMatch(Pattern* pat, u16 subjReg, Type* subjType,
                         std::vector<u32>& failJumps, bool isMutable = false);

    // Helper for global store in pattern bindings
    void emitGlobalStoreIfNeeded(const std::string& name, u16 reg);

    // Value-producing if-else helpers (for implicit returns)
    bool genBlockForValue(BlockStmt* block, u16 resultReg);
    void genIfStmtForValue(IfStmtNode* stmt, u16 resultReg);
    void genSwitchStmtForValue(SwitchStmtNode* stmt, u16 resultReg);

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
    u16 currentYieldCount_ = 0;
};

} // namespace ts

#endif /* codegen_hpp */
