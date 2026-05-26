# Theory of Operation

This document describes the internal architecture of the Tzopilotl compiler and virtual machine. It covers all phases from source text to execution, with particular attention to the type system and code generation.

## Overview

The system is a statically-typed, real-time-safe interpreter designed to run within an audio thread. It compiles source code through a four-phase pipeline — Lex, Parse, Type Check, Code Gen — producing register-based bytecode that executes on a direct-threaded virtual machine. Memory is reclaimed by an incremental tri-color snapshot-at-the-beginning (SATB) tracing garbage collector driven by per-PC stack maps; mark and sweep are interleaved with execution under a per-step deadline budget so audio-thread pauses stay bounded.

The pipeline is orchestrated by the `Compiler` class (`compiler.cpp`):

```
Source Text  →  Lexer  →  Parser  →  TypeChecker  →  CodeGen  →  CodeBlock
                                                                      ↓
                                                                   VM::execute()
```

All runtime memory allocation (objects, types, register file, call stack) is performed through a TLSF (Two-Level Segregated Fit) real-time allocator. The system allocator is never called during execution.

---

## Phase 1: Lexical Analysis

**Files:** `lexer.hpp`, `lexer.cpp`

The `Lexer` is a hand-written scanner that converts source text into a stream of `Token` values. Each token carries:

- `TokenKind` — one of ~80 kinds (literals, keywords, operators, delimiters)
- `SourceRange` — line/column/offset for error reporting
- `text` — the raw source text
- Parsed literal values (`intValue`, `floatValue`, `denominator`) for numeric tokens

Key characteristics:

- **Single-pass, forward-only** with one token of lookahead (`peek()`).
- **Save/restore** state support for tentative parsing (used by the parser for disambiguating `<` as less-than vs. template argument list).
- Lexes **symbol literals** (`'foo`), **imaginary literals** (`4i`, `3.14i`), **fraction literals** (`1/2`, `3/4` — no whitespace around `/`), **triple-quoted strings** (`"""..."""`), and **guillemet strings** (`«...»`).
- Lexes **dynamic variable references** (`` `varName ``) as `DynamicVar` tokens.
- Handles `--` line comments and `/* */` block comments (nestable).
- Splits compound tokens when needed: `>>` can be split into two `>` tokens for closing nested template argument lists, and `>=` can be split into `>` + `=`.

---

## Phase 2: Parsing

**Files:** `parser.hpp`, `parser.cpp`

The `Parser` is a hand-written recursive descent parser that uses **Pratt parsing** (precedence climbing) for expressions. It consumes the token stream from the lexer and produces an AST (`Program` containing a list of `ASTNode`s). The parser reads one token of lookahead via `current_` and occasionally uses `lexer_.peek()` for two-token lookahead to resolve ambiguities.

### Grammar

The following is the complete grammar of Tzopilotl as implemented by the parser. Notation: `|` for alternatives, `*` for zero-or-more, `+` for one-or-more, `?` for optional, and `'...'` for literal tokens.

#### Top Level

```
Program         = Declaration*

Declaration     = 'private'? ( ImportDecl | FnDecl | LetDecl | VarDecl
                              | ConstDecl | StructDecl | EnumDecl
                              | TypeAliasDecl | ConstraintDecl )
                | Statement
```

#### Declarations

```
ImportDecl      = 'import' DottedPath ImportTail ';'
DottedPath      = IDENT ( '.' IDENT )*
ImportTail      = '.' '*'                                   -- wildcard import
                | '.' '{' ImportName ( ',' ImportName )* '}' -- named imports
                | ( 'as' IDENT )?                            -- whole module, optional alias
ImportName      = IDENT ( 'as' IDENT )?

LetDecl         = 'let' ( Pattern '=' Expr ';'              -- destructuring
                        | IDENT TypeExpr? '=' Expr ';' )     -- simple binding
VarDecl         = 'var' ( Pattern '=' Expr ';'
                        | IDENT TypeExpr? '=' Expr ';' )
ConstDecl       = 'const' ( Pattern '=' Expr ';'
                          | IDENT TypeExpr? '=' Expr ';' )

FnDecl          = 'fn' FnName TypeParams? '(' ParamList? ')' TypeExpr?
                  WhereClause? ( '=' Expr ';' | Block )
CoroDecl        = 'coro' FnName TypeParams? '(' ParamList? ')' TypeExpr?
                  WhereClause? ( '=' Expr ';' | Block )
FnName          = IDENT | OPERATOR
WhereClause     = 'where' Constraint ( ',' Constraint )*
Constraint      = IDENT ':' IDENT                -- T: Numeric
                | IDENT ':' IDENT '<' TypeExpr '>' -- T: Comparable<U>
TypeParams      = '<' IDENT ( ',' IDENT )* '>'
ParamList       = Param ( ',' Param )*
Param           = '...' IDENT TypeExpr?             -- variadic (must be last)
                | IDENT TypeExpr? ( '=' Expr )?     -- regular with optional default

StructDecl      = 'struct' IDENT TypeParams? StructBody
StructBody      = '(' TypeExpr ( ',' TypeExpr )* ')' ';'  -- tuple struct
                | '{' StructField* '}'                      -- record struct
StructField     = IDENT TypeExpr ( ',' | ';' )?

EnumDecl        = 'enum' IDENT TypeParams? '{' EnumCase* '}'
EnumCase        = IDENT TypeExpr? ( ',' | ';' )?

TypeAliasDecl   = 'type' IDENT TypeParams? '=' TypeExpr ';'

ConstraintDecl  = 'constraint' IDENT TypeParams? '{' ConstraintBody '}'
                | 'constraint' IDENT TypeParams? '=' ConstraintUnion ';'
ConstraintBody  = ( 'requires' 'fn' FnName '(' TypeExprList? ')' TypeExpr? ';' )*
ConstraintUnion = IDENT ( '|' IDENT )*              -- union of types or constraints
```

**Notes on function declarations:**
- `FnName` can be an operator token (`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `~`, `&`, `|`, `^`, `<<`, `>>`, `$`, `<-`) to define operator overloads.
- If a parameter has no type annotation and no default value, the parser automatically generates a synthetic type parameter (A, B, C, ...) and a `NamedType` for that parameter, making the function a template. This means `fn foo(a, b)` desugars to `fn foo<A, B>(a A, b B)`.
- Parameters with default values must have explicit type annotations.
- Once a default is seen, all subsequent non-variadic parameters must also have defaults.

#### Statements

```
Statement       = Block
                | IfStmt
                | WhileStmt
                | ForStmt
                | SwitchStmt
                | MatchStmt
                | ReturnStmt
                | BreakStmt
                | ContinueStmt
                | ExprStmtOrAssign

Block           = '{' Declaration* '}'

IfStmt          = 'if' '(' Expr ')' Block ( 'else' ( IfStmt | Block ) )?

WhileStmt       = 'while' '(' Expr ')' Block

ForStmt         = 'for' '(' IDENT ':' Expr ')' Block

SwitchStmt      = 'switch' '(' Expr ')' '{' ( 'case' CaseArm )* '}'
MatchStmt       = 'match'  '(' Expr ')' '{' CaseArm* '}'
CaseArm         = Pattern Guard? ':' ( Block | Statement )
Guard           = 'if' '(' Expr ')'

ReturnStmt      = 'return' Expr? ';'

BreakStmt       = 'break' ';'
ContinueStmt    = 'continue' ';'

ExprStmtOrAssign= Expr ( '=' Expr )? TermOpt
TermOpt         = ';'           -- statement with explicit terminator
                | /* before } or EOF: marks as trailing expression */
```

**Trailing expressions:** An expression statement without a trailing semicolon, followed by `}` or EOF, is flagged `isTrailing = true`. The type checker and code generator treat trailing expressions as the implicit return value of the enclosing block. This is how `fn square(x Int) Int { x * x }` works — `x * x` is the block's value because there is no semicolon.

#### Expressions

Expressions are parsed using **Pratt parsing** (precedence climbing). The main loop is in `parseExpression(minPrec)`.

