# Tzopilotl: Full Implementation Plan

This plan covers the complete implementation of Tzopilotl, a statically-typed, real-time-safe interpreted language designed for audio thread execution. It is organized into phases, where each phase builds on the previous. Within each phase, tasks are ordered by dependency.

---

## Current State Summary

The following components exist and are functional:

- **Lexer** (`lexer.hpp/cpp`): Full tokenization including save/restore state for tentative parsing
- **Parser** (`parser.hpp/cpp`): Recursive descent + Pratt expression parsing with template type parameter support
- **Type System** (`type_system.hpp`): Type hierarchy with GC integration (Bool, Int, Float, Symbol, String, Fraction, Complex, Array, List, Range, Tuple, Struct, Enum, Ref, Function, Lambda, Method, Map, Alias)
- **Type Checker** (`type_checker.hpp/cpp`, ~3100 lines): Source-to-sink inference, scope management, numeric promotion, template monomorphization with caching
- **Code Generator** (`codegen.hpp/cpp`, ~3300 lines): Register-based code emission with auto-mapping, template monomorphization, for-loop optimization
- **VM** (`vm.hpp/cpp`): Register-based direct-threaded interpreter with `[[clang::musttail]]` dispatch
- **Opcodes** (`opcodes.hpp/cpp`): ~124 opcodes for arithmetic, comparisons, control flow, load/store, conversions, data structures, print
- **Builtins** (`builtins.cpp`): 100+ overloaded built-in functions (math, string, range, complex, fraction, bitwise)
- **Memory** (`tlsf_allocator.hpp`, `vm_allocator.hpp`, `stl_allocator.hpp`): TLSF O(1) allocator, GC-integrated allocator, STL adapter
- **GC** (`gc.hpp`): One-pass incremental real-time collector with bounded pause times
- **Values** (`value.hpp/cpp`): Runtime objects (CodeBlock, Fraction, Complex, StringObj, PodArray, ObjArray, ListNode, RangeObj, RefValue, Struct, Tuple, Enum, Primitive, Lambda, Method) plus lazy generators (BinopListGen, UnaryListGen, RangeListGen)
- **Main** (`main.cpp`): 5-phase pipeline (lex -> parse -> type check -> codegen -> execute)

**Working features**: Int/float/fraction/complex arithmetic, type conversions, let/var/const declarations, functions with overloading, if/else, while loops, for-each loops over arrays/lists/ranges, strings with comparisons, array/tuple literals, global variables, print/println, structs (with positional construction and templates), enums (with templates), match/pattern matching (including array/cons/rest patterns), lambda closures, pipeline syntax, implicit auto-mapping, explicit postfix `@` operator, List type with lazy arithmetic, Range type with range expressions, Ref type with `&`/`*`/`<-` operators, template functions/structs/enums with monomorphization.

**Not yet working**: Methods, struct inheritance.

---

## Completion Status

| Section | Status | Key Divergences |
|---------|--------|-----------------|
| 1.1 Structs | Done | Added positional construction (`Point{3.0, 4.0}`). Inheritance deferred to Phase 10. |
| 1.2 Enums | Done | Parser keyword is `enum`; internal naming uses both "Union" (AST) and "Enum" (type system, runtime). |
| 1.3 Pattern Matching | Done | Syntax: `match (expr) { pattern: body ... }`. Added `ArrayPattern`, `ConsPattern` (list `h :: t`), and rest patterns (`...rest`) beyond original spec. |
| 1.5 Lambda Closures | Done | No significant divergences. |
| 1.6 Pipeline Syntax | Done | Both `x \|> f` and implicit `x f` syntax work. |
| 2.2 Function Overloading | Done | No significant divergences. |
| 3.1 Implicit Auto-Mapping | Done | List auto-mapping uses lazy generators rather than eager loops. |
| 3.2 Explicit `@` Operator | Done | Changed to postfix syntax (`array @` not `@ array`). Supports `@@`, `@1`, `@2`. |
| 4.1 List Type | Done | Uses `List(1,2,3)` constructor, `::` cons operator, `nil`. Lazy generators for arithmetic. |
| 5.1 Template Functions | Done | `fn name<T>(...)` syntax. Monomorphization with caching. Type inference from arguments. |
| 5.2 Template Structs/Enums | Done | `struct Name<T> { ... }`, `enum Name<T> { ... }`. Inferred or explicit type args. |
| 6.1 For Loops | Done | `for x in iterable { ... }` over Arrays, Lists, and Ranges. Inline counter optimization for Int ranges. |
| 6.2 Range Expressions | Done | `(start..end)`, `(start,next..end)` for stepped, `(start..)` for infinite. RangeObj + builtins. |
| 8.1 Ref Type | Done | `&expr` creates ref, `*expr` dereferences, `refExpr <- value` assigns. `Ref<T>` type syntax. |
| 13.1 Math Functions | Done | 100+ overloads: trig, exp/log, rounding, abs, min/max/clamp/cmp, sign, bitwise, complex. |
| 13.2 String Functions | Done | `length`, `cmp`, `min`, `max`, comparison operators, `substring`, `contains`, `startsWith`, `endsWith`, `split`, `trim`, `toUpper`, `toLower`, `replace`, byte indexing, `codePoints`. |

