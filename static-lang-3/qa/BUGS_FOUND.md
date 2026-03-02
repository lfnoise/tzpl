# QA Bugs Found

All bugs discovered during QA testing of Language X. Each bug has an associated
test file in `qa/tests/` that demonstrates the issue.

---

## Round 1 - Fixed Bugs

| # | Bug | Severity | Test File |
|---|-----|----------|-----------|
| 1 | Implicit comparison auto-mapping SEGFAULT | CRITICAL | `bug_comparison_automap_crash.x` |
| 2 | nil $ List concatenation codegen error | Medium | `bug_nil_concat.x` |
| 3 | nil literal type inference as function arg | Medium | `bug_nil_as_arg.x` |
| 4 | Template fn with fn type param can't resolve | Medium | `bug_template_fn_type.x` |
| 5 | Scalar > array gives wrong result | Medium | `bug_comparison_automap_crash.x` |
| 6 | String reverse not available | Low | `bug_string_reverse.x` |
| 7 | Block expressions don't work as values | Low | `bug_block_expr.x` |

## Round 2 - Fixed Bugs

| # | Bug | Severity | Test File |
|---|-----|----------|-----------|
| 8 | Very large float literal crashes parser | Medium | `bug_large_float_literal.x` |
| 9 | Cannot overload unary operators | Low | `bug_unary_neg_overload.x` |

---

## Round 3 - New Bugs

### BUG: Unary `!` and `~` Don't Auto-map Over Collections

**Test file:** `bug_unary_automap.x`
**Severity:** Medium

Unary `-` auto-maps over arrays, tuples, and lists correctly:
`-[1, 2, 3]` produces `[-1, -2, -3]`. But `!` and `~` don't auto-map:

```x
println(![true, false, true]);  -- Error: "Logical not requires boolean operand"
println(~[0, 1, -1]);          -- Error: "Bitwise not requires integer operand"
```

**Workaround:** Use `map`:
```x
[true, false, true] map(fn(x Bool) Bool { !x }) println;
[0, 1, -1] map(fn(x Int) Int { ~x }) println;
```

---

### BUG: Closures Can't Capture Variables from Grandparent Lambda Scope

**Test file:** `bug_closure_depth.x`
**Severity:** Medium

A closure nested 3+ levels deep can capture from its immediate parent
lambda and from top-level scope, but NOT from a grandparent lambda:

```x
let x = 1;  -- top-level: capturable from any depth
let f = fn(a Int) {           -- level 1
    let g = fn(b Int) {       -- level 2
        let h = fn(c Int) Int { x + a + b + c };  -- level 3
        --                          ^^^ Error: Cannot find captured variable 'a'
        h(4)
    };
    g(3)
};
```

`h` can capture `x` (top-level) and `b` (from `g`, its parent), but
NOT `a` (from `f`, its grandparent).

**Workaround:** Pass grandparent values through intermediate parameters.

---

## Accepted Behaviors

- **Silent integer overflow:** `MAX_INT + 1` wraps to `MIN_INT` (C semantics).
  See `edge_overflow.x`.
- **var captured by value in closures:** Use `Ref` for shared mutable state.
  See `edge_closure_var.x`.
- **Template enum requires explicit type args:** Correct when type params are
  unconstrained. See `bug_template_enum_infer.x`.
- **NaN != NaN:** Follows IEEE 754. See `edge_runtime_errors.x`.
- **Hash values non-deterministic:** Randomized per run, but consistent within
  a run. See `edge_hash.x`.
- **`find` on arrays returns index (Int):** Returns the index of the first
  match, or -1 if not found. Not an Option. See `edge_find_filter.x`.
- **No `reverse`/`toArray` for Lists:** Lists can be lazy and infinite, so
  these operations are intentionally unavailable. Use `collect(n)` to
  materialize a finite prefix into an array first. See `bug_list_reverse.x`.
- **Integer div/mod by zero returns silent wrong results:** Follows C
  semantics (undefined behavior). For a creative coding language meant for
  live performance, weird results are preferable to crashing. See
  `bug_int_div_zero.x`.

---

## Test Coverage Summary

**84 test files** in `qa/tests/`.

**Round 1 (41 tests):** arithmetic, arrays, strings, auto-mapping, closures,
patterns, collections, structs, control flow, functions, types, refs, pipes,
formatting, conversions, symbols, scope, operators, list ops, nested automap,
any type, option, tuple arithmetic, bitwise ops, map edges, match
exhaustiveness, type aliases, struct inheritance, recursive data structures,
string escapes, complex auto-mapping.

**Round 2 (25 tests):** coroutines, ranges, complex numbers, fractions,
runtime errors (NaN/Inf), hashing, built-in math functions, template lambdas,
pipe operator `|>`, sets, collection functions, for loops, tuple operations,
operator overloading, advanced pattern matching, ref/`<-` overloading, global
variables, numeric tower, advanced enums, string operations, auto-mapping of
binary operators, higher-order functions, advanced map operations.

**Round 3 (18 tests):** division by zero, advanced coroutines (tuples/structs/
fizzbuzz/filter), deep auto-mapping, advanced refs (nested/shared), advanced
closures (factories/depth), string interpolation, advanced ranges (fraction/
stepped/infinite), overload dispatch, integer division, match patterns, template
structs, find/filter/takeWhile/dropWhile, sort with custom comparators, iter/
lazy list generators, unary operator auto-mapping bug, closure depth bug,
list reverse bug, integer division by zero bug.

---

## Summary Table

| # | Bug | Severity | Status | Test File |
|---|-----|----------|--------|-----------|
| 1 | Implicit comparison auto-mapping SEGFAULT | CRITICAL | FIXED | `bug_comparison_automap_crash.x` |
| 2 | nil $ List concatenation codegen error | Medium | FIXED | `bug_nil_concat.x` |
| 3 | nil literal type inference as function arg | Medium | FIXED | `bug_nil_as_arg.x` |
| 4 | Template fn with fn type param can't resolve | Medium | FIXED | `bug_template_fn_type.x` |
| 5 | Scalar > array gives wrong result | Medium | FIXED | `bug_comparison_automap_crash.x` |
| 6 | String reverse not available | Low | FIXED | `bug_string_reverse.x` |
| 7 | Block expressions don't work as values | Low | FIXED | `bug_block_expr.x` |
| 8 | Very large float literal crashes parser | Medium | FIXED | `bug_large_float_literal.x` |
| 9 | Cannot overload unary operators | Low | FIXED | `bug_unary_neg_overload.x` |
| 10 | Integer div/mod by zero returns wrong results | High | Accepted | `bug_int_div_zero.x` |
| 11 | Unary `!` and `~` don't auto-map over collections | Medium | **OPEN** | `bug_unary_automap.x` |
| 12 | Closures can't capture grandparent lambda vars | Medium | **OPEN** | `bug_closure_depth.x` |
| 13 | `reverse` / `toArray` not available for Lists | Low | Accepted | `bug_list_reverse.x` |
