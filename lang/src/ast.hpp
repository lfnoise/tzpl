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
//  ast.hpp
//  lang
//
//  Abstract Syntax Tree node hierarchy
//

#ifndef ast_hpp
#define ast_hpp

#include "error.hpp"
#include <string>
#include <vector>
#include <memory>

namespace ts {

// Forward declarations
class Type; // forward declaration matches type_system.hpp
class LambdaType;
class TemplateLambdaType;

// A single where-clause entry: T: ConstraintName (forward declaration for LambdaExprNode)
struct WhereConstraint {
    std::string typeParam;       // "T"
    std::string constraintName;  // "Comparable"
    SourceRange loc;
};

// Base AST node
struct ASTNode {
    enum Kind {
        // Declarations
        LetDecl, VarDecl, ConstDecl, FnDecl, StructDecl, UnionDecl, ImportDecl, TypeAliasDecl, ConstraintDecl,

        // Statements
        Block, ExprStmt, IfStmt, WhileStmt, ForStmt, SwitchStmt,
        ReturnStmt, AssignStmt, IndexAssignStmt, BreakStmt, ContinueStmt,

        // Expressions
        IntLiteral, FloatLiteral, ImaginaryLiteral, FractionLiteral, StringLiteral, BoolLiteral, SymbolLiteral,
        Identifier, DynamicVar, BinaryOp, UnaryOp, CallExpr, IndexExpr, FieldExpr,
        TupleLiteral, ArrayLiteral, ListLiteral, MapLiteral, SetLiteral, NilLiteral, StructLiteral, EnumConstructor,
        LambdaExpr, IfExpr, BlockExpr, AutoMap, RangeExpr, AsTypeExpr,

        // Type expressions
        NamedType, ArrayType, ListType, MapType, SetType, TupleType, FunctionType, RefType, TemplateType,
    };

    Kind kind;
    SourceRange loc;
    Type* resolvedType = nullptr;  // Filled by type checker