---

## Remaining Phases

| Phase | Section | Description | Status |
|-------|---------|-------------|--------|
| **7** | **7.1** | **Infinite Lists & Generators** | Done. 20+ lazy generator types, `ord`, lazy `to()`, `take`, `drop`, `map`, `filter`, `fold`, `scan`, `zip`, `enumerate`, `iter`, `cyc`, `ncyc`, `hang`, `join`, random generators, coroutine-to-list, array-to-list, `codePoints`. |
| **9** | **9.1** | **Module System** | Done. All import syntaxes, qualified access, circular detection, module caching, cascading error suppression. 13 module tests. |
| **10** | **10.1--10.3** | **Methods & OO** | Not started. Method declarations, struct inheritance, where clauses. MethodType and Method value types exist but no syntax/dispatch. |
| 11 | 11.1 | Dynamic Scoping | Done. Backtick-prefixed variables (`var \`name = expr`), `op_load_dynamic`/`op_store_dynamic`/`op_dynscope_push` opcodes, zero overhead on normal calls. |
| 12 | 12.1 | Event-Driven VM | Done. See `EVENT_DRIVEN_VM_PLAN.md`. Cross-thread ARC, NRT VM, RT VM on Silo, NRT and RT tempo schedulers, clock FFI. |
| 13 | 13.2--13.5 | Standard Library | Done (July 2026). String/array/list/map builtins + file/OS IO builtins (NRT) + `std.*` module namespace (result, fs, path, test, strings, json, message, thunk, futures). Expansion roadmap in 13.5. |
| 14 | 14.x | Optimizations | Mostly done. Register reclamation, tail call optimization, constant folding, range loop inlining all done. Remaining: general function inlining. |
| 15 | 15.x | Error Handling & Diagnostics | Done. Parser error recovery with synchronization, cascading error suppression, `expectClosing()` with diagnostic notes pointing to opening delimiters, readable token names. |
| 16 | 16.x | Testing Infrastructure | Done. 318 integration tests (`.x` files with expected output), `run_tests.sh` test runner, doc-extracted tests. |

---

## Phase 1: Core Language Completion (COMPLETE)

### 1.1 Struct Declarations and Construction — Done

**Status**: Complete. Added positional construction (`Point{3.0, 4.0}`) beyond original spec. Inheritance (task 11) deferred.

**Goal**: Parse, type-check, codegen, and execute struct declarations, construction, and field access.

**Files**: `ast.hpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`, `opcodes.hpp/cpp`

**Tasks**:
1. Add `StructDeclNode` to AST with fields list (name, type pairs) and optional parent struct for inheritance.
2. Parse `struct Name { field type, ... }` syntax. Fields are comma-separated `name type` pairs.
3. Type checker: Register struct types in a type registry. Create `StructType` in the VM. Validate field types.
4. Parse struct construction expressions: `Name { field: expr, ... }`. Add `StructLiteralExpr` AST node.
5. Type checker: Validate struct literal fields match the declared struct. Infer field expression types.
6. Codegen: Emit `op_make_struct` — allocate Struct object, populate fields from registers.
7. Add `op_make_struct` opcode: Takes struct type + N field registers, creates Struct, stores in dest register.
8. Parse field access: `expr.fieldName`. This may already work via `FieldExpr_` AST node.
9. Codegen: Emit `op_struct_get` for field access. Map field name to index at compile time.
10. Add `op_struct_get` opcode: Load field by index from Struct object.
11. ~~Support struct inheritance: Child struct includes parent fields. Field index offsets account for parent.~~ (Deferred to Phase 10)

### 1.2 Enum (Union) Declarations and Construction — Done

**Status**: Complete. Parser keyword is `enum`; internal naming uses both "Union" (AST) and "Enum" (type system, runtime).

**Goal**: Parse, type-check, codegen, and execute enum/union declarations and case construction.

**Files**: `ast.hpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`, `opcodes.hpp/cpp`

**Tasks**:
1. Add `UnionDeclNode` to AST with name and list of cases (each case has a name and optional type).
2. Parse `enum Name { caseName Type, caseName, ... }` syntax.
3. Type checker: Register enum types. Create `EnumType` in the VM with cases.
4. Parse enum construction: `Name.caseName(expr)` or `Name.caseName` (for no-data cases).
5. Type checker: Validate case exists, validate argument type matches case type.
6. Codegen: Emit `op_make_enum` — create Enum object with case index and value.
7. Add `op_make_enum` opcode: Takes enum type, case index, optional value register.

### 1.3 Pattern Matching — Done

**Status**: Complete. Added `ArrayPattern`, `ConsPattern` (list `h :: t`), and rest patterns (`...rest`) beyond original spec.

**Goal**: Full match with pattern matching on literals, enums, structs, tuples, and wildcards.

**Files**: `ast.hpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`, `opcodes.hpp/cpp`

**Tasks**:
1. Define pattern AST nodes:
   - `LiteralPattern` (int, float, string, bool, symbol)
   - `WildcardPattern` (`_`)
   - `BindingPattern` (variable name — binds matched value)
   - `EnumPattern` (`EnumName.caseName(pattern)`)
   - `StructPattern` (`StructName { field: pattern, ... }`)
   - `TuplePattern` (`(pattern, pattern, ...)`)
   - `GuardedPattern` (pattern + `if` condition)
   - `ArrayPattern` (matches array elements)
   - `ConsPattern` (matches list head `::` tail)
   - Rest patterns (`...rest`)
