# synthc M4 Plan -- Voicers + Spectral

> **Historical milestone notes** for the synthc port, retained as a development record.

Status: M4.1 DONE -- **all 8 non-ring-buffer target instruments byte-match**
(Tier-1 dump + Tier-2 codegen): organ, fmBell, fmBrass, sawLead, pwmPad, kick,
snare, subBass (plus sineVoice). Only pluck and modalBell remain, and both need
ring-buffer delays inside voices (combn / variable delays) -- that is M4.2.
Landed and byte-exact vs the C++ compiler:

- **Per-voice 1-sample delays** -- `voice_d<sn>[maxVoices*chans]` SoA data with
  per-voice `_wrpos`/`_mask[maxVoices]`; skipped from synth-wide delay decls/init/
  alloc; zeroed per noteOn (a channel loop for multichannel delays).
- **N-way select** -- the multi-line `sel(selector, std::array<T,N>{...})` envelope
  state machine (a latent pre-voicer gap: synthc emitted `/* FIXME select n-way */`
  for any adsr, voicer or not -- now fixed generally).
- **Multichannel voice bodies** (`flatVoiceChans > 1`) -- the flat loop runs
  `maxVoices * bodyChans` (phi loops use the already-voice-expanded chans); per-loop
  `flatChanShift`/`flatChanMask`; SoA indexing `[i>>shift]` (mono var), `[i]`
  (full-width), `[(i>>shift)*C + (i&(C-1))]` (partial); flat temps sized
  `[maxVoices*chans]`; the genTree scalar-local path is disabled in flat mode
  (mirrors the C++ `!inFlatVoiceMode`).
- **Per-voice RNG** -- `voice_rgen<g>[maxVoices]` (emitted first in the voicer
  block), seeded per voice in init + noteOn, indexed by voice `[i>>shift]`.

- **Constant power-of-two channel padding** -- a constant value vector is
  zero-extended to a power-of-two length (`padPow2`, using the `bitCeil` builtin),
  mirroring the C++ `VectorT::extend_to_next_power_of_two` that runs on every
  VectorT construction. This is what organ needed (a 7-partial harmonic vector ->
  8 with a trailing 0.0f, so `reduce_rows<8,1>` and the `& (chans-1)` masking stay
  valid). It is a GENERAL gap, not voicer-specific -- any non-power-of-two constant
  (e.g. a bare 7-element `sum`) was affected; earlier tests only used power-of-two
  vectors. Covered by new vec_diff cases sum7 / sum3.

`synthc_voicer_diff.x` covers 9 cases (tiny/tiny8/gateonly + sine_voice/adsr_synth
+ stereo_synth/noise_synth/stereo_noise + additive_synth); 382 lang tests + all
other diffs green.

M4.2 DONE: ring-buffer per-voice delays land pluck and modalBell, so **all 10
instrument synthdefs byte-match** (Tier-1 dump + Tier-2 codegen). Added: runtime
calloc'd and fixed inline per-voice rings (mono + multichannel), per-voice
alloc/free/wrpos/advance, ring read/`vread`/write indexed by the voice's write
head, and memset-on-noteOn for fixed rings. The per-voice delay-advance lines are
emitted in C++ `graph->delayBufs` (unordered_set, pointer-hash) order, which is
not derivable and semantically irrelevant -- `voicerCppCompare` order-normalizes
them, mirroring the antecedent-bracket sorting the dump comparators already use.
A C++ oracle bug was also fixed (`CompareOpExpr::update_type` computed the
narrowed operand type but never assigned it, leaving compare-output types
worklist-order-dependent -- any `tr`/trigger synth dumped non-deterministically);
the one-line fix makes the oracle deterministic with no blast radius (C++
self-test + all synthc diffs green).

