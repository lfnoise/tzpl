# Analysis of `TypeChecker::inferCall` (type_checker_calls.cpp)

## Section-by-Section Breakdown

### Preamble (lines 33-36)
Clears `autoMapArgs` and `innerAutoMapArgs` on the call expression node. This is needed because template bodies share AST nodes across monomorphizations -- a previous instantiation may have left stale data.

### Section 1: `std.func(args)` -- Built-in Qualified Calls (lines 38-128)

Handles `std.func(args...)` syntax. Checks if the callee is a `FieldExpr` with an `Identifier` object named `"std"`.

**Resolution order:**
1. Look up `fe->field` in `builtinFunctions_`
2. Infer all argument types
3. Try concrete (non-template) overloads via `isAssignable` (lines 58-70)
4. Try variadic built-ins (lines 72-89)
5. Try built-in templates via `fi.builtinTemplate()` callback (lines 91-114)

After resolution, sets `resolvedFuncGlobalIndex`, `isBuiltinCall`, `isCoroCall`, and triggers demand-driven return type inference if needed.

### Section 2: `module.func(args)` -- Module-Qualified Calls (lines 130-210)

Handles `module.func(args...)` where `module` is a name in `importedModules_`.

**Resolution order:**
1. Look up the module, then look up the field in its exports
2. If it's an exported function: infer arg types, try concrete overload match, try template via `tryResolveModuleTemplate`, fall back to single-concrete-overload
3. If it's an exported enum type: fall through to enum handling below
4. Otherwise: error "not callable"

### Section 3: Enum Case Construction (lines 213-343)

Handles `EnumName.caseName(value)` syntax for both concrete and template enums.

**Concrete enums (lines 218-282):**
- Looks up the enum in `enumTypes_`
- Finds the matching case by name
- Validates argument types against the case's data type
- Supports multi-arg construction for tuple-typed cases by synthesizing a `TupleLiteralExpr`
- Re-tags the call node as `ASTNode::EnumConstructor`

**Template enums (lines 284-341):**
- Looks up in `templateEnums_`
- Synthesizes `TupleLiteralExpr` for multi-arg tuple cases
- Infers type arguments from the argument via `unifyTypeExpr`
- Calls `monomorphizeEnum` to create the concrete enum type
- Re-tags as `EnumConstructor`

### Section 4: Non-Identifier Callees (lines 345-376)

Handles calls where the callee is not an identifier (e.g., `a[i](x, y)`, `getFn()(args)`).

- Infers the callee's type
- If it's a `FunctionType`: checks arg count and types, returns the function's return type
- If not callable but a `call` function exists in scope: rewrites to `call(expr, args...)` and falls through to identifier-based resolution

### Section 5: Special-Cased Function Names (lines 378-412)

Hardcoded handling for:
- `Complex(real, imag)` -- complex number constructor
- `getListPrintLimit()` -- no args, returns Int
- `setListPrintLimit(n)` -- one Int arg, returns Int

### Section 6: Tuple Struct Construction (lines 414-653)

Handles `StructName(arg1, arg2, ...)` for tuple structs.

**Concrete tuple structs (lines 416-519):**
- Checks arity against field count
- Infers arg types, detecting explicit `@` annotations
- Checks each arg against its field type
- Supports implicit auto-mapping: if a `[T]` or `List<T>` is passed where `T` is expected, auto-maps
- If any auto-mapping: wraps the result type in Array/List layers

**Template tuple structs (lines 521-653):**
- Infers arg types with `@` detection
- Tries direct unification of args against template field types
- If that fails or explicit `@` is present: unwraps Array/List from args, retries unification
- On success: monomorphizes the struct, applies auto-map wrapping

### Section 7: Lambda/Function Variable Calls (lines 656-899)

Handles calls where the callee is a variable holding a callable type.

**Deferred lambdas (lines 660-691):**
- Lambdas whose param types were deferred because they depended on call-site arg types
- Sets param types from call arg types, then infers the lambda

**Template lambda calls (lines 694-710):**
- Monomorphizes the template lambda based on argument types

**Concrete function type calls (lines 712-889):**
- Checks arg count
- If explicit `@` auto-map: unwraps, checks types, wraps return type
- If all types match directly: returns the function's return type
- Otherwise: tries implicit auto-mapping at increasing unwrap depths

**Callable objects via `call` (lines 892-899):**
- If the variable isn't a function type but a `call` function exists: rewrites to `call(varName, args...)`

### Section 8: Argument Inference with Deferred Lambdas (lines 901-1165)

Infers argument types for the main overload resolution path. Has special handling for:

**Explicit `@` annotation extraction (lines 903-921):**
- Detects `AutoMap` expressions and records their depth/cartesianIndex

**Deferred inference (lines 909-980):**
- Lambda args with untyped params are deferred (type set to `nullptr`)
- Template-only function references are deferred
- Template lambda variable references are deferred