2. Add `MatchStmtNode` to AST with subject expression and list of match arms (pattern + body).
3. Parse match: `match (expr) { pattern: body ... }`.
4. Parse each pattern kind recursively.
5. Type checker: Infer subject type. For each arm, check pattern is compatible with subject type. Introduce bindings from patterns into arm body scope.
6. Codegen: Compile match as a chain of conditional tests:
   - Literal patterns: compare subject to constant, branch.
   - Wildcard: always matches, unconditional branch.
   - Binding: assign subject to local, always matches.
   - Enum: check `which_` field, extract value, recurse on inner pattern.
   - Struct: extract fields, recurse on each field pattern.
   - Tuple: extract elements, recurse on each element pattern.
   - Array: check length, extract elements, recurse.
   - Cons: extract head/tail from list, recurse.
   - Guard: test pattern first, then evaluate guard expression.
7. Add opcodes as needed: `op_enum_get_which`, `op_enum_get_value`, `op_struct_get`, `op_array_get`, `op_list_head`, `op_list_tail`.

### 1.4 For Loops — Done (implemented in Phase 6)

See Phase 6 below. Implemented after templates were completed.

### 1.5 Lambda Closures — Done

**Status**: Complete. No significant divergences.

**Goal**: Full lambda expressions that capture free variables from enclosing scope.

**Files**: `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`, `opcodes.hpp/cpp`

**Tasks**:
1. Parsing may already handle `fn(params) retType { body }` as lambda. Verify and fix.
2. Type checker: When checking a lambda body, identify references to variables from enclosing scopes. Record these as free variables with their types. Create `LambdaType` including free variable types.
3. Codegen: When generating a lambda:
   a. Generate the lambda body as a separate `CodeBlock`.
   b. At the definition site, emit `op_make_lambda` which captures current values of free variables into the Lambda object.
4. Add `op_make_lambda` opcode: Takes CodeBlock pointer + list of registers for free variables. Creates Lambda object.
5. Add `op_load_free_var` opcode: Inside a lambda body, load a captured free variable by index.
6. Ensure `op_call` handles Lambda objects: push frame, set up registers, load free vars.

### 1.6 Pipeline Syntax — Done

**Status**: Complete. Both `x |> f` and implicit `x f` syntax work.

**Goal**: `x abs sqrt` and `x |> abs |> sqrt` both desugar to `sqrt(abs(x))`.

**Files**: `parser.hpp/cpp`

**Tasks**:
1. The `|>` operator should already be tokenized. Verify.
2. In the parser, after parsing a primary expression, check if the next token is an identifier (not an operator, keyword, or `=`). If so, treat it as a pipeline call: `expr ident` becomes `ident(expr)`.
3. If the identifier is followed by `(args)`, then `expr ident(args)` becomes `ident(expr, args)`.
4. Pipeline calls chain left-to-right: `x f g(y)` becomes `g(f(x), y)`.
5. The `|>` operator works identically but with explicit operator precedence.
6. Handle interaction with binary operators: `x abs + y sqrt` should parse as `abs(x) + sqrt(y)`. Pipeline binding should be tighter than arithmetic.
7. Ensure this works in all expression contexts: let/var initializers, return, function arguments, if conditions.

---

## Phase 2: Function Overloading (COMPLETE)

### 2.2 Function Overloading — Done

**Status**: Complete. No significant divergences.

**Goal**: Functions can be overloaded on the static types of their arguments.

**Files**: `type_checker.hpp/cpp`, `codegen.hpp/cpp`

**Tasks**:
1. Allow multiple `FnDeclNode` with the same name but different parameter types.
2. Type checker: Store functions in an overload set keyed by name. On call, resolve by matching argument types. Report ambiguity errors.
3. Codegen: Resolve overload at compile time (since types are static). Emit call to the correct CodeBlock/global index.

---

## Phase 3: Auto-Mapping and the `@` Operator (COMPLETE)

### 3.1 Implicit Auto-Mapping — Done

**Status**: Complete. List auto-mapping uses lazy generators rather than eager loops.

**Goal**: If a function expects a scalar but receives an Array or List, automatically map the function over each element.

**Files**: `type_checker.hpp/cpp`, `codegen.hpp/cpp`, `opcodes.hpp/cpp`

**Tasks**:
1. Type checker: When resolving a function call, if an argument's type is `[T]` but the parameter expects `T`, mark this argument for auto-mapping. The return type becomes `[ReturnType]`.
2. If multiple arguments are auto-mapped, they are zipped (element-wise, length = shortest).
3. Codegen: When an auto-mapped call is detected, emit a loop:
   a. Allocate result array.
   b. For each index, load element from each auto-mapped argument, call function, store result.
4. Add `op_auto_map_call` opcode or generate the loop inline with existing opcodes.

### 3.2 Explicit Auto-Mapping with Postfix `@` — Done

**Status**: Complete. Changed to postfix syntax (`array @` not `@ array`) so no parentheses are needed for chaining: `array @ reverse drop(2)`.

