# Documentation vs Implementation Discrepancies

> **Historical log.** Items below were resolved at the time of writing; retained as a development record.

This file lists examples from `Tzopilotl_by_Example.html` and `Builtin_Functions.html`
that were broken, required workarounds, or differ from the documented behavior.

## Fixed Issues

### 1. Anonymous `fn` at start of block body — FIXED

**Problem:** `fn(x Int) Int { x + n }` at the start of a block body was parsed as a
function declaration, not a lambda expression.

**Fix:** Parser now checks if `fn` is followed by `(` and treats it as a lambda expression.

**Files changed:** `src/parser.cpp`

---

### 2. Explicit type parameters on function calls — DOC REMOVED

**Documented syntax:** `42 identity<Int> println;`

The parser only supports explicit type parameters in type annotations and enum case
construction, not in function call expressions. Type inference works for all cases.

**Fix:** Removed from documentation; type inference handles this.

**Files changed:** `tests/docs/by_example/templates.x`

---

### 3. Map type annotation syntax — DOC FIXED

**Problem:** Documentation used `Map[String, Int]` syntax.

**Fix:** Documentation updated to use correct syntax `[String:Int]`. Removed empty map
with type annotation (`let empty Map[String, Int] = [:];`) since typed empty maps
are not implemented.

**Files changed:** `Tzopilotl_by_Example.html`, `tests/docs/by_example/maps.x`

---

### 4. Mixed List + Array auto-mapping — FIXED

**Problem:** `add(List(10, 20, 30), [1, 2, 3])` produced pointer values like
`List(5033248050, ...)` instead of `List(11, 22, 33)`.

**Fix:** `AutoMapListGen` now properly extracts individual elements from array arguments
during lazy list generation, with correct type promotion.

**Files changed:** `src/value.hpp`, `src/value.cpp`, `src/codegen.cpp`, `src/builtins.cpp`

---

### 5. Boolean array display — FIXED

**Problem:** `[1, 5, 3] @ > 2` produced `[0, 1, 1]` instead of `[false, true, true]`.

**Fix:** `PodArray<i64>::str()` now checks if the element type is Bool and formats
accordingly.

**Files changed:** `src/value.hpp`

---

### 6. Complex number negative zero display — FIXED

**Problem:** `conj(3+0i)` displayed as `3+-0i` instead of `3-0i`.

**Fix:** Complex `str()` now uses `std::signbit()` instead of `< 0` to correctly detect
negative zero in the imaginary component.

**Files changed:** `src/value.hpp`

---

### 7. Descending fraction range boundary — DOC FIXED

**Problem:** Documentation claimed `(3/1..1/2)` produces `3/1 2/1 1/1 1/2`, but the
actual output is `3/1 2/1 1/1`. The step is inferred as `-1`, so `1/2` is never reachable.

**Fix:** Documentation updated to show correct output.

**Files changed:** `Tzopilotl_by_Example.html`

---

### 9. `fmt` placeholder syntax — DOC FIXED

**Problem:** Documentation in the default_args section used `{}` placeholders instead
of the correct `%^` syntax.

**Fix:** Documentation updated to use `%^`.

**Files changed:** `Tzopilotl_by_Example.html`

---

## Remaining Limitations

### 8. Non-deterministic hash values

Symbol hashes are based on pointer addresses and change between runs. Array and tuple
hashes that include symbols or reference-type objects also vary.

**Workaround:** Tests only use deterministic hash values (`hash(42)`, `hash(3.14)`) and
equality comparisons (`hash(x) == hash(x)`).

**Files:** `tests/docs/by_example/hashing.x`, `tests/docs/builtins/hashing_eq.x`
