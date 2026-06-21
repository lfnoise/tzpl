# synthc M5 Plan -- SIMD, rewrites-on, crash fix, switchover

Status: M5.0 DONE. The end-to-end fixups are in:
- **Engine double-uninit crash FIXED** (the real root cause, engine-wide). Dropped
  the redundant `uninit()` in `engine/src/tzpl_node.cpp` `~Node()` -- the generated
  `_free` already owns `_uninit`, and built-ins tear down in `_free`/delete, so
  `_uninit` now runs exactly once. The spectral `defSynthX` render no longer
  crashes at teardown (0/10). No codegen change -> byte-match preserved.
- **Rewrites ON in `defSynthX`** (`compileToCpp` defaults `applyRewrites = true`
  -> `importGraph(name, true)`). The spectral input now emits the rewritten
  `((0.6f * f32(v2)) + -0.3f)`, byte-matching `defSynth` (the M4.5 divergence is
  gone). Render A/B stays bit-identical; rewrite_diff/analysis/voicer/spectral/
  controlflow diffs + 382 lang tests green.
- **`simdWidth` plumbing**: `genCpp(ctx, name, simdWidth Int = 0)` threads a
  `cgSimdWidth` dynvar (0 = scalar, unused until M5.1); `compileToCpp` passes it.
- **Spectral end-to-end guard** added to `run_synthc_render_ab.sh`: a `defSynthX`
  spectral render asserted non-silent (no C++ ref -- the C++ compiler's SIMD
  spectral codegen emits invalid C++), guarding the pipeline + the teardown fix.

M5.1 DONE: the SIMD scaffolding + simple var-ref forms byte-match the C++ at
width 4 (9 cases in `synthc_simd_diff.x`). What landed: the `_loopSimdWidth`
decision (width 4 only at the diff oracle's max=min=4, plus the disqualifying-expr
set), Case A (chans==width, no loop) / Case B (stride `i += 4`) loop emission, the
`f32x4`/splat/load helpers, the SIMD op-string spellings (bare `sin`/`max`, with
the gotcha that `rem` is not a synthc BinaryOp -- it had silently become a
catch-all), and the per-construct vector forms (inlet bare-pointer-at-Case-A vs
`&acc[cel]`, const/array load, scalar/control/sampleRate splat, cast `convert<T>`,
outlet Case-A-bare vs Case-B-`&acc[cel]` store). The SIMD store reuses `_varRef`'s
lvalue (`*(Tx*)(...)`), so genTree needed no change. Delays/buffers/voicer/spectral
are temporarily SIMD-disqualified so synthc stays correct-scalar at any width
(verified: a 4ch delay synth at width 4 == width 0); M5.2-M5.4 implement their
SIMD paths and drop the disqualification. `defSynthX` still passes width 0
(scalar) -- SIMD is opt-in via `genCpp(ctx, name, 4)` until the corpus is swept.

M5.2 (1-sample delays) DONE: in SIMD a 1-sample delay read is a contiguous vector
load `*(f32x4*)(&p->dN[cel])`, the write a vector store (the SIMD visitor appends
` DelayWrite` to the comment though the scalar form omits it), and a consumed-in-
loop FanOut temp declares as `f32x4 vN`. The disqualifier became ctx-aware so it
can gate on `allocSize` (1-sample allowed; ring + var-read still deferred). d4
(Case A) / d8 (Case B) byte-match.

M5.2 (ring buffers) DONE: `_delayFixReadExpr`/`_delayWriteStmt` grew SIMD branches
covering every dchans case, mirroring the C++ DelayFixRead/DelayWrite visitors:
- z1 (allocSize==1): dchans==1 splats `p->dN`; dchans>=width contiguous-loads;
  narrower gathers per lane (channel wrap via `_simdLaneChanIdx`).
- ring (allocSize>1): dchans==1 reads one ring slot and splats; multichannel gathers
  `f32x4{p->dN[ci][(p->dN_wrpos - u64(k)) & mask], ...}` (lane channel `ci` =
  `_simdLaneChanIdx(cel, j, dchans, loopChans)`).