**Goal**: The postfix `@` operator forces auto-mapping on an argument that would not otherwise auto-map. Numbered `@1`, `@2` do Cartesian/outer-product mapping.

**Files**: `lexer.hpp/cpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`

**Implementation**:
1. Lexer: Tokenize `@`, `@@`, `@@@`, `@1`, `@2`, etc. as `EachOp` tokens with a depth/index.
2. Parser: `@` appears after an expression as a postfix operator (e.g. `[1,2,3] @`). Tagged as `AutoMapExpr` wrapping the inner expression.
3. Type checker: Validates the `@`-tagged argument is iterable. Determines result shape:
   - Plain `@`: map one level deep.
   - `@@`: map two levels deep.
   - `@N`: Cartesian product — nested loops ordered by N.
4. Codegen for plain `@` and `@@`: Generate nested loops to appropriate depth.
5. Codegen for `@1`, `@2` (Cartesian): Generate nested loops. `@1` is outer, `@2` is inner. Result is nested arrays.
6. Works in data construction contexts (array literals, struct literals).

---

## Phase 4: List Type (COMPLETE)

### 4.1 List Type — Done

**Status**: Complete. Uses `List(1,2,3)` constructor, `::` cons operator, `nil`. Lazy generators for arithmetic.

**Goal**: Singly-linked, possibly lazy list. The last node may be a generator.

**Files**: `type_system.hpp/cpp`, `value.hpp/cpp`, `opcodes.hpp/cpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`

**Tasks**:
1. Define `ListType` in type system: `List<T>`.
2. Define `ListNode` runtime object: `{ head: Word, tail: ListNode* | Generator* }`.
3. Define `Generator` runtime object: A callable that produces the next `(value, nextGenerator)` pair on demand.
4. Add list literal syntax if desired, or construct via functions like `cons`, `list()`.
5. Add `op_cons`, `op_head`, `op_tail` opcodes.
6. Implement lazy evaluation: `op_tail` checks if tail is a Generator. If so, calls it to produce next node, then replaces the generator with the produced node (memoization).
7. Built-in list functions: `head`, `tail`, `cons`, `map`, `filter`, `fold`, `take`, `drop`, `zip`.
8. Auto-mapping should work on Lists the same as Arrays.

---

## Phase 5: Templates / Generics (COMPLETE)

### 5.1 Template Functions — Done

**Status**: Complete. Uses `<T>` angle bracket syntax (not `[T]`). Monomorphization with caching. Type parameters inferred from arguments at call sites.

**Goal**: `fn identity<T>(x T) T { x }` — functions parameterized by types.

**Files**: `ast.hpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`

**Implementation**:
1. `FnDeclNode` has `typeParams` vector for template type parameters.
2. Parser handles `fn name<T, U>(params) retType { body }` with `matchGreater()` for nested `>>` disambiguation.
3. Type checker: Template functions are registered with `isTemplate = true` and `resolvedFuncGlobalIndex = -2` sentinel. On call, type parameters are inferred via `unifyTypeExpr()` and cached monomorphized instances are reused.
4. Codegen: `genMonoInstance()` generates a separate CodeBlock for each monomorphized instance, re-type-checking the body with concrete type bindings.
5. Monomorphization cache keyed by (function name, concrete type arguments).

### 5.2 Template Structs and Enums — Done

**Status**: Complete. Both structs and enums support type parameters with inference or explicit specification.

**Implementation**:
1. `StructDeclNode` and `UnionDeclNode` have `typeParams` vectors.
2. Parser handles `struct Name<T, U> { fields }` and `enum Name<T> { cases }`.
3. Type checker: `monomorphizeStruct()` and `monomorphizeEnum()` create concrete types. Type args can be inferred from field values or constructor arguments.
4. Pattern matching handles template types by matching base names against subject types.
5. Each monomorphized type gets its own `StructType`/`EnumType` in the VM.

---

## Phase 6: For Loops & Ranges (COMPLETE)

### 6.1 For Loops — Done

**Status**: Complete. Syntax is `for x in iterable { body }`. Works with Arrays, Lists, and Ranges.

**Files**: `ast.hpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`

**Implementation**:
1. `ForStmtNode` in AST with loop variable name, iterable expression, and body block.
2. Parser handles `for varName in expr { body }`.
3. Type checker: Infers element type from `ArrayType`, `ListType`, or `RangeType`. Declares loop variable in body scope.
4. Codegen emits optimized loops per iterable type:
   - For Int ranges: Inline counter loop with no RangeObj allocation. Step inferred from direction or explicit next value.
   - For arrays: `op_array_length` + index loop with `op_array_get`.
   - For lists: `op_list_head` + `op_list_tail` + nil check loop.

### 6.2 Range Expressions — Done

**Status**: Complete. Range type with `RangeObj` runtime representation and built-in functions.

**Syntax**:
- `(start..end)` — range from start to end (step inferred from direction)
- `(start,next..end)` — range with explicit step (step = next - start)
- `(start..)` — infinite range

**Implementation**:
1. `RangeExprNode` AST node with start, optional next, optional end, and isInfinite flag.
2. `RangeType` in type system, `RangeObj` runtime value with start/end/step/isInfinite fields.
3. `op_make_range` opcode constructs RangeObj.
4. Built-in functions: `toArray(Range<Int>)`, `toList(Range<Int>)` (lazy via RangeListGen), `length(Range<Int>)`.

