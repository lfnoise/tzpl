---
name: write-tzpl
description: Use when writing, generating, or reviewing Tzopilotl code (.x files) for the lang project. Provides syntax rules, conventions, and examples to avoid common mistakes.
allowed-tools: Read, Glob, Grep
---

You are writing code in **Tzopilotl**, a statically typed language defined in `lang/`. Before writing any code, internalize these rules completely. Violations of these rules will cause parse or compile errors.

$ARGUMENTS

# Critical Rules (Most Common Mistakes)

1. **Statements end with `;`** — Every `let`, `var`, `const`, assignment, expression statement, `return`, `break`, `continue`, and expression-body `fn` MUST end with a semicolon.
2. **Comments use `--`** — NOT `//`. Block comments use `/* */` and can be nested.
3. **No `:` between parameter name and type** — Write `fn foo(x Int)` NOT `fn foo(x: Int)`.
4. **No `->` before return type** — Write `fn foo(x Int) Int` NOT `fn foo(x Int) -> Int`.
5. **No `mut` keyword** — Use `var` for mutable bindings, `let` for immutable.
6. **Write left-to-right, not nested** — Use space pipeline and `|>` to thread data through transformations. See Idiomatic Style below.

# Idiomatic Style: Prefer Left-to-Right

Write code that reads left to right, threading data through transformations, rather than nesting function calls. Rules of thumb:

- **Single-arg functions** — use space pipeline: `x f` instead of `f(x)`.
- **First arg is primary, rest are parameters** — use space pipeline with parens for extra args: `xs take(3)` instead of `take(xs, 3)`.
- **All args equally important** — keep nested: `min(a, b)`, `zip(xs, ys)`.
- **`|>` pipe** — use when the left side is a complex expression that space pipeline can't handle: `a + b |> f`. Don't use `|>` for trivially simple cases.

```
-- Less idiomatic (nested):
println(toList(reverse(take(toArray((1..10)), 5))))

-- Idiomatic (left to right):
(1..10) toArray take(5) reverse toList println
```

# Complete Syntax Reference

## Variables and Constants
```
let x = 42;                -- immutable binding
let x Int = 42;            -- with type annotation (no colon!)
var counter = 0;           -- mutable variable
var counter Int = 0;       -- mutable with type annotation
const MAX = 100;           -- constant
```

## Dynamic Scope Variables
```
var `indent = 0;           -- backtick prefix, declared with var
`indent = 5;               -- assign like regular var
fn inner() {
    var `indent = 100;     -- saves/restores on function exit
}
```

## Functions

Top-level declarations (`fn`, `struct`, `enum`, `constraint`, `type`) are
**order independent** -- a function may be called textually before its
definition, and mutual recursion needs no forward declarations. Order
functions for readability, not for scope. (Top-level `let`/`var` initializers
still run in program order.)

### Block body (no semicolon after closing brace)
```
fn max(a Int, b Int) Int {
    if (a > b) { return a; }
    b
}
```

### Expression body (semicolon required)
```
fn add(a Int, b Int) Int = a + b;
fn double(x Int) = x * 2;       -- return type inferred
```

### Void functions
```
fn greet(name String) Void { println("Hello " $ name); }
```

### Default arguments
```
fn greet(name String, greeting String = "Hello") String = greeting $ " " $ name;
```

### Variadic arguments
```
fn wrap(...xs) = xs;                     -- untyped, returns tuple
fn sumInts(...xs Int) Int { ... }        -- typed, xs is [Int]
fn greet(greeting String, ...names String) String { ... }  -- mixed
```

## Templates (Generics)
```
fn identity<T>(x T) T = x;
fn max_of<T>(a T, b T) T = a > b ? a : b;
fn first<T>(arr [T]) T = arr[0];
fn pick<T, U>(a T, b U) T = a;
```

## Lambda Expressions
```
let add = fn(a Int, b Int) Int { a + b };
let double = fn(x Int) Int { x * 2 };
let inc = fn(x Int) { x + 1 };          -- return type inferred

-- Template lambdas (omit parameter types)
let add = fn(a, b) { a + b };

-- Function types: (ArgTypes) ReturnType
fn apply(f (Int) Int, x Int) Int = f(x);
fn compose(f (Int) Int, g (Int) Int) (Int) Int {
    let result = fn(x Int) Int { f(g(x)) };
    result
}
```

## Structs
```
struct Point { x Float, y Float }        -- optional ; after }

-- Construction
let p = Point { x: 1.0, y: 2.0 };       -- named fields
let p = Point { 1.0, 2.0 };             -- positional
let p2 = Point { ...p, x: 10.0 };       -- spread/update