M4.4 DONE: the spectral chain (`SpectralFrameInput` + `SpectralChainExpr`) is a
ControlFlowExpr with a subgraph body, so analysis reused M3 wholesale; the codegen
is the FFT frame loop (per-channel ring write/read; every hopSize samples: windowed
forward FFT -> body subgraph on the packed split-complex frame -> inverse FFT +
overlap-add), plus struct fields, init/uninit, and the `tzpl_fft.hpp` include. Tier-1
dump + Tier-2 codegen byte-match for identity / scale / stereo (the C++
spectral_identity/spectral_scale targets), wired into the FFI harness.

M4.5 DONE (Tier-1/2 parity + M4.3 verification). Findings:
- **Tier-1 + Tier-2 are the parity substance and are comprehensive**: 9 synthc
  diff suites (incl. voicer + spectral) run in `test_synthdef_compiler_ffi`,
  24/24, deterministic across repeated runs. This validates byte-exact analysis
  AND generated C++ for the whole corpus.
- **M4.3 (event-rate iso-groups spanning subgraphs) verified, no gap**: a
  `control()` consumed inside an `if_` branch (`ctrl_in_branch`,
  `two_ctrl_branch`) byte-matches dump + codegen -- the existing iso-group
  machinery already handles cross-graph activation. Added to the controlflow diff.
- **Tier-3 bit-identical render A/B is blocked for M4 features until M5.**
  `defSynth` (the C++ reference) compiles with SIMD + rewrites ON; synthc's
  `defSynthX` is scalar, rewrites-off. For M1-M3 synths SIMD/rewrites are FP
  no-ops, so the existing render A/B (audio/control/vdelay/bubbles/rng/cf) stays
  bit-identical. For voicers the flat-voice loop SIMD-vectorizes `sinosc` -> a
  1-ULP (5.96e-08) diff; for spectral the input is rewritten ((2x-1)*c -> ...) and
  the frame copy SIMD-vectorizes. Bit-identical Tier-3 for voicer/spectral
  therefore waits on M5 (SIMD emission) + rewrites-on in `defSynthX`.

KNOWN BUG (to fix): the `defSynthX` spectral end-to-end path crashes at teardown
in `tzpl_fft_destroy` (the FFT setup pointer is corrupted by teardown -- the
identical-structure `defSynth` spectral tears down fine, so it is specific to the
synthc load/engine path, not the byte-matched codegen). Spectral remains validated
at Tier-1/2; the end-to-end render is gated behind this crash + the SIMD parity gap.

The only remaining M4 voicer item is the AoS `voice_state` layout for genuinely
branching (if_-based) bodies -- no corpus user needs it. M4 is functionally
complete; what remains is M5 (SIMD emission, rewrites-on in defSynthX, full
Tier-3 render parity, and the defSynth->defSynthX switchover).

Note: synthetic ring/comb test synths must avoid `fadein` (and other f64-ramp +
f32-increment patterns) -- the C++ oracle infers that f32/f64 mix
non-deterministically (sometimes erroring), independent of synthc which is
deterministic. This is a second oracle type-inference non-determinism (the first,
in `CompareOpExpr::update_type`, was fixed in M4.2); the delay-feedback one is left
as-is and simply avoided in the test synths.

---

Earlier (M4.0). Status: M4.0 DONE (voicer spike). The flat-voice-mode voicer codegen byte-matches
the C++ compiler (Tier-1 dump + Tier-2 generated C++) for non-branching,
delay-free voice bodies -- `integration-tests/scripts/synthc_voicer_diff.x`
(tiny / tiny8 / gateonly), wired into the `test_synthdef_compiler_ffi` harness
(`test_synthc_voicer_diff`, 21/21). 382 lang tests pass; no regressions in the
analysis / rewrite / controlflow / vec / rng diffs.

What M4.0 landed: `voicerK(Int)` IR node (control-flow kind, body subgraph reused
from M3); the importer/types/passes wiring (shape = maxVoices * bodyRootChans via
`_voicerChans`, type via `_updateCF`); and the codegen -- `voicer.hpp` header,
per-voice `voice_v<serial>[maxVoices]` struct fields + `RowVoicer` / `voicer_params`,
flat-voice-mode body loops (`for (int i = 0; i < maxVoices; ++i)`), the
`noteParam -> p->voicer_params[i][serial]` gather (noteParam had NO codegen before),
per-voice init, and the five note-lifecycle functions + funs-table entries.