- write: contiguous store only for z1 dchans>=width; otherwise `<vtype> _dwN = value;
  // <s> DelayWrite` then per-lane scatter (`p->dN[ci] = _dwN[j];` for z1,
  `p->dN[ci][p->dN_wrpos & mask] = _dwN[j];` for ring; per-lane lines self-indent).
- `_simdLaneChanIdx` mirrors C++ `simdLaneChanIdx` (constant cel folds `+ j`; wrap by
  `bufChans-1` when narrower). The disqualifier now allows all fixed reads/writes.
r4 (Case A), r8 (Case B), rfb (ring feedback) byte-match in `synthc_simd_diff.x`.

M5.2 (variable-read interpolation) DONE: `_delayVarReadSimd` + `_genSimdInterp`
(+ `_genSimdSincCoeffs`, `_interpTaps`, `_interpKernel`, `_delayVarGenRead`,
`_genDelayOffScalar`) port the C++ DelayVarRead SIMD visitor. A scalar offset
(chans 1) is read once in scalar mode (`_di`, frac cast); a vector offset gives
per-lane `_di{j}` + `trunc(_off)`. interpNone gathers per lane inline; linear/cubic/
lagrange/sinc build the `[&]() -> f32xW { ... }()` lambda (tap gathers + kernel call;
sinc adds the per-lane `tzpl_sinc_coeffs` table). `delayVarReadK` un-disqualified. vc/
vl/vg/vs/vn (vector offset, all kernels) + sc/ss/sn (scalar offset) byte-match.

NOTE: adding the var-read tests to synthc_simd_diff.x surfaced + fixed a latent GC
crash -- multi-word inline-struct globals (a ControlSpec offset control) had their
inline trailing payload words scanned as Obj* roots. Fixed in codegen's two stack-map
emitters (skip the inline span). See [[lang_gc_inline_trailing_words]].

M5.2 COMPLETE.

M5.3 (flat-voice SIMD) DONE. The flat-voice loop now strides the voices x channels
trip count (`_flatLoopSimdWidth` over `trip`; Case A no-loop / Case B `i += w`,
comment `// SIMD <w>x (flat voice)`). The shared disqualifier scan was factored out
(`_loopHasSimdDisqualifier`) so RNG/select voicers stay correct-scalar. New SIMD
forms (mirroring the C++ inSimdMode && inFlatVoiceMode visitor):
- var-ref: voice-local SoA load (`*(Tx*)(&voice_v[cel])`) when chans==loopChans or
  mono+shift0; mono splat/gather by voice; multichan wrap-gather. Non-voice values
  splat (mono) / wrap-load (chans>=w) / element-gather (narrower). Key fix: a
  constant/control in the voice graph is NOT voice-local (needs `_isInstVar ||
  isTempVarCut`), else its channel wrap is dropped.
- noteParam (`_noteParamSimd`): per-voice gather from voicer_params (splat when all
  lanes share a voice + mono param).
- index helpers `_flatSimdVoiceIdx` / `_flatSimdBufIdx` / `_flatSimdVarWrapIdx`
  (mirror flatSimdVoiceIdx / flatSimdBufIdx).
- per-voice delays: 1-sample contiguous load/store + splat/gather/scatter
  (`_voiceDelayFixReadSimd` / `_voiceDelayWriteSimd`); ring gather/scatter with
  per-voice wrpos/mask; variable-read interpolation (`_voiceDelayVarReadSimd`).
  `_genSimdInterp` was generalized to take per-lane bufs/wrposs/masks arrays so the
  non-flat and flat-voice gathers share it.
Verified: the full voicer corpus (7) + all 10 instrument synthdefs (organ, fmBell,
fmBrass, sawLead, pwmPad, pluck, modalBell, kick, snare, subBass) byte-match at
width 4; new `synthc_voicer_simd_diff.x` (7 synths incl. an rng-free cubic-vread
voicer that exercises the per-voice interp lambda) in the FFI harness (30/30);
scalar voicer diff + 383 lang tests + render A/B unchanged.

M5.3 COMPLETE.