---

## Phase 7: Infinite Lists and Generators (COMPLETE)

**Rationale**: Core List infrastructure exists from Phase 4. This phase adds infinite list support.

### 7.1 Infinite Lists and Generators — Done

**Status**: Complete. 20+ lazy generator types implemented in `value.hpp` and `builtins_listgen.cpp`.

**Completed tasks**:
1. Built-in generators: `ord` (infinite integers), lazy `toList` for ranges/arrays/coroutines, `iter` (infinite function iteration), random generators (`urands`, `brands`, `irands`, `rands`, `xrands`), `picks` (random selection). Done.
2. List operations: `take`, `drop`, `stride`, `stutter`, `cat`, `cyc`, `ncyc`, `hang`, `join`, `map`, `filter`, `fold`, `scan`, `zip`, `enumerate`, `takeWhile`, `dropWhile`. All lazy. Done.
3. String-to-code-points: `codePoints(String) -> List<Int>` lazily decodes UTF-8. Done.
4. GC handles lazy list chains correctly -- generators use `releaseChildren()` for proper lifecycle. Done.
5. Infinite list printing bounded (displays first elements then `...`). Done.

---

## Phase 8: Ref Type and Mutability (COMPLETE)

### 8.1 Ref Type — Done

**Status**: Complete. Full mutable reference support with GC integration.

**Goal**: `Ref<T>` is a mutable reference to a value.

**Files**: `type_system.hpp`, `value.hpp/cpp`, `opcodes.hpp/cpp`, `parser.cpp`, `type_checker.cpp`, `codegen.cpp`, `lexer.hpp/cpp`, `ast.hpp`, `vm.hpp/cpp`

**Syntax**:
- `&expr` creates a `Ref<T>` (prefix `&` operator)
- `*refExpr` dereferences a `Ref<T>` to get the value (prefix `*` operator)
- `refExpr <- expr` sets the ref value and returns the value (infix `<-` operator)
- All three operators are overloadable for user types via function overloading
- `<-` is right-associative at precedence 0 (lowest)
- `Ref<T>` type annotation syntax for explicit type declarations

**Implementation**:
1. `RefType` in type system with cached factory (`vm.refType(T)`).
2. `RefValue` runtime object wraps a `Word` plus its `Type*`. GC-safe with `gcScan()` for Obj values and `writeBarrier()` on `op_ref_set`.
3. Lexer: `LeftArrow` (`<-`) token.
4. AST: `Ref` and `Deref` in `UnaryOpExpr::Op`, `LeftArrow` in `BinaryOpExpr::Op`, `RefTypeNode` for type expressions.
5. Parser: `&`/`*` as prefix unary ops, `<-` as binary op, `Ref<T>` type parsing, `<-` as overloadable function name.
6. Type checker: `&expr` infers `Ref<T>`, `*expr` infers `T`, `<-` validates `Ref<T>` left-hand side.
7. Opcodes: `op_make_ref`, `op_ref_get`, `op_ref_set`.

---

## Phase 9: Module System (COMPLETE)

### 9.1 Module Loading and Imports — Done

**Status**: Complete. All import syntaxes implemented and tested.

**Goal**: `import module`, `import module as alias`, `import module.{ name1, name2 }`, `import module.*`.