The three target unknowns are resolved byte-exactly: flat-voice-mode detection,
noteParam serial/gather, and per-voice naming (incl. the scalar-local vs
per-voice-array distinction -- a body value consumed within its own voice loop
stays a scalar `vN`, only cross-loop/inst-var values become `[i]` arrays).

Reclassification: `sineVoice` (the originally-named M4.0 exit synth) needs
**per-voice delay state** (its `fadein`/`lag`/`phasor` envelope+oscillator expand
to delayVar state machines -> `voice_d<sn>[maxVoices]` + `voice_d<sn>_mask`,
zeroed per noteOn). Its analysis dump already matches; only the per-voice delay
CODEGEN remains, which is squarely **M4.1**. M4.0's scope was correctly the
minimal flat-voice case.

---

Earlier scoping (M1/M2/M3 context). M1/M2/M3 are DONE -- the Tzopilotl-hosted compiler
(`lang/modules/synthc/`) byte-matches the C++ `synthdef-compiler/` across the
whole corpus it covers (constants, ops, controls, delays, multichannel, rewrite
engine, vec/reduce, RNG, mergeDelays, buffers, and control flow if/for/switch
with nested subgraphs), validated by Tier-1 dump diffs, Tier-2 source byte-match,
and Tier-3 NRT render A/B (rewrites off AND on).

M4 adds **polyphonic voicers** (`voicer` / `noteParam` / `gate`) and the
**spectral chain** (windowed FFT / IFFT overlap-add). This doc records the survey
findings so the work can proceed without re-deriving the C++ behavior.

---

## The headline finding: M4 is a voicer milestone, not a spectral one

The two M4 target corpora -- `lang/modules/instrument_synthdefs.x` (10 synths)
and `integration-tests/scripts/voicer_test.x` -- are **entirely voicer-driven and
use zero spectral**. Every one of the 10 instrument synths is
`voicer(maxVoices, fn(){ ... noteParam/gate ... outlet })`, and the bodies are
built from envelopes (`adsr`/`decay2`/`asr`), oscillators (`phasor`-based
`sinosc`/`lfsaw`/`smoothSquare`), and filters (`rlpf`/`lpf`/`bpf`/`ring`) -- all
of which expand to the **delayVar state machines + `if_`/`select2` control flow
that M1-M3 already compile**. So:

- The dominant, high-risk M4 work is the **voicer**: importing its subgraph body
  (cheap -- M3 machinery) and, above all, **per-voice state codegen** (the
  expensive part: ~445 lines of specialized C++ emission in the reference).
- **Spectral is small and orthogonal**: 2 self-contained node kinds, ~110 lines
  of codegen, and **no corpus users at all** -- its only validation is
  purpose-built tests mirroring the C++ `spectral_identity` / `spectral_scale`
  unit tests. It can be the last phase, or split out of M4 entirely (see
  "Sequencing decision" below).

## The key architectural insight

A `VoicerExpr` and a `SpectralChainExpr` are both `ControlFlowExpr`s with a
PhiNode-wrapped subgraph body -- structurally identical to the `if_`/`for_`/
`switch_` nodes M3 already imports, topo-sorts, cuts, and buckets per-graph. So
the **analysis** side of M4 is mostly reuse: a voicer body is a subgraph
(`graphOf` tag, `subs`, `phiK` wrapping, subgraph-aware `topologicalSortExprs`,
GraphCut::ControlFlow, per-graph audio-loop bucketing -- all built in M3).

The **new** work is concentrated in codegen, where a voicer replicates its body's
state across `maxVoices` voices. The C++ reference has two layouts:

- **SoA "flat voice mode"** -- chosen when the voice body has no branching;
  per-voice state becomes `voice_<x>[maxVoices*chans]` arrays and the body's
  audio loops run vectorized over voices.