M5.4 (SIMD buffers + corpus sweep) DONE. Buffer ops were un-disqualified and given
SIMD codegen mirroring the C++ Buf* visitors:
- BufFixRead (`_bufFixReadExpr`): 1-chan splat; multichannel per-lane gather
  (`bp ? bp->data[(start+ci)&(chans-1)][index & mask] : 0.0`, ci = `_simdLaneChanIdx`).
- BufVarRead (`_bufVarReadExpr`): scalar-offset 1-chan splat; else a per-lane gather
  wrapped in a pointer-caching lambda `[&]() -> Tx { auto _b = bp; return Tx{...}; }()`
  (all five interp kernels; `_bufGenRead` shared with the scalar path).
- BufLength: splat the guarded scalar.
- BufWrite: NOT special-cased -- the C++ BufWrite visitor emits the scalar-form store
  with vector operands inside a SIMD loop, so synthc's existing `_bufWriteStmt` already
  byte-matches once un-disqualified (verified bw4).
Helpers: `_bufSimdChanExpr` (lane channel wrap), `_bufGenRead`. The read/length/write
nodes were re-disqualified to `false` in `_exprSimdDisqualified`.
Corpus sweep (width 4 AND width 2 -- the width-2 decision branch the width-4 oracle
never reaches): the buffer corpus (fixed/var/length/write, all kernels, 1/2/4 chans),
the M5.1-M5.3 simple/delay/voicer forms, 6 example_synthdefs, and the 10 instrument
synthdefs all byte-match at both widths. New `synthc_buffer_simd_diff.x` in the FFI
harness (33/33). (apverbTest is excluded -- it uses `vec_join`, which the front-end
does not expose; it errors at width 0 too, unrelated to SIMD.)

M5.4 COMPLETE.

M5.5 (switchover) DONE. synthc is now the production compiler:
- `compileToCpp` (compile.x) defaults to the production config -- rewrites ON + SIMD
  width 4 (= the C++ compiler's max=min=4 defaults; synthc never emits width-2 at
  cgSimdWidth=4, so production stays scalar on 2-channel loops just like the C++).
  `defSynthX` therefore emits production-grade SIMD.
- Spectral guard: the C++ SIMD-vectorizes the per-bin spectral frame-body loop into
  an illegal lvalue-cast store (`(f32x4)(vN) = ...`) that never compiles -- so its
  spectral dylib never builds and there is no valid SIMD reference. synthc, as the
  production compiler, must actually render, so `_isSpectralBodyGraph` keeps spectral
  frame-body loops scalar at any width (added to `_loopHasSimdDisqualifier`). Spectral
  w0==w4 now, and the spectral render is non-silent + crash-free.
- The switchover: `instrument_synthdefs.x` (the real production corpus) now imports
  synthc.compile and compiles all 10 instruments via `defSynthX` -- verified loading
  it compiles every instrument "successfully (synthc)". `defSynth` (sexpr -> C++) +
  the toLisp serializer + the synthdefGenCppFromSexpr/synthdefAnalysisDump FFIs are
  RETAINED as the diff-test oracle (doc-marked legacy in synthdef.x).
- Validation: a new `synthc_prod_diff.x` byte-matches synthc's PRODUCTION output
  (rewrites-on + width-4) against the C++ at the same config across the 10 instruments
  + an arith + delay synth (the other SIMD diff suites are rewrites-OFF; this is the
  only one at the true production config). In the FFI harness (36/36). Render A/B
  (audio/control/delay/bubbles/rng/cf bit-identical + spectral non-silent) + 383 lang
  tests green. (A voicer render A/B was attempted but dropped: scheduled noteOn does
  not drive voices in the --nrt harness -- an engine NRT-event limitation orthogonal
  to synthc; the production-config byte-match is the stronger, deterministic proof.)
- Benchmark (codegen only, clang excluded; clang ~600 ms/synth dominates both):
  synthc import+analyze+genCpp ~18 ms/synth (interpreted) vs the C++ codegen
  ~0.5 ms/synth -- synthc ~35x slower at pure codegen, but that is ~3% of the total
  per-synth compile time, so the switchover adds ~3% (~18 ms) per synth. Negligible.

M5 COMPLETE.

---