-- Field access
p.x println;

-- Tuple structs
struct Temperature(Float);
let t = Temperature(98.6);
t.0 println;
```

## Enums
```
enum Shape {
    circle Float,
    rect (Float, Float),
    point,
}

let s = Shape.circle(5.0);
let p = Shape.point;

match (s) {
    Shape.circle(r): println(r);
    Shape.rect(dims): println(dims.0 * dims.1);
    Shape.point: println("point");
}
```

## Template Structs and Enums
```
struct Box<T> { value T }
struct Pair<T, U> { first T, second U }
enum NumResult<T: Numeric> { ok T, err String }
```

## Control Flow

### If/Else (parentheses required around condition)
```
if (x > 0) { println("positive"); }
if (x > 0) { "big" } else { "small" }    -- as expression
if (x > 0) { ... } else if (x == 0) { ... } else { ... }
```

### Ternary
```
let result = x > 0 ? x : -x;
```

### While
```
var i = 0;
while (i < 10) {
    println(i);
    i = i + 1;
}
```

### For loops
```
for (i : (1..5)) { println(i); }          -- range (inclusive)
for (i : (0,2..10)) { println(i); }       -- stepped range
for (x : arr) { println(x); }             -- array
for (x : lst) { println(x); }             -- list
```

### Break and Continue
```
while (true) { if (done) { break; } }
for (x : xs) { if (x < 0) { continue; } }
```

### Match (pattern matching)
```
match (value) {
    1: println("one");
    2: println("two");
    _: println("other");
}

-- With guards
match (x) {
    n if (n < 0): return -n;
    n: return n;
}

-- Destructuring in match
match (point) {
    Point { x: a, y: b }: println(a + b);
}
match (arr) {
    [head, ...tail]: println(head);
    _: println("empty");
}
match (list) {
    h :: t: println(h);
    nil: println("empty");
}
```

## Destructuring
```
let (a, b) = (1, 2);                    -- tuple
let (first, ...rest) = (1, 2, 3, 4);    -- rest pattern
let [h, ...t] = [1, 2, 3];              -- array
var (x, y) = (10, 20);                  -- mutable
const (cx, cy) = (42, 84);              -- const
```

## Types
- Primitives: `Int`, `Float`, `Bool`, `String`, `Symbol`, `Fraction`, `Complex`, `Void`
- Collections: `[T]` (Array), `List<T>`, `(T, U)` (Tuple), `Map<K,V>`, `Set<T>`
- Special: `Ref<T>`, `Any`, `Option<T>`, `Range`
- Function: `(Int, Int) Int`, `(String) Void`

## Type Annotations (no colon!)
```
let x Int = 42;                          -- NOT let x: Int = 42
fn foo(x Int, y Float) String { ... }    -- NOT fn foo(x: Int, y: Float) -> String
```

## Operators
```
+ - * / % //                             -- arithmetic (// is integer division)
== != < > <= >=                          -- comparison
&& || !                                  -- logical
& | ^ ~ << >>                           -- bitwise
$                                        -- string/array/list concatenation
::                                       -- list cons (prepend)
? :                                      -- ternary
<-                                       -- ref assignment
```

## Refs (Mutable References)
```
let x = &42;           -- create ref
*x println;            -- dereference with *
x <- 100;              -- assign with <-
```

## Strings
```
"hello"                                  -- regular string
"tab\there\nnewline"                     -- escape sequences
"\u0041"                                 -- 4-digit unicode
"\U0001F600"                             -- 8-digit unicode
"""raw string, no \n escapes"""          -- triple-quoted raw
<<raw string, no \n escapes>>              -- guillemet raw
```

## String Formatting
```
"%^ + %^ = %^" fmt(1, 2, 3) println;    -- %^ is positional placeholder
```

## Collections
```
-- Arrays
let a = [1, 2, 3];
a[0] println;
a length println;
[1, 2] $ [3, 4]                         -- concatenation

-- Lists
let xs = List(1, 2, 3);
let ys = 0 :: xs;                        -- cons
1 :: 2 :: 3 :: nil                       -- build from nil

-- Tuples
let t = (1, "hello", 3.14);
t.0 println;
let single = (42,);                      -- 1-tuple needs trailing comma

-- Maps
let m = ["a": 1, "b": 2];
m["a"] unwrap println;                   -- subscript returns Option
get(m, "key", default) println;          -- get with default

-- Sets
let s = Set(1, 2, 3);
s contains(2) println;

-- Ranges
(1..5)                                   -- 1 to 5 inclusive
(0,2..10)                                -- 0, 2, 4, 6, 8, 10
(5..1)                                   -- descending
```

## Pipeline Syntax
```
-- Space pipeline: x f is f(x)
5 double println;
"hello" length println;
15 clamp(0, 10) println;