- **AoS "voice_state"** -- chosen when the body branches (which the corpus
  envelopes do, via `if_`/`select2`); per-voice state is a `struct VoiceState`
  and a `for (v < maxVoices)` loop wraps the body.

Reproducing **both layouts byte-for-byte**, plus the 5 note-lifecycle ABI
functions, is the bulk of M4.

## Already done that M4 builds on (large)

- **M3 subgraph machinery** -- `_importSubgraph` / `_addCFNode` / `phiK` linking,
  subgraph-aware topo sort, GraphCut::ControlFlow, `addSubgraphAntecedents`,
  per-graph audio-loop bucketing, multi-graph dump (`GRAPH N`). A voicer body
  imports through this unchanged.
- **`noteParamK` already exists** in `ir.x` (NodeKind, ~line 144) -- defined in
  M2 but never exercised (no voicer context yet); needs only codegen.
- **The entire body vocabulary** the corpus needs -- delayVar init/read/vread/
  write, RNG (urand/birand), vec ops (at/sum/join), select2, all unary/binary/
  compare/cast ops, `if_` -- is M1-M3 and compiles today.
- **Front-end is fully formed**: `synthdef.x` `voicer()` (~:482), `noteParam()`
  (~:474), `gate()` (~:478), `spectralChain()` (~:487) all build the right
  `SignalExprKind`s. The importer simply stub-rejects them right now:

      importer.x:762  voicer(_, _):          _impError("voicer not yet supported by synthc (M4)")
      importer.x:763  spectralChain(_, _, _): _impError("spectralChain not yet supported (M4)")
      importer.x:764  spectralFrame(_):       _impError("spectralFrame not yet supported (M4)")

- **Runtime support is shipped** -- `shared/tzpl_voicer.hpp` (`RowVoicer`
  template: allocVoice, noteOn/Off, getRow, gate-as-param-0) and
  `shared/tzpl_fft.hpp` (`tzpl_fft_create`/`forward`/`inverse`,
  `tzpl_window_sqrt_hann`). Codegen only *emits references* to these; nothing to
  port.

## C++ IR we mirror

Voicer / note (synthdef_expr.hpp / .cpp):

- **VoicerExpr** (`expr.hpp:752`) -- `ControlFlowExpr`; rate from `voice_body`;
  `chans = maxVoices * voice_body->in0()->chans`; `get_subgraph(0)=body`;
  `insertPhiNodes()` registers body; GraphCut::ControlFlow (`synth.cpp:292`).
- **NoteParam** (`expr.hpp:204`) -- rate `audioSignalRate`; `serial = (name ==
  "gate" ? 0 : nextNoteParamSerialNo())` (`expr.cpp:51`); type fixed at ctor;
  GraphCut::Input (`synth.cpp:256`); `should_hash_cons == false`. Collected into
  `synth->noteParams` (`synth.cpp:108`).
- Per-voice RNG (`URand/BiRand/Rand64Expr`, `expr.hpp:1123+`) spawn
  `voice_rgen<graph>[maxVoices]` and are re-seeded per noteOn.

Spectral (synthdef_expr.hpp / .cpp, synthdef_builtin_ops.cpp:484):

- **SpectralFrameInput** (`expr.hpp:796`) -- rate audio; type f32;
  `chans = inputChans * fftSize` (packed split-complex); `chainSerial` back-ptr.
- **SpectralChainExpr** (`expr.hpp:830`) -- `ControlFlowExpr`; rate from input;
  type forced f32 (`expr.cpp:535`); `chans = input chans` (`expr.cpp:670`); body
  subgraph entered at the SpectralFrameInput; GraphCut::ControlFlow.

## Codegen we mirror (synthdef_cpp_codegen.cpp -- the bulk of M4)

Voicer (~445 lines total):