    ASTNode(Kind k, SourceRange l) : kind(k), loc(l) {}
    virtual ~ASTNode() = default;
};

using ASTPtr = std::unique_ptr<ASTNode>;
using ASTList = std::vector<ASTPtr>;

// --- Type expression nodes ---

struct TypeExpr : ASTNode {
    TypeExpr(Kind k, SourceRange l) : ASTNode(k, l) {}
};

using TypeExprPtr = std::unique_ptr<TypeExpr>;

struct NamedTypeNode : TypeExpr {
    std::string name;
    NamedTypeNode(SourceRange l, std::string n)
        : TypeExpr(NamedType, l), name(std::move(n)) {}
};

struct ArrayTypeNode : TypeExpr {
    TypeExprPtr elemType;
    bool isImmutable = false;  // true for #[T] (persistent vector type)
    ArrayTypeNode(SourceRange l, TypeExprPtr elem)
        : TypeExpr(ArrayType, l), elemType(std::move(elem)) {}
};

struct ListTypeNode : TypeExpr {
    TypeExprPtr elemType;
    ListTypeNode(SourceRange l, TypeExprPtr elem)
        : TypeExpr(ListType, l), elemType(std::move(elem)) {}
};

struct MapTypeNode : TypeExpr {
    TypeExprPtr keyType;
    TypeExprPtr valueType;
    bool isImmutable = false;  // true for #[K:V] (persistent map type)
    MapTypeNode(SourceRange l, TypeExprPtr k, TypeExprPtr v)
        : TypeExpr(MapType, l), keyType(std::move(k)), valueType(std::move(v)) {}
};

struct SetTypeNode : TypeExpr {
    TypeExprPtr elemType;
    SetTypeNode(SourceRange l, TypeExprPtr elem)
        : TypeExpr(SetType, l), elemType(std::move(elem)) {}
};

struct TupleTypeNode : TypeExpr {
    std::vector<TypeExprPtr> elemTypes;
    TupleTypeNode(SourceRange l, std::vector<TypeExprPtr> elems)
        : TypeExpr(TupleType, l), elemTypes(std::move(elems)) {}
};

struct FunctionTypeNode : TypeExpr {
    std::vector<TypeExprPtr> paramTypes;
    TypeExprPtr returnType;
    FunctionTypeNode(SourceRange l, std::vector<TypeExprPtr> params, TypeExprPtr ret)
        : TypeExpr(FunctionType, l), paramTypes(std::move(params)), returnType(std::move(ret)) {}
};

struct RefTypeNode : TypeExpr {
    TypeExprPtr elemType;
    RefTypeNode(SourceRange l, TypeExprPtr elem)
        : TypeExpr(RefType, l), elemType(std::move(elem)) {}
};

struct TemplateTypeNode : TypeExpr {
    std::string name;
    std::vector<TypeExprPtr> typeArgs;
    TemplateTypeNode(SourceRange l, std::string n, std::vector<TypeExprPtr> args)
        : TypeExpr(TemplateType, l), name(std::move(n)), typeArgs(std::move(args)) {}
};

// --- Expression nodes ---

struct Expr : ASTNode {
    Expr(Kind k, SourceRange l) : ASTNode(k, l) {}
};

using ExprPtr = std::unique_ptr<Expr>;
using ExprList = std::vector<ExprPtr>;

struct IntLiteralExpr : Expr {
    i64 value;
    IntLiteralExpr(SourceRange l, i64 v)
        : Expr(IntLiteral, l), value(v) {}
};

struct FloatLiteralExpr : Expr {
    f64 value;
    FloatLiteralExpr(SourceRange l, f64 v)
        : Expr(FloatLiteral, l), value(v) {}
};

struct ImaginaryLiteralExpr : Expr {
    f64 value;  // The imaginary coefficient (e.g., 4.0 for 4i)
    ImaginaryLiteralExpr(SourceRange l, f64 v)
        : Expr(ImaginaryLiteral, l), value(v) {}
};

struct FractionLiteralExpr : Expr {
    i64 numerator;
    i64 denominator;
    FractionLiteralExpr(SourceRange l, i64 n, i64 d)
        : Expr(FractionLiteral, l), numerator(n), denominator(d) {}
};

struct StringLiteralExpr : Expr {
    std::string value;
    StringLiteralExpr(SourceRange l, std::string v)
        : Expr(StringLiteral, l), value(std::move(v)) {}
};

struct BoolLiteralExpr : Expr {
    bool value;
    BoolLiteralExpr(SourceRange l, bool v)
        : Expr(BoolLiteral, l), value(v) {}
};

struct SymbolLiteralExpr : Expr {
    std::string value;
    SymbolLiteralExpr(SourceRange l, std::string v)
        : Expr(SymbolLiteral, l), value(std::move(v)) {}
};

struct IdentifierExpr : Expr {
    std::string name;
    i32 resolvedFuncGlobalIndex = -1;  // Set by type checker for function-as-value references
    LambdaType* funcRefLambdaType = nullptr;  // Set by type checker for function-as-value wrapping
    LambdaType* templateLambdaSpecType = nullptr;  // Set by type checker: specialization for template lambda passed as arg
    IdentifierExpr(SourceRange l, std::string n)
        : Expr(Identifier, l), name(std::move(n)) {}
};

struct DynamicVarExpr : Expr {
    std::string name;
    DynamicVarExpr(SourceRange l, std::string n)
        : Expr(DynamicVar, l), name(std::move(n)) {}
};

// Auto-map annotation for function call arguments, struct literal fields, and binary op operands
struct AutoMapArg {
    int depth = 0;          // 0=not mapped, 1=@, 2=@@, 3=@@@, etc.
    int cartesianIndex = 0; // 0=zip, 1+=cartesian (@1, @2, ...)
    bool isList = false;    // true when auto-mapping over a List (result is lazy List, not Array)

    // Convenience: true if any auto-mapping is active
    explicit operator bool() const { return depth > 0; }
};

struct BinaryOpExpr : Expr {
    enum Op {
        Add, Sub, Mul, Div, Mod, IntDiv,
        BitAnd, BitOr, BitXor, ShiftL, ShiftR, UShiftR,
        And, Or,
        Eq, Ne, Lt, Le, Gt, Ge,
        Pipeline,  // |>
        Concat,    // $
        Cons,       // ::
        LeftArrow,  // <-
        RightArrow, // ->
    };
    Op op;
    ExprPtr left;
    ExprPtr right;
    i32 resolvedFuncGlobalIndex = -1;  // Set by type checker for operator overloads
    bool isBuiltinCall = false;        // Set by type checker: true for primitive built-in functions
    bool builtinAcceptsInlineArgs = false;  // Phase 4g.6: builtin reads inline-composite args natively
    AutoMapArg leftAutoMap;            // Set by type checker: auto-map annotation for left operand
    AutoMapArg rightAutoMap;           // Set by type checker: auto-map annotation for right operand

