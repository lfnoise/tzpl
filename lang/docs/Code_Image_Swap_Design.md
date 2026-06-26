# Design Note: Immutable Code Image + Mutable Data Segment

Status: proposed
Scope: lang VM (globals representation, opcodes, codegen, GC, install) + engine
silo command path
Motivation tickets: silo live redefinition (commit `6b093dd`); RT-thread
overload of silo code loading.

## 1. Problem

A silo VM keeps every global in one flat array, `VM::globals_`
(`lang/src/vm.hpp:241`, a `VMVector<Word>` over the silo's TLSF pool). That array
interleaves three kinds of slot allocated from a single monotonic counter
(`Compiler::addGlobal`, `compiler.cpp:111`):

1. **Function `CodeBlock*`** (user functions).
2. **Builtin `Primitive*`** (math/array/etc.).
3. **Top-level `var`/`let` values** -- mutable, and the ones that can hold heap
   `Obj*` roots.

Installing compiled code onto a live silo runs on the **audio thread**:
`SiloCodeInstallCmd::doRT` -> `VM::install` (`bridge/.../tzpl_audio_engine_ffi.cpp`,
`vm.cpp:450`), drained once per buffer at the head of `Silo::processFrames`
(`engine/src/tzpl_silo.cpp:94`, `processRTCommands` at `:418`; install commands are
`schedImmediate`, `tzpl_command.hpp:41`). `install` appends `newGlobals` and
applies `reusedGlobals` in place.

This is RT-*safe* (TLSF O(1) alloc, lock-free delivery, no syscall) but not
RT-*bounded*: the cost is `O(new globals + top-level work)`, concentrated in one
buffer. The worst case is a silo's **first** load, which installs all builtins
plus every imported module's globals in a single callback -- enough to risk a
buffer underrun if the silo is already producing audio.

A redefinition today is already cheap (a few in-place pointer stores via
`reusedGlobals`), but there is no way to swap a whole rebuilt environment in
constant time, and no way to move the heavy first-load install off the audio
thread.

## 2. Key observation

The VM's opcodes **already partition globals by mutability**:

| Concern | Opcodes | Access |
|--------|---------|--------|
| Functions / builtins | `op_call`, `op_call_primitive`, `op_func_ref`, `op_tail_call`, `op_coro_create`, `op_async_call`, constraint dispatch (`opcodes.cpp` ~`992,1035,5178`, etc.) | **read only** |
| `var`/`let` globals | `op_load_global`, `op_store_global`, `op_store_global_obj`, `op_init_global_obj`, `op_{load,store,init}_global_inline` (`opcodes.cpp:122-199`) | read **and write** |

There is no opcode that writes a function slot at runtime: a `CodeBlock*` is
placed in its slot at install time and only ever read thereafter. And
`CodeBlock`s (and builtin `Primitive`s, and compile-time constants) are
**immortal** -- system/arena allocated, never GC'd, never mutated (relied upon
already, e.g. the `SiloRunStartCmd` comment "immortal CodeBlock ... so no rooting
is needed").

So the function/builtin half of the globals is a pure, immutable, read-only
image. Only the `var`/`let` half is mutable state that must survive across a code
change.

## 3. Design

Split the single global index space into two:

- **Code image** -- builtin `Primitive*`, function `CodeBlock*`, and their
  immortal constants. Immutable. Held behind a single pointer, rebuilt off the
  audio thread, published with one store. Snapshotted by the RT thread once per
  buffer (code can only change between buffers, at the `processRTCommands` point),
  so per-call reads stay plain indexing -- **no per-call atomics**.
- **Data segment** -- `var`/`let` globals (including inline composites and heap
  roots). Mutable, never swapped; *extended in place* with capacity reserved so
  existing slot addresses stay stable.

Redefining or adding any number of functions becomes: build a new code image on
the NRT thread (copy previous + apply changes), ship it, do one atomic pointer
store on the RT thread -- O(1) on the audio thread regardless of how many
functions changed.

### Index spaces

Two counters replace the one in `VMTargetData`: `codeCount` and `dataCount`. The
type checker routes function/builtin global allocation to the code counter and
`var`/`let` allocation to the data counter. Codegen already keeps function
references in a **distinct AST field**, `resolvedFuncGlobalIndex` (emitted at the
~15 call/ref sites in `codegen.cpp`), separate from variable global indices, so
the two index kinds do not get conflated at emission -- the change is to point
the function-ref opcodes at the code image and leave the `op_*_global` family on
the data segment.

### VM representation

```
// Immutable, swappable. Built on NRT, read on RT.
struct CodeImage {
    Vec<Word> slots;        // CodeBlock* / Primitive* (all immortal)
    u32 generation;
};
std::atomic<CodeImage*> codeImage_;     // published by swap command
CodeImage* rtCodeImage_ = nullptr;      // RT per-buffer snapshot

// Mutable, extend-in-place. The only GC-root-bearing globals.
Vec<Word> dataGlobals_;
Vec<u8>   dataIsObj_;
Vec<std::pair<u32,Type*>> inlineObjData_;
```

`vm.code(idx)` reads `rtCodeImage_->slots[idx]`; `vm.data(idx)` reads
`dataGlobals_[idx]`.

### GC

`TracingGC::step_root_globals` (`tracing_gc.cpp:205`) currently walks all
`numGlobals()` skipping the (majority) function slots via `globalIsObj`. After the
split it walks only `dataGlobals_` -- the code image holds no roots and is never
scanned. Smaller root set, simpler loop.

### Swap semantics (Erlang-style, already consistent with the language)

- An in-flight coroutine/frame holds its `CodeBlock*` directly, not via a global
  index, so it **finishes on the old body**; the next *call through a global*
  resolves to the new image. This matches the language's existing
  capture-by-value rule (`let f = someFunc` already snapshots the current body;
  `op_func_ref` building a function value pins the current `CodeBlock*` -- see
  `codegen.cpp:3453`).
- Old images are retired on the NRT thread via the existing two-stage
  command cleanup (`doNRT`) after a grace period (see Risks for the
  reclamation policy).

## 4. Staged plan

Each stage is independently shippable and leaves the suite green
(`lang/tests` 392, silo harnesses, synthdef difftests, Release + ASan).

### Stage 1 -- Split storage, no swap yet (semantics-preserving refactor)

Introduce `codeImage_`/`dataGlobals_` as two plain arrays (no atomics), two
counters in `VMTargetData`, and `vm.code()/vm.data()` accessors. Route:
- type-check/codegen function & primitive indices -> code array;
- `var`/`let` indices -> data array;
- function-ref opcodes (`op_call`, `op_call_primitive`, `op_func_ref`,
  `op_tail_call`, `op_coro_create`, `op_async_call`, constraint dispatch) ->
  `vm.code()`;
- `op_*_global` / `op_*_global_inline` -> `vm.data()`.
`VM::install` splits incoming slots into the two arrays; `reusedGlobals` apply
in place to `codeImage_`. GC scans only `dataGlobals_`.

Files: `compiler.{hpp,cpp}` (counters, `addCodeGlobal`/`addDataGlobal`),
`type_checker*` (index routing), `codegen.cpp` (emit code-image opcodes),
`opcodes.{hpp,cpp}` (accessor split), `vm.{hpp,cpp}` (storage, `install`),
`tracing_gc.cpp` (scan only data), `incremental_compiler.cpp` /
`CompileResult` (carry split globals + split reused/new).

Observable behavior unchanged. Delivers: GC skips code; code and data each have
their own append base (no cross-coupled `globalBase` invariant).
Risk: largest stage (touches codegen + opcodes + type checker); pure refactor, so
the test suite is a strong oracle. **Biggest, most mechanical stage.**

### Stage 2 -- Swappable code image (the O(1) win)

Make `codeImage_` a heap `CodeImage` behind `std::atomic<CodeImage*>`. RT thread
snapshots `rtCodeImage_` once in `processRTCommands` before the sample loop.
Redefinition/addition: NRT builds a fresh `CodeImage` (copy previous slots + apply
new/redefined), ships a new `SiloCodeSwapCmd` whose `doRT` does the atomic store
(O(1)); `doNRT` retires the prior image after the grace period. Data-segment
additions, when present, ride a separate small `SiloDataExtendCmd` ordered before
the swap.

Files: `vm.{hpp,cpp}` (atomic image, snapshot hook), engine command subclass
(`SiloCodeSwapCmd`/`SiloDataExtendCmd`), bridge install path (build image on NRT
instead of in-place install). Delivers: O(1) batch / whole-module hot swap;
image build moves off the audio thread.
Risk: image lifetime/retirement; RT snapshot timing.

### Stage 3 -- First-load priming + shared standard image

Build the builtins + standard-module code image entirely on NRT and hand it to a
silo at `attachVM` (or as the first swap), so a silo's first real load only ships
its own deltas. Because the standard image is immutable and read-only, **one
prebuilt instance can be shared by reference across all silos** (each silo still
has its own mutable data segment). Removes the first-load install spike from the
audio thread -- the original overload concern.

Files: bridge `attachVM`/context (prebuilt shared image), VM (accept an initial
image by reference). Delivers: the overload fix; lower per-silo memory.
Risk: sharing a `CodeImage` across VMs with different TLSF pools -- the image
holds only immortal pointers, so it is pool-independent, but lifetime must
outlive all referencing silos.

## 5. Risks & open questions

- **Old-image reclamation.** Frames pin `CodeBlock*` directly. Need a retirement
  policy: simplest is "retire after K buffers" (conservative) or refcount images
  by RT snapshot ownership. Since `CodeBlock`s are immortal anyway, never-freeing
  is bounded by swap count -- acceptable for short sessions, but a live-coding
  session wants eventual reclamation. **Decide in Stage 2.**
- **Constraint / existential dispatch** reads callee globals at several sites
  (`opcodes.cpp` ~`4108,4685,4957,5178`); all must route to the code image.
  Inventory them precisely in Stage 1.
- **`op_func_ref` baking `CodeBlock*` into a `LambdaType`** at codegen
  (`codegen.cpp:3453`) pins a version into a function value. This is the intended
  capture-by-value semantics, but document it so redefinition's "new calls see
  new code, captured values do not" is explicit.
- **synthc / differential tests.** The lang-hosted synthdef compiler runs as VM
  code; its *output* bytes are independent of VM global layout, so difftests must
  stay byte-identical through Stage 1. Confirm early.
- **Data-segment growth** is still not O(1)-swappable (it holds live state), but
  it is append-only, small, and reservable; only genuinely-new top-level `var`s
  hit it.

## 6. Effort estimate (rough)

- Stage 1: large but mechanical (codegen + opcodes + type checker + GC + install).
  The suite is a tight oracle. ~the bulk of the work.
- Stage 2: moderate (atomic image, swap command, retirement).
- Stage 3: small once Stage 2 lands (build off-thread, share by reference).

Recommend landing Stage 1 behind no flag (pure refactor, fully covered by tests),
then Stage 2, then Stage 3. If the overload fix is wanted sooner with less churn,
the interim `attachVM`-time silent priming (install builtins/standard modules
while the silo is silent, off the audio path) gives most of the Stage 3 benefit
without the index-space split -- but it does not give O(1) swap and is strictly a
stopgap.
