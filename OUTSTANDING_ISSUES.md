# Outstanding Issues and Findings -- 2026-04-10

## Remaining Crash: Stale ARC Release (5th eval)

### Symptom
Evaluating `example_synthdefs.x` 5 times in the GUI app crashes with `EXC_BAD_ACCESS` in `TLSFAllocator::insertBlock`. The crash always shows `this=0x...6f` instead of `this=0x...70` -- the low byte of a `TLSFAllocator*` was decremented by 1.

With the heartbeat thread enabled, crash happens on eval 5 (heartbeat thread).
With the heartbeat disabled, crash happens on eval 20 (eval thread's gcHeartbeat).
The bug is deterministic on eval count, not timing-dependent.

### Root Cause
A stale `release()` call on a freed GCObj. `release()` does `fetch_sub(1)` on the refcount field (offset +8 from the GCObj pointer). If the freed GCObj's memory was recycled into a live object, the `fetch_sub` corrupts 4 bytes at offset +8 from the stale pointer. When that offset overlaps a `TLSFAllocator*` field (in a VMString's STLAllocator or an ObjArray's vector allocator), the pointer is decremented by 1, causing subsequent TLSF operations to access invalid memory.

### What It Is NOT
- Not pool exhaustion (crashes with 256MB pool)
- Not a threading race (crashes with heartbeat disabled, just takes longer)
- Not an ObjArray element issue (ObjArray was refactored to private v_ with retaining accessors)
- Not a FuncInfo vector invalidation (changed to deque)
- Not the heartbeat thread itself (it just surfaces the bug faster)

### What It IS
A missing `retain()` in a non-ObjArray container. Some container stores an `Obj*` without retaining it, but its `releaseChildren()` releases it on destruction. Candidates:

1. **Struct** (`value.hpp`): fields stored via `op_make_struct` (retains -- looks correct). Check if any path modifies struct fields without retain.
2. **Tuple** (`value.hpp`): fields stored via `op_make_tuple` (retains -- looks correct). Check `op_tuple_slice` and any tuple copy paths.
3. **ListNode** (`value.hpp`): `head_`, `tail_`, `generator_` -- check if list construction retains properly. `cons`, `toList`, lazy evaluation paths.
4. **Lambda** (`value.hpp`): free variables stored via `op_make_lambda` -- check if all capture paths retain.
5. **Enum** (`value.hpp`): `word_` field -- check if enum case value is retained.
6. **MapObj** (`value.hpp`): key/value pairs -- check if map operations retain.
7. **CoroutineObj** (`value.hpp`): `args_[]` flexible array -- check coroutine creation and yield/resume.

### Recommended Fix
Apply the same pattern as the ObjArray refactor: make each container's `Obj*` fields private with accessor methods that handle `retain()` on store and `release()` on overwrite. This eliminates the class of bug rather than fixing individual call sites.

### How to Find It
1. Build with ASan using system malloc for GCObj (`__has_feature(address_sanitizer)` in `vm.cpp`).
2. Use 8MB pthread stack for the eval thread (`gui_state.cpp`).
3. Evaluate example_synthdefs.x repeatedly.
4. ASan will report `heap-use-after-free` with three stack traces: alloc, free, and use.
5. The "free" trace shows which `releaseChildren` freed the object. The container in that trace has the missing retain.

Note: ASan with TLSF won't work because TLSF is a custom allocator invisible to ASan. The `#if __has_feature(address_sanitizer)` blocks in `GCObj::operator new/delete` route GCObj allocation through system malloc when under ASan. However, VMString buffers still use the TLSF pool via STLAllocator, so ASan can't detect corruption of those buffers.

---

## GC Design Issues

### gc() Builtin Limitations
The `gc()` lang builtin can only process the deferred delete queue -- it cannot drain the auto-release pool. Draining the pool during execution kills objects that are still referenced by VM registers (registers are unmanaged raw `Word` values, not ARC-retained). This means `gc()` can only free objects that are ALREADY dead (refcount=0 in the deferred queue), not objects that would become dead if the pool released them.

### Auto-Release Pool Design
The auto-release pool conflates two concerns:
1. Keeping newly-created objects alive until retained by something
2. Providing a "drain point" to release temporaries

Because VM registers don't retain, ALL objects created during execution are in the pool. Draining the pool mid-execution kills temporaries that the VM is still using. The pool can only be safely drained BETWEEN execution events (after `vm.execute()` returns).

### Heartbeat Thread
The NRT heartbeat thread (`NRTVM::startHeartbeat`, 20ms interval) acquires the VM mutex and calls `gcHeartbeat()` which:
1. Drains foreign delete queue into deferred queue
2. Drains auto-release pool (safe because no VM code is running while mutex is held)
3. Processes up to 4096 deferred deletes

This is correct and safe -- the mutex prevents concurrent VM execution. The heartbeat surfaces the stale-release bug faster because it processes the deferred queue more frequently.

### RT Heartbeat
A per-audio-buffer heartbeat callback is implemented on `Silo` (`heartbeatFn_`) but never activated because `AttachVMCmd` is never sent. The infrastructure is in place for when an RT VM is attached to a silo.

---

## Bugs Fixed in This Session

### 1. SIMD Broadcast Codegen
**File**: `synthdef-compiler/src/synthdef_cpp_codegen.cpp:657`
**Bug**: When a variable narrower than the loop but wider than the SIMD width was broadcast (e.g., 8-chan into 16-chan SIMD-4 loop), the generated code read past the variable's bounds (`v65[i]` for `i` up to 15, but `v65` had only 8 elements).
**Fix**: Wrap the load offset with `& (chans-1)` to cycle within the variable's range.
**Impact**: `sum(2)` produced silence because clang's `-O3` propagated the UB and deleted the entire reduce + outlet write.

### 2. Synthdef Compiler Arena Leak
**Files**: `bridge/src/tzpl_synthdef_compiler_ffi.cpp`, `synthdef-compiler/src/synthdef_arena.hpp`, `synthdef-compiler/src/synthdef_cpp_codegen.cpp`
**Bug**: `compileSynthDefPipeline` never freed the `Synth*` returned by `synthFromSExprText`. Each `defSynth` call leaked a `Synth` and its entire `Arena` of expression nodes.
**Fix**: Wrap in `std::unique_ptr`. Added `virtual ~ArenaObj() = default` so `Arena::clear()` runs derived destructors. Removed `ArenaObj` inheritance from `CppCodeGen` (it was stack-allocated, registering a stack pointer in the arena).

### 3. Mono Cache Thrash
**File**: `lang/src/type_checker.cpp:499`
**Bug**: `checkREPLInput` cleared `monoCache_`, `monoStorage_`, and `monoInstances_` on every REPL eval. Each re-monomorphization called `addGlobal()`, growing the globals vector by ~123 entries per eval. After 5 evals, the 64MB TLSF pool was exhausted.
**Fix**: Preserve the mono cache across evals. The `programs` vector keeps ASTs alive, so `declNode` pointers remain valid.

### 4. FuncInfo Vector Invalidation (Manual Fix)
**File**: `lang/src/type_checker_overload.cpp:990-994, 1092-1095`
**Bug**: `tryResolveTemplate` and `tryResolveModuleTemplate` held `FuncInfo*` into `functions_[name]` (a vector) across calls to `monomorphize()` which `push_back`'d to the same vector, invalidating the pointer. Found via AddressSanitizer.
**Fix**: Copy `*best->fi` into a local `FuncInfo bestFI` before calling `monomorphize`.

### 5. FuncInfo Vector Invalidation (Systemic Fix)
**Files**: `lang/src/type_checker.hpp`, `lang/src/builtins_internal.hpp`, `lang/src/module_compiler.hpp`, and 6 other files
**Bug**: 16+ sites where `FuncInfo*` pointers into `functions_[name]` were held across calls that could `push_back` to the same vector.
**Fix**: Changed `std::vector<FuncInfo>` to `std::deque<FuncInfo>` everywhere. Deque guarantees pointer/reference stability on `push_back`.

### 6. Option\<T\> Use-After-Free
**File**: `lang/src/type_checker.cpp:234`
**Bug**: The synthetic `Option<T>` `UnionDeclNode` was created by `registerBuiltins()` and owned by the `TypeChecker` (`syntheticOptionDecl_` unique_ptr). When the module's TypeChecker was destroyed, the node was freed, but the module's export table still held the raw pointer. Found via AddressSanitizer.
**Fix**: Mark `syntheticOptionDecl_->isPrivate = true` so it's never exported. Each importing TypeChecker creates its own via `registerBuiltins()`.

### 7. ObjArray ARC Violations (Multiple Sites)
**Files**: `lang/src/opcodes.cpp`, `lang/src/builtins_array.cpp`, `lang/src/builtins_internal.hpp`, `bridge/src/tzpl_audio_engine_ffi.cpp`
**Bug**: Many code paths stored `Obj*` into `ObjArray` without calling `retain()`, but `ObjArray::releaseChildren()` released all elements on destruction. Missing retains in: `writeElem` (automap helper), `op_array_set`, `op_concat_array`, `op_array_slice`, `builtin_push_array`, `builtin_cat_array`, `builtin_sort_string_array`, `builtin_repeat_obj`, `txArray`, `arrayPush`, `builtin_muss`, `ffi_listSynthDefs`.

### 8. ObjArray Systemic Refactor
**Files**: `lang/src/value.hpp` and 11 other files
**Fix**: Made `ObjArray::v` private (renamed to `v_`) with accessor methods:
- `set(i, val)`: retains new, releases old
- `push(val)`: retains before push_back
- `get(i)` / `operator[]`: read-only
- `copyFrom(src)`: releases old, copies and retains new
- `rawVec()`: escape hatch for sort/txArray lambdas
All direct `v[i]=` and `v.push_back()` replaced with safe accessors.

### 9. release() Guard
**File**: `lang/src/gc.hpp:95`
**Fix**: Added `if (rc == 0) return false;` guard to prevent refcount underflow when `release()` is called on an already-dead object.

### 10. processN Budget
**File**: `lang/src/vm.hpp:287`
**Fix**: Increased from 256 to 4096 per `gcHeartbeat()` call.

### 11. TLSF Pressure Relief (Added then Removed)
**Files**: `lang/src/tlsf_allocator.hpp`, `lang/src/vm.cpp`
**Added**: `PressureReliefFn` callback to drain deferred deletes when TLSF pool is exhausted.
**Removed**: Draining deferred deletes inside `allocate()` caused double-frees -- objects deleted by pressure relief were still in the auto-release pool.
**Status**: The callback API exists in TLSFAllocator but is not used. The concept is sound but the implementation needs to avoid draining the auto-release pool.

### 12. gc() Builtin
**File**: `lang/src/builtins.cpp:1529`
**Added**: `gc()` function callable from lang code. Only processes the deferred delete queue (does NOT drain the auto-release pool -- that's unsafe during execution).

### 13. NRT Heartbeat Thread
**File**: `lang/src/nrt_vm.hpp`
**Added**: `NRTVM::startHeartbeat(interval)` / `stopHeartbeat()`. Background thread calls `gcHeartbeat()` under the VM mutex every 20ms.

### 14. RT Per-Buffer Heartbeat
**Files**: `engine/src/tzpl_silo.hpp`, `engine/src/tzpl_silo.cpp`, `bridge/include/tzpl_vm_commands.hpp`
**Added**: `Silo::heartbeatFn_` callback, called once per audio buffer in `processFrames()`. `AttachVMCmd` wires it to `vm->gcHeartbeat()`. Infrastructure is ready but `AttachVMCmd` is not yet sent by any code path.

### 15. 8MB Eval Thread Stack
**File**: `app/src/gui_state.cpp`
**Changed**: `AsyncEval::launch` uses `pthread_create` with `pthread_attr_setstacksize(8MB)` instead of `std::thread` (512KB default). Required for AddressSanitizer which triples per-frame stack usage.

### 16. VM Pool Size
**File**: `app/src/main.cpp:676`
**Changed**: 64MB to 256MB. The pool was exhausting after 5 evals of 21 defSynth calls. With 256MB and the heartbeat draining between evals, the pool stays bounded.

---

## ASan Build

An AddressSanitizer build is configured at `build-asan/`:

```bash
cmake -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address" \
  -DTZPL_BUILD_APP=ON
```

When ASan is detected (`__has_feature(address_sanitizer)`), `GCObj::operator new/delete` routes through system `malloc`/`free` instead of TLSF, so ASan can track individual GCObj allocations. VMString buffers still use TLSF via STLAllocator.

Run with: `build-asan/app/tzpl_app -I "./bridge/modules:./lang/modules"`