    BinaryOpExpr(SourceRange l, Op o, ExprPtr lhs, ExprPtr rhs)
        : Expr(BinaryOp, l), op(o), left(std::move(lhs)), right(std::move(rhs)) {}
};

struct UnaryOpExpr : Expr {
    enum Op { Neg, Not, BitNot, Deref, Ref };
    Op op;
    ExprPtr operand;
    i32 resolvedFuncGlobalIndex = -1;  // Set by type checker for operator overloads
    bool isBuiltinCall = false;        // Set by type checker
    bool builtinAcceptsInlineArgs = false;  // Phase 4g.6

    UnaryOpExpr(SourceRange l, Op o, ExprPtr expr)
        : Expr(UnaryOp, l), op(o), operand(std::move(expr)) {}
};

struct CallExpr_ : Expr {
    ExprPtr callee;
    ExprList args;
    i32 resolvedFuncGlobalIndex = -1;  // Set by type checker for overload resolution
    bool isBuiltinCall = false;        // Set by type checker: true for primitive built-in functions
    bool builtinAcceptsInlineArgs = false;  // Phase 4g.6: builtin reads inline-composite args natively
    bool isCoroCall = false;           // Set by type checker: true when calling a coro fn
    bool isCoroResume = false;         // Set by type checker: true for next() on Coroutine
    bool isCoroYield = false;          // Set by type checker: true for yield() in coro fn
    bool isCoroYieldAll = false;       // Set by type checker: true for yieldAll() in coro fn
    std::vector<AutoMapArg> autoMapArgs;  // Set by type checker: explicit @ auto-map info for each arg
    std::vector<AutoMapArg> innerAutoMapArgs;  // Set by type checker: implicit auto-map (inner loop) for each arg
    i32 variadicPackStart = -1;       // Set by type checker: arg index where variadic packing begins (-1 = none)
    Type* variadicPackType = nullptr;  // Set by type checker: TupleType* or ArrayType* for packed arg
    LambdaType* resolvedTemplateLambdaType = nullptr;  // Set by type checker: monomorphized type for template lambda calls

    CallExpr_(SourceRange l, ExprPtr c, ExprList a)
        : Expr(CallExpr, l), callee(std::move(c)), args(std::move(a)) {}
};

struct IndexExpr_ : Expr {
    ExprPtr object;
    ExprPtr index;
    AutoMapArg autoMap;      // Set by type checker: explicit @ on object (arr @ [i])
    AutoMapArg indexAutoMap;  // Set by type checker: index is Array/List of indices

    IndexExpr_(SourceRange l, ExprPtr obj, ExprPtr idx)
        : Expr(IndexExpr, l), object(std::move(obj)), index(std::move(idx)) {}
};

struct FieldExpr_ : Expr {
    ExprPtr object;
    std::string field;
    AutoMapArg autoMap;  // Set by type checker when object is Array/List of structs/tuples

    FieldExpr_(SourceRange l, ExprPtr obj, std::string f)
        : Expr(FieldExpr, l), object(std::move(obj)), field(std::move(f)) {}
};

struct TupleLiteralExpr : Expr {
    ExprList elements;
    std::vector<AutoMapArg> autoMapElements;  // Set by type checker: auto-map info for each element
    TupleLiteralExpr(SourceRange l, ExprList elems)
        : Expr(TupleLiteral, l), elements(std::move(elems)) {}
};

struct ArrayLiteralExpr : Expr {
    ExprList elements;
    TypeExprPtr elemTypeExpr;  // non-null when using [Type](...) syntax
    std::vector<AutoMapArg> autoMapElements;  // Set by type checker: auto-map info for each element
    bool isImmutable = false;  // true for #[...] (persistent vector literal)
    ArrayLiteralExpr(SourceRange l, ExprList elems)
        : Expr(ArrayLiteral, l), elements(std::move(elems)) {}
};

struct ListLiteralExpr : Expr {
    ExprList elements;
    ListLiteralExpr(SourceRange l, ExprList elems)
        : Expr(ListLiteral, l), elements(std::move(elems)) {}
};

struct MapLiteralExpr : Expr {
    struct Entry {
        ExprPtr key;
        ExprPtr value;
    };
    std::vector<Entry> entries;
    bool isImmutable = false;  // true for #[...] (persistent map literal)
    MapLiteralExpr(SourceRange l, std::vector<Entry> e)
        : Expr(MapLiteral, l), entries(std::move(e)) {}
};

struct SetLiteralExpr : Expr {
    ExprList elements;
    SetLiteralExpr(SourceRange l, ExprList elems)
        : Expr(SetLiteral, l), elements(std::move(elems)) {}
};

struct NilLiteralExpr : Expr {
    NilLiteralExpr(SourceRange l)
        : Expr(NilLiteral, l) {}
};

struct StructFieldInit {
    std::string name;
    ExprPtr value;
    SourceRange loc;
};

struct StructLiteralExpr : Expr {
    std::string structName;
    std::vector<StructFieldInit> fields;
    std::vector<AutoMapArg> autoMapFields;  // Set by type checker: auto-map info for each field
    bool positional = false;          // True when fields are specified by position, not by name
    ExprPtr spreadExpr;               // ... source expression for struct update syntax
    std::vector<TypeExprPtr> typeArgs; // Explicit type args (empty = infer)