- **Struct fields**, two layouts (`:3050-3102`): SoA `voice_rgen[mv]`,
  `voice_<var>[mv*chans]`, `voice_d<sn>_wrpos[mv]`, `voice_d<sn>[...]`,
  `RowVoicer<mv,nUser> voicer`, `f32 voicer_params[mv][1+nUser]`; vs AoS
  `struct VoiceState{...}; VoiceState voice_state[mv];` + voicer + params.
- **flatVoiceMode heuristic** (`:368`) -- flat iff the voice body has no
  branching. Drives the whole layout + loop shape; must match exactly.
- **NoteParam access** (`:832-873`) -- gather `p->voicer_params[vi][serial]`
  (SIMD gather / scalar index / AoS row variants).
- **Voice loop** (`:1879-1913`) -- flat: vectorized `genLoops`; AoS: `for (v <
  mv)` per-voice loop over the body's audio loops.
- **DelayWrite in voice context** (`:1915-2019`) -- per-voice ring index, 3 modes.
- **Note lifecycle, 5 ABI fns** (`:3191-3304`): noteOn (voicer.noteOn, fill
  param defaults, zero per-voice vars + delay wrpos, reseed RNG), noteOff,
  allNotesOff, noteSetParams, noteSetParamRange. ABI fn-ptrs at
  `tzpl_plugin_abi.h:174-182`; `load()` wires them.
- **Init** (`:2680-2756`) -- memset `voicer_params`, `voicer.setParams`,
  per-voice delay alloc + RNG seed.

Spectral (~110 lines, self-contained):

- 10 struct fields per chain (`:3134`): `fftsetup`, `inbuf[ch]`, `outbuf[ch]`,
  `fftbuf`, `frame`, `window`, `wrpos`, `rdpos`, `hopcount`.
- Init (`:2516`): `tzpl_fft_create`, `calloc` ring bufs, `tzpl_window_sqrt_hann`;
  free in uninit (`:2639`).
- Audio loop (`:1771-1878`): ring write/read per sample; every `hopSize` samples
  window+forward-FFT per channel -> run body subgraph on the packed frame ->
  inverse-FFT + windowed overlap-add. `#include "tzpl_fft.hpp"` guard (`:3401`).

## Data-model changes (ir.x)

- New NodeKinds: `voicerK(Int)` (maxVoices), `spectralChainK(Int, Int)`
  (fftSize, hopSize), `spectralFrameK(Int)` (fftSize). `noteParamK` already
  exists.
- `isControlFlowKind` extended to include `voicerK` + `spectralChainK` (both are
  ControlFlowExpr: subgraph body + GraphCut::ControlFlow, so M3's import/topo/cut
  paths pick them up).
- Per-voicer-graph state needed by codegen: `maxVoices`, the `flatVoiceMode`
  flag, and the ordered `noteParams` list (serial 0 = gate). Most naturally hung
  off `GraphInfo` (the voicer body is a graph) + a synth-level noteParams array.

## Staging