Original scoping. M1-M4 are DONE: the Tzopilotl-hosted compiler
(`lang/modules/synthc/`) byte-matches the C++ `synthdef-compiler/` across the
whole corpus -- constants, ops, controls, delays, multichannel, rewrite engine,
vec/reduce, RNG, mergeDelays, buffers, control flow, voicers (per-voice delays /
RNG / multichannel / ring buffers), and spectral chains -- validated by 9 Tier-1
dump + Tier-2 codegen diff suites in `test_synthdef_compiler_ffi` (24/24,
deterministic) and the Tier-3 NRT render A/B for the M1-M3 corpus.

M5 is the last milestone: **SIMD code generation**, plus three smaller items that
finish the end-to-end pipeline -- rewrites-on in `defSynthX`, the spectral
teardown crash fix, and the `defSynth` -> `defSynthX` switchover. This doc records
the survey findings so the work can proceed without re-deriving the C++ behavior.

---

## The headline: M5 is one big thing (SIMD) plus several small ones

- **SIMD codegen is the milestone** -- ~1300 lines of intricate conditional
  emission in `synthdef_cpp_codegen.cpp` to port into `codegen.x` (currently
  scalar-only, `simdWidth()` hardwired to 0). Everything else is small.
- **Rewrites-on in `defSynthX`**: one line (`importGraph(name, true)`). The
  rewrite engine is already built and Tier-2-validated with rewrites ON
  (`synthc_rewrite_diff.x`).
- **The switchover**: ~2 lines -- `defSynth` (synthdef.x) is a drop-in for
  `defSynthX` (same `(GraphFn, String) String` signature, same engine
  registration). The S-expression path stays as the diff-test oracle bridge.
- **The spectral teardown crash is NOT what it looked like** (see below) -- it's a
  pre-existing ENGINE double-uninit bug, small to fix, independent of synthc.

## The spectral crash -- root-caused (overturns the M4.5 note)

Diagnosis: `engine::Node::~Node()` calls BOTH `funs.uninit(synth)` AND
`funs.free(synth)`, and the generated `_free` itself calls `_uninit` -- so
`_uninit` runs **twice**. A synth whose `_uninit` releases a resource without
nulling it then double-frees. Spectral `_uninit` does
`tzpl_fft_destroy(p->specN_fftsetup)` (+ frees inbuf/outbuf) without nulling, so
the second `tzpl_fft_destroy` reads a stale `JscsFFTSetup` and hands vDSP a
garbage pointer -> `EXC_BAD_ACCESS` (the varying 0x107/0x291/0x30a addresses are
recycled heap).

Key corrections to the M4.5 assumption:
- It is **engine-side and generic**, not synthc-specific. The C++ compiler's
  generated synths would double-free too.
- Delay synths survive because `genDelayDealloc` nulls after free
  (`free(p->dN); p->dN = nullptr;`); built-in nodes survive because their
  `_uninit` is empty. Spectral is the only `_uninit` that frees without nulling.
- "`defSynth` (the C++ ref) tears down fine" was a **confound**: the C++
  compiler's SIMD spectral codegen emits INVALID C++ (`(f32x4)(v28) = ...`, an
  illegal lvalue cast assignment) when it SIMD-vectorizes the frame-copy loop, so
  the ref dylib never builds, never registers, and is never freed. It doesn't
  crash because it never runs.

Locations: `engine/src/tzpl_node.cpp:156` (`uninit();`) + `:172`
(`funs.free(synth);`); generated `_free` -> `_uninit` at
`synthdef_cpp_codegen.cpp:2669-2670`; non-null teardown in `genSpectralFree`
(synthc) / `tzpl_fft_destroy` (`shared/tzpl_fft.hpp:61`, doesn't null
`s->vdspSetup`).

Fix direction (M5.0): (1) stop the engine double-invoking -- drop the explicit
`uninit()` call in `Node::~Node()` since `_free` contractually owns teardown
(audit: built-ins put teardown in `_free`/`delete`, so safe); plus (2) a cheap
safety net -- null `s->vdspSetup` in `tzpl_fft_destroy` and null the spec pointers
in synthc's `genSpectralFree` (mirroring `genDelayDealloc`). Side note: the C++
compiler's illegal SIMD-spectral cast is a real pre-existing bug to file, but it
just means **SIMD never applies to spectral** -- `SpectralChainExpr` is already in
the SIMD disqualifying list, so synthc keeps spectral scalar and there is no valid
oracle for SIMD-spectral anyway.