    StructLiteralExpr(SourceRange l, std::string name, std::vector<StructFieldInit> f)
        : Expr(StructLiteral, l), structName(std::move(name)), fields(std::move(f)) {}
};

struct LambdaExprNode : Expr {
    struct Param {
        std::string name;
        TypeExprPtr typeExpr;
        Type* resolvedType = nullptr;  // Set by backward inference for untyped params
    };
    struct CapturedVar {
        std::string name;
        Type* type = nullptr;    // filled by type checker
        // True iff the captured variable is a mutable `var` — captured by
        // reference through an UpVar cell (Lua-style upvalue). False for
        // `let`/`const` captures, which are snapshot copies of the value.
        bool byReference = false;
    };
    std::vector<Param> params;
    TypeExprPtr returnType;
    ASTPtr body;
    std::vector<CapturedVar> captures;  // populated by type checker, consumed by codegen
    LambdaType* lambdaType = nullptr;   // runtime type with free var info, set by type checker

    // Template lambda support
    std::vector<std::string> typeParams;              // ["T", "U"], empty = non-generic
    std::vector<WhereConstraint> whereConstraints;    // constraints on type params
    TemplateLambdaType* templateLambdaType = nullptr;  // set by type checker for template lambdas

    bool isCoroutine = false;  // Set by parser when 'coro fn(...)' lambda syntax is used

    LambdaExprNode(SourceRange l, std::vector<Param> p, TypeExprPtr ret, ASTPtr b)
        : Expr(LambdaExpr, l), params(std::move(p)),
          returnType(std::move(ret)), body(std::move(b)) {}
};

struct IfExprNode : Expr {
    ExprPtr condition;
    ASTPtr thenBranch;
    ASTPtr elseBranch;  // nullable

    IfExprNode(SourceRange l, ExprPtr cond, ASTPtr then_, ASTPtr else_)
        : Expr(IfExpr, l), condition(std::move(cond)),
          thenBranch(std::move(then_)), elseBranch(std::move(else_)) {}
};

// Block expression: { stmts; trailing_expr }
struct BlockExprNode : Expr {
    ASTPtr body;  // BlockStmt
    BlockExprNode(SourceRange l, ASTPtr b)
        : Expr(BlockExpr, l), body(std::move(b)) {}
};

// Explicit auto-map expression: @expr, @@expr, @1 expr, @2 expr
struct AutoMapExpr : Expr {
    ExprPtr inner;
    int depth;           // 1 for @, 2 for @@, 3 for @@@, etc.
    int cartesianIndex;  // 0 for plain @, 1+ for @1, @2, etc.

    AutoMapExpr(SourceRange l, ExprPtr e, int d, int ci)
        : Expr(AutoMap, l), inner(std::move(e)), depth(d), cartesianIndex(ci) {}
};

struct RangeExprNode : Expr {
    ExprPtr start;
    ExprPtr next;      // nullable — second value in (start,next..end)
    ExprPtr end;       // nullable — if absent, range is infinite
    bool isInfinite;