```
Expr            = Unary ( BinOp Expr )*         -- Pratt precedence climbing
                | Unary '?' Expr ':' Expr        -- ternary conditional
                | Unary '|>' PipeTarget          -- pipe operator

Unary           = ( '-' | '!' | '~' | '&' | '*' ) Primary TightPostfix*
                | Primary Postfix*

Primary         = INT_LIT | FLOAT_LIT | IMAGINARY_LIT | FRACTION_LIT
                | STRING_LIT | SYMBOL_LIT | 'true' | 'false' | 'nil'
                | DYNAMIC_VAR                      -- `varName (dynamic scope reference)
                | IDENT StructLiteralBody?       -- identifier or struct literal
                | IDENT TypeArgs StructLiteralBody  -- template struct literal
                | IDENT TypeArgs '.' IDENT CallArgs?  -- template enum constructor
                | 'List' '(' ExprList? ')'       -- list literal
                | 'Set' '(' ExprList? ')'        -- set literal
                | 'Fraction' CallArgs             -- fraction constructor
                | 'Complex' CallArgs              -- complex constructor
                | '(' ')'                         -- unit tuple
                | '(' Expr ')'                    -- parenthesized expression
                | '(' Expr ',' ')'                -- 1-tuple
                | '(' Expr ',' Expr ( ',' Expr )* ','? ')'   -- n-tuple
                | '(' Expr '..' Expr? ')'         -- range: (start..end) or (start..)
                | '(' Expr ',' Expr '..' Expr? ')' -- stepped range: (start,next..end)
                | '[' ']'                          -- empty array
                | '[' ':' ']'                      -- empty map
                | '[' Expr ':' Expr ( ',' Expr ':' Expr )* ']'  -- map literal
                | '[' Expr ( ',' Expr )* ']'       -- array literal
                | '#' '[' ']'                      -- empty persistent vector
                | '#' '[' ':' ']'                  -- empty persistent map
                | '#' '[' Expr ':' Expr ( ',' Expr ':' Expr )* ']'  -- persistent map literal
                | '#' '[' Expr ( ',' Expr )* ']'   -- persistent vector literal
                | 'fn' '(' LambdaParams? ')' TypeExpr? WhereClause? ( '=' Expr | Block )  -- lambda
                | 'coro' '(' LambdaParams? ')' TypeExpr? ( '=' Expr | Block )  -- coroutine lambda
                | 'if' '(' Expr ')' Block ( 'else' ( IfExpr | Block ) )?     -- if expression
                | Expr 'as' TypeExpr               -- type cast/assertion

StructLiteralBody = '{' NamedFields '}'          -- Point { x: 1, y: 2 }
                  | '{' PositionalFields '}'      -- Point { 1, 2 }
NamedFields     = IDENT ':' Expr ( ',' IDENT ':' Expr )*
PositionalFields= Expr ( ',' Expr )*

PipeTarget      = '@' Postfix*                    -- pipe into auto-mapped chain
                | Primary Postfix*                 -- pipe into function/call

ExprList        = Expr ( ',' Expr )*
CallArgs        = '(' ExprList? ')'
LambdaParams    = LambdaParam ( ',' LambdaParam )*
LambdaParam     = IDENT TypeExpr?                  -- type optional (inferred)
```

#### Postfix Operations

There are two postfix parsing functions, `parseTightPostfix` and `parsePostfix`. The tight version is used as the operand of unary operators (so that `-f(x)` parses as `-(f(x))`); the full version additionally handles **space-pipeline** syntax.

```
TightPostfix    = '(' ExprList? ')'               -- function call
                | '[' Expr ']'                     -- index access
                | '.' ( IDENT | INT_LIT )          -- field/tuple-index access
                | '@' | '@@' | '@1' | '@2' ...     -- auto-map annotation

Postfix         = TightPostfix
                | IDENT CallArgs?                   -- space-pipeline: x f or x f(y)
```

**Space-pipeline** is the key syntactic sugar of Tzopilotl. When an identifier appears after an expression at postfix level, the parser desugars it to a function call with the expression prepended as the first argument:

- `x f` becomes `f(x)` (single-arg pipeline)
- `x f(y, z)` becomes `f(x, y, z)` (multi-arg pipeline with prepended receiver)
- `x f g` becomes `g(f(x))` (chained pipeline)
- `x f(y) g(z)` becomes `g(f(x, y), z)` (chained multi-arg pipeline)

Keywords (`let`, `var`, `fn`, `if`, `else`, `while`, `for`, `break`, `continue`, `return`, `switch`, `match`, `const`, `coro`, `yield`, `constraint`, `requires`, `where`) cannot be consumed by space-pipeline because they are lexed as distinct token kinds, not as `Identifier`. The postfix loop only enters space-pipeline handling when `current_` is `TokenKind::Identifier`.

**Chained tuple field access** (`expr.1.0`) requires special handling: the lexer produces `.` + `FloatLiteral("1.0")`, which the parser splits into two consecutive field accesses `.1` then `.0`.

#### Operator Precedence

Binary operators are handled by Pratt parsing with the following precedence table (low to high):

| Prec | Operators | Associativity | Description |
|------|-----------|---------------|-------------|
| 0 | `<-` | Right | Ref assignment |
| 1 | `|>` | Left (special) | Pipe operator (desugared to function call) |
| 2 | `||` | Left | Logical OR |
| 3 | `&&` | Left | Logical AND |
| 4 | `::` | Right | List cons |
| 5 | `|` | Left | Bitwise OR |
| 6 | `^` | Left | Bitwise XOR |
| 7 | `&` | Left | Bitwise AND |
| 8 | `==` `!=` | Left | Equality |
| 9 | `<` `<=` `>` `>=` | Left | Comparison |
| 10 | `<<` `>>` `>>>` | Left | Bit shifts |
| 11 | `+` `-` `$` | Left | Additive / Concatenation |
| 12 | `*` `/` `%` `//` | Left | Multiplicative / Floor division |
| — | (postfix) | Left | Call, index, field, space-pipeline, `@` |
| — | (prefix) | Right | `-` `!` `~` `&` `*` |

The ternary conditional `?:` has effective precedence 0 (it binds looser than all binary operators). The pipe operator `|>` is handled specially: the right side is parsed as a `Primary + Postfix*` chain (not a full expression), and then desugared to a function call. If `|>` is followed by `@`, the pipe input is wrapped in an `AutoMapExpr` and the postfix chain continues from there.

`::` and `<-` are **right-associative**: the parser uses the same precedence (rather than `prec + 1`) when parsing their right operand.

#### Type Expressions

```
TypeExpr        = '[' TypeExpr ']'                         -- [T] array
                | '[' TypeExpr ':' TypeExpr ']'            -- Map[K, V]
                | '#' '[' TypeExpr ']'                     -- #[T] persistent vector
                | '#' '[' TypeExpr ':' TypeExpr ']'        -- #[K:V] persistent map
                | '(' ')'                                   -- unit type
                | '(' ')' TypeExpr                          -- zero-arg function type
                | '(' TypeExpr ')'                          -- parenthesized type
                | '(' TypeExpr ')' TypeExpr                 -- single-arg function type
                | '(' TypeExpr ( ',' TypeExpr )+ ')'        -- tuple type
                | '(' TypeExpr ( ',' TypeExpr )+ ')' TypeExpr  -- multi-arg function type
                | 'List' '<' TypeExpr '>'                   -- List[T]
                | 'Ref' '<' TypeExpr '>'                    -- Ref[T]
                | 'Set' '<' TypeExpr '>'                    -- Set[T]
                | IDENT '<' TypeExpr ( ',' TypeExpr )* '>'  -- template type
                | NamedType

NamedType       = 'Int' | 'Float' | 'String' | 'Bool' | 'Symbol' | 'Void'
                | 'Fraction' | 'Complex' | 'Any' | IDENT
```

**Ambiguity between tuple types and function types:** After parsing `(Type, ...)`, the parser checks whether a return type follows. If the next token can start a type expression (a type keyword, identifier, `[`, or `(`), the group is parsed as a function type `(ArgTypes) ReturnType`. Otherwise it is a tuple type. This means `(Int, Int)` alone is a tuple type, but `(Int, Int) Int` is a function type `(Int, Int) -> Int`. Similarly, `(Int)` alone is a parenthesized type (just `Int`), but `(Int) Int` is a single-argument function type.

**Special-cased template types:** `List<T>`, `Ref<T>`, `Set<T>`, and `Coroutine<T>` are parsed into dedicated AST nodes (`ListTypeNode`, `RefTypeNode`, `SetTypeNode`, `CoroutineTypeNode`) rather than the generic `TemplateTypeNode`. This ensures backward compatibility with the type checker.

**Persistent (immutable) collection types and literals:** A leading `#` marks the immutable form of a bracket type or literal. `#[T]` is a persistent vector and `#[K: V]` a persistent map, parsed by the same routines as their mutable `[...]` counterparts with an `isImmutable` flag set on the resulting `ArrayTypeNode` / `MapTypeNode` (and on `ArrayLiteralExpr` / `MapLiteralExpr` for the literal forms). The flag is what later distinguishes `PersistentVectorType` from `ArrayType` and `PersistentMapType` from `MapType`; the surface syntax is otherwise identical. In type position the disambiguation is unambiguous (`#[K: V]` always parses as a map when a `:` follows the first type); in expression position `#[...]` is always a literal — the typed-constructor form `[Type](...)` applies only to the mutable bracket.

#### Patterns

Patterns are used in `match`/`switch` arms and in destructuring `let`/`var`/`const` declarations.