## The SIMD port (the bulk of M5)

The C++ SIMD codegen lives in `synthdef_cpp_codegen.cpp` (helpers :51-200, width
:440-492, genVarRef :575-765, the ExprCodegen/Rank1 visitor SIMD branches
:783-2045, loop emission :2198-2385, op strings :3584-3648). Shape:

- **Width decision** (`simdWidth(loop)`): width 4 if `totalCount % 4 == 0`, else 2
  if `% 2 == 0`, else 0 (scalar). Disabled for control-flow loops, `totalCount < 2`,
  and loops containing **disqualifying exprs**: vec reorder ops (transpose/rotate/
  reverse/stride/stutter/at/put/join/ncyc), CompareOp, Select, Reduce,
  SpectralChain/SpectralFrame, URand/BiRand/Rand64, Debug. Those loops stay scalar.
- **Remainder**: stride loop `for (i = 0; i < chans; i += width)` (Case B); when
  `chans == width`, a single vector op with no loop (Case A). No masked tail.
- **Types/helpers**: `f32x4`/`f64x2`/... (`{base}x{width}`); splat `(f32x4)(x)`;
  load/store `*(f32x4*)(ptr)`; gather `f32x4{a,b,c,d}`; lane index helpers
  (`simdLaneChanIdx`, `flatSimdVoiceIdx`, `flatSimdBufIdx`). The generated code
  calls into `shared/tzpl_simd.hpp` (types + math overloads) and
  `shared/tzpl_delay_interp.hpp` (interp kernels) -- both already shipped.
- **Per-construct paths**: scalar-in-SIMD -> splat; same-width array -> load;
  narrower -> gather/wrap; ops -> `simd_func(a,b)` / infix; delay fixed-read
  (contiguous load / per-lane ring gather); delay var-read (the per-lane
  interpolation lambda -- the hardest); delay write (contiguous store / per-lane
  scatter); flat-voice gather/scatter.

Plumbing into `codegen.x` is small and follows the voicer-dynvar precedent: a
`cgSimdWidth` dynvar set per loop, threaded through genLoop (Case A/B + stride),
genExpr/_inlineExpr (splat/load/op forms), and the index algebra. `genCpp(ctx,
name)` -> `genCpp(ctx, name, simdWidth)`; `defSynthX` passes width 4 to match the
C++ default.

## Staging