**Backward inference (lines 982-1165):**
- After inferring non-deferred args, collects element types from collection args and scalar types
- Uses heuristics to deduce lambda param types:
  - If enough collection elem types: each lambda param gets one (map/filter/zip pattern)
  - If scalars + collections: first param = scalar (accumulator), rest = collection elems (fold/scan)
  - If only collections: all params get the first collection's elem type (fold1/scan1)
- For template lambda refs: monomorphizes with guessed arg types
- For template function refs: instantiates template with guessed arg types

### Section 9: Explicit `@` Auto-Map Resolution (lines 1167-1398)

When explicit `@` is present on any argument:

1. Unwraps each `@`-annotated arg by its depth
2. Tries overload resolution with unwrapped types: exact match -> template -> promotion
3. If that fails: tries additionally unwrapping non-`@` args (implicit auto-mapping on top of explicit), creating `innerAutoMapArgs`
4. Sets `autoMapArgs` on the expression
5. Computes the return type by wrapping in Array/List for:
   - Inner auto-map depth
   - Outer `@` depth (cartesian vs zip mode)

Also contains post-resolution logic for:
- Demand-driven return type inference
- Coroutine call/resume/yield/yieldAll detection
- Variadic packing info
- RT safety checking

### Section 10: Standard Overload Resolution (lines 1400-1545)

The main path for regular function calls without explicit `@`:

1. **Exact match** (lines 1406-1418): inline loop checking `paramTypes[j] != argTypes[j]`
2. **Template resolution** (line 1420): `tryResolveTemplate()`
3. **Promotion match** (line 1423): `tryResolveOverload()` (which does exact + promotion + variadic)
4. **Implicit auto-mapping** (lines 1426-1536): tries unwrapping Array/List args at increasing depths, retrying overload + template resolution at each level
5. **Error reporting** (lines 1539-1544): `resolveOverload()` with diagnostics

### Section 11: Post-Resolution Finalization (lines 1547-1645)

After resolving the function, performs:
- RT safety check
- Demand-driven return type inference (with canonical func handling for default args)
- Sets `resolvedFuncGlobalIndex`, `isBuiltinCall`
- Coroutine detection: `isCoroCall`, `isCoroResume`, `isCoroYield`, `isCoroYieldAll`
- Variadic packing info
- Auto-map return type wrapping

---

## Refactoring Opportunities

### 1. Extract Post-Resolution Finalization

The "finalize resolved function" logic appears **three times** in nearly identical form:
- Lines 119-127 (std.func path)
- Lines 187-199 (module.func path)
- Lines 1547-1645 (main path -- most complete version)

And a similar but simpler version at lines 1297-1398 (explicit `@` path).

**Proposal:** Extract a `finalizeResolvedCall(CallExpr_*, FuncInfo*, const std::string& name, const std::vector<Type*>& argTypes)` that handles:
- Demand-driven inference
- Setting resolvedFuncGlobalIndex, isBuiltinCall
- Coroutine marking
- Variadic packing
- RT safety check
- Returns the function's return type

### 2. Extract Overload Resolution for Built-in / Module Scopes

The inline overload resolution in the `std.func()` path (lines 52-114) and the module path (lines 141-186) duplicates `tryResolveOverload` + `tryResolveTemplate` but operates on a local vector of `FuncInfo` instead of `functions_`.

**Proposal:** Create `FuncInfo* resolveFromOverloadSet(const std::string& name, const std::vector<FuncInfo>& overloads, const std::vector<Type*>& argTypes, CallExpr_* expr)` that performs the same exact -> template -> promotion -> variadic resolution order on an arbitrary overload set. This would unify the std, module, and main paths.

**Subtle bug note:** The std.func path uses `isAssignable()` in its first pass (line 63), conflating exact matching with promotion. The main path correctly separates these. A unified function would fix this inconsistency.

### 3. Extract Auto-Map Argument Detection

The pattern of iterating args, checking for `ASTNode::AutoMap`, and extracting depth/cartesianIndex appears in:
- Lines 426-437 (tuple struct)
- Lines 526-537 (template tuple struct)
- Lines 722-736 (lambda calls)
- Lines 903-921 (main path)

**Proposal:** `std::vector<AutoMapArg> extractAutoMapAnnotations(const ExprList& args)`

### 4. Extract Auto-Map Return Type Wrapping

The pattern of wrapping a return type in Array/List based on auto-map depths appears in:
- Lines 494-514 (tuple struct)
- Lines 632-644 (template tuple struct)
- Lines 773-785 (lambda explicit @)
- Lines 867-877 (lambda implicit auto-map)
- Lines 1357-1397 (explicit @ main path)
- Lines 1627-1642 (implicit auto-map main path)

These all do essentially the same thing but with slightly different variable names. `wrapAutoMapResult` already exists in `type_checker_overload.cpp` (line 263) for binary operators but is not used here.