    RangeExprNode(SourceRange l, ExprPtr s, ExprPtr n, ExprPtr e, bool inf)
        : Expr(RangeExpr, l), start(std::move(s)), next(std::move(n)),
          end(std::move(e)), isInfinite(inf) {}
};

struct AsTypeExprNode : Expr {
    ExprPtr subject;
    TypeExprPtr targetType;
    Type* resolvedTargetType = nullptr;  // filled by type checker
    int autoMapDepth = 0;    // number of Array/List layers to map through
    bool autoMapIsList = false;  // true if any layer is a List
    AsTypeExprNode(SourceRange l, ExprPtr subj, TypeExprPtr type)
        : Expr(AsTypeExpr, l), subject(std::move(subj)),
          targetType(std::move(type)) {}
};

struct EnumConstructExpr : Expr {
    std::string enumName;
    std::string caseName;
    ExprPtr arg;  // nullable for no-data cases
    std::vector<TypeExprPtr> typeArgs; // Explicit type args (empty = infer)

    EnumConstructExpr(SourceRange l, std::string eName, std::string cName, ExprPtr a)
        : Expr(EnumConstructor, l), enumName(std::move(eName)),
          caseName(std::move(cName)), arg(std::move(a)) {}
};

// --- Pattern nodes (for switch/case and destructuring) ---

struct Pattern {
    enum Kind {
        LiteralPat, WildcardPat, BindingPat, EnumPat, StructPat, TuplePat, ArrayPat, ConsPat, GuardedPat, TypeTestPat
    };
    Kind kind;
    SourceRange loc;
    Type* resolvedType = nullptr;
    int enumCaseIndex = -1;           // >= 0 when pattern matches an unqualified enum case
    Type* enumCaseDataType = nullptr; // data type of the matched enum case

    Pattern(Kind k, SourceRange l) : kind(k), loc(l) {}
    virtual ~Pattern() = default;
};

using PatternPtr = std::unique_ptr<Pattern>;

struct LiteralPattern : Pattern {
    ExprPtr literal;
    LiteralPattern(SourceRange l, ExprPtr lit)
        : Pattern(LiteralPat, l), literal(std::move(lit)) {}
};

struct WildcardPattern : Pattern {
    WildcardPattern(SourceRange l) : Pattern(WildcardPat, l) {}
};

struct BindingPattern : Pattern {
    std::string name;
    BindingPattern(SourceRange l, std::string n)
        : Pattern(BindingPat, l), name(std::move(n)) {}
};

struct EnumPattern : Pattern {
    std::string enumName;
    std::string caseName;
    PatternPtr innerPattern;  // nullable for no-data cases
    EnumPattern(SourceRange l, std::string eName, std::string cName, PatternPtr inner)
        : Pattern(EnumPat, l), enumName(std::move(eName)),
          caseName(std::move(cName)), innerPattern(std::move(inner)) {}
};

struct StructPatternField {
    std::string name;
    PatternPtr pattern;
    SourceRange loc;
};

struct StructPattern : Pattern {
    std::string structName;
    std::vector<StructPatternField> fields;
    StructPattern(SourceRange l, std::string sName, std::vector<StructPatternField> f)
        : Pattern(StructPat, l), structName(std::move(sName)),
          fields(std::move(f)) {}
};

struct TuplePattern : Pattern {
    std::vector<PatternPtr> elements;
    bool hasRest;                       // true if ... present
    std::string restName;              // non-empty if rest is bound (e.g. ...c)
    std::string structName;            // non-empty for tuple struct patterns: Name(pat, ...)
    TuplePattern(SourceRange l, std::vector<PatternPtr> elems,
                 bool rest = false, std::string rname = "", std::string sname = "")
        : Pattern(TuplePat, l), elements(std::move(elems)),
          hasRest(rest), restName(std::move(rname)), structName(std::move(sname)) {}
};

struct ArrayPattern : Pattern {
    std::vector<PatternPtr> elements;  // leading fixed elements
    bool hasRest;                       // true if ... present
    std::string restName;              // non-empty if rest is bound (e.g. ...tail)
    ArrayPattern(SourceRange l, std::vector<PatternPtr> elems,
                 bool rest = false, std::string rname = "")
        : Pattern(ArrayPat, l), elements(std::move(elems)),
          hasRest(rest), restName(std::move(rname)) {}
};

struct ConsPattern : Pattern {
    PatternPtr head;
    PatternPtr tail;
    ConsPattern(SourceRange l, PatternPtr h, PatternPtr t)
        : Pattern(ConsPat, l), head(std::move(h)), tail(std::move(t)) {}
};

struct GuardedPattern : Pattern {
    PatternPtr pattern;
    ExprPtr guard;
    GuardedPattern(SourceRange l, PatternPtr pat, ExprPtr g)
        : Pattern(GuardedPat, l), pattern(std::move(pat)),
          guard(std::move(g)) {}
};

struct TypeTestPattern : Pattern {
    std::string bindingName;
    TypeExprPtr typeExpr;  // full type expression (e.g. Int, [Any])
    Type* resolvedTargetType = nullptr;  // filled by type checker
    TypeTestPattern(SourceRange l, std::string name, TypeExprPtr texpr)
        : Pattern(TypeTestPat, l), bindingName(std::move(name)),
          typeExpr(std::move(texpr)) {}
};

// --- Statement nodes ---

struct Stmt : ASTNode {
    Stmt(Kind k, SourceRange l) : ASTNode(k, l) {}
};

using StmtPtr = std::unique_ptr<Stmt>;
using StmtList = std::vector<StmtPtr>;

struct BlockStmt : Stmt {
    ASTList stmts;
    BlockStmt(SourceRange l, ASTList s)
        : Stmt(Block, l), stmts(std::move(s)) {}
};

struct ExprStmtNode : Stmt {
    ExprPtr expr;
    bool isTrailing = false;  // True when this is the last expr in a block without trailing ';'
    ExprStmtNode(SourceRange l, ExprPtr e)
        : Stmt(ExprStmt, l), expr(std::move(e)) {}
};

struct IfStmtNode : Stmt {
    ExprPtr condition;
    ASTPtr thenBranch;
    ASTPtr elseBranch;  // nullable