| Phase | Scope | Risk |
|---|---|---|
| **M5.0 End-to-end fixups** | DONE. Engine double-uninit fix (`tzpl_node.cpp` -- dropped redundant `uninit()`; the real engine-wide fix, no codegen change); rewrites-on in `defSynthX` (`compileToCpp` -> `importGraph(name, true)`); `simdWidth` param threaded through `genCpp`/`compileToCpp` (default 0); spectral end-to-end render guard in the A/B harness. Spectral renders crash-free; rewrites-on matches `defSynth`. | low |
| **M5.1 SIMD scaffolding** | DONE. Width decision (`_loopSimdWidth`), Case A (no loop) / Case B (stride) emission, SIMD op-strings, and the simple var-ref forms: inlet load/splat, const load/splat, sampleRate/Dur splat, control splat, cast `convert<T>`, ops, mono broadcast, outlet store. 9 cases byte-match at width 4 (`synthc_simd_diff.x`, in the FFI harness, 27/27). Delays/buffers/voicer/spectral are SIMD-disqualified (stay correct-scalar) until M5.2-M5.4. | **high** |
| **M5.2 SIMD delays** | DONE. 1-sample + ring delays: z1 contiguous load/store (dchans>=width), splat (dchans==1), or gather/scatter (narrower); ring per-lane gather/`_dwN`-scatter; `_simdLaneChanIdx` for lane channel + wrap. Variable-read interpolation: `_delayVarReadSimd`/`_genSimdInterp` (none/linear/cubic/lagrange/sinc, scalar + vector offset). All delay reads/writes un-disqualified. d4/d8/r4/r8/rfb + vc/vl/vg/vs/vn/sc/ss/sn byte-match (`synthc_simd_diff.x`, 22 cases, harness 27/27). Fixed a latent inline-struct-global GC crash uncovered by the scalar-offset control tests ([[lang_gc_inline_trailing_words]]). | **high** |
| **M5.3 SIMD flat-voice** | DONE. Flat-voice loop strides voices x chans (`_flatLoopSimdWidth`); voice-local SoA load/splat/gather + non-voice wrap var-refs; `_noteParamSimd` per-voice gather; per-voice delay SIMD (1-sample, ring gather/scatter, var-read interp via the shared per-lane `_genSimdInterp`). RNG/select voicers stay disqualified->scalar. Full voicer corpus + all 10 instrument synthdefs byte-match at width 4; `synthc_voicer_simd_diff.x` in the harness (30/30). (Spectral stays scalar -- disqualified.) | **high** |
| **M5.4 SIMD buffers + sweep** | DONE. BufFixRead/BufVarRead splat+gather (all interp kernels, pointer-caching lambda), BufLength splat; BufWrite stays the scalar-form store (vector operands) -- all un-disqualified. Sweep at width 4 AND width 2: buffer corpus + simple/delay/voicer forms + 6 example_synthdefs + 10 instrument synthdefs byte-match both widths. `synthc_buffer_simd_diff.x` in the harness (33/33). | med |
| **M5.5 Switchover + Tier-3** | DONE. `defSynthX` is the production compiler (rewrites-on + SIMD width 4); `instrument_synthdefs.x` switched to it (all 10 compile via synthc). Spectral frame-body loops kept scalar (`_isSpectralBodyGraph` -- the C++ SIMD spectral is invalid C++ that never builds). Production-config byte-match suite `synthc_prod_diff.x` (harness 36/36); render A/B + 383 lang tests green. `defSynth`/sexpr retained as the diff oracle (doc-marked legacy). Benchmark: synthc codegen ~18 ms/synth vs C++ ~0.5 ms, but clang (~600 ms/synth) dominates -> ~3% total overhead. Voicer render A/B dropped (NRT noteOn doesn't drive voices -- engine limitation; byte-match is the stronger proof). | med |

## M5.1 implementation notes (captured references + plan)

Done: SIMD op-string spellings (opnames.x). DONE = the bare-name vector overloads
(`sin` not `std::sin`, `min` not `std::min`, `sinpi` with no f32 suffix), threaded
via an optional `simd` flag on `genUnopStr`/`genBinopStr`.

Key simplification: the diff oracle is `synthdefGenCppFromSexpr(sexpr, 4, false)`,
which passes `max=min=4`, so `simdWidth()` only ever returns **4** (the width-2
branch needs `min<=2`, never true at width 4). So M5.1 targets **width 4 only**.

Width decision (`_loopSimdWidth(ctx, loop)`, mirror `simdWidth()`): return 0 if
`cgSimdWidth < 2`, `loop.isControlFlow`, `cgVoiceCount > 0` (flat-voice SIMD is
M5.3), `loop.chans < 2`, or any tree expr is disqualified; else 4 if
`loop.chans % 4 == 0`. Disqualified kinds: vecK EXCEPT take/drop (stride/stutter/
ncyc/reverse/transpose/rotate), compareopK, selectK, reduceK, spectralChainK,
spectralFrameK, urandK/birandK/rand64K, debugK.

Loop emission (genLoop, when `_loopSimdWidth>0`): emit `// SIMD 4x` after the LOOP
comment; **Case A** (`chans == width`): no `for`, emit trees at cel `"0"` (a
constant VIdx); **Case B** (`chans > width`): `for (usize i = 0; i < chans; i += 4)
{ ... }`, trees at cel `"i"`. Set a `cgSimdW` dynvar = the width while emitting.

Reference forms to byte-match (from `/tmp/simd_ref.out`, s4 = Case A 4ch, s8 =
Case B 8ch, both `inlet * const`):
- SIMD load (rvalue): `*(f32x4*)(ptr)` for a bare pointer (inlet:
  `*(f32x4*)(((f32**)p->inlets)[0])`); `*(f32x4*)(&arr[cel])` for an array
  (constant cN, inst var p->vN, local vN).
- Scalar-in-SIMD -> splat: `(f32x4)(scalarExpr)` (e.g. `(f32x4)(0.1f)`,
  sampleRate `(f32x4)(p->fs)`).
- SIMD store (lvalue): `*(f32x4*)(storePtr) = value`. The storePtr is the bare
  pointer when cel is the constant 0 (Case A: outlet `*(f32x4*)((f32**)p->outlets)[0]`,
  inlet `*(f32x4*)(&((f32**)p->inlets)[0][0])`) vs `&accessor[i]` when cel="i"
  (Case B: `*(f32x4*)&((f32**)p->outlets)[0][i]`). NB the Case-A-vs-B and
  inlet-vs-outlet pointer/address forms differ subtly -- thread cel as a VIdx and
  branch on `cel` being constant-0.
- Ops: `genUnopStr(op, isF32, arg, true)` / `genBinopStr(..., true)` (already
  implemented). E.g. `sin(simdval)`, `(a * b)`.
- Type name: `f32x4`/`f64x2`/`i32x4` = `<base>x<width>` (helper `_simdType(t, w)`).

These forms live in `genExpr`/`_inlineExpr` (loads/splats), `_varRef`/`_varName`
(store targets), `_sinkStmt`/`genTree` (the assignment store), gated on `cgSimdW > 0`.
The existing `VIdx` index machinery (vc/vs) already distinguishes constant-0 from
variable cels -- reuse it for the Case-A-vs-B store-pointer choice. M1 audio synths
(s4/s8) are the first byte-match targets; once they pass, sweep the audio-only
corpus, then M5.2 adds the delay SIMD paths.

## Highest-risk areas

1. **SIMD variable-delay interpolation** (`genSimdInterp`, ~250 LOC): per-lane tap
   gather, frac, and the sinc coefficient table lookup. Byte-matching the gather
   index expressions and the kernel calls is the single hardest port in M5.
2. **Flat-voice SIMD gather/scatter**: `flatSimdVoiceIdx`/`flatSimdBufIdx` across
   the SoA-flat vs ring-buffer cases, with the lane/voice/channel index algebra.
3. **The width decision + disqualifying-expr set**: getting any loop's SIMD-vs-
   scalar choice wrong diverges the whole loop. Must mirror `simdWidth()` exactly,
   including Case A (no loop) vs Case B (stride).
4. **Index algebra under SIMD**: the existing `VIdx` symbolic-index machinery must
   extend to wide/strided indices without breaking the scalar paths.

## Rough size

SIMD codegen: ~1000-1300 lines of `.x` (mirroring the C++ surface). The non-SIMD
items are tiny: rewrites-on (1 line), switchover (~2 lines), crash fix (~10 lines
across engine + headers + genSpectralFree). Shared SIMD/interp headers already
exist -- no runtime port, only emit references.

## What M5 does NOT need

- **SIMD for spectral / select / reduce / vec-reorder / RNG / control-flow**: all
  disqualified -- they stay scalar, so no SIMD port and no SIMD oracle for them.
- **A new runtime**: `tzpl_simd.hpp` / `tzpl_delay_interp.hpp` ship the vector
  types, math, and interpolation kernels; codegen only emits calls.
- **Sexpr removal**: the sexpr serializer + the `synthdefGenCppFromSexpr` /
  `synthdefAnalysisDump` FFI stay as the differential-test oracle (the 9 diff
  suites depend on them); only the production `defSynth` path stops using sexpr.

## Exit criteria

- Every corpus synth byte-matches the C++ compiler at SIMD width 4 (and 2 where
  applicable) with rewrites ON, via the diff suites.
- The full Tier-3 NRT render A/B (audio, control, delay, voicer, spectral) is
  bit-identical between `defSynthX` and the C++ `defSynth`.
- `defSynth` is the synthc pipeline; the C++ compiler is retired as the production
  path (kept only as the diff oracle).
- A benchmark of compile time (synthc vs C++) is recorded.