-- Pipe operator for precedence
3 + 4 |> double println;

-- Chaining
[1, 2, 3] map(fn(x Int) { x * x }) filter(fn(x Int) { x > 2 }) println;

-- Postfix try: unwrap Result/Option, early-return err/none from the
-- enclosing fn. Requires the fn to declare a matching Result/Option
-- return type (same error type; no conversion).
fn addParsed(a String, b String) Option<Int> {
    let x = parseInt(a) try;             -- none returns immediately
    let y = parseInt(b) try;
    Option.some(x + y)
}
```

## Auto-mapping
```
-- Implicit: scalar function auto-maps over arrays/lists
fn double(x Int) Int = x * 2;
[1, 2, 3] double println;               -- [2, 4, 6]

-- Explicit @ operator
[1, 2, 3] @ process println;
[[1, 2], [3, 4]] @@ reverse println;    -- double-deep
```

## Operator Overloading
```
fn +(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };
fn ==(a Point, b Point) Bool = a.x == b.x && a.y == b.y;
```

## Function Overloading
```
fn describe(x Int) String = "integer";
fn describe(x Float) String = "float";
fn describe(x String) String = "string";
```

## Callable Objects
```
struct Adder { amount Int }
fn call(a Adder, x Int) Int = x + a.amount;
let add5 = Adder { 5 };
add5(10) println;                        -- 15
```

## Indexable Objects
Defining `at` makes a type indexable: `obj[idx]` rewrites to `at(obj, idx)`.
Defining `put!` makes it index-assignable: `obj[idx] = v` rewrites to
`put!(obj, idx, v)`. Built-in indexable types keep their built-in behavior.
Arrays also provide `at`/`put!` as builtins (`a at(i)` == `a[i]`,
`a put!(i, v)` == `a[i] = v`), so the protocol is uniform in generic code.
```
struct Cycle { items [Int] }
fn at(c Cycle, i Int) Int = c.items[i % c.items length];
fn put!(c Cycle, i Int, v Int) Void { c.items[i % c.items length] = v; }
let cyc = Cycle { [10, 20, 30] };
cyc[4] println;                          -- 20
cyc[[0, 1, 2, 3]] println;               -- [10, 20, 30, 10] (auto-maps)
cyc[4] = 99;                             -- put!(cyc, 4, 99); wraps to slot 1
```

## Constraints (Type Classes)
```
constraint Numeric = Int | Float;
constraint Addable<T> = requires { +(T, T) T };
constraint Ordered<T> = Numeric & Comparable<T>;

fn add<T: Numeric>(a T, b T) T = a + b;
fn multiply<T>(a T, b T) T where T: Numeric = a * b;
```

## Type Aliases
```
type Coordinate = (Float, Float);
type Matrix = [[Float]];
type Pair<A, B> = (A, B);
private type InternalId = Int;
```

## Modules
```
import math_utils;                       -- qualified: math_utils.square(5)
import math_utils.{square, cube as cb}; -- named imports
import std.*;                            -- wildcard
import mymodule as m;                    -- alias
```

## The Any Type
```
let a = any(5);                          -- wrap value
a as(Int) println;                       -- unwrap with type
match (x) {
    v Int: println(v);                   -- match by type
    v String: println(v);
    _: println("unknown");
}
```

## Semicolon Rules Summary
**NEEDS semicolon:**
- `let`, `var`, `const` declarations
- Expression-body functions: `fn foo() Int = expr;`
- Expression statements: `x println;`
- `return expr;`, `break;`, `continue;`
- Assignments: `x = 5;`, `x <- 10;`

**NO semicolon needed after:**
- Block-body functions: `fn foo() { ... }`
- `if/else`, `while`, `for`, `match` blocks
- `struct` and `enum` definitions (optional)

## Printing
```
println(value);           -- function call style
value println;            -- pipeline style (preferred)
println(a, b, c);         -- multiple values, space-separated
```

# Test Files for Reference
When in doubt about syntax, check existing test files in `lang/tests/` for working examples. Key directories:
- `functions/` - function definitions, overloading, defaults, variadic
- `control_flow/` - if/else, while, for, match, break/continue
- `data_structures/` - structs, enums, arrays, lists, maps, tuples
- `type_system/` - constraints, templates, type aliases
- `operators/` - arithmetic, comparison, bitwise, overloading
- `expressions/` - pipelines, ternary, lambdas
- `variables/` - let, var, const, destructuring
- `modules/` - imports, visibility
- `automap/` - auto-mapping and @ operator