    IfStmtNode(SourceRange l, ExprPtr cond, ASTPtr then_, ASTPtr else_)
        : Stmt(IfStmt, l), condition(std::move(cond)),
          thenBranch(std::move(then_)), elseBranch(std::move(else_)) {}
};

struct WhileStmtNode : Stmt {
    ExprPtr condition;
    ASTPtr body;

    WhileStmtNode(SourceRange l, ExprPtr cond, ASTPtr b)
        : Stmt(WhileStmt, l), condition(std::move(cond)), body(std::move(b)) {}
};

struct ForStmtNode : Stmt {
    std::string varName;
    ExprPtr iterable;
    ASTPtr body;

    ForStmtNode(SourceRange l, std::string var, ExprPtr iter, ASTPtr b)
        : Stmt(ForStmt, l), varName(std::move(var)),
          iterable(std::move(iter)), body(std::move(b)) {}
};

struct BreakStmtNode : Stmt {
    BreakStmtNode(SourceRange l) : Stmt(BreakStmt, l) {}
};

struct ContinueStmtNode : Stmt {
    ContinueStmtNode(SourceRange l) : Stmt(ContinueStmt, l) {}
};

struct ReturnStmtNode : Stmt {
    ExprPtr value;  // nullable for void return

    ReturnStmtNode(SourceRange l, ExprPtr v)
        : Stmt(ReturnStmt, l), value(std::move(v)) {}
};

struct AssignStmtNode : Stmt {
    std::string target;  // Variable name for MVP (could be extended to lvalues)
    ExprPtr value;
    bool isDynamic = false;  // true for dynamic scope assignment: `name = expr;

    AssignStmtNode(SourceRange l, std::string t, ExprPtr v)
        : Stmt(AssignStmt, l), target(std::move(t)), value(std::move(v)) {}
};

// `obj[index] = value` -- in-place write to an Array or Map. Sets are not
// indexable; set mutation goes through `s insert!(elem)` etc.
struct IndexAssignStmtNode : Stmt {
    ExprPtr object;
    ExprPtr index;
    ExprPtr value;
    // Resolved by type checker: ArrayType* or MapType*
    Type* containerType = nullptr;

    IndexAssignStmtNode(SourceRange l, ExprPtr o, ExprPtr i, ExprPtr v)
        : Stmt(IndexAssignStmt, l), object(std::move(o)), index(std::move(i)), value(std::move(v)) {}
};

// Case clause for switch statement
struct CaseClause {
    PatternPtr pattern;
    ASTPtr body;
    SourceRange loc;
};

struct SwitchStmtNode : Stmt {
    ExprPtr subject;
    std::vector<CaseClause> cases;