```
Pattern         = PrimaryPattern ( '::' Pattern )?    -- cons (right-assoc)

PrimaryPattern  = 'nil'                                -- nil literal
                | 'true' | 'false'                     -- boolean literals
                | '-'? INT_LIT                          -- integer literal (possibly negative)
                | '-'? FLOAT_LIT                        -- float literal (possibly negative)
                | STRING_LIT                            -- string literal
                | SYMBOL_LIT                            -- symbol literal
                | '_'                                    -- wildcard
                | '[' PatternList? RestPattern? ']'     -- array pattern
                | '(' PatternList? RestPattern? ')'     -- tuple pattern
                | IDENT '.' IDENT EnumPayload?          -- enum pattern: Name.case(pat)
                | IDENT '(' PatternList? RestPattern? ')'  -- tuple struct: Name(pats)
                | IDENT '{' StructPatFields '}'         -- struct: Name { f: pat, ... }
                | IDENT                                  -- binding pattern

PatternList     = Pattern ( ',' Pattern )*
RestPattern     = '...' IDENT?                          -- rest capture
EnumPayload     = '(' Pattern? ')'
StructPatFields = StructPatField ( ',' StructPatField )*
StructPatField  = IDENT ':' Pattern
```

The cons pattern `head :: tail` is right-associative and is used for list decomposition. Multiple levels chain naturally: `a :: b :: rest` parses as `a :: (b :: rest)`.

**Guarded patterns** in `match`/`switch` are parsed after the primary pattern: `Pattern 'if' '(' Expr ')'`. They are represented as a `GuardedPattern` wrapping the inner pattern and a guard expression.

### AST Structure

The AST node hierarchy (`ast.hpp`) is organized into five categories:

1. **Declarations** (`Decl`): `LetDecl`, `VarDecl`, `ConstDecl`, `FnDecl`, `StructDecl`, `UnionDecl`, `ImportDecl`, `TypeAliasDecl`, `ConstraintDecl`
2. **Statements** (`Stmt`): `Block`, `ExprStmt`, `IfStmt`, `WhileStmt`, `ForStmt`, `SwitchStmt`, `ReturnStmt`, `AssignStmt`, `BreakStmt`, `ContinueStmt`
3. **Expressions** (`Expr`): Literals, `Identifier`, `DynamicVarExpr`, `BinaryOp`, `UnaryOp`, `CallExpr`, `IndexExpr`, `FieldExpr`, `TupleLiteral`, `ArrayLiteral`, `ListLiteral`, `MapLiteral`, `SetLiteral`, `StructLiteral`, `EnumConstructor`, `LambdaExpr`, `IfExpr`, `BlockExpr`, `AutoMap`, `RangeExpr`, `AsTypeExpr`
4. **Type expressions** (`TypeExpr`): `NamedType`, `ArrayType`, `ListType`, `MapType`, `SetType`, `TupleType`, `FunctionType`, `RefType`, `TemplateType`
5. **Patterns** (`Pattern`): `LiteralPat`, `WildcardPat`, `BindingPat`, `EnumPat`, `StructPat`, `TuplePat`, `ArrayPat`, `ConsPat`, `GuardedPat`

Every `ASTNode` carries a `SourceRange` for error messages and a `resolvedType` pointer that the type checker fills in during Phase 3.

### Key Parsing Strategies

#### Tentative Parsing for Template Arguments

Template argument lists (`Name<Type, Type>`) are syntactically ambiguous with comparison expressions (`a < b, c > d`). The parser resolves this with `tryParseTypeArgs()`:

1. Save the parser and lexer state (token positions, error count).
2. Attempt to consume `<`, then parse comma-separated type expressions, then match `>`.
3. On success: accept the parse, return the type arguments.
4. On failure (at any point): restore the saved state completely (including rolling back any errors generated during the attempt) and return `false`.

The `>` match uses `matchGreater()`, which can split compound tokens: `>>` is split into `>` + `>` (for nested templates like `Box<List<Int>>`), and `>=` is split into `>` + `=`.

#### Struct Literal Disambiguation

An identifier followed by `{` is always parsed as a struct literal (since `IDENT {` has no other valid meaning in expression context). The parser disambiguates between named and positional field syntax by peeking: if the first token inside the braces is an identifier followed by `:`, it's named syntax (`Point { x: 1, y: 2 }`); otherwise it's positional (`Point { 1, 2 }`).

#### Range Expression Parsing

Range expressions are parsed inside parentheses, which are also used for tuples and grouping. The parser disambiguates by looking for `..`:

- `(expr)` — parenthesized expression
- `(expr,)` — 1-element tuple
- `(expr, expr, ...)` — n-element tuple
- `(expr..expr)` — range with start and end
- `(expr..)` — infinite range
- `(expr, expr..expr)` — stepped range (start, next .. end)
- `(expr, expr..)` — stepped infinite range

The parsing logic: after parsing the first expression inside `(`, check for `..` (range), `,` (tuple or stepped range), or `)` (grouping). If `,` is found, parse the second expression, then check again for `..` (stepped range) or `,`/`)` (tuple continuation).

#### Assignment Parsing

Assignment is not an operator in the expression grammar. Instead, `parseExprStmtOrAssign()` first parses a full expression, then checks if the next token is `=`. If so, and the expression is an `Identifier`, it creates an `AssignStmt`. This keeps assignment as a statement-level construct, not something that can appear inside expressions.

#### Auto-Map Annotation

The `@` token is lexed as a single token. `@@` is lexed as a two-character `@` token, `@@@` as three-character, etc. `@1` through `@9` are lexed with the digit attached. The parser converts these into `AutoMapExpr` nodes:

- `@` → depth=1, cartesianIndex=0 (zip)
- `@@` → depth=2, cartesianIndex=0 (two levels deep)
- `@1` → depth=1, cartesianIndex=1 (Cartesian, first dimension)
- `@2` → depth=1, cartesianIndex=2 (Cartesian, second dimension)

The lexer accepts `@1` through `@9` (a single `@` followed by one non-zero digit not adjacent to further alphanumerics), so up to nine Cartesian dimensions can be named. There is no longer a fixed two-dimension cap on the codegen side (see Auto-Mapping Analysis).

`@` appears in both tight postfix and full postfix positions, meaning it can be used after any expression: `arr @ reverse`, `[1,2,3] @ + 10`, `arr @1`.

---

## Phase 3: Type Checking

**Files:** `type_checker.hpp`, `type_checker.cpp`, `type_checker_calls.cpp`, `type_checker_constraints.cpp`, `type_checker_decls.cpp`, `type_checker_exprs.cpp`, `type_checker_infer.cpp`, `type_checker_overload.cpp`, `type_checker_stmts.cpp`, `type_checker_types.cpp`, `type_system.hpp`, `type_system.cpp`, `type_universe.hpp`, `type_universe.cpp`

The `TypeChecker` performs source-to-sink type inference, overload resolution, template instantiation, and auto-map analysis. It annotates every AST node with its resolved `Type*` and sets metadata needed by code generation (global indices, auto-map flags, resolved function references).

### Type Representation

Types are runtime objects inheriting from `Type : Obj : GCObj`. They live in the TLSF heap because types created during execution (e.g., through template instantiation) must be managed alongside other objects. Types created at compile time are marked immortal and are invisible to the tracing GC for the rest of the program's life.

The type hierarchy:

```
Type (abstract base)
├── AliasedType            { name_, aliasedType_ }
├── AtomType (stored by value in a 64-bit Word)
│   ├── BoolType
│   ├── IntType
│   ├── FloatType
│   ├── SymbolType
│   └── VoidType
└── ObjType (stored by pointer to heap object)
    ├── StringType
    ├── FractionType
    ├── ComplexType
    ├── ArrayType          { elemType_ }
    ├── ListType           { elemType_ }
    ├── RangeType          { elemType_ }
    ├── RefType            { elemType_ }
    ├── MapType            { keyType_, valueType_ }
    ├── SetType            { elemType_ }
    ├── PersistentVectorType { elemType_ }        -- displays as #[T]
    ├── PersistentMapType  { keyType_, valueType_ } -- displays as #[K:V]
    ├── TupleType          { fields_: Vec<Type*> }
    ├── StructType         { name_, fields_: Vec<NameTypePair> }
    ├── EnumType           { name_, cases_: Vec<NameTypePair> }
    ├── FunctionType       { argTypes_, returnType_ }
    │   ├── LambdaType     { freeVarTypes_ }
    │   └── MethodType     { receiverType_ }
    ├── TemplateLambdaType { typeParams_ }
    ├── CoroutineType      { yieldType_ }
    └── AnyType
```

All `AtomType` values fit in a single 64-bit `Word` and are classified as non-object types (`isObjType() == false`). All `ObjType` values are accessed through `Obj*` pointers and are GC-managed. This distinction is fundamental: it determines whether a value occupies an `i64` slot or an `Obj*` slot in registers, arrays, struct fields, and lambda captures; whether a global slot is added to the precise root set; and whether stores into the slot need an SATB write barrier.