| Phase | Scope | Risk |
|---|---|---|
| **M4.0 Voicer spike** | DONE. Minimal flat-voice voicer (delay-free, non-branching body): tiny / tiny8 / gateonly byte-match Tier-1 + Tier-2. Import via M3 subgraph path; SoA flat layout; 5 note fns + `load()` wiring; noteParam gather. Resolved flatVoiceMode detection, noteParam serial/gather, per-voice naming (scalar-local vs per-voice-array). | **high** |
| **M4.1 Voicer codegen parity** | DONE -- 8/8 non-ring-buffer instruments byte-match (organ, fmBell, fmBrass, sawLead, pwmPad, kick, snare, subBass + sineVoice): per-voice 1-sample delays, n-way select, per-voice RNG, multichannel voice bodies, constant power-of-two padding. AoS layout for branching bodies deferred (no corpus user). | **high** |
| **M4.2 Per-voice ring-buffer delays (pluck + modal bell)** | DONE -- 10/10 instruments byte-match. Per-voice ring buffers (runtime calloc'd + fixed inline, mono + multichannel): SoA fields, per-voice alloc/free/wrpos, ring read/`vread`/write indexed by the voice's head, memset-on-noteOn for fixed rings, per-voice wrpos advance. Advance-line order normalized in the comparator (C++ pointer-hash order); C++ oracle compare-type determinism fixed. | med |
| **M4.3 noteParam/gate + event parity** | DEFERRED into M4.5 verification. The iso-group / `processEvents` machinery is already fully implemented and working (M1-M3); NO corpus synth uses event-rate `control()` (voicers use noteParam/gate, a separate audio-rate mechanism). The "event-rate iso-groups spanning subgraphs" M3-tail is a theoretical, zero-coverage edge case -- folded into M4.5 as a quick synthetic cross-graph sanity check rather than a milestone. | low |
| **M4.4 Spectral** | DONE. `spectralFrameK(fft)` + `spectralChainK(fft,hop)` IR nodes (control-flow, reuses M3 subgraph/phi; frame is a cut-Graph source, chans = inputChans*fftSize). Codegen: the FFT frame loop (per-channel ring write/read, hop-triggered windowed forward FFT -> body subgraph on the packed frame -> inverse FFT + overlap-add), struct fields, init (`tzpl_fft_create` + calloc + sqrt-Hann window), uninit, `tzpl_fft.hpp` include. Tier-1 dump + Tier-2 codegen byte-match for identity / scale / stereo (`synthc_spectral_diff.x`, wired into `test_synthdef_compiler_ffi`). | low-med |
| **M4.5 Parity + harness** | Tier-1/2 parity COMPLETE: all 9 synthc diff suites (analysis, rewrite, vec, rng, buffer, mergedelays, controlflow, voicer, spectral) wired into `test_synthdef_compiler_ffi` (24/24, deterministic). M4.3 cross-graph iso-group case verified (ctrl_in_branch / two_ctrl_branch in controlflow diff). Tier-3 bit-identical render A/B passes for M1-M3 synths but is DEFERRED to M5 for voicer/spectral -- `defSynth` SIMD-vectorizes (e.g. the flat voice loop / frame copy) and rewrites, producing 1-ULP FP differences synthc (scalar, rewrites-off) can't match until M5 SIMD + rewrites-on parity. | -- |

## Highest-risk areas

1. **SoA-flat vs AoS-voice_state layout + per-voice array-ization** -- the
   largest, most detail-dense codegen surface (~445 lines) and the place
   Tier-2 byte-match is hardest. Both layouts must reproduce exactly.
2. **The flatVoiceMode heuristic** (body-has-no-branching). Get it wrong and the
   entire struct layout, loop shape, and note-reset code diverge at once. Nail it
   in M4.0 against a known C++ dump/source before scaling.
3. **Per-voice delay & RNG naming + noteOn reset** -- `voice_d<sn>_wrpos[vi]`,
   `voice_rgen<sn>[vi]`, zeroing vs constant-init-rate vars.
4. **Tier-3 for voicers** needs scheduled note events (noteOn/noteOff timeline),
   unlike the steady-state audio-only renders -- a new render harness shape.

## Rough size

Voicer: ~450-600 lines of `.x` codegen + ~80 import/types/ir. Spectral: ~150
lines. Total M4 ≈ 700-800 lines of `.x`. (Reference C++ voicer codegen ≈ 445
lines; spectral ≈ 110.)

## Sequencing decision (settled)

**Voicer first, spectral last.** Spectral (M4.4) has zero corpus coverage and is
orthogonal -- validated only by synthetic tests -- so the high-value, high-risk
voicer work (M4.0-M4.3) is front-loaded and unlocks all 10 instruments + the
voicer test before spectral lands. The staging table above already reflects this
order; M4.4 stays the final pre-parity phase and could slip to its own small
milestone without blocking voicer parity if scope pressure demands.

## Deferred to M5

SIMD voice vectorization (flat-mode lanes), the `defSynth` -> `defSynthX`
switchover, and benchmarking. No corpus synth uses `buffer` inside a voice, so
in-voice buffers stay deferred until a synth needs them.