    SwitchStmtNode(SourceRange l, ExprPtr subj, std::vector<CaseClause> c)
        : Stmt(SwitchStmt, l), subject(std::move(subj)),
          cases(std::move(c)) {}
};

// --- Declaration nodes ---

struct Decl : ASTNode {
    Decl(Kind k, SourceRange l) : ASTNode(k, l) {}
};

using DeclPtr = std::unique_ptr<Decl>;

struct LetDeclNode : Decl {
    std::string name;
    TypeExprPtr typeExpr;  // nullable (inferred)
    ExprPtr init;
    PatternPtr pattern;    // nullable; when set, name is empty and pattern provides bindings
    bool isPrivate = false;

    LetDeclNode(SourceRange l, std::string n, TypeExprPtr t, ExprPtr i)
        : Decl(LetDecl, l), name(std::move(n)),
          typeExpr(std::move(t)), init(std::move(i)) {}
    LetDeclNode(SourceRange l, PatternPtr pat, ExprPtr i)
        : Decl(LetDecl, l), init(std::move(i)), pattern(std::move(pat)) {}
};

struct VarDeclNode : Decl {
    std::string name;
    TypeExprPtr typeExpr;  // nullable (inferred)
    ExprPtr init;
    PatternPtr pattern;    // nullable; when set, name is empty and pattern provides bindings
    bool isPrivate = false;
    bool isDynamic = false;  // true for dynamic scope variables: var `name = expr;
    // Set by the type checker when this `var` is captured by any nested
    // closure. Triggers codegen to mark the local as an open upvalue slot
    // so reads/writes inside the closure go through op_load/store_upvar_n
    // and on function return the slot's words are closed into the UpVar.
    bool capturedByClosure = false;

    VarDeclNode(SourceRange l, std::string n, TypeExprPtr t, ExprPtr i)
        : Decl(VarDecl, l), name(std::move(n)),
          typeExpr(std::move(t)), init(std::move(i)) {}
    VarDeclNode(SourceRange l, PatternPtr pat, ExprPtr i)
        : Decl(VarDecl, l), init(std::move(i)), pattern(std::move(pat)) {}
};

struct ConstDeclNode : Decl {
    std::string name;
    TypeExprPtr typeExpr;  // nullable (inferred)
    ExprPtr init;
    PatternPtr pattern;    // nullable; when set, name is empty and pattern provides bindings
    bool isPrivate = false;

    ConstDeclNode(SourceRange l, std::string n, TypeExprPtr t, ExprPtr i)
        : Decl(ConstDecl, l), name(std::move(n)),
          typeExpr(std::move(t)), init(std::move(i)) {}
    ConstDeclNode(SourceRange l, PatternPtr pat, ExprPtr i)
        : Decl(ConstDecl, l), init(std::move(i)), pattern(std::move(pat)) {}
};

// Required function signature in a structural constraint
struct RequiredFnSig {
    std::string name;                      // "<", "hash", etc.
    std::vector<TypeExprPtr> paramTypes;   // type exprs (may reference constraint's type params)
    TypeExprPtr returnType;
    SourceRange loc;
};

// A single constraint component (used in composition)
struct ConstraintComponent {
    std::string name;                      // "Comparable", "Numeric"
    std::vector<TypeExprPtr> typeArgs;     // for parameterized refs like Comparable<T>
    SourceRange loc;
};

// constraint Foo<T> = ...
struct ConstraintDeclNode : Decl {
    std::string name;
    std::vector<std::string> typeParams;          // e.g. ["T"] for Comparable<T>, empty for Numeric

    // Union items: types and/or constraint refs joined by |
    std::vector<TypeExprPtr> items;                // non-empty for type-set / union constraints

    // Structural form: constraint Comparable<T> = requires { ... }
    std::vector<RequiredFnSig> requiredFns;        // non-empty for structural constraints

    // Composition: references to other constraints joined by &
    std::vector<ConstraintComponent> components;   // non-empty for composite constraints

    bool isPrivate = false;