**Proposal:** Generalize `wrapAutoMapResult` or create `Type* wrapReturnForAutoMap(Type* scalarReturn, const std::vector<AutoMapArg>& autoMapArgs, bool anyList)` and use it everywhere.

### 5. Extract Implicit Auto-Map Resolution

The "try unwrapping at increasing depths" loop appears **three times**:
- Lines 805-878 (lambda calls)
- Lines 1430-1488 (main path, overload)
- Lines 1491-1536 (main path, template)

The lambda version and the main overload version are structurally identical. The main template version is also nearly identical but calls `tryResolveTemplate` instead of `tryResolveOverload`.

**Proposal:** `FuncInfo* tryImplicitAutoMap(const std::string& name, const std::vector<Type*>& argTypes, CallExpr_* expr, bool& hasListArg)` that tries both overload and template resolution at each depth level.

### 6. Consolidate the Explicit+Implicit Auto-Map Logic

Section 9 (lines 1167-1398) handles explicit `@` with nested implicit auto-mapping (`innerAutoMapArgs`). This is the most complex section and could be its own function:

**Proposal:** `Type* resolveWithExplicitAutoMap(CallExpr_* expr, const std::string& name, const std::vector<Type*>& argTypes, const std::vector<AutoMapArg>& explicitAutoMap)`

### 7. Extract Deferred Lambda Backward Inference

The backward inference heuristic (lines 982-1165) is a self-contained algorithm that:
1. Collects collection element types and scalar types from non-deferred args
2. Guesses lambda param types based on patterns (map, fold, zip, etc.)
3. Infers deferred lambdas and template references

This is complex enough to be its own function:

**Proposal:** `void resolveBackwardInference(CallExpr_* expr, std::vector<Type*>& argTypes)`

### 8. Extract Special-Cased Intrinsics

`Complex()`, `getListPrintLimit()`, `setListPrintLimit()` (lines 380-412) could be handled by registering them as built-in functions rather than hardcoding in `inferCall`. This would eliminate special cases from the main flow.

---

## Potential Bugs

### Bug 1: `std.func()` Uses `isAssignable` Instead of Exact Match First

Lines 62-66 use `isAssignable()` for the first resolution pass. This means if there are two overloads like `foo(Int)` and `foo(Float)`, calling `std.foo(myInt)` would match whichever overload comes first in the map, because `Int` is assignable to both. The main resolution path (lines 1406-1418) correctly tries exact match first.

### Bug 2: Missing RT Safety Check on Several Paths

`checkRTSafety()` is called at line 1548 for the main path, but is **not called** for:
- `std.func()` calls (Section 1)
- Module-qualified calls (Section 2)
- Lambda/function variable calls (Section 7)
- Explicit `@` auto-map path (Section 9) -- it resolves a `func` but never calls `checkRTSafety` on it

If `rtRestricted_` is ever true for these paths, non-RT-safe functions could be called without error.

### Bug 3: Error Message Typo in std.func

Line 116: `"No matching overload for std.\" + fe->field + \"'\"` -- there's a missing opening single quote before `std.`. Should be `"No matching overload for 'std." + fe->field + "'"`.

### Bug 4: `tryResolveOverload` Redundantly Checks Exact Match

At line 1423, `tryResolveOverload()` is called after the inline exact-match loop (lines 1406-1418) already failed to find an exact match. But `tryResolveOverload()` starts by trying exact match again. This is harmless but wasteful.

### Bug 5: Unreachable Fallback in Module Path

Lines 169-186: After trying concrete overloads and template resolution, there's a fallback that tries to use the "single concrete overload" even if it didn't match. This seems intentionally lenient but could silently produce type errors at runtime.

---

## Proposed Refactored Structure

```
Type* inferCall(CallExpr_* expr) {
    clearStaleAutoMap(expr);

    // Qualified calls
    if (isFieldExpr(expr->callee)) {
        if (auto* result = tryInferStdCall(expr))        return result;
        if (auto* result = tryInferModuleCall(expr))     return result;
        if (auto* result = tryInferEnumConstruct(expr))  return result;
    }

    // Non-identifier callees (indexing, field access, etc.)
    if (expr->callee->kind != ASTNode::Identifier) {
        if (auto* result = tryInferIndirectCall(expr))   return result;
        // May rewrite to call(expr, args...) and fall through
    }

    auto* ident = static_cast<IdentifierExpr*>(expr->callee.get());

    // Intrinsics
    if (auto* result = tryInferIntrinsic(expr, ident))   return result;

    // Tuple struct construction
    if (auto* result = tryInferTupleStructConstruct(expr, ident)) return result;

    // Variable holding a callable
    if (auto* result = tryInferVariableCall(expr, ident)) return result;

    // Standard function call resolution
    return inferNamedFunctionCall(expr, ident);
}
```

Each extracted function would be 50-150 lines and have a clear, descriptive name. The main `inferCall` becomes a dispatcher that's easy to follow.