**Completed tasks**:
1. `ImportDeclNode` in AST with module path, import kind (whole/star/named), aliases. Done.
2. All import syntaxes parsed: whole, wildcard (`*`), named with aliases. Done.
3. Module registry with compilation caching. Done.
4. Module compilation on first import, with relative and include-path-based file resolution. Done.
5. Qualified access (`module.func(args)`) works including in pipeline syntax. Done.
6. All export types supported: functions, variables, structs, enums, templates, type aliases. Done.
7. Circular import detection. Done.
8. Module caching (don't recompile already-loaded modules). Done.
9. Cascading errors from failed module imports suppressed. Done.
10. 13 module-specific tests. Done.

---

## Phase 10: Methods and Object System

**Rationale**: Deferred from Phase 2. OOP features are implemented after functional features (templates, for-loops, generators) are complete.

### 10.1 Method Declarations

**Goal**: Methods are functions that dispatch on the type of a receiver. They are declared outside structs.

**Files**: `ast.hpp`, `parser.hpp/cpp`, `type_checker.hpp/cpp`, `codegen.hpp/cpp`, `opcodes.hpp/cpp`

**Tasks**:
1. Method syntax is: `fn ReceiverType.methodName(params) retType { body }`.
2. Add `MethodDeclNode` to AST with receiver type, method name, params, return type, body.
3. Parser: Parse method declarations.
4. Type checker: Register method in a method table keyed by (receiver type, method name). Validate receiver type exists. Bind `this` in body scope.
5. Codegen: Generate method body as a CodeBlock. Store in a method dispatch table.
6. Add `op_call_method` opcode: Look up method by receiver type + method name. Push frame with `this` as first register.

### 10.2 Struct Inheritance

**Goal**: Structs can inherit from a parent struct. Child has all parent fields plus its own.

**Files**: `parser.hpp/cpp`, `type_checker.hpp/cpp`

**Tasks**:
1. Parse: `struct Child : Parent { additionalFields }`.
2. Type checker: Validate parent struct exists. Merge parent fields into child (parent fields first). Allow child to be used where parent is expected (structural subtyping or nominal).
3. Method dispatch: Methods on parent type should also work on child type.

### 10.3 Where Clauses / Type Constraints

**Goal**: `fn sort<T>(arr [T]) [T] where T: Comparable { ... }`.

**Tasks**:
1. Parse `where` clauses with trait/interface constraints.
2. This requires a trait/interface system. May defer further.

---

## Phase 11: Dynamic Scoping (COMPLETE)

### 11.1 Dynamic Scope Variables — Done

**Status**: Complete. Backtick-prefixed variables use dynamic (call-chain) scoping.

**Goal**: Variables that are looked up in the dynamic call chain rather than lexical scope.

**Completed tasks**:
1. Syntax: `var \`name = expr` declares/sets a dynamic variable. Done.
2. VM: Separate `dynVars_` table with save/restore stack. Zero overhead on normal function calls (one u32 write on pushFrame, one comparison on popFrame). GC integration. Done.
3. `op_load_dynamic`, `op_store_dynamic`, `op_dynscope_push` opcodes. Done.
4. Type checker: Pre-scan registers dynamic vars before body checking, shared registry on Compiler. Done.
5. Codegen: Emit dynamic load/store opcodes. Done.
6. Test file: `tests/dynamic_scope.x`. Module example: `modules/dynvar.x`. Done.

---

## Phase 12: Event System (COMPLETE)

### 12.1 Event-Driven VM -- Done

**Status**: Complete. Full design and core infrastructure implemented. See `EVENT_DRIVEN_VM_PLAN.md` for the detailed plan covering RT and NRT VMs.

**What was implemented**:

1. **Cross-thread ARC deletion** (`gc.hpp`, `arc.hpp`, `tlsf_allocator.hpp`, `vm.hpp/cpp`): Lock-free MPSC `ForeignDeleteQueue` (Treiber stack) per VM. When an object's last reference is dropped on a foreign thread, it is enqueued on the home VM's foreign delete queue and freed during that VM's `gcHeartbeat()`. `GCObj::operator delete` uses `homeAllocator_` for correct cross-thread deallocation.

2. **NRT VM with mutex serialization** (`nrt_vm.hpp`, `nrt_scheduler.hpp/cpp`): `NRTVM` wrapper struct provides `call()`, `callCallable()`, `compileAndInstall()`, and `execute()` -- all acquire a per-VM mutex, call `makeCurrent()`, and run `gcHeartbeat()`. Any thread (OSC server, NATS client, scheduler, UI) can call in. `NRTScheduler` runs on its own thread with wall-clock timing and logical time for drift-free scheduling. Handlers are retained `Obj*` with proper lifecycle management.

3. **RT VM on Silo** (`bridge/include/tzpl_vm_commands.hpp`, `engine/src/tzpl_silo.hpp`): `Silo::vm_` opaque pointer for attaching a VM. Engine command subclasses (`VMEventCmd`, `VMCallableCmd`, `CodeInstallCmd`, `AttachVMCmd`, `DetachVMCmd`) flow through the existing FIFO/scheduler to deliver events to the RT VM.

4. **VM::callCallable()** (`vm.hpp/cpp`): New method to call a Lambda or Primitive from C++ host code, handling free variable setup for closures.

**All wiring work completed**:
- Clock FFI module: `sched`, `schedAbs`, `after`, `at`, `cancel`, `setTempo`, `getTempo`, `getBeats`, `getBeatDur`, `schedTempoChange`, `setLatency`, `getLatency`. Done.
- OSC handler registration (`osc.onMessage` and typed variants). Done.
- RT event handlers and RT-to-NRT reply removed. General inter-VM messaging planned.

---

## Phase 13: Standard Library (DONE — see Phase 13.5 roadmap for expansion)

### 13.1 Math Functions — Done

**Status**: Complete. 100+ overloaded built-in functions registered across all numeric types.

**Registered functions**:
- **Float unary**: abs, sqrt, cbrt, floor, ceil, round, trunc, frac, log, log2, log10, log1p, exp, exp2, expm1, exp10, sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, asinh, acosh, atanh, erf, erfc, tgamma, lgamma, sinpi, cospi, tanpi
- **Float binary**: pow, atan2, hypot, copysign, nextafter
- **Float predicates**: isNan, isInf, isFinite, isNormal
- **Multi-type**: abs, min, max, clamp, cmp, sign (Int, Float, Fraction, Complex, String)
- **Int bitwise**: clz, clo, ctz, cto, popCount, rotl, rotr, bitCeil, bitFloor, bitWidth, hasSingleBit
- **Complex**: sqrt, abs, log, exp, sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, asinh, acosh, atanh, real, imag, arg, norm, conj, polar, pow
- **Range**: toArray, toList, length
- All support auto-mapping.

### 13.2 String Functions — Done

**Done**: `length`, `cmp`, `min`, `max`, comparison operators (`<`, `<=`, `>`, `>=`, `==`, `!=`), `$` concatenation, `substring`, `contains`, `startsWith`, `endsWith`, `split`, `trim`, `toUpper`, `toLower`, `replace`, byte indexing (`s[i]`), `codePoints` (lazy `List<Int>` of Unicode code points), `toString`, `fmt`. Tested in `tests/builtins/string_functions.x` and `tests/builtins/codepoints.x`.

July 2026: `indexOf`/`lastIndexOf` (byte offsets, `Option<Int>`) and strict
`parseInt(s)`/`parseInt(s, radix)`/`parseFloat(s)` (`Option` returns) added
in `builtins_math.cpp`; richer utilities (padding, lines, glob, stripping)
live in `std.strings`. Tested in `tests/stdlib/`.

### 13.3 Array/List Functions — Done

**Done**: `length`, indexing, concatenation (`$`), construction, `push`, `pop`, `reverse`, `sort`, `grade`, `muss`, `take`, `drop`, `stride`, `stutter`, `repeat`, `cat`, `join`, `flatten`, `map`, `filter`, `fold`, `fold1`, `scan`, `scan1`, `find`, `zip`, `enumerate`, `iter`, `cyc`, `ncyc`, `hang`, `head`, `tail`, `cons`, `isNil`, `notNil`, `toList`, `toArray`, `collect`, `pick`, `picks`, `takeWhile`, `dropWhile`. All lazy list operations use generators.

### 13.4 IO Functions (Non-Real-Time Only) — Done

**Done**: `print`, `println`.

**Done July 2026** (`builtins_io.cpp`, all registered `rtSafe=false` so the
type checker rejects them in RT contexts): `readFile`, `readFileBytes`,
`writeFile`/`appendFile` (String and Bytes overloads), `fileExists`,
`isDirectory`, `fileSize`, `fileModTime`, `listDir`, `makeDir`, `removeFile`,
`renameFile`, `getEnv`, `programArgs` (CLI args after the script filename),
`currentDir`. Fallible ops return `Option`; mutating ops return `Bool`.
Whole-file granularity only — no streaming handles (Bytes accessors cover
structured binary reads). Ergonomic wrappers in `std.fs`; pure path helpers
in `std.path`. Tested in `tests/stdlib/fs.x`.

### 13.5 Standard Library Expansion (July 2026: P0 done; P1/P2 roadmap)

**P0 — shipped July 2026**:
- `std.*` namespace: general-purpose modules moved to `modules/std/`
  (`strings`, `json`, `message`, `messageEncoding`, `futures`, `thunk` +
  new `result`, `fs`, `path`, `test`). Audio-domain modules and bridge FFI
  wrappers stay flat. All import sites updated; docs' `import std.math`
  fiction replaced with real examples.
- New builtins: file/OS IO (13.4), `parseInt`/`parseFloat`,
  `indexOf`/`lastIndexOf`.
- New modules: `std.result` (Result<T,E> + combinators), `std.path`
  (basename/dirname/extension/joinPath/splitPath), `std.fs` (readLines/
  writeLines/readFileOr + Result variants), `std.test` (assertEq/assertTrue/
  assertNear/check/testSummary, stable PASS/FAIL lines), `std.strings`
  expansion (padStart/padEnd/repeatString/splitLines/capitalize/stripPrefix/
  stripSuffix/equalsIgnoreCase + glob with `* ? [a-z] [!...]`).
- Docs: new `docs/Standard_Library.html`; NRT badge + File & OS section in
  `Builtin_Functions.html`. Golden tests in `tests/stdlib/`.

**P1 — breadth (next)**:
- Builtins: `zip3`/`unzip`; persistent-vector variants of the newer
  reductions (sum/product/mean/any/all/sums/...); lazy-list `clump`/`spread`;
  time: `nowMonotonic()`/`nowUnix()` (non-blocking clock reads, rtSafe),
  `localUtcOffsetSeconds()` + `sleepSeconds()` (NRT).
- `.x`: `std.json` parser (recursive descent over codePoints,
  `Result<Json, String>` with byte offset in errors); `std.datetime`
  (civil-time math, days-from-civil algorithm, UTC + fixed offsets only, no
  tz database — distinct from the tempo `clock` module); `std.collections`
  (`groupBy`, `distinct`, `partition`, `minBy`/`maxBy`, `count`, `sumBy`,
  `frequencies`, `windowed`, `intersperse`; `sortBy(xs, keyFn)` as a thin
  wrapper since comparator `sort`/`grade` already exist); `std.functional`
  (`identity`, `always`, `compose`, `flip`, `tap`) + `std.ops` (named
  wrappers for operators lacking builtin twins, chiefly unary `neg`);
  `std.stats` (`median`, `variance`, `stddev`, `mode`, `percentile`,
  `normalize`, `histogram`).

**P2 — depth (on demand)**:
- Regex builtin: Thompson NFA (linear time, no catastrophic backtracking —
  consistent with the RT philosophy); opaque `Regex` value type;
  `regex(pattern) Option<Regex>`, `reMatch`, `reFind`, `reFindAll`,
  `reReplace`, `reSplit`. Largest single C++ item (~1–2 wk).
- `std.wavfile`: NRT WAV header/sample parsing over `readFileBytes` +
  Bytes accessors (wavetables, analysis).
- Language work item (not stdlib): operators as first-class values in
  binding/return position (today only call-arg position, e.g. `fold(0, +)`);
  shrink `std.ops` afterward.

---

## Phase 14: Optimizations (MOSTLY DONE)

### 14.1 Register Allocation Improvements — Done

Register reclamation implemented. Liveness tracking reclaims registers for temporaries that are no longer live. `--no-reg-reclaim` flag to disable for debugging.

### 14.2 Constant Folding — Done

AST-level constant folding evaluates constant expressions at compile time. `--no-const-fold` flag to disable.

### 14.3 Inline Caching for Method Dispatch

**Tasks**:
- Cache the last resolved method for a given call site to avoid repeated lookup.
- Deferred (methods not yet implemented, see Phase 10).

### 14.4 Tail Call Optimization — Done

Tail calls detected in codegen. `op_tail_call` reuses the current frame. `--no-tco` flag to disable.

### 14.5 Range Loop Inlining — Done

Int and Fraction range for-loops inlined to avoid `RangeObj` allocation. Direct counter loop emitted.

### 14.6 General Function Inlining

**Remaining**: Inline small functions at call sites (beyond range loops).

---

## Phase 15: Error Handling and Diagnostics (DONE)

### 15.1 Better Error Messages — Done

- `expect()` reports the actual token found (e.g. "Expected ')', got '{'"). Done.
- `expectClosing()` attaches a `DiagnosticNote` pointing to the opening delimiter (e.g. "to match ( here" at line 5). Done.
- `formatError()` renders notes with source context and caret underlining. Done.
- `tokenKindString()` produces readable names for all token kinds. Done.

### 15.2 Runtime Error Handling

Runtime errors (division by zero, index out of bounds) produce error messages. No exceptions in real-time path.

### 15.3 Parser Error Recovery — Done

- On parse error, synchronization skips to next statement boundary (`Semicolon`, `Fn`, `Let`, `Var`, `Const`, `Struct`, `Enum`, `Import`, etc.). Done.
- Progress-check mechanism prevents infinite loops. Done.
- Multiple errors reported per compilation. Done.
- Cascading errors from failed module imports suppressed. Done.

---

## Phase 16: Testing Infrastructure (DONE)

### 16.1 Unit Tests

C++ test framework not added. Testing done via integration tests (16.2).

### 16.2 Integration Tests — Done

- 318 test programs in `.x` files with expected output. Done.
- `run_tests.sh` test runner: compiles and runs each `.x` file, compares stdout to `.expected`. Done.
- Coverage: arithmetic, functions, recursion, closures, structs, enums, pattern matching, auto-mapping, pipelines, modules, coroutines, dynamic scoping, builtins (string, array, list, math, formatting, random, refs, sets, maps), error cases, type system, doc-extracted examples. Done.

### 16.3 Stress Tests

Stress testing done informally. No dedicated stress test suite.

---

## Implementation Order Summary

| Priority | Phase | Description | Status |
|----------|-------|-------------|--------|
| 1 | 1.1 | Struct declarations and construction | Done |
| 2 | 1.2 | Enum/union declarations and construction | Done |
| 3 | 1.3 | Pattern matching (match) | Done |
| 4 | 1.5 | Lambda closures | Done |
| 5 | 1.6 | Pipeline syntax | Done |
| 6 | 2.2 | Function overloading | Done |
| 7 | 3.1 | Implicit auto-mapping | Done |
| 8 | 3.2 | Explicit `@` operator (postfix) | Done |
| 9 | 4.1 | List type | Done |
| 10 | 5.1 | Template functions | Done |
| 11 | 5.2 | Template structs/enums | Done |
| 12 | 6.1 | For loops & iterables | Done |
| 13 | 6.2 | Range expressions | Done |
| 14 | 8.1 | Ref type | Done |
| 15 | 13.1 | Math builtins (100+ overloads) | Done |
| 16 | 7.1 | Infinite lists / generators | Done |
| 17 | 9.1 | Module system | Done |
| 18 | 10.1 | Method declarations | |
| 19 | 10.2 | Struct inheritance | |
| 20 | 10.3 | Where clauses / type constraints | |
| 21 | 11.1 | Dynamic scoping | Done |
| 22 | 12.1 | Event-driven VM | Done |
| 23 | 13.2--13.4 | Standard library (remaining) | Mostly done |
| 24 | 14.x | Optimizations | Mostly done |
| 25 | 15.x | Error handling / diagnostics | Done |
| 26 | 16.x | Testing infrastructure | Done |

---

## Architecture Principles

1. **Every feature touches 5 layers**: Lexer -> Parser/AST -> Type Checker -> Codegen -> Opcodes/VM. Implement each feature through all layers before moving to the next feature.
2. **Real-time safety is non-negotiable**: No system allocator calls, no blocking syscalls, no unbounded loops in the VM. All allocation through TLSF. GC pauses bounded.
3. **Types are statically known**: Words are untagged. The compiler resolves all types. No runtime type checks except for enum dispatch and method dispatch.
4. **Source-to-sink inference**: Types flow forward from definitions, not backward from usage. Function parameter types must be annotated. Return types can be inferred.
5. **Immutable by default**: Only `var` declarations and `Ref` types allow mutation. Everything else is immutable.
6. **Test each feature**: Write a `.x` test program for each feature as it's implemented.