`PersistentVectorType` (`#[T]`) and `PersistentMapType` (`#[K:V]`) are the immutable counterparts of `ArrayType` (`[T]`) and `MapType` (`[K:V]`). Both are `ObjType`s (heap, `Repr::Pointer`). They are deliberately **distinct types with no implicit conversion** to or from their mutable counterparts — exactly the relationship `List<T>` has to `[T]`. A `#[T]` is never accepted where a `[T]` is expected (and vice versa); moving between the representations requires an explicit conversion. This keeps the mutability discipline visible in the type and avoids accidental aliasing of a shared persistent structure as if it were a mutable array.

### Type Interning

Composite types are interned (deduplicated) by the VM through caches:

- `arrayTypeCache_`: `Type* → ArrayType*`
- `listTypeCache_`: `Type* → ListType*`
- `tupleTypeCache_`: `Vec<Type*> → TupleType*`
- `functionTypeCache_`: `Vec<Type*> → FunctionType*` (key includes return type)
- `mapTypeCache_`: `(Type*, Type*) → MapType*`
- And similar for `RangeType`, `RefType`, `SetType`, `PersistentVectorType`, `PersistentMapType`, `CoroutineType`, `EnumType` (for `Option<T>`)

This ensures pointer identity is sufficient for type equality of structural types. Named types (`StructType`, `EnumType`) are unique because they're created once per declaration.

### Numeric Tower

The type system defines a numeric promotion hierarchy:

```
Bool (rank 0) → Int (rank 1) → Fraction (rank 2) → Float (rank 3) → Complex (rank 4)
```

The function `isAssignable(from, to)` permits implicit promotion up this chain: an `Int` argument can be passed where a `Float` parameter is expected. The function `commonNumericType(a, b)` finds the least upper bound: `Int + Float → Float`. Division has a special case: `Int / Int → Fraction` (exact arithmetic).

The numeric tower extends to containers: `[Int] + [Float] → [Float]`, and `List<Int> + Float → List<Float>` (broadcast).

### Type Inference Direction

Types flow **source to sink** (forward only, not bidirectional). The type of an expression is determined by its operands, never by how its result is used. For example:

- A literal `42` has type `Int`.
- `42 + 3.14` resolves to `Float` via numeric promotion.
- `let x = 42 + 3.14` infers `x` as `Float` from the initializer.

The one exception is lambda parameters: when a lambda is passed as an argument to a function with a known parameter type, the expected parameter types can flow backward into untyped lambda parameters.

### Function Return Type Inference

When a function is declared without an explicit return type, the type checker infers it from the function body using `inferFunctionReturnType()`:

1. Enter "inference mode" and check the body.
2. Collect types from explicit `return` statements (`inferredReturnType_`).
3. Extract the trailing expression type from the block (`getBlockTrailingType()`).
4. Reconcile: if both exist, they must agree (or be unifiable via the numeric tower).
5. If neither exists, the function returns `Void`.

Cycle detection prevents infinite recursion on mutually-recursive functions: `fi->inferring` is set before checking the body and cleared after.

### Multi-Pass Type Checking

The `check()` method processes a program in multiple ordered passes:

1. **Pass 0 — Imports:** Process `import` declarations to load module types and functions.
2. **Pass 1a — Struct Registration:** Register all struct types (resolve field types, create `StructType` objects). Template structs are registered without resolving fields.
3. **Pass 1b — Enum Registration:** Register all enum/union types similarly.
4. **Pass 1c — Type Aliases:** Register concrete aliases immediately; store generic aliases for on-demand resolution.
5. **Pass 1d — Constraints:** Register constraint declarations (type-set constraints, interface constraints with required function signatures).
6. **Pass 2 — Function Registration:** Register all function declarations. For each non-template function, resolve parameter types and return type (if annotated), allocate a global slot for the `CodeBlock`, and create a `FuncInfo` entry. Template functions are registered with `isTemplate = true` but no resolved types. Functions with `where` clauses have their constraints recorded for checking during template instantiation.
7. **Pass 3 — Body Checking:** Check all function bodies (demand-driven: bodies are checked when first needed for return type inference).
8. **Pass 4 — Top-level Statements:** Check all non-declaration top-level items in order.

### Overload Resolution

Functions may be overloaded. The `resolveOverload()` algorithm:

1. **Filter by arity** (exact match on argument count), skipping template entries.
2. **Exact match:** all argument types match parameter types by pointer identity.
3. **Promotion match:** all arguments are `isAssignable` to the corresponding parameter. If exactly one candidate matches, it's selected. If multiple match, the call is ambiguous.
4. **Variadic match:** for functions with `...` parameters, check that `argc >= fixedParamCount` and that surplus arguments are assignable to the variadic element type.
5. **Template resolution:** if no concrete match is found, `tryResolveTemplate()` attempts to unify argument types against template parameter patterns to infer type bindings.

### Template Instantiation (Monomorphization)

Template functions, structs, and enums are instantiated on demand. The process for functions:

1. `tryResolveTemplate()` finds template `FuncInfo` entries and calls `inferTypeParams()` to unify the call's argument types against the template's parameter type expressions.
2. `unifyTypeExpr()` recursively matches type expression AST nodes against concrete types, binding type parameters (e.g., matching `[T]` against `[Int]` binds `T = Int`).
3. A `MonoKey` (name + bound type args) is checked against `monoCache_` to avoid duplicate instantiation.
4. `monomorphize()` creates a new `FuncInfo` with concrete parameter/return types and the type parameter bindings. The body is re-type-checked with those bindings via `recheckTemplateBody()`.
5. The monomorphized instance is added to `monoInstances_` for code generation.

Template structs and enums follow a similar pattern via `monomorphizeStruct()` and `monomorphizeEnum()`.

**Built-in templates** use a `BuiltinTemplateResolver` callback instead of an AST body — the resolver inspects argument types and returns the appropriate concrete parameter types, return type, and C function pointer.

### Constraints

**Files:** `type_checker_constraints.cpp`

Constraints restrict which types may be bound to template parameters. Two forms exist:

**Type-set constraints** enumerate allowed types directly or by union:

```
constraint Numeric = Int | Float | Fraction | Complex;
constraint Ordered = Numeric | String;
```

**Interface constraints** require that a type implement specific function signatures:

```
constraint Comparable<T> {
    requires fn <(a T, b T) Bool;
    requires fn ==(a T, b T) Bool;
}
```

Constraints are checked during template instantiation. When a function declares `fn sort<T>(arr [T]) [T] where T: Comparable<T>`, the type checker verifies that the concrete type bound to `T` satisfies the constraint before proceeding with monomorphization.

Constraint names can also appear directly in parameter type position, desugaring into fresh type parameters with implicit where-clauses: `fn abs(x Numeric) Numeric` desugars to `fn abs<T>(x T) T where T: Numeric`.

**Resolution semantics and coherence.** Constraints are *predicate-based*, not
*dictionary-based*: a constraint is a yes/no test used to accept or reject a template during
overload resolution, never a vtable threaded through generic code. This is the C++-concepts /
Go-interface model rather than the Rust-trait / Haskell-typeclass model, so Tzopilotl needs no
orphan rule or global coherence rule. Concretely:

- A constrained generic's body is **re-type-checked at each instantiation** by
  `recheckTemplateBody` (`type_checker_overload.cpp`), which activates `ImportedModuleScopeGuard`
  (`type_checker.cpp`) to merge the *defining* module's functions on top of the importer's scope.
  Overloaded calls inside the body (e.g. `+` in `fn double<T: Addable>(x T) T = x + x`) therefore
  resolve against *definition-module ∪ importer* scope.
- If that merged scope makes two conflicting same-signature overloads visible, `resolveOverload`
  reports an **"Ambiguous overload"** error. The conflict is *detected*, never silently resolved
  one way or the other.
- Monomorphizations are cached per `TypeChecker` (i.e. per module) under the key
  `{name, typeArgs, declNode}`, so there is no single shared instance that two modules could
  disagree about. Combined with untagged values (which carry no implementation pointer), the
  Rust-style failure where a value built with one instance is later used with a different,
  incompatible one cannot arise.

Two same-named constraints declared in *different* modules are a hard error when both are imported
into one program — see `mergeImportedConstraint` (`type_checker_decls.cpp`). Diamond re-imports of
a single constraint (which share one `declNode`) are allowed.

### Auto-Mapping Analysis

Auto-mapping is analyzed during type checking and annotated on AST nodes for code generation. Two forms exist:

**Implicit auto-mapping:** When a function expects a scalar but receives an `Array`, `List`, or persistent vector `#[...]`, the type checker automatically wraps the call in a mapping operation. Binary operators also auto-map: `[1,2,3] + 1 → [2,3,4]`.

**Explicit auto-mapping (`@`):** The `@` operator (parsed as `AutoMapExpr`) is extracted by `extractAutoMapAnnotation()` and stored as `AutoMapArg` on the relevant AST node. The type checker then:

1. Calls `unwrapAutoMapLayers()` to peel off `depth` layers of `Array`/`List` from the tagged argument's type, exposing the element type.
2. Performs normal type checking on the scalar types.
3. Calls `wrapAutoMapResult()` to re-wrap the scalar result type in the appropriate container layers.

`AutoMapArg` carries four fields:
- `depth`: number of nesting levels (1 for `@`, 2 for `@@`, etc.)
- `cartesianIndex`: 0 for zip-style mapping, 1+ for Cartesian product (`@1`, `@2`, …)
- `isList`: whether the source container is a `List` (result will be lazy `List` instead of `Array`)
- `isPVec`: whether the source container is a persistent vector `#[...]` (result will be a persistent vector instead of `Array`)

**Persistent-vector auto-mapping.** Auto-mapping applies to persistent vectors exactly as it does to arrays. A function expecting a scalar maps over a `#[...]`; binary operators broadcast a scalar over a vector and zip two vectors elementwise; and the explicit `@`, deep `@@`, and Cartesian `@n` forms all work over persistent vectors. When the layer-unwrapping loop (`type_checker_calls.cpp`) peels a `PersistentVectorType` it sets `thisPVec`, propagated into `AutoMapArg::isPVec`. When the operand kinds of a mapped expression mix (e.g. an `Array` operand against a `#[...]` operand, or either against a `List`), the **result-kind precedence is List > persistent vector > Array**: the wrap logic checks `anyList` first, then `anyPVec`, otherwise falls back to `Array` (`wrapAutoMapResult` and the per-expression wrap sites in `type_checker_calls.cpp`). So mixing a list anywhere yields a (lazy) list result, mixing a persistent vector without any list yields a persistent vector, and only all-array operands yield an array.

**N-dimensional Cartesian mapping.** Cartesian mapping (`@n`) is now N-dimensional with no small fixed limit — the lexer accepts `@1` through `@9`, naming up to nine independent dimensions. (Previously the Cartesian codegen was hardcoded to a maximum of two dimensions; that cap is removed.) Codegen is a **single recursive, result-type-driven nest**: `genCartesianCall` / `genCartesianBinaryOp` compute `maxCart` (the largest cartesian dimension present, with a plain `@` acting as dimension 1) and then call `emitCartesianNest(level, maxCart, resultType, …)`. Dimension `n` becomes the n-th nesting level of the result. At each level the per-level container kind — `Array` vs. persistent vector — is read directly from the result `Type*` (an `ArrayType` builds an array; a `PersistentVectorType` materializes via a temporary array and then freezes it with `op_pvec_from_array`), and the function recurses on the level's element type until the innermost dimension (`level == maxCart`), where the leaf emits the scalar call or binary op. Because the container kind comes from the result type rather than the source operands, arrays, persistent vectors, and arbitrary mixtures all flow through one code path (mapped persistent-vector arguments are normalized to a temporary array via `op_pvec_to_array` for uniform indexed reads).

### Lambda and Closure Analysis

When the type checker enters a lambda body, it sets `lambdaBoundary_` to the current scope depth. Variable lookups that cross this boundary trigger **capture detection**: the variable is added to the lambda's `captures` list. The type checker builds a `LambdaType` that includes `freeVarTypes_` (types of captured variables) and `gcFreeVars_` (indices of captures that hold `Obj*` pointers, so the tracing GC's `Lambda::gcScanChildren` knows which free-var slots to mark).

### Scope and Variable Management

Variables are tracked in a scope stack (`scopes_`). At global scope (empty stack), variables are stored in the VM's globals table. At local scope, variables map to registers (this mapping is consumed by code generation). The type checker stores `VarInfo` with the variable's type, mutability, and global index (if global).

---

## Phase 4: Code Generation

**Files:** `codegen.hpp`, `codegen.cpp`

The `CodeGen` class walks the type-annotated AST and emits register-based instructions into `CodeBlock` objects. Each function gets its own `CodeBlock`; the top-level program body also gets one.

### Instruction Format

Instructions are encoded as sequences of `Code` words (each 64 bits):

```cpp
union Code {
    Operation op;      // Function pointer to opcode handler
    i64       i;       // Integer immediate
    f64       f;       // Float immediate
    u16       regs[4]; // Packed register indices (dst, src1, src2, src3)
    SymbolPtr s;       // Symbol immediate
    void*     p;       // Non-GC pointer (e.g., Code* jump target)
};
```

A typical instruction consists of 2–5 `Code` words:

| Word | Contents |
|------|----------|
| 0 | `Operation` function pointer (the opcode handler) |
| 1 | Packed register operands: `regs[0]` = destination, `regs[1..3]` = sources |
| 2+ | Immediates: integer constants, float constants, global indices, `Type*` pointers, `CodeBlock*` pointers |

This is a **direct-threaded** encoding: the first word of every instruction is the function pointer to its handler. There is no opcode dispatch table or switch statement. The VM calls the first handler, and each handler tail-calls the next via `[[clang::musttail]]`.

### Register Allocation

The code generator uses a simple **linear scan register allocator**:

- `nextReg_` is a monotonically-advancing counter.
- `allocReg()` returns `nextReg_++`.
- `allocRegs(n)` allocates a contiguous block of `n` registers.
- `freeRegsTo(r)` resets `nextReg_ = r`, reclaiming registers for reuse (when `enableRegReclaim` is true).

Function arguments are placed in contiguous registers starting at the call site's `argBase`. The callee's register window starts at `baseReg + argBase`, so arguments don't need to be copied.

### Code Generation for Declarations

**Let/Var/Const declarations:** Generate the initializer expression, then record the result register as the local variable's location. For global-scope variables, also emit `op_store_global` (or `op_store_global_obj` / `op_init_global_obj` for object-typed values, which run the SATB write barrier before overwriting the slot) to persist the value.

**Function declarations:** Each function compiles to its own `CodeBlock`. The process:

1. Save the current `CodeBlock`, `nextReg_`, `maxReg_`, and scope state.
2. Create a new `CodeBlock`.
3. Parameters occupy registers `0..n-1`.
4. If the function has default arguments, emit code for each default value at separate entry points (`defaultEntryOffsets`).
5. Compile the body.
6. Emit `op_return` or `op_return_void`.
7. Store the `CodeBlock*` in the function's global slot.
8. Restore the saved state and continue emitting into the parent block.

**Monomorphized template instances** are compiled via `genMonoInstance()`, which is essentially the same as `genFnDecl()` but operates on the monomorphized `FuncInfo`.

### Code Generation for Expressions

Each `genExpr(expr)` call returns the register holding the result. Key cases:

**Literals:** Emit `op_load_int_const`, `op_load_float_const`, `op_load_bool_true`, `op_load_bool_false`, or `op_load_obj` (for string and other object constants stored in the `CodeBlock`'s `objConstants` array).

**Binary operations:** Based on the resolved types:
- Numeric scalar: emit type-specialized opcodes (`op_add_int`, `op_add_float`, `op_add_fraction`, `op_add_complex`).
- Composite (array/tuple of numerics): emit `op_add_composite` with runtime type pointers.
- With auto-mapped operands: emit loops or lazy list generators.
- With operator overloads (`resolvedFuncGlobalIndex != -1`): emit a function call.

Numeric promotion instructions (`op_int_to_float`, `op_int_to_fraction`, etc.) are inserted when the operand types don't match the operation type.

**Function calls:** The type checker stores the resolved function's global index on the `CallExpr_` node. Code generation:
1. Evaluate all arguments into contiguous registers.
2. Insert numeric promotion instructions where argument types don't match parameter types.
3. Emit `op_call_primitive` (for built-in functions — no call frame needed) or `op_call` (for user functions — pushes a call frame).

**Lambda expressions:** Compiled similarly to function declarations:
1. Compile the lambda body into a new `CodeBlock`.
2. Emit `op_make_lambda` with the code block and a register range containing the captured free variables.
3. The `Lambda` object is created at runtime with copies of the captured values.

**Auto-map calls (Array):** When a function call involves an auto-mapped Array argument:
1. Emit code to determine the array length.
2. Emit `op_array_alloc` for the result array.
3. Emit a loop: extract element, call function, store result.
4. Support for zip (parallel iteration), Cartesian product (nested loops), and deep mapping (recursive unwrapping).

**Auto-map calls (List):** When the auto-mapped argument is a `List`:
1. Build an `AutoMapCallInfo` descriptor encoding the function, which argument is the list, broadcast values, and type information.
2. Emit `op_make_lazy_automap`, which creates an `AutoMapListGen` — a lazy list generator that calls the function one element at a time.

### Jump Resolution

Jumps are emitted in two passes:

1. During emission, jump targets are stored as **code position indices** (integers).
2. After the entire `CodeBlock` is emitted, `resolveJumps()` converts all indices to `Code*` pointers (base pointer + offset). This is possible because `CodeBlock::code` is a `Vec<Code>` that doesn't relocate after emission.

### Constant Folding

When `enableConstFold` is true, the code generator tracks which registers hold known compile-time constants (`constRegs_` map). For binary operations on two constants, the operation is evaluated at compile time and the result is loaded directly. This extends to built-in function calls where all arguments are constant.

### Tail Call Optimization

When `enableTailCalls` is true, the code generator detects calls in tail position (the last expression in a function body, or the last expression in both branches of an `if` in tail position). Tail calls emit `op_tail_call` or `op_tail_call_lambda` instead of `op_call`/`op_call_lambda`. These opcodes reuse the current call frame instead of pushing a new one, enabling unbounded recursion in constant stack space.

### Pattern Match Code Generation

Pattern matching (used in `switch`/`match` and destructuring `let`) generates a sequence of tests and jumps:

1. For each case, `genPatternMatch()` emits tests against the subject value.
2. **Literal patterns:** compare the subject against a constant; jump to the fail label if unequal.
3. **Binding patterns:** move the subject value into a new local register.
4. **Enum patterns:** emit `op_enum_get_which` to get the case index, compare, then optionally destructure the inner value.
5. **Tuple/struct patterns:** emit field access instructions and recursively match sub-patterns.
6. **Array patterns with rest:** emit length checks, extract fixed elements, optionally slice the remainder.
7. **Cons patterns (`h :: t`):** emit `op_list_is_nil` check, then `op_list_head`/`op_list_tail`.
8. **Guarded patterns:** match the pattern, then evaluate the guard expression; if false, fall through to the next case.

Each pattern accumulates a list of "fail jumps." If any test fails, execution jumps to the next case.

---

## The Virtual Machine

**Files:** `vm.hpp`, `vm.cpp`, `opcodes.hpp`, `opcodes.cpp`

### Architecture

The VM is a **register-based, direct-threaded interpreter**. Key components:

- **Register file:** A flat array of `Word` values, allocated from TLSF. Default capacity 4,096 registers. Each call frame occupies a window within this array.
- **Call frame stack:** An array of `CallFrame` structs (return PC, code block, base register, number of registers, result register, dynamic scope stack mark). Default capacity 512 frames.
- **Global variables:** A `Vec<Word>` indexed by global slot number. Each function's `CodeBlock*` is stored as a global, as are user-declared global variables. A parallel `globalIsObj_` array tracks which globals hold `Obj*` pointers, both to drive the GC's global-root scan and to gate the write barrier on stores.
- **Dynamic scope variables:** A `Vec<Word>` of dynamic variables (accessed via `` `varName `` syntax) with a save stack for automatic restore on function return. The `dynStackMark` in each `CallFrame` records the save stack level at entry.
- **Coroutine state:** Optional `currentCoroutine_` and `currentCoroFrame_` pointers tracking the active coroutine during `yield`/`resume` operations.
- **Program counter:** A `Code*` pointer into the current `CodeBlock`'s instruction stream.
- **Tracing GC:** A `TracingGC` driven by `op_safepoint` polls plus `rtTick` / `nrtTick` from host idle paths. Roots are precise (globals, dyn vars, frames via stack maps, plus host-registered extra-root scanners). See Memory Management below.

### Word Representation

All values are stored in untagged 64-bit `Word` unions:

```cpp
union Word {
    i64 i;         // Integer, Bool
    f64 f;         // Float
    SymbolPtr s;   // Interned symbol pointer
    void* p;       // Generic pointer
    Obj* o;        // GC-managed object pointer
};
```

Because the type system statically knows every value's type, no runtime tag checking is needed. This avoids the overhead of tagged pointers or NaN boxing.

### Direct-Threaded Dispatch

Each opcode handler is a standalone function with the signature:

```cpp
void op_xxx(VM& vm, Code* pc);
```

The handler reads its operands from `pc[1]`, `pc[2]`, etc., performs its operation, and tail-calls the next handler:

```cpp
#define DISPATCH(offset) \
    [[clang::musttail]] return (pc + (offset))->op(vm, pc + (offset))
```

The `[[clang::musttail]]` attribute guarantees the compiler emits an actual tail call (jump, not call), so the native stack never grows during instruction dispatch. This is critical for real-time safety: dispatch has bounded, predictable stack usage.

### Function Call Protocol

**`op_call` (user functions):**
1. Read the callee's `CodeBlock*` from the global at the given index.
2. Push a `CallFrame` saving the return PC, current code block, base register, and result register.
3. Set `baseReg_` to `argBase` (arguments are already in place).
4. Jump to the callee's entry point (or a default-argument entry point if the callee has defaults).

**`op_call_primitive` (built-in functions):**
1. Read the `Primitive*` from the global at the given index.
2. Call the primitive's `cfun_` directly (a plain C function pointer).
3. No call frame is pushed or popped — primitives return inline.

**`op_return`:**
1. Read the return value from the specified register.
2. Pop the call frame, restoring the caller's base register.
3. Store the return value in the caller's result register.
4. Jump to the caller's return PC.

**`op_tail_call`:**
1. Copy arguments to the current frame's register 0..n-1.
2. Replace the current frame's code block with the callee's code block.
3. Jump to the callee's entry point. No frame is pushed.

### Execution Lifecycle

```
VM::execute(CodeBlock* block)
  → push initial frame
  → call first instruction's handler
  → handlers tail-call each other
  → op_halt returns to execute()
  → return reg(0) as the result
```

---

## Runtime Object Model

**Files:** `value.hpp`, `value.cpp`

All heap-allocated values inherit from `Obj : GCObj`. The `Obj` base class holds a `Type*` pointer used by `str()` for display and by the type system for runtime type checks.

### Object Types

| Class | Description | Storage |
|-------|-------------|---------|
| `StringObj` | UTF-8 string | `VMString` (TLSF-allocated) |
| `Fraction` | Rational number | `r64` (numer/denom pair) |
| `Complex` | Complex number | `x64` (std::complex<double>) |
| `PodArray<T>` | Homogeneous array of POD values | `Vec<T>` for `T` ∈ {`i64`, `f64`} |
| `ObjArray` | Array of object pointers | `Vec<Obj*>` |
| `ListNode` | Singly-linked immutable list node | `head_` (Word), `tail_` (ListNode*), `generator_` |
| `Tuple` | Fixed-length heterogeneous tuple | Flexible array member `Word v[]` |
| `Struct` | Named product type instance | Flexible array member `Word v[]` |
| `Enum` | Sum type instance | `which_` (case index), `word_` (payload) |
| `RangeObj` | Range (start..end by step) | `start_`, `end_`, `step_`, `isInfinite_` |
| `MapObj` | Mutable hash map | `unordered_map<Word, Word>` with custom hash/equal |
| `SetObj` | Mutable hash set | `unordered_set<Word>` with custom hash/equal |
| `PVec` / `PVecNode` / `PVecLeaf` | Persistent vector (`#[T]`): AMT handle, interior node, stride-packed leaf | see Persistent Collections |
| `PMap` / `PMapNode` | Persistent map (`#[K:V]`): HAMT handle, bitmap/collision node | see Persistent Collections |
| `RefValue` | Mutable reference | `value_` (Word) |
| `CodeBlock` | Compiled function | `Vec<Code> code`, `Vec<Obj*> objConstants` |
| `Primitive` | Built-in function | `cfun_` (C function pointer) |
| `Lambda` | Closure | `codeBlock_`, flexible array `Word freeVars_[]` |
| `CoroutineObj` | Suspended coroutine | `frame_` (CoroutineFrame*), `done_` flag |
| `CoroutineFrame` | Coroutine execution state | Saved registers, PC, caller frame chain |

`Struct`, `Tuple`, and `Lambda` use **C flexible array members** (`Word v[]` / `Word freeVars_[]`) to store their fields inline within the allocation, avoiding a separate heap allocation for the field array. They are created via `create()` static methods that compute the allocation size and use placement new.

### Lazy Lists

Lists are singly-linked chains of `ListNode`. The last node may hold a `ListGenerator*` instead of concrete values. When `force()` is called on a node with a generator, it invokes `generate()` to compute the head value and create a new tail node (which may itself be lazy).

Generator subclasses implement a wide variety of lazy operations:

- `RangeListGen` — lazy integer ranges
- `BinopListGen` — lazy binary arithmetic on lists
- `MapListGen` — lazy `map(list, fn)`
- `FilterListGen` — lazy `filter(list, fn)`
- `AutoMapListGen` — lazy auto-map of function calls
- `TakeListGen`, `DropListGen`, `StrideListGen`, `CycleListGen`, etc.

This design supports infinite lists with constant memory: only the nodes that have been forced remain in memory.

### Arrays

Arrays use a split representation based on element type:

- `PodArray<i64>` — for `[Int]`, `[Bool]`, `[Symbol]` (values stored inline as 64-bit words)
- `PodArray<f64>` — for `[Float]` (values stored inline as doubles)
- `ObjArray` — for arrays of object-typed elements (values stored as `Obj*` pointers, with an SATB write barrier on element stores)

This avoids boxing overhead for numeric arrays.

### Persistent Collections

**Files:** `persistent_vector.hpp`, `persistent_vector.cpp`, `persistent_map.hpp`, `persistent_map.cpp`

Tzopilotl provides two **immutable, structurally-shared** heap collections that are distinct from the mutable `Array`/`Map` (they stand in the same relation to `Array`/`Map` that `List` does to `Array`): the persistent vector `#[T]` and the persistent map `#[K:V]`. Every operation that would mutate returns a *new* value; the original is never modified in place. Because updates copy only the path from the root to the changed node and share everything else, an update allocates O(log₃₂ n) new nodes rather than copying the whole collection.

A deliberate design choice unifies their element storage with the mutable collections: rather than maintaining separate typed backends, both store their payloads in **uniform stride-packed `Vec<Word>`** storage and reuse the same `strideForType` / `hashWords` / `wordsEqual` / `gcScanPayload` primitives that `Array`, `Map`, and `Set` already use. `strideForType` gives the number of `Word`s per element, so inline composites (`Complex`, `Fraction`, small structs/tuples) live unboxed in the trie leaves exactly as they do in `MapObj`/`SetObj`.

**Persistent vector (`#[T]`) — array-mapped trie with a tail.** `PVec` is the handle (`count_`, `shift_`, `stride_`, `root_`, `tail_`); it implements a 32-way bit-partitioned **array-mapped trie (AMT)** in the Clojure `PersistentVector` style. Interior nodes (`PVecNode`) hold up to 32 child `Obj*` pointers; leaves (`PVecLeaf`) hold up to 32 stride-packed elements in a `Vec<Word>`. A small **tail buffer** (`tail_`, itself a `PVecLeaf`) absorbs appends so that `push` is amortized O(1) in allocations — only when the 32-slot tail fills is it pushed into the trie. The trie gives O(log₃₂ n) indexed reads (`elemAt`) and updates (`assocN`). `tailoff()` is the first index held by the tail; the tree covers `[0, tailoff)`. Both `push` and `assocN` use **pure path-copying** (`PVecNode::copyOf` shallow-copies a node, then the one changed child slot is overwritten in the fresh copy) and return a new `PVec`; no existing node is ever mutated.

**Persistent map (`#[K:V]`) — hash array-mapped trie.** `PMap` is the handle (`count_`, `root_`); `PMapNode` is a **HAMT** node consuming 5 bits of the key hash per level (branching factor 32). A bitmap node carries two popcount-compacted bitmaps over the current 5-bit hash slice: `dataBitmap_` marks the slots holding an inline (key, value) pair (stored stride-packed in `data_`), and `nodeBitmap_` marks the slots holding a child node (`nodes_`). Two distinct keys whose hashes collide all the way down fall back to a flat **collision node** (`kind_ == Collision`) that stores the colliding pairs linearly and compares them with `wordsEqual`. `assoc` and `dissoc` path-copy the affected nodes and share the rest, returning a new `PMap`. `PMapIter` is a stack-based, C++-stack-only iterator (not a GC object) used to walk a map's pairs in traversal order.

---

## Memory Management

### TLSF Allocator

**Files:** `tlsf_allocator.hpp`

All runtime memory (register file, call frames, objects, type caches, STL containers) is allocated from a TLSF pool. TLSF provides O(1) worst-case allocation and deallocation, making it suitable for real-time audio.

The `rt::STLAllocator<T>` adaptor allows standard containers (`std::vector`, `std::unordered_map`, `std::string`) to allocate from the TLSF pool instead of the system allocator.

### Incremental Tracing Garbage Collector

**Files:** `gc.hpp`, `tracing_gc.hpp`, `tracing_gc.cpp`

Memory is managed by an **incremental tri-color SATB tracing collector** with precise roots derived from per-PC stack maps. The collector runs on the same thread as the mutator, interleaved at safepoints; a single mark or sweep step is budgeted in wall-clock nanoseconds so the worst-case pause stays bounded (sub-millisecond on the audio thread).

```cpp
class GCObj {
    GCColor color_;        // White / Gray / Black
    bool    immortal_;
    GCTag   gcTag_;        // tag-dispatched child scan
    GCObj*  allObjsNext_;  // singly-linked all-objects list
};
```

The header is 24 bytes including the vtable pointer. Compile-time constants (types, immortal strings, the global symbol table) are flagged `immortal_ = true` and are never visited or freed. There is no per-object home-allocator pointer: each VM owns exactly one TLSF pool, the thread-local `rt::gCurrentAllocator` names it at every allocation and deallocation site, and sweep (the only deletion path) always runs on the VM thread, so the deleting and allocating thread's allocator pointers always agree.

**Tri-color invariant.** Each non-immortal object is exactly one of White (unmarked, candidate for sweep), Gray (marked, children not yet scanned), or Black (marked, children scanned). The collector preserves the SATB (snapshot-at-the-beginning) invariant: any object that was reachable when the cycle started survives the cycle, even if the mutator overwrites the only reference to it before the marker visits it.

**Phases.**

1. **Idle.** No cycle in progress. A new cycle is requested when `allocsSinceLastCycle` exceeds a proportional threshold (`max(kMinTriggerAllocs, lastBlackCount * kGrowthFactor)`). The trigger keeps mark cost amortized O(1) per allocation regardless of live-set size.

2. **Mark.** The marker drains a worklist of Gray objects in chunks. Roots are scanned incrementally across four substates — globals, dynamic-scope vars, active call frames (via per-PC stack maps), and host-registered extra roots (NRTVM handler tables) — each with a cursor so a single step can pause and resume under the deadline. Very large containers (>64 entries) split their child scan into per-step chunks via a partial-container queue so a 100k-element Map can never overrun the budget.

   **Top-frame root-scan correctness fix.** Precise root scanning of a call frame relies on the frame's program counter to select the correct per-PC stack map. A pre-existing bug was that the VM's `vm.pc_` was not kept synchronized during execution — handlers thread the live `pc` along as a function argument and only write it back to the VM at specific points — so when the GC scanned roots, the *currently-executing top frame* had a stale (or null) `pc_`, no matching stack map was found, and live `Obj*` registers in that frame could be missed and swept while still in use (e.g. a loop accumulator being built across an incrementally-collected cycle). The fix synchronizes `vm.pc_` at every safepoint poll: `op_safepoint` now calls `vm.setPc(pc)` immediately before `vm.safepointPoll()` (`opcodes.cpp`; `setPc` in `vm.hpp`). Since the GC only ever advances at a safepoint poll, publishing the current `pc` there guarantees the top frame's stack map is exact whenever the root scan runs. This was committed independently as `dbad513`.

3. **Sweep.** Walks the all-objects list via a slot-pointer (`GCObj**`) so freed Whites are unlinked inline without a back pointer. Sweep is budgeted the same way as mark.

**SATB write barrier.** The code generator emits a write barrier before any heap store that overwrites an `Obj*` slot. The barrier hot path is one comparison: if `phase_ != Mark`, return. During Mark, if the slot's previous value was White and non-immortal, it is colored Gray and pushed onto the worklist. This guarantees that even if the mutator rewires the heap mid-cycle, the snapshot is preserved. Write-barrier sites: `op_store_global_obj`, `op_store_dynamic_obj`, `op_ref_set`, and the array/struct/tuple/enum field setters (`storesObjPtr(Type*)` in `type_system.hpp` is the single source of truth for which slots need it).

**Tag-dispatched child scan.** `GCObj::gcTag_` lets the marker bypass virtual dispatch for the highest-frequency subclasses (Tuple, Struct, Enum, ObjArray, ListNode, RefValue, MapObj, SetObj, Lambda, the persistent-collection nodes, …). `gcScanByTag` is a switch keyed on the tag that issues a qualified non-virtual call to the right subclass's scanner. Untagged objects (`GCTag::Default`) fall back to the virtual `gcScanChildren`.

**Persistent collections under SATB: pure path-copying needs no construction write barrier.** The persistent vector and persistent map (see Persistent Collections, above) introduce the new tags `PVec`, `PVecNode`, `PVecLeaf`, `PMap`, and `PMapNode`, all handled in `gcScanByTag`. The *node* scans mark inline `Obj*` children (`PVecNode::gcScanChildren` marks its child pointers; `PMapNode::gcScanChildren` marks the child nodes in `nodes_`), while the *leaf/data* scans call `gcScanPayload` per stride-packed slot (`PVecLeaf` over its elements; `PMapNode` over each inline key and value), so unboxed composite elements are traced through the same payload-scanning machinery as `Array`/`Map`/`Set`. Crucially, these collections need **no write barrier at all on construction**. The SATB invariant guarantees that any object reachable when the cycle started survives the cycle, and new trie nodes built mid-cycle are black-allocated. Every child a freshly-built node references is therefore either (a) a pre-existing node, which was reachable at the snapshot and is kept live by SATB, or (b) another node created during the same operation, which is itself black. Because persistence *never mutates an existing node in place* — `assoc`/`assocN`/`push`/`dissoc` only ever write into freshly-allocated copies — the mutator never overwrites a slot that the barrier would have to catch, so the tri-color invariant holds without one. (The SATB barrier is still required for the genuinely mutable slots enumerated below.)

**Driver model.** Three call sites advance the collector:

- **`op_safepoint`** — emitted at backward jumps, function entries, and call sites by codegen. Each safepoint poll runs `step(deadline)` with the configured per-poll budget. This is the path that bounds the worst-case mutator pause.
- **`vm.rtTick(deadline)`** — called from the audio callback to use whatever budget is available between blocks.
- **`vm.nrtTick(deadline)`** / **`vm.gcHeartbeat()`** — called by host idle paths (NRT scheduler, REPL between commands, the NRT VM's background heartbeat thread). Uses a more generous budget than the audio thread.

Per-driver telemetry (`stepCountBySource`, `stepMaxNanosBySource`, …) lets you confirm that worst-case audio-thread pauses stay under budget without that statistic getting diluted by less-time-sensitive callers.

**Object registration.** New objects are registered via `registerNewObj(obj, tag)`:

- **During compilation** (`gCurrentCompiler` is set): the compiler tracks the object and marks it immortal. It becomes a constant in `CodeBlock::objConstants` and is invisible to the collector forever after.
- **During execution** (`gCurrentVM` is set): the object is flagged mortal, given its tag, and prepended onto the VM's singly-linked all-objects list in O(1). It starts White; the next cycle either marks it Black or sweeps it.

**Key real-time properties.**
- No stop-the-world phase; mutator pauses are bounded by the per-step deadline.
- Roots are precise (stack maps, not conservative stack scanning), so the collector never has to scan native stack frames.
- Allocator is TLSF: O(1) allocate, O(1) free.
- Cycles are collected (the old refcount-based scheme leaked them).
- No locks or atomics on the GC path; the VM is single-threaded by construction, and cross-thread interaction goes through explicit message queues outside the heap.

---

## Module System

**Files:** `module_compiler.hpp`, `module_compiler.cpp`

The `ModuleCompiler` handles multi-file compilation:

1. **Resolution:** Module paths (`import std.math`) are resolved to filesystem paths by searching include directories.
2. **Compilation:** Each module is independently lexed, parsed, type-checked, and code-generated through the same pipeline. The result is a `ModuleInfo` with an `initBlock` (module-level code), exports (functions, types, variables), and the retained AST (needed for template re-instantiation).
3. **Caching:** Compiled modules are cached by canonical path. Circular imports are detected via `compiling` flag.
4. **Import forms:**
   - `import path` — whole module import with qualification
   - `import path as alias` — whole module with alias
   - `import path.*` — wildcard import of all exports
   - `import path.{name1, name2 as alias}` — selective import
5. **Initialization:** Module init blocks are called once (guarded by a flag global) before the importing module's code runs.

Exports include functions (with overloads), variables, struct types, enum types, template declarations, type aliases, and constraints. Template declarations are exported as AST nodes, enabling cross-module monomorphization. Constraint declarations are also exported for cross-module constraint checking during template instantiation.

---

## Coroutines

Coroutines are declared with the `coro` keyword and use `yield` to suspend execution and produce values:

```
coro fibonacci() Int {
    var a = 0;
    var b = 1;
    while (true) {
        yield a;
        let t = a + b;
        a = b;
        b = t;
    }
}
```

### Coroutine Lifecycle

1. **Creation:** `op_coro_create` (or `op_coro_create_lambda` for coroutine lambdas) allocates a `CoroutineObj` containing a `CoroutineFrame` with its own register file.
2. **Resumption:** `op_coro_resume` saves the caller's state and switches to the coroutine's frame, continuing from where it last yielded. The coroutine returns an `Option<T>` — `Some(value)` while it produces values.
3. **Yielding:** `op_yield` saves the coroutine's registers and PC into its frame, then returns the yielded value (wrapped in `Option.Some`) to the caller.
4. **Completion:** When a coroutine returns normally (falls off the end or executes `return`), subsequent resumes return `Option.None`, and `op_coro_is_done` returns `true`.

Coroutines can be used as lazy list generators via `CoroutineListGen`, which wraps a coroutine in the `ListGenerator` interface for seamless integration with lazy list operations.

### Coroutine Types

A coroutine declaration `coro foo() Int` has type `() Coroutine<Int>`. The `CoroutineType` carries the yield type. Coroutine lambdas (`coro (params) body`) work identically.

---

## Dynamic Scope Variables

Dynamic scope variables use backtick syntax (`` `varName ``) and are visible across the call stack without explicit parameter passing:

```
var `sampleRate = 48000;

fn nyquist() Float { `sampleRate toFloat / 2.0; }
```

### Implementation

Dynamic variables are stored in a flat `Vec<Word>` (`dynVars_`) on the VM, separate from globals. Each function call saves and restores dynamic bindings via a save stack (`dynStack_`), with the `CallFrame::dynStackMark` recording the stack level at entry. On function return, bindings are restored to their prior values.

The code generator emits `op_load_dynamic` and `op_store_dynamic` (or `op_store_dynamic_obj` / `op_init_dynamic_obj` for object-typed values, which run the SATB write barrier before overwriting the slot) for dynamic variable access.

---

## Built-in Functions

**Files:** `builtins.hpp`, `builtins.cpp`

Built-in functions are registered during type checking via `registerBuiltinFunctions()`. Each built-in has:

- A name and parameter types (for overload resolution).
- A `CFun` function pointer (the runtime implementation).
- An `isBuiltin = true` flag in its `FuncInfo`.
- A global slot holding a `Primitive` object.

Built-in template functions use a `BuiltinTemplateResolver` callback that receives argument types and returns concrete types and a `cfun`.

Categories of built-ins include:

- **Math:** `sin`, `cos`, `sqrt`, `abs`, `pow`, `log`, `exp`, `floor`, `ceil`, `round`, `min`, `max`, `clamp`, `lerp`, etc.
- **Type conversions:** `toInt`, `toFloat`, `toFraction`, `toComplex`, `toString`
- **Collection operations:** `len`, `reverse`, `sort`, `take`, `drop`, `map`, `filter`, `fold`, `scan`, `zip`, `enumerate`, `join`, `cat`, `cons`
- **List operations:** `head`, `tail`, `toList`, `toArray`, `take`, `drop`, `stride`, `stutter`, `cyc`, `ncyc`, `hang`, `iter`, `takeWhile`, `dropWhile`
- **I/O:** `print`, `println`, `readFile`, `writeFile`
- **Other:** `assert`, `error`, `range`, `keys`, `values`, `has`

---

## Optimization Summary

| Optimization | Phase | Description |
|---|---|---|
| Type-specialized opcodes | CodeGen | Arithmetic uses `op_add_int`, `op_add_float`, etc. rather than a generic dispatch |
| Untagged values | All | No runtime tag checks; types are statically known |
| Constant folding | CodeGen | Compile-time evaluation of operations on known constants |
| Tail call optimization | CodeGen | `op_tail_call` reuses the call frame for tail-position calls |
| Register reclamation | CodeGen | Dead registers are reclaimed for reuse within a function |
| Primitive call fast path | VM | `op_call_primitive` calls C functions directly without pushing a call frame |
| Direct threading | VM | No opcode dispatch loop; each handler tail-calls the next |
| Lazy lists | Runtime | Infinite sequences use constant memory; work is deferred until needed |
| Lazy auto-map | Runtime | Auto-mapping over lists creates generators instead of materializing arrays |
| POD arrays | Runtime | `PodArray<i64>` and `PodArray<f64>` avoid boxing for numeric arrays |
| Flexible array members | Runtime | `Struct`, `Tuple`, `Lambda` store fields inline, avoiding extra allocations |
| Structural sharing | Runtime | Persistent vector (AMT+tail) and map (HAMT) updates path-copy O(log₃₂ n) nodes and share the rest; no construction write barrier needed under SATB |
| Type interning | Compiler | Structural types are deduplicated, enabling pointer comparison |
| Incremental tracing GC | Runtime | Mark and sweep are budgeted per safepoint; worst-case pauses stay sub-millisecond |
| Precise roots from stack maps | CodeGen + Runtime | No conservative stack scanning; root set is exact at every safepoint PC |
| Tag-dispatched child scan | Runtime | High-frequency Obj subclasses skip virtual dispatch via a switch on `gcTag` |
| Immortal objects | Compiler | Built-in types and compile-time objects are invisible to the collector |