    ConstraintDeclNode(SourceRange l, std::string n, std::vector<std::string> tp)
        : Decl(ConstraintDecl, l), name(std::move(n)), typeParams(std::move(tp)) {}
};

struct FnParam {
    std::string name;
    TypeExprPtr typeExpr;
    ExprPtr defaultExpr;       // default value expression (nullptr = required)
    SourceRange loc;
    bool isVariadic = false;
};

struct FnDeclNode : Decl {
    std::string name;
    std::vector<std::string> typeParams;  // Template type params, e.g. ["T", "U"]
    std::vector<FnParam> params;
    TypeExprPtr returnType;  // nullable (inferred as void)
    TypeExprPtr returnTypeConstraint;  // Set by desugaring when return type contains constraint names
    ASTPtr body;             // BlockStmt
    i32 resolvedFuncGlobalIndex = -1;  // Set by type checker for codegen
    bool isPrivate = false;  // Set by parser when 'private' keyword is used
    bool isCoroutine = false;  // Set by parser when 'coro' keyword is used
    bool hasParseError = false;  // Set by parser if any error occurred while parsing this function
    LambdaType* localLambdaType = nullptr;  // Set for local fn declarations (codegen wraps in Lambda)
    std::vector<LambdaExprNode::CapturedVar> captures;  // captured vars for local functions
    std::vector<WhereConstraint> whereConstraints;  // type param constraints

    FnDeclNode(SourceRange l, std::string n, std::vector<std::string> tp,
               std::vector<FnParam> p, TypeExprPtr ret, ASTPtr b)
        : Decl(FnDecl, l), name(std::move(n)), typeParams(std::move(tp)),
          params(std::move(p)), returnType(std::move(ret)), body(std::move(b)) {}
};

struct StructField {
    std::string name;
    TypeExprPtr typeExpr;
    SourceRange loc;
};

struct StructDeclNode : Decl {
    std::string name;
    std::vector<std::string> typeParams;  // Template type params, e.g. ["T", "U"]
    std::vector<StructField> fields;
    std::string parentName;  // Optional parent struct for inheritance (empty if none)
    bool isPrivate = false;
    bool isTupleStruct = false;  // True for tuple structs: struct Point(Int, Int);
    std::vector<WhereConstraint> whereConstraints;  // type param constraints

    StructDeclNode(SourceRange l, std::string n, std::vector<std::string> tp,
                   std::vector<StructField> f, std::string parent = "")
        : Decl(StructDecl, l), name(std::move(n)), typeParams(std::move(tp)),
          fields(std::move(f)), parentName(std::move(parent)) {}
};

struct UnionCase {
    std::string name;
    TypeExprPtr typeExpr;  // nullable for no-data cases
    SourceRange loc;
};

struct UnionDeclNode : Decl {
    std::string name;
    std::vector<std::string> typeParams;  // Template type params, e.g. ["T", "U"]
    std::vector<UnionCase> cases;
    bool isPrivate = false;
    std::vector<WhereConstraint> whereConstraints;  // type param constraints

    UnionDeclNode(SourceRange l, std::string n, std::vector<std::string> tp, std::vector<UnionCase> c)
        : Decl(UnionDecl, l), name(std::move(n)), typeParams(std::move(tp)), cases(std::move(c)) {}
};

// --- Type alias declaration ---

struct TypeAliasDeclNode : Decl {
    std::string name;
    std::vector<std::string> typeParams;  // e.g. ["T", "U"] for generic aliases
    TypeExprPtr typeExpr;
    bool isPrivate = false;
    std::vector<WhereConstraint> whereConstraints;  // type param constraints

    TypeAliasDeclNode(SourceRange l, std::string n, std::vector<std::string> tp, TypeExprPtr t)
        : Decl(TypeAliasDecl, l), name(std::move(n)),
          typeParams(std::move(tp)), typeExpr(std::move(t)) {}
};

// --- Import declaration ---

enum class ImportKind { Whole, Wildcard, Named };

struct ImportName {
    std::string name;
    std::string alias;  // empty if no alias
};

struct ImportDeclNode : Decl {
    std::vector<std::string> modulePath;  // ["std", "math"]
    ImportKind importKind;
    std::string alias;                     // for "import X as Y" (Whole only)
    std::vector<ImportName> names;         // for Named imports
    bool isReExport = false;               // true for "export import ..."

    ImportDeclNode(SourceRange l, std::vector<std::string> path,
                   ImportKind kind, std::string a, std::vector<ImportName> n)
        : Decl(ImportDecl, l), modulePath(std::move(path)),
          importKind(kind), alias(std::move(a)), names(std::move(n)) {}
};

// Top-level program: a list of declarations and statements
struct Program {
    ASTList items;
    SourceRange loc;
};

} // namespace ts

#endif /* ast_hpp */
