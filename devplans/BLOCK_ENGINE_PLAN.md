# Block Engine Plan: tensor signals, block processing, no Controls

**Branch:** `block-engine` (from `main` at 53f3c5f)
**Status:** proposal, 2026-10-25
**References:** `~/Dropbox/dev/tensor-ugens/ARCHITECTURE.md` (cited below as TU §n) and
the `tensor-ugens/` code.

---

## 0. Summary

Today the engine runs one sample at a time, and every synth's `processAudio()` computes
one frame. This keeps one-sample feedback between nodes, but SIMD can only run across
channels, so mono signals get none.

This plan moves the engine and synthc to **blocks** (default 64 samples), with:

- **Tensor signals.** A signal is a `Shape` (up to rank 4) of non-interleaved channel
  buffers, each holding a block of samples. Channel counts are no longer rounded to
  powers of two, and broadcasting uses the lcm of each dimension.
- **No Controls and no event rate.** A control is an ordinary input, so it can be
  patched, ramped and crossfaded. Individual elements or slices of an input tensor can
  be set. Rates are `const`, `init`, `reset` and `audio`.
- **Sample accuracy, with sub-sample fade starts.** It comes from two mechanisms:
  - Value changes (sets, ramps, impulses) are rendered into input buffers by the
    engine. Crossfades remain XFader nodes, which nest (§3.2).
  - Discrete events (node start and stop, voice reset) split the node's block at the
    event's offset.
- **Change detection to win back event-rate efficiency.** Parameter-only subgraphs are
  recomputed only when their source inputs change, or are not constant over the block.
- **synthc rewritten for block code.** Operations are fused into loops that each have a
  strategy: channel-vector, sample-vector or serial (see Terminology below).
  - Recurrences are still written as `delayVar` relations. synthc classifies each
    feedback cycle and derives its parallel form: scan, block state space, or a minimal
    scalar cycle.
  - A small runtime support header (vector types and vmath for i32, i64, f32 and f64,
    transposes, scans) is adapted from tensor-ugens.
- **The C++ synthdef-compiler is deprecated**, except for the compile-and-link step,
  which moves out of it.

The order is **correctness first**:
1. Port the engine to blocks.
2. Have synthc emit block-shaped code whose inner loop is still per sample, and get the
   whole system (bridge, music libs, live, app) working end to end.
3. Only then build recurrence analysis (scans, block state space) and the loop fusion
   optimiser.

### Terminology

A block of a signal has two axes: **channels** and **samples**. A loop is named by what
its SIMD vector holds:

| Term | A vector holds | Loop nesting | tensor-ugens name |
|---|---|---|---|
| **channel-vector loop** | the same sample of 4 channels | groups of 4 channels outside, samples inside | "across channels" |
| **sample-vector loop** | 4 consecutive samples of 1 channel | channels outside, vectors of samples inside | "along samples" |
| **serial loop** | one sample at a time; channels may still share vectors | samples outside | "per sample" / run1 |

Buffer layouts:
- **planar:** one buffer per channel. Sample-vector loops read it directly.
- **channel-interleaved:** for each group of 4 channels, sample-major, so that each
  vector is one sample of the 4. Channel-vector loops read it directly.

Other terms:
- A **channel group** is the 4 channels that share one channel vector.
- **Per sample** describes how often something happens (a value that changes every
  sample). It is not a loop kind.

---

## 1. Decisions needed before work starts

D1, D2, D7 and D10 are decided (2026-10-25). Each other item gives the plan's
recommendation, and the milestones assume it. Items marked ⚑ change the user-visible
semantics.

**D1 ⚑ Feedback between nodes (decided, in two phases).**
- Today a cycle between nodes is a one-sample delay. With blocks it becomes a one-block
  delay, unless the cycle runs a sample at a time.
- **Phase 1 (M2–M10): feed-forward only.**
  - A `connect` or `reconnectOutput` that would close a cycle is refused with
    `tzpl_errCyclicConnection` (already in `tzpl_SErr`, previously unused).
  - The check runs in NRT, on the graph shadow, before the bundle is submitted.
  - The whole bundle aborts, as on any other apply error.
- **Phase 2 (milestone MF): sample-interleaved feedback.**
  - The engine finds strongly connected components (SCCs) of nodes and runs each one
    sample-interleaved: `process(p, off, 1)` per node, per sample.
  - Every synth supports `n = 1` anyway (§4.3), so this keeps today's semantics at a cost
    paid only by cyclic patches.
  - A per-silo option, `feedbackMode = sample | block`, gives the cheap
    SuperCollider-style one-block delay instead.

**D2 ⚑ The note model: noteParams become a voice-parameter input tensor (decided).**
- A voiced synth gets an ordinary input port, `notes`, of shape `{maxVoices, 1+N}`.
  Column 0 is the gate.
- Voice allocation moves out of the generated code into the engine (one
  `tzpl_voicer.hpp` instance per voiced node).
- `noteOn` works in three steps:
  1. allocate a voice v;
  2. split the node at the onset offset and call `resetVoice(p, v)`;
  3. write row v of `notes` (gate = 1, params) as a step at that offset.
- `noteSetParams` is then just an element set, so per-voice glides come free with ramps.
- The plugin ABI loses `noteOn`, `noteOff`, `noteSetParams` and their variants, and
  gains `resetVoice`.
- The alternative is to keep note functions in the ABI with a sample offset. It is less
  uniform, and it duplicates the input stage machinery inside every plugin.

**D3 Recurrences stay in the notation; synthc finds their parallel forms.**
- `delayVar` is how the language writes a recurrence relation, and it stays that way.
  `common_ugens.x` keeps its mathematical definitions (phasor, lag, onepole, biquad,
  adsr). No opaque ugen primitives are added to the IR.
- synthc analyses each feedback cycle (a strongly connected component through delay
  reads) and chooses its implementation (§4.7):
  - **Outside the cycle:** coefficient design, shaping and scaling are ordinary
    elementwise code in sample-vector loops.
  - **Associative accumulation** (`s = frac(s(1) + x)`, running sums): a prefix scan.
  - **Affine in the state, with coefficients constant for the block:** block state space
    (TU §6.2), with the matrices derived automatically from the cycle body.
  - **First-order affine with audio-rate coefficients:** a scan over affine maps.
  - **Everything else** (higher-order with time-varying coefficients, nonlinear): a
    channel-vector loop when there are 4 or more channels or voices, otherwise a serial
    loop over the cycle alone.
- tensor-ugens is the **reference** for the code these forms should produce and the
  speed they should reach. It is not a library that synths call.

**D4 Block-constant inputs: runtime flags, values passed with a stride.**
- Any input can be patched at run time, so whether an input is constant for a block is
  only known at run time.
- Each input channel carries a `constant this block` flag and a version counter (§3.3).
- Values computed by a parameter group reach audio loops through a pointer plus stride,
  counted in vectors:
  - stride 0 for a value designed once per block;
  - stride 1 for a per-sample buffer.
  This is the TU wire scheme.
- **Constness is per channel, at two times.** Per-channel buffers make shape operations
  free at run time, because channel remappings are resolved at compile time. So any
  value can be a "scrambled Rubik's cube" of constant and audio channels: columns joined
  into a matrix where some are constants, then a rotated row; or a matrix with an
  audio-rate diagonal and constant zeroes elsewhere.
  - **Compile time:** a channel that resolves to a literal is known statically. It is
    folded per channel and never reaches run time (§4.2 step 1a).
  - **Run time:** inlet channels are constant or not individually, through per-element
    `setInput`, region faders, and inputs only partly patched. Whatever depends on them
    inherits that per channel.
- **Stride granularity is the consumer's vector, not the value:**
  - **Sample-vector consumer:** stride **per channel**. The buffer is planar. A constant
    channel is a **splat**, `{c, c, c, c}`.
  - **Channel-vector consumer:** stride **per channel group**. The buffer is
    channel-interleaved.
    - A group whose 4 channels are all constant has stride 0 and stores one **lane
      vector**, `{c0, c1, c2, c3}`.
    - A mixed group has stride 1. Its producer fills the constant lanes of every vector
      with their values, which is a cheap store.
  - Either way the inner loop is the same, `V x = *in; in += inc`, with no transpose and
    no branch, whatever the mix of constant and audio channels.
  - The producer writes the layout its consumer wants. The consumer's strategy is known
    at compile time (§4.2 step 6), so a transpose is paid at most once, on the producer's
    store: none when the producer is itself a channel-vector loop, one per vector when
    it is a sample-vector loop.
  - This is TU's per-wire stride scheme, with the transpose moved from the consumer's
    every vector to the producer's store.
- This avoids compiling every audio loop twice. Recurrences branch on the stride, as TU's
  `Filter_run` does, to choose block state space or per-sample design.

**D5 Device buffer sizes that are not a multiple of the block.**
- The engine processes each device buffer in chunks of up to B samples. The last chunk
  may be shorter.
- Synths already handle any `n` (vector body plus scalar tail), so no latency is added.

**D6 Block size.**
- Set when the engine is created (`EngineConfig::blockSize`, default 64, a multiple of 4,
  up to 1024). It is passed to synths in `SynthData::maxBlock` at `alloc` time, and the
  synth sizes its arena from it.
- Generated code never assumes a fixed n.

**D7 Shape layout: row-major (decided).**
- The first sketch of `Shape::strides()` had the order backwards (`s[0] = 1`).
- The decision is row-major, as in tensor-ugens: `cols`, the last dimension, has stride 1,
  and `strides()` counts down from `dims[3]`, matching `Shape_strides` and
  `broadcastChannel`.
- Also choose the element type order: the ABI today has `kI32=0, kF32=1, kI64=2, kF64=3`,
  while the proposed enum is `kI32, kI64, kF32, kF64`. *Recommendation:* adopt the new
  order in ABI v3. Nothing persisted depends on the old order.

**D8 Trigger inputs.**
- With event rate gone, a trigger input (`tr`, buttons) is set with an **impulse** input
  operation: the value at the offset, then 0 from the next sample. It is the same
  machinery as a step.

**D9 What happens to the C++ synthdef-compiler.**
- Freeze it at the branch point. `defSynth`, the s-expression path and the parity tests
  against it are removed on the branch.
- `synthdef_compile_link.cpp` (PCH, compile flags, dylib link) moves to
  `bridge/src/tzpl_synth_build.cpp`, because synthc depends on it.
- The `synthdef-compiler/` directory is deleted at merge (M10).

**D10 No MLIR or LLVM dependency (decided).**
- synthc keeps emitting C++ that the system clang compiles. The domain analyses (§4.2,
  §4.5, §4.7) are custom passes either way, and clang -O3 handles the low-level work on
  explicit vector code.
- The IR is designed to stay translatable: SSA values, explicit loop ops tagged with
  their strategy, regions for structured control flow and recurrences.
- Compile latency (from `defSynthX` until the synth is loaded) is tracked in the M8/M9
  benchmarks.

---

## 2. Data model

### 2.1 Shape, SignalType, Rate, ElemType (shared C header and synthdef.x)

```c
// shared/tzpl_plugin_abi.h (ABI v3)
#define TZPL_MAX_RANK 4
typedef struct tzpl_Shape { uint32_t dims[TZPL_MAX_RANK]; } tzpl_Shape;   // row-major (D7)
typedef enum { tzpl_constRate, tzpl_initRate, tzpl_resetRate, tzpl_audioRate } tzpl_Rate;
typedef enum { kI32, kI64, kF32, kF64 } tzpl_ElemType;
typedef struct tzpl_SignalType { tzpl_Shape shape; tzpl_ElemType elem; tzpl_Rate rate; } tzpl_SignalType;
```

- The C++ helpers from the request go in a new header, `shared/tzpl_shape.hpp`:
  constructors, `chans()`, `rank()`, `strides()`, `broadcast()`, and
  `broadcastChannel(src, out, w)`, ported from tensor-ugens.
- `synthdef.x` gets a matching `struct Shape { dims [Int] }` with the same operations.
- `type Chans`, `asChans` and `padPow2` are deleted.
- Constants become shaped tensors, with cyclic indexing per dimension.
- `rate` on a port is only informational. Every input is audio rate at the ABI level
  (D4). `const`, `init` and `reset` remain *internal* rates in synthc:
  - `const`: literals.
  - `init`: `sampleRate()`, `maxBlock`, and values computed once at `init`.
  - `reset`: values latched at `resetVoice` (sample bank lookup, per-voice init).
  - `block`: the internal placement level for parameter groups, with a run-time
    fallback to `audio` (§4.2, §4.5). It never appears on a port.

### 2.2 Ports and their metadata

- `tzpl_ControlDef`, `tzpl_ControlDispatchTable` and the controls arrays go away.
- `tzpl_ControlSpec` (lo, hi, init, warp, kind) moves onto **input port definitions** as
  optional UI metadata, so widgets and the plugin browser keep working:

```c
typedef struct tzpl_PortDef {
    const char* name; tzpl_SignalType type;
    int hasSpec; tzpl_ControlSpec spec;     // init = default value of an unconnected input
} tzpl_PortDef;
```

- An unconnected input holds its `spec.init` value (or 0), constant per channel.

---

## 3. Engine (`engine/`)

### 3.1 Buffers and the block loop

- Every port channel is a 64-byte-aligned buffer of `maxBlock` elements. Its
  `OutPort::chanBufs[c]` is allocated in the NRT node constructor from a per-silo pool.
- A synth sees, for each port, a pointer to an array of channel pointers:

```c
typedef struct tzpl_InPort  { void* const* chans; const uint8_t* isConst; const uint32_t* version; } tzpl_InPort;
typedef struct tzpl_OutPort { void* const* chans; } tzpl_OutPort;
```

**Connections are still zero-copy.**
- An input's channel pointer table points straight at the source output's channel
  buffers.
- Broadcasting (lcm, per-channel mapping) is pointer remapping, computed once at connect
  time, as TU `mapInputWires` does.
- `ChanAdapt`'s widening case disappears. **Narrowing** (fold-sum) and **fan-in**
  (mixing) still need hidden mixer nodes, which now sum blocks.

**`Silo::processBlock(T0, n)`** replaces the per-frame loop in `processFrames`:
1. `processRTCommands()`: NRT bundles, as today.
2. **Collect the block's events.** Gather every clock- or time-scheduled command due in
   `[T0, T0+n)`, plus RT task wakeups. Resolve each to a fractional sample time t in the
   block (`TempoClock::beatAtSample` once per command, not once per sample). Run RT tasks
   in time order; the commands they issue (such as `playNote`) carry the task's wake time.
3. **Apply each event**, in time order, to the state it targets:
   - Input changes go to an *input stage* (§3.2).
   - Node start and free events, and voice resets, go to the node's per-block event list
     (§3.4).
   - Graph edits take effect at their offsets: a new connection starts as a crossfade or
     cut at t.
4. Copy the hardware input: deinterleave the device frames into the audio-in node's
   channel buffers.
5. `sortNodes()` if needed. The graph is acyclic in phase 1 (D1); SCC detection comes in
   MF.
6. `runNodes()`, for each node in sorted order:
   - **render input stages:** for each input channel, write a ramp or impulse into the
     input's own buffer, or just repoint, and set `isConst` and `version`;
   - **process segments:** call `process(p, off, len)` for each segment between that
     node's discrete events;
   - **SCCs (MF):** a cyclic SCC runs sample-interleaved (D1 phase 2).
7. Taps (per block, per channel); interleave into the silo's `outbuf_`.
8. `sampleTime_ += n`. The GC heartbeat stays per block.

Then `mixDown`, the safety limiter (already block-based; it now reads channel buffers)
and the master meter.

### 3.2 Input stages and XFader nodes: the engine side of "no Controls"

There are two mechanisms, split by whether the operation has to compose.

**Input stages (per input channel, processed before the node runs).** These cover
constant values only:

| Stage | Buffer contents | isConst |
|---|---|---|
| `Const(v)` | not written; pointer to a 1-element splat buffer, or a filled block | 1 |
| `Connected` | the channel pointer is the source's channel buffer (zero-copy) | 0 |
| `Ramp(v0→v1, t0, dur, curve)` | rendered into the input's own buffer; reverts to `Const(v1)` at the end | 0 while ramping |
| `Impulse(v, t)` | v at t, else the previous const value | 0 for that block |

- A ramp started while another ramp is running starts from the current rendered value.
  It doesn't need to keep the old ramp alive, so stages don't need to nest.

**XFader nodes (kept from today, now block-based).** These cover every crossfade:
`connect`, `disconnect*`, `reconnectOutput`, `replaceNode`, and `setInputX` to a
constant.

- **They compose.** A crossfade started while another is still running uses the running
  fader's output as its "old" input, giving a crossfade of a crossfade, with three (or N)
  sources audible at once.
  - The existing finish logic carries over. When the inner fader completes, it hands its
    new source to whatever its output fed, which is now the outer fader's `in0`, and
    retires.
  - The topological sort orders nested faders like any other nodes.
- `XFadeTwo`, `XFadeIn`, `XFadeOut` and `XFadeSet` keep their roles. A fader's inputs are
  ordinary node inputs, so a constant endpoint is a `Const` stage on the fader's own
  input, replacing today's `norm = dst->dataBuffer_` trick.
- **Faders can cover a region.** Input channel pointer tables are per channel, and one
  table may point different channels at different outputs. A fader can therefore be
  shaped to a region, such as one element of a control tensor. The destination's table
  points that region's channels at the fader output, and the other channels keep their
  current sources.
  - Its `in0` table is the region's previous per-channel mapping, which may itself
    include another fader's output.
- **Fade starts are sub-sample.** The fader is given a fractional start time t0 (a double,
  in samples). The gain at sample i is `curve(clamp((i - t0)/dur))`, so a fade starting
  at t0 = 17.3 gives its first sample g(0.7/dur). No node split is needed, and ramps are
  evaluated the same way.
- The 7 curves from `tzpl_xfader.cpp` move into a `tzpl_fade_curves.hpp`, shared by
  faders and ramps.
- `fade()` becomes a sample-vector loop over channel buffers.
- A fader's output reports `isConst = 0` while it runs. When it retires, the destination
  reverts to the plain stage or connection, and that channel's `version` is bumped.

**Each stage change bumps the channel's `version`.** A const channel whose value changes
by a *step* renders as non-constant for the block containing the step, then reports
`isConst` with the new version from the next block on. Change detection (§4.5) keys on
`(isConst, version)`.

### 3.3 Public API changes (`tzpl_client_interface.hpp`)

**Removed:**
- `setControl` (every overload), `listDefControls`, `ControlDesc`, `errControlNotFound`;
- `noteOn`, `noteOff`, `noteSetParams` and `noteSetParamRange` in their ABI forms (they
  return as engine-side calls, below);
- the event-rate parts of `DefDesc`.

**Added or changed:**
- `setInput(node, port | portName, Region, span<T> values, rampTime = 0, curve = Linear)`.
  - `Region` is a box: `offset[4]` plus `extent[4]` in shape coordinates, with helpers
    for a whole port, one element, and a slice of the last dimension.
  - `values` are broadcast cyclically over the region.
  - `rampTime > 0` ramps from the current value.
- `setInputX(... , xfadeTime, curve)` keeps its meaning: crossfade from the current stage
  to a new constant.
- `impulseInput(node, port, Region, values)` (D8).
- Every `connect` / `disconnect*` / `reconnectOutput` / `replaceNode` keeps its xfade
  arguments, still implemented by XFader nodes (§3.2), which nest. `connect` may take a
  destination `Region`, so a signal can be patched into one element of a control tensor,
  with or without a fade.
- **Notes, engine-side** (D2):
  - `noteOn(node, noteID, span<f32> params)`;
  - `noteOff(node, noteID)`;
  - `allNotesOff`;
  - `noteSetParams(node, noteID, first, span<f32>, rampTime = 0)`.
  These become voicer operations plus `notes`-row input stages. The voicer lives in the
  engine's RT node.
- `PortDesc` gains `shape` and `spec`. `DefDesc` gains `voiced` and `maxVoices`.
  `tzpl_SErr` drops nothing (it is append-only), but `errControlNotFound` is retired.

**Bundles:** `BundleOp` and `bundles.x`'s `EngineCmd` lose `setControl` and
`setControlNamed`. `setInput` gains its region, ramp and impulse fields.

### 3.4 Discrete events and node splitting

- A node keeps a small RT array of `(offset, kind, arg)` events for the current block:
  - `start`: a new node's first block starts at its offset; the output is zero before it.
  - `free`: the node runs up to the offset, and the output is zero after it.
  - `resetVoice(v)`.
  - `reset`.
- `runNodes` walks the events, calls `process(p, segStart, segLen)` between them, and
  applies each event at its boundary.

### 3.5 Things that move from per sample to per block

| Thing | Today | Block engine |
|---|---|---|
| TempoClock `process` | each sample, if the queue is non-empty | once per block: pop everything due before T0+n, with offsets |
| `SiloTaskScheduler::tick` and `tickActors` | each sample | once per block; wake tasks in time order with `sampleTime_` set to each task's wake time; the commands they issue carry it |
| Taps (peak, RMS, scope) | each sample, interleaved | each block, per channel buffer; scope FIFO stays interleaved for readers |
| Hardware I/O memcpy | each frame | deinterleave and interleave once per block |
| Legacy `sched_` / `streamTime_` path | unreachable | delete |

### 3.6 Plugin ABI v3 (`shared/tzpl_plugin_abi.h`)

```c
#define TZPL_PLUGIN_ABI_VERSION 3
typedef struct tzpl_SynthFuns {
    struct tzpl_SynthData* (*alloc)(int32_t maxBlock);
    tzpl_SErr (*free)(struct tzpl_SynthData*);
    tzpl_SErr (*init)(struct tzpl_SynthData*);
    tzpl_SErr (*uninit)(struct tzpl_SynthData*);
    tzpl_SErr (*reset)(struct tzpl_SynthData*);
    void      (*process)(struct tzpl_SynthData*, int32_t offset, int32_t n);   // any n in [1, maxBlock-offset]
    tzpl_SErr (*resetVoice)(struct tzpl_SynthData*, int32_t voice);            // NULL if not voiced
    struct tzpl_Buffer* (*swapBuffer)(struct tzpl_SynthData*, int64_t bufID, struct tzpl_Buffer*);
} tzpl_SynthFuns;
typedef struct tzpl_SynthData {
    tzpl_SynthFuns funs; struct tzpl_Engine* engine; struct tzpl_Node* node;
    int32_t num_ins, num_outs, maxBlock;
    tzpl_InPort* ins; tzpl_OutPort* outs;
    double fs, sd;
} tzpl_SynthData;
```

- The loader refuses `abi_version < 3`. Old dylibs in caches have to be rebuilt, which
  the plugin cache's revision check already handles.
- `tzpl_Buffer` drops its power-of-two `chans` rounding.
- `swapSampleBank`, `loadBufferDefs`, `loadTags` and `tzpl_sharedInput` are unchanged.
- The `tzpl_SynthDef` port arrays gain a `voiced`/`maxVoices` field (or a `loadVoiceInfo`
  symbol; follow the rule in [[app-plugin-browser]] about never growing a by-value struct
  without a version bump; v3 is that bump).

### 3.7 Engine tests (new; there is no engine unit-test harness today)

`integration-tests/src/test_block_engine.cpp` drives an NRT engine with hand-written C++
test plugins (`tzpl_test_plugins.cpp`, rewritten for v3), covering:

- block sizes 1, 4, 64 and 256, and device chunks of 37 and 100 samples (D5);
- input stages: set, step, ramp and impulse at fractional offsets, each checked
  per sample against a closed-form reference; per-element and slice sets; xfades with
  every curve; **nested crossfades**: start A→B, then B→C halfway through, then C→D,
  and check every sample against the closed-form three-way and four-way mix and the
  clean retirement of the inner faders; region faders on one element of a 2×3 input;
- broadcasting connections at shapes {3}→{6}, {2,3}→{4,3}, and a narrowing fold;
- node start and free at offsets; voice reset at an offset;
- cycle refusal (D1 phase 1): a connect, a reconnect, and a bundle that closes a cycle
  are each refused with `tzpl_errCyclicConnection`, and the graph is left unchanged. Phase 2 (MF) adds
  these: a 2-node cycle with `feedbackMode=sample` must equal the old one-sample
  semantics, and `block` mode gives a one-block delay;
- silo RT task wakeups at sample offsets within a block (`playNote` from a task lands
  at the task's wake time), since the corpus can't render silo task scripts;
- multi-silo mixdown; taps; the safety limiter; engine stop clearing state (keeping the
  regressions in [[engine-stop-clears-buffers]] and [[engine-stopped-mode-sorted-list-uaf]]
  fixed).

---

## 4. synthc rewrite (`lang/modules/synthc/`, `lang/modules/synthdef.x`)

synthc stops being a byte-for-byte port of the C++ compiler. Its tests become
**equivalence of forms** and **reference renders** (§8), not text diffs against an oracle.

### 4.1 Front end (`synthdef.x`)

- `Shape` replaces `Chans` (§2.1). Every constructor takes a shape: `inlet`, `take`,
  `stride`, `reduce`, `transpose`, and the others. Broadcasting is lcm per dimension.
- `control(name, spec, shape)` stays as *syntax* but builds an ordinary `inlet` with a
  spec. `trigger`, `toggle` and `choice` are inlets whose spec kind sets the widget and
  the impulse/step behaviour.
- `noteParam` and `gate` declare columns of the voiced synth's `notes` input (D2).
- Deleted: `eventToAudio`, `Rate.event`, `asChans` and `padPow2`, and `defSynth` (the
  legacy C++ path).
- No new expression kinds for DSP. Ugens remain Tzopilotl definitions over `delayVar`
  (D3).

### 4.2 IR and passes

`Ctx`'s `chans [Int]` becomes `shape [Shape]`.

#### IR model: a signal flow graph with delay-weighted edges

`SignalExpr` graphs are already close to a sea-of-nodes IR:
- pure ops whose edges are only data dependencies, with no instruction order;
- value numbering through hash-consing at construction (`importer.x:544`);
- peephole folding and rewriting as nodes are built;
- scheduling decided late (`cutGraphToTrees`, `treesToLoops`).

The block compiler keeps all of that and changes two things.

**1. Delays become explicit edges.**
- *Today:* state flows through side-effecting delay writer and reader nodes. A read has
  no edge to its writer, so the graph is a DAG, and order is recovered afterwards as
  "write after reads" antecedents (pass 13). Feedback is implicit and has to be
  rediscovered.
- *Block IR:* a read `d(k)` is an **edge from the value written to the delay, with delay
  weight k**. The graph becomes the classic signal flow graph with delays on edges
  (synchronous dataflow, Leiserson–Saxe retiming), and cycles are real cycles:
  - **The delay node** keeps only storage facts: ring size, initial contents, `maxDelay`,
    element type. It becomes an annotation on the edge set of one written value.
  - **Feedback cycles** are the SCCs of the graph.
  - **A cycle's delay weight** is the sum of the weights around it. It decides vector
    feasibility directly: weight ≥ `kVecLen` can run a vector at a time, weight 1–3 must
    run in a serial loop.
  - **Weight 0 cannot occur.** A zero-weight cycle is a zero-delay feedback loop, which
    is already an error at graph build (`synthdef.x:859-871`) and is now also checked
    structurally.
  - **Variable reads** (`vread(idx)`) get weight equal to a static lower bound of `idx`.
    It comes from interval analysis of the index expression (clamps, `max(…, k)`,
    constants), and defaults to 1 when nothing is known.
  - **Recurrence classification** (§4.7) is a walk around a cycle.
  - **Retiming** (moving delay weight across nodes to raise a cycle's per-vector slack,
    or to shorten a cycle) becomes available as a later optimisation.
  - **Ordering** of writer and readers falls out of the edges, so pass 13
    (`addDelayAntecedents`) goes away.
  - **Feedforward small delays** (`x - x(1)`) are edges of weight 1 that lie on no
    cycle. They run in block form.
  - **The front end is unchanged.** `delayVar`, `<-`, `d(k)`, `init` and `vread` build the
    same graphs; the importer turns writer and reader pairs into weighted edges.

**2. Rates become a schedule lattice with a per-block level.**
- Rate inference is already global code motion: "a node's rate is the max of its
  inputs'" places each node in the outermost loop where its inputs are available
  (schedule-early). The rewriter's lower-rate reassociations are loop-invariant code
  motion.
- The block compiler makes this explicit as a placement lattice:

  `const` (compile time) < `init` (once) < `reset` (per voice onset) < `block` (per block or segment) < `audio` (per sample)

  - `block` is new. A node is placed at `block` when its inputs are all at `block` or
    lower, where inlets are *nominally* `block`.
  - Each `block`-placed region is a **parameter group** (§4.5), compiled with its
    run-time fallback to `audio`. The fallback is used when any source channel reports
    `isConst = 0`, and its values then flow with stride 1 rather than 0 (D4).
  - Parameter groups stop being a separate analysis. They are the nodes that float to
    `block`.
- Within `audio`, placement into loops (strategy and fusion) is the schedule-late half
  of global code motion: put each node in the loop that saves the most, per TU §10.

**What stays structured.** `if_`, `switch_`, `for_`, `voicer` and `spectralChain` keep
nested subgraphs with phi roots, which are regions in the MLIR sense. They do not become
floating control edges. That suits DSP, and gives fusion clean boundaries.

**Prior art: Faust.** Faust's signal IR is hash-consed signal expressions with explicit
recursion groups. Its vector mode (`-vec`) splits a block into separate loops, keeping
recursive loops scalar and vectorizing the rest: the "minimal cycle plus vectorized
inputs" strategy of §4.7's General class. Before M7 and M8, read Faust's signal
normalisation, recursion-group handling and `-vec` loop separation and scheduling. Its
papers are on automatic vectorization and parallelization. What this plan adds beyond
it: scans and block state space for recurrences, channel-vector loops with transposes,
voices on the channel axis, and run-time constancy fallbacks.

#### Passes

Kept, with shape and rate updates:
- import, folding, hash-consing and the rewriter (its "lower rate" rules use the new
  4-rate order);
- mergeDelays, consumers. The topological sort now ignores weighted edges, so it orders
  the graph with its delay edges cut;
- type inference with widening (keep [[synthdef-widen-on-conflict]]);
- default types; delay length calculation.

New or rewritten:

0. **Delay edges:** the importer converts delay writer and reader pairs into weighted
   edges, and checks for zero-weight cycles. Interval analysis bounds `vread` indices.
1. **Shape inference:** the lcm rule, and views (take, drop, stride, transpose, reshape,
   rotate, join) become index maps resolved to per-channel source tables, not loop index
   arithmetic.
   - **1a. Per-channel provenance and folding.** After resolution, each channel of each
     value is either a compile-time constant or a channel of a computed node.
     Elementwise ops fold **per channel**: a channel whose inputs are all constants
     becomes a constant, and identities (`x*0`, `x*1`, `x+0`) simplify per channel.
   - Structure is exploited automatically. A matmul by a matrix with an audio diagonal
     and zeroes elsewhere keeps only the diagonal multiplies, and a `sum` over partly
     constant channels adds only the live ones plus one folded constant.
   - Only channels that remain non-constant generate loop work.
   - Each channel also records its **run-time source set**: the inlet channels it
     depends on, computed through the same maps. Parameter groups (§4.5) use it.
2. **Placement (rate) inference:** schedule-early over
   `const < init < reset < block < audio`. Inlets are nominally `block`.
3. **Parameter groups:** the `block`-placed regions, keyed by their source sets. They
   replace iso-groups (§4.5).
4. **Feedback cycles:** the SCCs of the weighted graph.
   - A cycle with total weight ≥ `kVecLen` is vector-feasible as it stands.
   - A cycle with weight 1–3 is a recurrence.
   - Each recurrence is cut down to its **minimal cycle**. Any node not on a path from
     the cycle back to itself is moved out to ordinary elementwise code.
5. **Recurrence classification** (§4.7): scan, affine block state space, affine scan, or
   general.
6. **Strategy assignment:** channel-vector, sample-vector or serial for every cycle, from its class,
   its channel count, the stride of its coefficients, and a cost table (TU §10).
   Elementwise ops have no strategy of their own; they follow a neighbour.
   - **Channel grouping.** Channel groups need not be contiguous channels, since every
     channel is reached through a pointer.
   - The compiler forms groups of 4 from channels with the same work. These are
     non-constant channels of the same op chain, preferring channels whose run-time
     source sets coincide, so their strides tend to agree.
   - Compile-time constant channels are never placed in a group. When a leftover group
     must hold one, as a spare lane, it is a register splat blended into that lane.
7. **Fusion (trees to loops)** is schedule-late placement within `audio`. Split loops at
   a change of strategy, a non-absorbable channel remap (reduce, matmul), a serial
   region, a weighted edge shorter than `kVecLen` read in its writer's loop, or a
   register-budget overflow. Fan-out within a loop is a local variable.
8. **Buffer arena:** interval colouring over loop order (TU §4, §12), with in-place reuse
   rules. One arena per instance, `maxBlock` deep.
   - Each inter-loop buffer gets a layout, planar or channel-interleaved,
     chosen by its consumer's strategy (D4).
   - A value with consumers of both strategies gets the layout that costs fewer
     transposes, or both layouts when it is cheap.
9. splitRates: `init`, `reset` and `block` (parameter group) loops, plus `audio` loops.

### 4.3 Code shape

```
void NAME_process(NAME* p, i32 off, i32 n) {
    // parameter groups (§4.5): recompute or reuse; results as {ptr, stride}
    // loop A: channel-vector | sample-vector | serial; vector body over n & ~3, scalar tail over n & 3
    // loop B: ...
    // advance delay write heads by n
}
```

- **Every loop has a scalar tail** that uses the serial form of each op, with the
  same state layout (lane k of a group is channel k; TU §6.5). `n = 1` is just the tail.
- **Channel-vector loops** transpose planar input blocks in and planar output blocks out
  (`transpose4`). Channel-interleaved buffers need no transpose.
  Spare lanes repeat channel 0 and drop their outputs.
- **Element types:** every type has a 4-lane vector. Masks are widened at type changes,
  integers wrap (`-fwrapv` or unsigned vectors), and float-to-int conversion is defined
  with a clamp (TU §9).
- **Compile flags:** keep `-O3 -ffast-math -fno-finite-math-only`, and add
  `-Wdouble-promotion -Wnan-infinity-disabled` for generated code (TU §7, §11). There
  must be no NaN sentinels in generated code or the support library.

### 4.4 Delays, buffers, control flow, spectral

**Delays**
- Ring buffers sized to `bitCeil(maxDelay + maxBlock + headroom)`, so that a whole block
  can be written before it is read.
- `d(k)` with k ≥ 4 is read a vector at a time. Smaller k is a register shift when it is
  feedforward, and a serial loop when it is feedback.
- `d(0)` semantics are unchanged ([[synthdef-d0-write-semantics]]).

**Buffers and banks**
- Reads are sample vectors, wrapping at the true length ([[tzpl-wavetable-osc]]).
- `bankLookup` stays at reset rate, and is now run by `resetVoice`.

**Control flow**
- `if_`, `switch_` and `for_` on an audio-rate condition run in serial loops.
- On a parameter-group condition they branch once per block or segment, which is new and
  cheap.
- `select` and `select2` are elementwise and vectorize.

**Spectral:** `spectralChain` already works on frames. Adapt it to read and write blocks.
Its hop is independent of B.

### 4.5 Parameter groups: change detection (replacing event rate and iso-groups)

- A **parameter group** is a maximal region placed at the `block` level of the schedule
  lattice (§4.2): nodes whose values depend only on inlets (and const, init or reset
  values), not on audio-rate internal state or on any weighted edge.
- **The decision is per output unit, not per group.** A unit is one channel when the
  consumer is a sample-vector loop, or one channel group when it is a channel-vector loop
  (D4).
- Each unit has a static **source set**: the inlet channels it depends on, from §4.2
  step 1a, including `notes` columns.
- At the top of `process`, for each unit, there are three cases:
  - **All sources `isConst`, and versions equal the cached ones:** skip. The cached
    splat or lane vector is still valid (stride 0).
  - **All sources `isConst`, but some version changed:** recompute that unit once and
    cache (stride 0).
  - **Otherwise:** compute that unit per sample into its arena buffer (stride 1).
- Ramping one element of a control tensor therefore recomputes only the units that
  depend on that element. The rest of the group stays cached.
- **Checking cheaply.** Source sets are compile-time bitmasks over the inlet channels.
  The engine keeps a per-input "changed or non-constant this block" bitmask, so a unit's
  check is one AND and test. The check is made once per block, never per sample.
- **Code size.** Each group's code is emitted once, over a unit index. The scalar
  (stride 0) and vector (stride 1) evaluations are its two forms, as in TU's run and
  run1. Units are dispatched by a small per-group loop over the units that need work.
- Downstream loops read `{ptr, stride}` per unit. A recurrence whose coefficients come from a
  group checks their stride and chooses a design-once path (block state space in a
  sample-vector loop) or a design-every-sample path (TU §6.2, §4.7).
- **Voiced synths:** a group whose sources include `notes` columns runs per voice, and
  the cache is per voice.

This regains the event-rate saving for the common case: knobs that sit still, and notes
whose params are constant while they sound. It also lets the same synth be driven at
audio rate with no recompile.

### 4.6 Voicers

- (voice, channel) is flattened into one axis and grouped in 4s. Channel-vector loops run 4
  voices at once, and per-voice filters fuse as per-channel ones do (TU §13).
- A group of 4 voices that are all idle is skipped. The engine-side voicer exposes an
  active mask in `SynthData`.
- `resetVoice(v)` zeroes voice v's state lanes and delay lanes, and runs the reset loops
  for v.
- **AoS mode** (control flow in the voice body) becomes a serial loop per voice in
  the first version. Optimising it is a later item.
- Keep the regression guards: non-power-of-two voice counts
  ([[synthdef-nonpow2-voicer-mask-bug]]), the `VecAt` voice offset, the voice-steal
  tiebreak, and the unused-noteParam sizing bug.

### 4.7 Recurrence analysis

Every cycle of weight 1–3 left after step 4 of §4.2 is a minimal recurrence
`s' = F(s, x)`. Here s is the values carried on the cycle's weighted edges (its state),
and x is the values that flow into the cycle from outside. synthc classifies F by walking the cycle body:

- **Linear forms.** For each node, track a linear form over the state variables:
  - a node that doesn't depend on s is a *coefficient* (an x);
  - `+` and `-` of linear forms, and a linear form times a coefficient, stay linear;
  - anything else is nonlinear.
- **Wrap.** `frac` (and `wrap(lo, hi)`) applied to the result of a pure accumulation is
  recognised as addition in a cyclic group.

The classes, in order of preference:

| Class | Condition | Sample-vector form | Channel-vector form |
|---|---|---|---|
| **Scan** | `s' = s ⊕ x`, with ⊕ associative: `+`, `+` then wrap, `max`, `min`, `xor` | lane prefix scan (`prefixSum` and its siblings), then wrap. Only the last lane carries over. This is how the phasor and its oscillators vectorize over samples, as in TU §6.3 | plain recurrence on transposed vectors |
| **Affine, block-constant coefficients** | F affine in s, with every coefficient from a stride-0 parameter group (§4.5) | block state space `y = Ys s + Yx x`, `s' = Ss s + Sx x`. The matrices are built automatically by evaluating the cycle body on unit states and unit inputs when the group recomputes. State steps through Ss² two vectors at a time (TU §6.2) | plain recurrence |
| **Affine, first order, varying coefficients** | `s' = a·s + b` with a and b at audio rate | scan over affine maps `(a, b)`, which compose associatively | plain recurrence |
| **General** | higher-order affine with varying coefficients, or nonlinear (`select` state machines such as `adsr`, `lfnoise`-style wraps that draw) | everything outside the cycle stays in sample-vector loops; the cycle runs as a serial loop over pre-computed vectors. This is TU's audio-rate filter design: design is most of the cost, and it vectorizes | plain recurrence, the default when there are 4 or more channels or voices |

**One cycle, two forms.** An affine cycle whose coefficients come from a parameter group
gets both forms, the block-constant one and the varying one. The run-time stride check
picks between them per block and per unit (D4): each channel of a sample-vector cycle,
each channel group of a channel-vector cycle. So a filter bank whose cutoffs are constant except one channel
ramping runs block state space on the steady channels and per-sample design on the
ramping one.

**Shared state layout.** The state layout is shared across forms, and with the scalar
tail: lane k of a group is channel k (TU §6.5). A cycle can therefore switch forms
between blocks, and segments of any length are exact continuations.

**Accuracy contracts.** Block state space and scans round differently from the plain
recurrence: about 1e-14 relative for f64 and 2e-5 for f32 (TU §7; f32 relaxed from 1e-5,
decided 2026-10-25, see M1). The equivalence tests
in M8 use these tolerances. A synth can ask for exact recurrence order with
`defSynthX(..., strictRecurrences: true)`, for reference renders.

**Diagnostics.** The analysis dump lists each cycle with its class, its chosen strategy,
and, when the class is General, the node that made it general. A missed optimisation is
then visible, not silent.

---

### 4.8 Tensor shape operations (axis arguments)

Today the shape operations (`VecOp` in `synthdef.x`: `take`, `drop`, `stride`, `stutter`,
`ncyc`, `rotate`, `reverse`, `transpose`, `join`, `reduce`, `at`, `put`) act on a
signal's row-major flattening and produce rows. With tensor signals they need an
**axis**:

- **Operations:** `take(n, axis)`, `drop(n, axis)`, `stride(k, axis)`,
  `stutter(k, axis)`, `slice(offset, length, axis)`, `reverse(axis)`,
  `rotate(amount, axis)`, `reduce(op, axis)`, `scan(op, axis)`; `join(axis)` concatenates
  along an axis, and `transpose` generalizes to a permutation of axes. `at`/`put` index
  along an axis. Also `stack(axis)` (signals of one shape along a new axis) and
  `reshape(dims)` (the same channels in a new shape, one dim may be −1).
- **Tensor dimensions rule every operation (decided 2026-10-26).** No operation reads a
  flat list of channels as rows and columns: the old fold `sum(n)` (channel j into
  j mod n) and `transpose(n)` (the flattening as an n-column matrix) are removed. A
  stereo mixdown comes from a real axis: a voicer of stereo voices is voices×2 and
  `sum(0)` mixes it; `[l, r] stack sum(cols)` mixes panned banks; `pandust` returns
  2×chans.
- **The `Int` argument is the axis (decided 2026-10-26):** `sum(axis)`, `take(n, axis)`,
  ...; `sum` with no argument reduces every channel to one (as NumPy's `axis=None`).
- **Axis numbering (decided 2026-10-26), outer to inner, zero-based, as in J and
  NumPy:** for a value of rank r, axis 0 is the outermost and axis r − 1 the last (the
  columns). Negative axes count from the last, as in NumPy: −1 is the columns, −2 the
  rows, −3 the planes, at any rank. Named constants are those negatives (`cols` = −1,
  `rows` = −2, `planes` = −3), so they name the same axis whatever the rank. (Cells, in
  J's sense, are numbered the other way, bottom-up; axes are not.) Broadcasting aligns
  trailing axes, so negative axis numbers name aligned axes of operands of different
  ranks.
- **Default axis:** −1, the last. A one-dimensional signal is a row, so every existing
  use keeps its meaning.
- **`Shape` gets an explicit `rank`** (decided 2026-10-26). Today rank is inferred from
  the first dimension that isn't 1, so `matrix(1, n)` and `row(n)` are the same shape.
  With a rank member, a 1×n matrix stays rank 2 (axis 0 is its single row): an axis
  outside −r..r − 1 is an error, and shape operations keep or change rank explicitly (`reduce` removes an axis,
  `join` along a new axis adds one). Broadcasting's result rank is the operands'
  largest. The plugin ABI's `tzpl_Shape` (4 dims only) gets the rank too (decided
  2026-10-26), for port metadata (UI shape labels, `tapShape`) and the engine's
  shape checks on connections: a layout change, made to ABI v3's structs before v3
  ships, with every plugin regenerated.
- **Shape inference** gives each result's shape from the operand's shape, the axis and
  the constant arguments (`take` shortens that axis to n, `reduce` removes it, ...).

**Static operations** (every one listed above except the two below) are resolved at
compile time:
- The rearranging ones (`take`, `drop`, `stride`, `stutter`, `slice` with a constant
  offset, `reverse`, `rotate` by a constant, `join`, `transpose`) are **index maps**:
  each output element is one source element. They emit no code. They fold into their
  consumers' per-channel source tables (§4.2 step 1), so a consumer reads the source
  channel directly, through its channel pointer.
- `reduce` and `scan` along an axis become elementwise op trees over that axis's
  elements (a scan as running partial results), folded per channel like any
  elementwise op (§4.2 step 1a: constant elements fold away).

**Dynamic operations:** `slice` with a varying offset (fixed length) and `rotate` with
a varying amount. Output element i reads source element `(offset + i) mod n` along the
axis.
- **Offset at block rate** (a parameter group): the consumer's per-channel source table
  becomes a table of channel pointers rebuilt when the group changes, and consumers read
  through it with no copy (D4: every channel is reached through a pointer).
- **Offset at audio rate:** a per-sample gather, in the serial form or as 4-lane gathers.

**Tests:** shape-inference goldens, including rank (a 1×n matrix stays rank 2); each
operation against a reference on the flattened layout (axis −1) and on explicit tensors
(axes 0 to r − 1 and −1 to −r); errors for an axis outside −r..r − 1; the corpus unchanged, since
existing uses act on rows.

## 5. Runtime support library (`shared/tzpl_dsp/`)

These are header-only pieces that generated code includes, adapted from tensor-ugens and
generalised to the four element types (TU Part II). They are building blocks, not ugens:

| File | Contents |
|---|---|
| `vec_types.hpp` | `i32x4`, `i64x4`, `f32x4`, `f64x4` and masks as clang `ext_vector_type`; `elem_t`, `bits_t` and `mask_t` traits; `transpose4`; portable min/max/select/reduce (TU §11 table) |
| `vmath.hpp` | One interface. Apple backend: `<simd/simd.h>` math. Elsewhere: Sleef inline headers (`d4` on AVX2; 2×`d2` on NEON; `f4`). Libm lane fallback for gaps such as `tanpi = sinpi/cospi`, `logb`, `nearbyint` |
| `scan.hpp` | Lane prefix scans for `+`, `max`, `min`, `xor`, and affine maps |
| `state_space.hpp` | Builds `Ys, Yx, Ss, Sx` (and `Ss²`, `Sx2`) for an N-state affine cycle from a callable step; the stepping loop |
| `random.hpp` | xoroshiro128++ `RandState<4>`, uniform floats with widths from the element type; independent lane streams for the sample-vector form of the existing random primitive |

- This replaces `shared/tzpl_simd.hpp` for generated code. The engine and lang keep
  using the old header until M10.

**The tensor-ugens UGens are the reference implementations.** Phasors, the filter
family, lags, decays, noises and lfnoise are kept as test oracles and benchmark
baselines in `benchmarks/tensor_ugens_ref/`. For each one, a synth written in the
notation (`common_ugens.x`) must:
- match the reference within the TU tolerance contracts;
- run within 1.25× of its ns/sample/channel (TU §8 table).

**Support library tests** (`shared/tzpl_dsp/tests/`, a C++ executable added to CI):
- scans against sequential sums;
- state space against the step function, for N = 1, 2 and 4;
- each element type;
- tolerance contracts of 1e-12 for f64 and 2e-5 for f32 (f32 decided 2026-10-25, see M1).

---

## 6. Bridge, libraries, app (API consumers)

The full inventory is in the survey notes in §11.

| Area | Change |
|---|---|
| `bridge/src/tzpl_audio_engine_ffi.cpp`, `bridge/modules/audio_engine.x`, `bundles.x` | Remove `setControl` (×2). Add `setInput(node, port|name, value or [Float], region…, ramp)`, `setInputX`, `impulseInput`, element and slice forms. Notes remain under the same names (engine-side voicer). The rtOnly `playNote`/`releaseNote` call the engine voicer at the task's wake offset. `wireDef(shape)` and `gainDef(shape)`. `tapChans` becomes a shape. `resizeBuffer` and `fillBuffer` take any channel count. |
| `lang/modules/music/score.x`, `play.x` | `Voice.names` become `notes` column names from `DefDesc`. `NoteCmd` keeps the same three cases, but `noteSetParams` gains an optional ramp. The silo `scorePlayer` can finally honour `noteSetParams`. |
| `lang/modules/live/{proxy,plan,pattern}.x` | `ctlNames` become input names. `set(p, name, v)` and `<-(p, [String:Float])` call `setInput`. `opSetControl` becomes `opSetInput`. `chans` becomes `Shape`. Anchor and monitor defs are named by shape. Proxies can now patch one proxy into another's control inputs. |
| `bridge/modules/ui.x`, `tzpl_ui_ffi.cpp`, `tzpl_ui_node_controls.cpp`, `tzpl_ui_state.hpp` | `bindControl` becomes `bindInput(node, port, element)`. `UIEngineTarget{nodeID, port, region, silo}`. Multichannel inputs use per-element sets, which removes the "no per-element setControl" limitation. |
| App: `controls_dispatch.cpp`, `controls_panel.cpp`, `graph_model`, `graph_edits`, `graph_view`, `plugin_browser_*` | `setInput` replaces `setControl`. Ports show their shape (`2×3 f32`). The 'e' rate glyph goes. The Controls section becomes "Inputs" with specs. `canConnect` uses shape broadcastability. Keep the ImGui twin in parity ([[app-juce-port]]). |
| `app/juce/juce_audio_backend.cpp`, RtAudio, `tzpl_nrt_render.cpp` | Unchanged calls; `processAudioBlock` chunks internally (D5). |
| OSC `/engine/setControl`, NATS `engine.setControl` | Become `/engine/setInput` with name and region, plus `/engine/impulseInput`. Remove `setControl`. NATS gains `noteSetParams`. |
| `lang/modules/instruments.x` (27 decls), `effects.x` (62), `common_ugens.x`, `filters.x`, `examples/*.x` | `control` becomes `inlet` with a spec (mostly mechanical, since `control(...)` syntax can stay). Ugen definitions keep their recurrence notation (D3). Change one only where its form defeats classification for no good reason, e.g. an accumulation written in a non-associative order. Check the effects library's power-of-two channel rule ([[tzpl-effects-library]]); it can go. |

---

## 7. Milestones

Each milestone ends green on its own tests, with a commit series on `block-engine`.
`main` stays releasable throughout; the branch is merged once, at M10.

**M0: Baseline and scaffolding (done 2026-10-25)**
- D1, D2, D7 and D10 are decided. The milestones assume the recommendations for the
  rest.
- **Reference render corpus** in `tools/block_engine/`:
  - `corpus.tsv` lists 118 entries, about 74 minutes of audio:
    - integration-test renders;
    - the music libraries and `music_fx_demos.x`;
    - 29 `example_synthdefs.x` defs, and `instrumentTest.x`;
    - all 27 `effects.x` effects, each on a test source;
    - the 12 SC2 conversion files (about 140 examples).
  - Each entry is marked `same` or `change`.
  - `render_corpus.py` renders it with fixed engine flags (48 kHz, `--buffer-frames 64`,
    stereo, 4 silos), seeded synth and language RNGs (`TZPL_RNG_SEED`,
    `TZPL_LANG_SEED`), and a private plugin build directory per worker.
    - It writes the WAVs and a `manifest.tsv` (peak, RMS, non-finite count, SHA-256)
      to `~/tzpl-build/block_engine_corpus/<dir>`, outside git.
    - `--buffer-frames 64` makes NRT's quantization of immediate commands to buffer
      boundaries match the future block size.
  - `compare_corpus.py` compares two renders, by tolerance or byte-exact
    (`--exact`). Two full renders from the same commit are byte-identical.
  - Two fixes on the branch made this possible:
    - **`TZPL_LANG_SEED`** in `lang/src/vm.cpp`. Scripts draw language random
      numbers while building graphs, e.g. `apverb`'s delay times.
    - **No mouse poller in `--nrt` mode** (`app/src/main.cpp`). It wrote the live
      desktop mouse into the shared-input slots that `mouseX` and `mouseY` read,
      which made every mouse-reading render depend on the pointer position.
- **Not in the corpus:**
  - the `silo_*_audible.x` scripts, which hang under `--nrt`; the per-block task
    tick is covered by `test_block_engine` instead;
  - `pat_nrt.x`, which renders silence on main;
  - the cookbook notebooks, which are binary `.tzd` files that depend on silos and
    interactive cells. Their libraries are covered by other entries.

**M1: Runtime support library (§5) (done 2026-10-25)**
- Independent of everything else, so it can run in parallel with M2.
- The vec types, vmath with both backends, scans, the state space builder and random.
- Move the tensor-ugens UGens into `benchmarks/tensor_ugens_ref/` as oracles and
  baselines.
- Add the tests to CI on macOS arm64 and Linux x86-64.
- **What landed:**
  - `shared/tzpl_dsp/`, header only, namespace `tzpl::dsp`: `vec_types.hpp`,
    `vmath.hpp`, `scan.hpp`, `state_space.hpp`, `random.hpp`, and `tzpl_dsp.hpp`
    including them all. CMake target `tzpl_dsp` (interface).
  - vmath has three backends: Apple simd, Sleef and a libm lane fallback.
    `TZPL_DSP_VMATH_LIBM` and `TZPL_DSP_VMATH_SLEEF` force one. Rounding, abs,
    sqrt, fma, min and max are `__builtin_elementwise_*` on every backend. Scalar
    `sinpi`/`cospi`/`tanpi` are computed by exact reduction, since libm has none.
  - scan.hpp has prefix scans for `+`, `max`, `min`, `xor`, wrapped `+` (the
    phasor) and affine maps, each with a carry form and a block form that takes any
    `n` (scalar tail) and stride-0 inputs.
  - random.hpp: xoroshiro128++ `RandState<1>` and `RandState<4>`.
    - Each lane is seeded with splitmix64 from a caller's seed and stream number,
      with no system calls. tensor-ugens seeds from `arc4random`.
    - `uniform` and `bipolar` give f32 and f64 values with 23- and 52-bit mantissas,
      and `uniform` gives i32 and i64 bits.
  - `BlockStateSpace<Z, N>::build(step)` takes any N. It is tested for N = 1, 2, 3
    and 4 (lpf1, decay, RBJ lowpass, lag3, formlet), in segments of mixed length,
    with switches to the recurrence part way through.
  - `tzpl_dsp_tests` runs 482 checks. It is built three ways: the platform backend,
    forced libm, and `-ffast-math`. All three pass on macOS arm64. The Sleef backend
    also passes on arm64 against a local Sleef 3.7 build.
  - CI: a step in `lang-tests` (macOS) and in `linux-core` (Linux x86-64, Sleef)
    runs all three. The workflow triggers on pushes to `main` and on pull requests,
    so the Linux run happens on the first PR from this branch.
  - `benchmarks/tensor_ugens_ref/`: the tensor-ugens sources, copied unchanged from
    commit 062f75e, as the static library `tensor_ugens_ref`, plus
    `bench_tensor_ugens`, which prints ns/sample/channel at 1, 2, 4 and 16 channels.
    - It is Apple only, since the sources use `<simd/simd.h>`, and it is off by
      default (`TZPL_BUILD_BENCHMARKS`).
    - The root CMake no longer requires the synthdef compiler for `benchmarks/`.
    - The f64 numbers are close to TU §8. For example, at 1/4/16 channels:
      sinosc 1.67/1.39/1.40, white 0.59/0.41/0.40, lfnoise0 0.37/0.26/0.28.
    - Two rows differ from TU §8:
      - phasor at 1 channel: 0.99 to 1.35 across two runs, against 0.90;
      - lfnoise3 with an audio-rate frequency at 16 channels: 0.64, against 1.15.
    - It also measures the filters, which TU §8 doesn't: lpf with constant
      parameters 0.44 to 0.57 / 1.03 / 1.04, and with an audio-rate frequency
      4.7 / 3.8 / 3.8.
- **Deviations:**
  - **Sleef link library, not inline headers.** The Sleef backend calls the dispatch
    functions in `<sleef.h>`, using the Sleef that `shared/` already fetches on
    Linux. `f64x4` is two `d2` calls on every ISA, as in `tzpl_simd.hpp`, because
    the `d4` names exist only in AVX builds. Inline headers and per-ISA `d4` calls
    are left for when synthc picks its target ISA.
  - The Sleef backend maps only the functions `tzpl_simd.hpp` already calls. So
    `logb`, `erf`, `erfc`, `tgamma`, `lgamma`, `remainder`, `fdim` and `nextafter`
    run lane by lane on Linux. On Apple, only `logb` does.
  - **f32 state space contract is 2e-5, not 1e-5** (accepted 2026-10-25). Errors are measured against
    the recurrence in f64. On an RBJ lowpass at f = .01, q = 2, the f32 state space
    is 1.4e-5 to 1.7e-5 off, while the f32 recurrence is 3e-6 to 8e-6 off. The test
    accepts the larger of 2e-5 and 3 times the f32 recurrence's own error.
    - A state space built in f32 itself (from a step that only takes f32) has its
      own limit, 1e-4 (accepted 2026-10-25). The same lowpass measured 4.2e-5 to
      5.3e-5 on the CI runners (Apple clang -ffast-math, Linux Sleef).
    - Typical f64 cycles are within 2e-13.
    - The ill-conditioned case, poles at radius 0.9997 (f = .001, q = 10),
      reaches 1.3e-11 for f64 and up to 7.6e-4 for f32. It is tested at 1e-10 and
      1e-3.
    - `build` computes the matrices in f64 for an f32 cycle whose step also takes
      f64, and rounds them once. This helps the slow decays most: 3.6e-7 against
      1.8e-6 when built in f32.
  - `i64` is `long`, not `int64_t`, so that `i64x4` is the same type as Apple's
    `simd::long4` and `tzpl_simd.hpp`'s `i64x4`.

**M2: Engine core on ABI v3 (§3), with hand-written test plugins (done 2026-10-25)**
- Port buffers, the block loop, input stages, XFader nodes (block form, region-shaped,
  sub-sample start), broadcasting, the mixer and narrow nodes,
  event collection with offsets, node splitting, cycle refusal (D1 phase 1), device
  chunking, NRT render, taps, the limiter and mixdown.
- Port the clock and task scheduler ticks to block form.
- Pass `test_block_engine`.
- Run the `audio_engine_lib` ASan build (live-mode scripts, per [[asan-nrt-stack-overflow]]).

What landed:
- **ABI v3** in `shared/tzpl_plugin_abi.h`, as in §3.6:
  - `tzpl_Shape`, the new element order, and `process(off, n)` / `resetVoice`.
  - `tzpl_PortDef` gains `hasSpec`/`spec`; `tzpl_SynthDef` gains `voiced`/`maxVoices`.
  - `tzpl_OutPort` also carries `isConst`/`version`, so producers can report
    constness and the engine passes it on to the inputs they feed.
  - Loaders refuse ABI < 3.
  - Shape helpers are in `shared/tzpl_shape.hpp`.
- **Engine** (`engine/src/`):
  - Each port channel is its own 64-byte-aligned buffer of `blockSize` samples.
  - Connections remap pointers per channel. When the destination is larger in every
    dimension, a connection broadcasts; when it is smaller in some dimension, a
    hidden fold node sums.
  - Per-channel input stages (`InPort::stepTo/rampTo/impulse`) render
    incrementally to each event's offset and report `isConst`/`version`.
  - XFader nodes run on blocks and take a fractional start. They nest, and retire
    after the block in which they finish. The fade curves are in `tzpl_fade_curves.hpp`.
  - `Silo::processBlock`:
    1. processes the device buffer in chunks of up to `blockSize`;
    2. runs immediate bundles at offset 0;
    3. merges clock-scheduled commands with task wakeups in time order, each run
       with `now_` (fractional) and `nowOffset_`;
    4. splits nodes at start, free and resetVoice events;
    5. returns the block's commands to NRT only after the block, so nodes freed
       mid-block are still alive while it runs.
  - The engine-side voicer (`tzpl_note_voicer.hpp`) drives the `notes` input
    tensor (D2).
  - Cycle refusal (the existing, previously unused `tzpl_errCyclicConnection`) runs at submit, on the shadow plus the
    bundle's edits, and again on RT.
  - `EngineConfig::blockSize` defaults to 64.
- **Tests:**
  - `integration-tests/src/test_block_engine.cpp`: 121 closed-form checks, covering
    - block sizes 1, 4, 64 and 256 against device chunks of 37, 64 and 100;
    - start and free at fractional offsets;
    - step, ramp, interrupted ramp and impulse;
    - box regions;
    - broadcast {3}→{6} and {2,3}→{4,3}, and folds {8}→{2} and {3}→{2};
    - all 7 curves;
    - nested A→B→C→D fades;
    - setInputX on a connected input;
    - scheduled cuts;
    - cycles;
    - voices and resetVoice offsets;
    - task wakeups;
    - multi-silo mixdown, taps and the limiter.
  - Mutating the event offset rounding or the fade position makes about 100 of the
    checks fail.
  - `test_stopped_engine`, `test_graph_shadow`, `test_metering`, `test_audio_engine_ffi`,
    the app tests, the JUCE self-test and the lang suite (486) all pass.
  - Under ASan+UBSan, all four engine tests and the FFI test are clean apart from one
    downcast report in `lang/src/tzpl_cpp.cpp`, outside the engine.

Deviations and notes:
- **No live-mode ASan scripts.** No v3 synths exist until M4 (synthc still emits v2,
  which the loader refuses), so the engine tests and the FFI test ran under
  ASan+UBSan instead. Two bugs found that way are fixed:
  - **A use-after-free in `sortNodes`.** It walked the previous sorted list, which
    could hold hidden nodes the NRT side had already deleted. The old engine's
    unexplained second `sortNodes()` call was masking this. Sorting is now
    generation-stamped and never walks the old list, and `pushDeadNode` unlinks
    the node first.
  - **An init-order race.** `Engine::sampleRateChanged_` was read by the NRT thread
    before it was constructed.
- **Deferred to M5 (consumers):**
  - region-shaped faders, and `connect` into a destination `Region`;
  - `setInputX` on part of a *connected* input. Value regions work everywhere
    (`setInput`/`impulseInput` on an element, a range or a box). `setInputX` fades
    a connected input only over the whole port.
- **Graph edits at a mid-block offset.** In a scheduled bundle, connect, reconnect
  and disconnect pre-allocate a 0-length (step) fader, so a cut lands on its exact
  sample on float ports. Integer ports, `disconnectOutput`, `disconnectNode` and
  `freeAllNodes` apply at the block start.
- **Freeing a node at offset t** zeroes its outputs from t on. Inputs it fed revert
  to their stage values from the next block.
- **The offset rule** for a clock event is the first sample whose beat has reached
  it, as in the per-sample engine.
  - Silo tasks fire at their beat's exact fractional sample and reschedule from
    their own beat, so jitter no longer accumulates.
  - Silo actor timers resolve at the sample their beat falls on (`VM::nextActorTimerBeat`).
- **Compatibility shims, removed in M5:**
  - `setControl` by name, and by ID (the ID is now the input index);
  - `ControlDesc`/`listDefControls`, derived from inputs with specs;
  - the flat `setInput(port, n, values)` overloads.
  Notes keep their API names and run on the engine voicer.
- **The C++ synthdef-compiler executable** is behind `TZPL_SYNTHDEF_LEGACY_EXE`
  (off). Its CI step is replaced by `test_block_engine`. The library still builds,
  for compile-and-link.
- **The M0 corpus** cannot render until synthc emits v3 plugins (M4/M5).

**M3: synthc front end and IR (done 2026-10-25)**
- `Shape` and the new rates in `synthdef.x` and `ir.x`; shape and type inference; inlets
  with specs; `notes` columns.
- **Delay-weighted edges in the IR (§4.2):**
  - the importer converts writer and reader pairs into weighted edges;
  - zero-weight cycle check;
  - interval bounds for `vread`;
  - the topological sort ignores weighted edges;
  - `addDelayAntecedents` is removed;
  - the analysis dump prints cycles with their weights.
- The `block` placement level and parameter group detection, analysis only. The M4
  codegen doesn't use it yet.
- Remove the event, control and `eventToAudio` paths. Remove `defSynth` and the
  C++-oracle diff tests, and move the compile and link code (D9).
- New analysis dump tests (golden text) for shapes and rates over the old diff corpus's
  graphs.

What landed:
- **D9:**
  - Compile and link moved to `bridge/src/tzpl_synth_build.{hpp,cpp}`.
  - The f32 constant formatter is rebuilt on `std::to_chars`.
  - The bridge no longer links `synthdef_compiler_lib`, and
    `TZPL_BUILD_SYNTHDEF_COMPILER` defaults OFF.
  - Removed: `defSynth`, the s-expression serializer, the
    `compileSynthDef*`/`synthdefGenCppFromSexpr`/`synthdefAnalysisDump` FFIs,
    `synthc/difftest.x`, and every C++-oracle suite (the analysis, rewrite, voicer,
    spectral, SIMD, buffer, bank, control-flow, merge-delay, widen, non-flat, prod
    and effects diffs, the render A/B, codegen validity, `eventToAudio`).
- **Front end (`synthdef.x`):**
  - `Shape` (4 row-major dims) with `chans`, `rank`, lcm `broadcast`, `strides`,
    `broadcastChannel` and `prependDim`.
  - Shaped `fill`/`tensor` constants.
  - `inlet`, `control` and `urand` take a `Shape` (Int overloads keep meaning
    a row).
  - `Rate` is `constant, init, reset, block, audio`.
  - `control`/`trigger`/`toggle`/`choice` build an `inlet` carrying an `InletSpec`.
  - `noteParam`/`gate` are columns of `notes`.
  - No power-of-two rounding anywhere.
- **IR:**
  - `ctx.shape` per node, with `ctx.chans` kept in step.
  - Shape inference uses the lcm rule; constant folding broadcasts its operands
    first (`gatherShape`).
  - Inputs with specs; event rate, iso-groups and event loops are gone.
  - A voicer creates the synth's `notes` input `{maxVoices, 1+N}`, with gate in
    column 0.
- **Delay-weighted edges (`synthc/cycles.x`):**
  - Each reader gets `dsrc`/`dweight`.
  - SCCs by iterative Tarjan, minimum cycle weight by Dijkstra inside the
    component, and the zero-weight check.
  - Interval analysis bounds `vread` indices.
  - `addDelayAntecedents` is removed.
- **Parameter groups (`synthc/paramgroups.x`):** block-placed nodes keyed by source
  set (input names, `notes[c]` columns); groups fed by notes are per voice.
- **Dump:** PORTS (shapes, specs), sorted exprs with shapes, trees, loops including
  BLOCK, PARAMETER GROUPS, CYCLES (weight, vector/recurrence, edges) and DELAY.
- **Tests:**
  - `integration-tests/run_synthc_dump_test.sh` holds goldens for 158 graphs in
    13 corpus files under `integration-tests/synthc_dump/`:
    - the old diff corpus's graphs;
    - the instruments.x and effects.x library synths;
    - a new `shapes.x` with lcm, matrix and fold broadcasting, control kinds,
      groups, recurrences, `vread` bounds and notes columns.
    `--update` regenerates them.
  - `live_plan_check` was updated: controls are input ports now.
  - The lang suite (486) passes.

Deviations and notes:
- **synthc's code generator is detached until M4.** `codegen.x` (the per-sample
  generator M4 starts from) is no longer imported, and `defSynthX` reports that
  block code generation is pending. Nothing generated could load anyway (ABI v3).
  CI runs the dump goldens and `run_live_plan_test.sh` in place of the render
  tests until M5.
- **Shaped views are not done yet.** The vec ops (take, drop, stride, transpose,
  join, at, put, reduce) still work on the row-major flattening and produce rows.
  Per-channel provenance and folding (§4.2 step 1a) are left for the code
  generator stages (M4/M7). Source sets are per node.
- **Plain inlets are nominally `audio`.** Only inputs with a spec (controls) and
  note parameters are nominally `block`. An unspecced `inlet` is declared as a
  signal input; treating it as `block` would make every effect's audio path a
  parameter group that always falls back.
- **Control flow on a block-placed condition still runs at audio.** Per-block
  branching (§4.4) comes with codegen.
- **Delay reads stay at audio.** The old event-rate promotion of delays is gone.
- **The `vread` weight is `max(1, floor(lower bound) - lookahead)`.** The
  lookahead is 1 for cubic and 3 for the 8-point kernels.
- **`inplaceDelayOps` is kept.** The M4 generator must order its VecAt loads
  before the in-place store; `addDelayAntecedents` used to do that.
- **A second voicer in one synth** gets an input `notes1`.

**M4: synthc serial-loop codegen (done 2026-10-25)**
- Correctness first. Emit `process(p, off, n)` with every loop serial: an outer sample
  loop around today's one-sample loop bodies:
  - channel-vector SIMD where `chans % 4 == 0`;
  - non-power-of-two channel counts through per-channel tables;
  - delays with block-aware ring sizes;
  - voicers on the `notes` input plus `resetVoice`;
  - buffers and banks; control flow; spectral.
- There are no parameter groups yet: every inlet is read per sample.
- This produces plugins that run on the M2 engine with the old semantics, so every later
  layer has something real to integrate against.

What landed (2026-10-25):
- **`codegen.x` on ABI v3:**
  - The struct header is v3 (`num_ins`, `num_outs`, `maxBlock`, `ins`, `outs`).
  - `NAME_process(p, off, n)` is a sample loop. Each sample:
    1. `NAME_gather(p, bi)` copies every input port's channels into
       `alignas(16) p->in<sn>[]`;
    2. the old loop bodies run on those arrays;
    3. delays advance;
    4. outlets `p->out<sn>[]` scatter to `outs[sn].chans[c][bi]`.
  - `load()` emits static `tzpl_PortDef` tables (shape, element type, spec), plus
    `voiced`, `maxVoices` and `noteDefaults`. `alloc(maxBlock)` is v3 too.
- **Rates.** `block` is demoted to `audio` (`demoteBlockRate`), so every inlet is read
  per sample. Write-after-read delay ordering (`addDelayOrdering`) is kept for the
  serial bodies.
- **Non-power-of-two channel counts:**
  - `_indexWrapShape` wraps with a mask (power of two), `%`, or a static `bmap` table
    for shaped broadcasts.
  - SIMD and flat voice mode need power-of-two channel counts; other loops run scalar,
    or AoS for voices.
- **Voicers:**
  - `noteParam` reads its column of the `notes` input.
  - `resetVoice(p, vi, off)` gathers the inputs at `off`, zeroes the voice state,
    reseeds the voice's RNG and runs the reset loops.
- **ABI and engine:**
  - `resetVoice` gained the `offset` argument.
  - `tzpl_SynthDef` gained `noteDefaults` (appended).
  - The engine voicer fills columns a noteOn doesn't give from the defaults; idle rows
    hold them too.
- **Driver.** `defSynthX` compiles and loads again. `dumpCorpus` with
  `SYNTHC_CORPUS_MODE=compile` compiles and loads every graph; all 158 dump graphs
  load.
- **Fixed four latent flat-voice indexing bugs** shared by both old compilers. The M0
  references contain them, and the new noteDefaults exposed them (idle voices are
  no longer all zero):
  - `reduce` over a voice-local input summed a window at the flat index instead of
    the voice's rows, so voices leaked into each other.
  - Vec ops (`ncyc`, `transpose`, `stride`, ...) on a voice-local input remapped the
    flat index and lost the voice base.
  - A voice temp narrower than the loop was indexed `[i]` and read past its voice's
    slots.
  - `rotate` read its amount at element 0, which is voice 0's slot.
- **`TZPL_POW2_CHANS=1`.** A verification-only switch that restores the old
  power-of-two rounding (rows, vec ops, `reduce`, note-param columns, constant
  padding, `join`). With it the corpus can be compared with the old engine.
- **Corpus tooling:**
  - The `synthc_render_ab_*` scripts and `event_to_audio.x` are restored under
    `tools/block_engine/scripts/`.
  - `corpus_fx.x` writes its chord as four explicit voices, matching the old padding.

Verification:
- **Oracle.** The old app, built at 6261a06, with the four indexing fixes applied to
  both of its compilers. It is a local worktree and is not committed.
- **Corpus in `TZPL_POW2_CHANS=1` mode against the oracle: 107 of 118 pass.** The
  rest:
  - `event_to_audio` is silent: its API is gone, and it is rewritten in M5.
  - `sc2_6`: the old engine is silent for the first note of each
    `stringWanderVibrato` voice (24–28 s). This is an old defect; the new engine
    plays them, and the renders match exactly afterwards.
  - Precision only, -53 to -98 dBFS: `live_nrt_render`, `music_fx_demos`, `fxPingPong`,
    `fxMultiTap`, `sc2_3`, `sc2_7`, `sc2_8`.
  - Slow phase drift in high-Q resonators: `instrument_test` (modalBell, from
    27.6 s) and `sc2_9`.
- **Normal mode: 101 of 118.** The extra differences are exact tensor shapes, by
  design: a 7-partial organ is no longer padded to 8 by repeating a partial, and
  20 Klank modes are no longer 32.
- **Unchanged:** the dump goldens, the lang suite (486) and `test_block_engine`
  (121) all pass.

Deviations and notes:
- **Precision differences come from per-sample recomputation.** Values the old code
  computed once at noteOn or on an event (ring and resonator coefficients, envelope
  rates) are recomputed per sample. The SIMD `cos` and scalar `std::cos` differ by
  an ulp, and FMA contraction changes. A per-voice compare (a filter's `freq <
  0.45 fs` mask) now disqualifies a loop from SIMD. M6 parameter groups hoist all of
  these again.
- **Ring sizes are unchanged.** The serial sample loop advances delays per sample,
  so it needs no block-aware sizing. That comes with block strategies (M7/M8).
- **One known gap.** Flat-voice SIMD `take`/`drop` of a partial-width voice-local
  input still indexes without a voice base. The corpus doesn't hit it.
- **CI runs on `block-engine` pushes.** Seven render scripts pass again and are back in
  the headless integration step: fx compile, fillBuffer, bit ops/d0, osc, pink,
  neg-abs and widen. `run_live_test.sh` is gated off on this branch because live
  proxy self-reference needs inter-node feedback (MF). The C++-oracle scripts
  (codegen validity, render A/B) were removed in M3.

**M5: Consumers (§6) (done 2026-10-25, corpus precision deferred to M6)**
- Port the bridge FFI, `bundles.x`, the music libs, live, ui, the app (JUCE and ImGui),
  OSC and NATS, `instruments.x`, `effects.x` and the examples.
- Port the integration scripts. The noteparam and eventToAudio tests are rewritten as
  input-stage and voice-tensor tests.
- **Gate:** the M0 corpus re-renders within tolerance (−100 dBFS max difference for f32
  synths, except for the files on the expected-to-change manifest), and every
  `run_*.sh` passes.

What landed (2026-10-25):
- **Engine.** The deprecated `setControl` overloads, `ControlDesc`,
  `listDefControls` and `DefDesc::controls` are gone; a def's controls are its
  inputs with a spec (`DefDesc::ins[i].hasSpec`).
  - New: `setInputX` by input name, `rtNoteSetParams` (silo tasks), `tapShape`, and
    shape forms of `ensureWireDef`/`ensureGainDef` (`_wire2x3`).
- **Bridge FFI and `audio_engine.x`:**
  - Two primitives, `inputOp` (whole input or flat channel range) and `inputOpBox`,
    each with op set, crossfade or impulse.
  - `.x` wrappers: `setInput`, `setInputAt`, `setInputBox`, `setInputX`, `impulseInput`
    and `impulseInputAt`. The port is an index or a name; the value is a `Float` or
    `[Float]`; ramp time and curve are optional.
  - Also: `noteSetParams` with a ramp, silo-only `setNoteParams`, `tapShape`,
    `wireDef([dims])`/`gainDef([dims])`, and `errRegionOutOfRange` in `Err`.
- **`bundles.x`.** `EngineCmd.input` replaces `setInput`/`setInputX`/`setControl`/
  `setControlNamed`, with builders for every input form. `noteSetParams` carries a ramp.
  `toString` prints each command as the builder call that makes it.
- **ui:**
  - `bindInput(w, node, input, element = -1)` and `bindInputY` replace
    `bindControl`/`bindControlY`; `element` drives one element of a multichannel input.
  - `UIEngineTarget` is `{node, port, first, count, silo}`, and both apps' dispatchers
    send `setInput` with a region.
- **Apps (JUCE and ImGui):**
  - The plugin browsers show Inputs (with spec columns) and Outputs.
  - Graph ports show their shape (`2×3`).
  - The graph model counts specced inputs.
- **OSC/NATS.** `setControl` is removed. New messages: `setInput` (port by index or name,
  values broadcast), `setInputAt`, `setInputRamp`, `setInputX` (by name too),
  `impulseInput`, `impulseInputAt` and `noteSetParamsRamp`. NATS gains `noteSetParams`.
- **live:**
  - `opSetControl` becomes `opSetInputNamed`.
  - Proxies carry a `Shape`: `ndef(shape(...))` and `reshape(p, shape(...))`, with
    anchor and monitor defs named by shape.
- **music.** `NoteCmd.noteSetParams` has a ramp, and the silo `scorePlayer` honours it
  through `setNoteParams`.
- **Libraries, examples, scripts:**
  - Every `setControl` is now `setInput` by name.
  - `event_to_audio` is rewritten as input steps plus `impulseInput`, and is on the
    change manifest.
  - The noteparam test adds a second voice and a ramp, and is also on the change
    manifest.
  - The `voicer` built-in test def keeps its old name: M2's rename to `voicerTest` had
    silenced the silo and panic scripts.
- **Docs.** Writing_SynthDefs, Live_Controls_and_Notebooks, FFI_Guide, the Cookbook and
  engine/Architecture.md are updated for inputs, the rates, shapes, the new calls and the
  messages.
- **Fixes:**
  - synthc: a 1-channel inlet used as a scalar join/put operand is element 0 of its
    gathered array.
  - The integration test `test_nats` has the stdlib on its module path.

Gate status:
- **Scripts.** Every `run_*.sh` passes except `run_live_test.sh`, whose self-referencing
  proxy needs inter-node feedback (MF). CI gates that script off on this branch; run
  them serially, because the live-audio ones contend for the device.
- **C++ suites.** All pass:
  - `test_block_engine`, `test_audio_engine_ffi` (66), `test_foreign_modules`,
    `test_graph_shadow`, `test_metering`, `test_stopped_engine`,
    `test_synthdef_compiler_ffi`, `test_voicer`;
  - `test_osc` (25) and `test_nats` (27, with a local nats-server);
  - the app's six test binaries;
  - the lang suite (486).
- **Corpus: not met; precision is deferred to M6.**
  - Against the patched old-app oracle in `TZPL_POW2_CHANS=1` mode, 107 of 118 pass,
    plus the two expected changes.
  - The rest are as in M4: precision from per-sample recomputation (-53 to -98 dBFS),
    resonator phase drift (instrument_test, sc2_9), and the oracle's own silence
    defect (sc2_6).
  - In normal mode the extra differences are the exact tensor shapes.

Deviations and notes:
- **A VM bug, worked around.**
  - `EngineCmd.input` first carried nested enums (port, region, mode). With them,
    `music_fx_demos` segfaulted in cold-cache renders (`ListNode::force` reading a
    Float as a pointer).
  - The payload is flat now. The crashing form and its repro steps are in
    `devplans/repros/nested_enum_payload_crash/`. The VM bug itself is not found:
    standalone repros don't crash.
- **Not done:** patching one proxy into another proxy's control inputs (§6 live), which
  is a new feature rather than a port.
- The formant bank in `effects.x` keeps four formants (now only so it sounds as it
  did).

**M6: Parameter groups and change detection (§4.5) (done 2026-10-25)**
- Group detection, caching keyed on version, and `{ptr, stride}` plumbing.
- Benchmark: CPU per node for an idle-knob synth versus `main`'s event-rate build. Target
  at most 1.1×.

What landed:
- **Placement.** `block` survives in the codegen pipeline (the M4 demotion is gone) in
  the root graph and in a flat-voice voicer body (`restrictBlockRate`,
  `paramgroups.x`). Elsewhere it is demoted to audio, as in M4: control-flow bodies,
  spectral frames and AoS voices.
- **Groups and loops:**
  - A group is keyed by (source set, graph), so a voice body's groups cache per voice
    and the root's once.
  - `ParamGroup` carries structured sources: the input port serials and the `notes`
    columns.
  - `treesToLoops` keeps one group per block loop (`Loop.group`).
- **Storage.** Every materialized block value is an instance variable
  (`blockValuesToInstVars`), so a skipped group's consumers read its cached value. This
  is the stride-0/stride-1 plumbing in the serial generator: the value is either still
  cached or rewritten every sample.
- **Change detection at the top of `process`:**
  - Each watched input channel is classified against its cached version:
    0 = constant and unchanged, 1 = constant with a new version, 2 = not constant this
    block. A non-constant channel caches an invalid version, so its groups recompute
    once when it settles.
  - A group's mode is the largest over its sources, per voice for a voice group (the
    voice's `notes` columns).
  - In the sample loop, mode 0 skips the group, 1 runs it at the first sample, and 2
    runs it every sample.
  - Voice block loops use the single-voice loop forms for each due voice, behind an
    any-voice check.
  - `init` marks everything due once (`pg_first`), and `resetVoice` marks the voice due
    (`pg_vforce`).
- **Fixes:** the synthc dump test filters Linux realtime-setup warnings from stderr;
  they had failed every golden on the CI runner.

Results:
- **Benchmark** (`/tmp`-local script: N nodes rendered for 61 s minus 1 s, ms of CPU per
  node-second, the `main` event-rate build first):

  | synth | main | M6 | ratio |
  |---|---|---|---|
  | idle-knob filter (4 controls, two RLPFs on noise) | 0.284 | 0.267 | 0.94× |
  | `fxChorus` | 1.083 | 0.911 | 0.84× |
  | `resonBank`, 4 of 8 voices sounding | 2.523 | 2.578 | 1.02× |

  Before the any-voice check, the bank was 1.32×.
- **Corpus, `TZPL_POW2_CHANS=1` against the patched oracle: 111 of 118** (M5: 107).
  instrument_test, fxPingPong and fxMultiTap now pass.
  - The two expected changes.
  - Precision-level differences: live_nrt_render (-88), music_fx_demos (-63), sc2_3
    (-98) and sc2_8 (-70 dBFS). The oracle computed some per-note values with SIMD
    transcendentals in its event loops; the new code computes them with scalar ones.
  - Slow resonator drift in sc2_7 and sc2_9.
  - The oracle's own silence defect in sc2_6.
- **Unchanged:** every `run_*.sh` passes except `run_live_test.sh` (MF). The dump
  goldens are regenerated: block loops now split by group, and groups by graph. All 158
  dump graphs compile and load.

Deviations and notes:
- **Units are inputs and `notes` columns, not channels.** A group recomputes when any
  channel of a source input changes. Per-channel provenance (§4.2 step 1a) would narrow
  that to one element of a control tensor; it is left for the fused block code (M8).
- **Inputs are still gathered every sample**, cached groups included.

**M7: Recurrence analysis (D3, §4.7).**
- Minimal-cycle extraction, linear-form classification, wrap recognition.
- Scan, block state space and affine-scan code generation. The general-class scalar
  cycle with vectorized inputs.
- Run every `common_ugens.x` and `filters.x` ugen against its tensor-ugens reference, for
  accuracy and speed.
- The corpus re-renders within the tolerance contracts.

Progress (2026-10-25; M7 is done, with the multichannel layout work left for M8):
- **Classification (`synthc/recurrence.x`):**
  - Each weight 1–3 cycle is walked with linear forms over its state (the in-cycle
    delay reads), tracking the largest rate of any coefficient that multiplies state.
  - The classes are scan (`+`, `+` with wrap written `x - floor(x)`, `max`, `min`,
    `xor`), affine-block (order = total delay weight), affine-scan, and general, which
    names the node that made it general.
  - The analysis dump prints each recurrence's CLASS and STRATEGY. Over the dump
    corpus: 123 scans, 63 affine-block (44 first order), 4 affine-scan; the rest are
    general (state machines and varying-coefficient filters).
  - The "minimal cycle" is the SCC itself for now.
- **Block layout of `process` (codegen):**
  - Each root-graph loop runs over the block's samples in turn.
  - Loops that share state run together sample by sample, as one region: a delay (its
    writer and every reader, wherever they are nested), a graph's random generator
    (its draw order is sample order), or a written buffer.
  - A temp consumed in a later region becomes a block buffer (`bt<serial>`, interleaved
    channel rows). So does a root-graph instance value at block or audio rate
    (`bk<serial>`), read at row `bi × stride`: stride 1 while its parameter group runs
    every sample, else 0. This is the `{ptr, stride}` plumbing.
  - One-channel ports are read and written in place. Multichannel ports go through
    interleaved block buffers, gathered once per call, and a multichannel input read
    only by parameter groups (`notes`) is gathered only when it changed.
    **This copy is temporary.** It exists only so the inherited per-sample loop code,
    with its 4-wide channel loads, can stay as it is. M8 removes it: ports are read
    zero-copy through the channel-pointer table, and channel-vector loops transpose in
    registers (D4, §4.3).
  - Buffer bases are hoisted `__restrict` locals.
- **Block forms:** first-order cycles run over the block in two passes around a
  `tzpl_dsp` scan:
  - Pass 1 computes the scan input, or the affine map's `a` and `b` taken from the
    cycle's linear form.
  - The scan is `scanSumWrapBlock`, `scanSumBlock` or `scanAffineBlock`.
  - Pass 2 runs the cycle's trees with the state read replaced by the previous
    written value. The delay write is left out, and the state is set from the scan's
    carry.
  - **Eligible cycles:** scan `+`/`+wrap`, affine-scan, or affine-block of order 1;
    one channel; root graph; one weight-1 read of a delay with no other reader, kept
    as a single value; no random draw inlined into the cycle's inputs.
  - In the dump corpus: 96 scans and 13 first-order affine cycles.
- **Block state space** (`tzpl_dsp::BlockStateSpaceIO<Z, N, I, O>`) runs every affine
  cycle with block-rate coefficients, of any order (`recurrence.x stateSpaceModel`):
  - **States are delay taps.** Each delay the cycle reads gives its taps y[−1] .. y[−K]
    (K its largest read weight) as a shift register: a single value, or an inline ring
    of at least K samples. So `ring`'s `y(1)`, `y(2)` are two states of one delay.
  - **Input delays** are delays outside the cycle whose every read feeds it through
    inlined linear expressions, and whose written value is such an expression: their
    taps are states too (`ring`'s `x z2`, `leakdc`'s `x z1`).
  - **Inputs** are the materialized audio values those expressions reach (block
    buffers, up to 4); **outputs** are the cycle values used outside it (up to 4).
    `brf`'s shared `b0·x` temp is a second input.
  - **Channels:** every cycle node and delay has C channels; each channel runs its own
    state space (inputs of 1 channel broadcast). Buffers are read at their row stride;
    a multichannel input port is read straight from its channel pointer.
  - **The step function** is the cycle's equations over `s[]`, `x[]`, writing `y[]`,
    emitted as a generic lambda per channel, so f32 matrices are built in f64.
  - **Per block, at run time:** while any coefficient group recomputes every sample,
    the block runs as the plain recurrence; a first-order cycle runs its two-pass
    affine scan instead (the plan's stride check: "one cycle, two forms"). Otherwise
    the block runs on the matrices, rebuilt when a group recomputed once (or on first
    use). The state layout is shared, so the forms continue each other.
  - **Regions:** a state space's region may hold loops that share no state with it.
    Those that depend on it run after the block, the others before it; one that does
    both makes the region run sample by sample. Its outputs are always block temps.
  - **First-order scans** (`scan+`, `scan+wrap`, audio-rate affine) keep the two-pass
    form, unless their inputs read another delay; affine cycles with block-rate
    coefficients go to the state space even at order 1 (0.22 ns/sample for a one-pole
    against 0.69 as a scan).
- **Loop order** (`passes.x hoistBlockLoops`): root loops that share state form one
  region spanning everything between them, so a block cycle's loops are made
  contiguous: each moves as early as dependences and shared state allow, and loops
  caught between them move after them where they can. A loop of input-port trees
  emits no code and joins whichever region it lands in. Should a cycle still span two
  regions, both run as plain regions (never the block form twice).
- **Dump and codegen agree:** over the 158 dump graphs, every cycle the analysis dump
  marks "block" runs in block form (checked by comparing the dump's STRATEGY lines
  with the generated `// recurrence k: block` comments). Counts: 94 `scan+wrap`, 2
  `scan+`, 12 first-order `affine` (audio-rate), 11 state space.
- **`TZPL_STRICT_RECURRENCES=1`** turns block forms off: every recurrence runs in
  its own order. This is the plan's `strictRecurrences`, as an environment variable
  for now.
- **Plugins** can include `tzpl_dsp/` (the build stages the subdirectory).
- **Fixes:**
  - `repeat()` on a payload-free enum crashed (lang, with a test).
  - A GC crash from an array of tuples in synthc was worked around; it is a separate
    VM bug, the second found on this branch.

Status:
- **Corpus, compat mode:**
  - Strict recurrences match M6 within -100 dBFS on 112 of 118. The rest are FMA
    contraction across the new buffer boundaries, -40 to -94 dBFS.
  - Block forms against strict differ only by rounding, as expected. The differences
    are amplified where a scanned value drives a decision (a sample-accurate gate, a
    sequencer step), a delay-read position (echo times from a lagged control), or a
    high-Q filter.
- **Benchmark against M6, same app binary** (ms of CPU per node-second, M6 → now):
  knobs 0.305 → 0.333, chorus 0.973 → 1.026, bank 2.612 → 2.330. Block forms gain
  little yet: most loops still compute one sample at a time between them, and
  vectorizing those over samples is M8's fusion.
- **Left for M8:**
  - Multichannel cycles run one state space per channel through interleaved block
    buffers. At 4 channels the one-pole filters spend most of their time in the
    interleave copies (0.7–1.2 ns against TU's 0.15–0.45); M8's planar layouts
    remove them.
  - Multichannel scans (phasors) run the channel-vector recurrence, the plan's form
    for 4 or more channels, but through the same copies.

Reference-ugen comparison (2026-10-25), `benchmarks/ugen_compare/`:
- **How it runs:**
  - `run_ugen_compare.sh [case]` runs the whole comparison.
  - `ugen_cases.x` writes a synthc plugin per ugen at 1 and 4 channels. Parameters
    are converted to our units: Hz, seconds, and 40 dB times for TU's 60 dB times.
  - The script compiles the plugins with the bridge's flags.
  - `bench_ugen_compare` (Apple only) runs each plugin and its TU graph on the same
    seeded input.
  - It reports the error (rms of the difference over the reference's rms, in dB) and
    ns per sample per channel for both.
- **Accuracy (29 cases):** most match to the f32 floor:
  - oscillators −89 to −152 dB (our phasors output the previous phase, so they are
    compared one sample later);
  - lag, decay, decay2, leaky and leakdc −108 to −127 dB;
  - all RBJ biquads −98 to −120 dB.
- **Bugs found in our ugens, fixed:**
  - `lag3(x, t)` ran two poles, not three (was −5 dB, now −112 dB).
  - `lag(x, u, d)` used the up time while the output fell and the down time while it
    rose (+6 dB). It now takes seconds, like `lag(x, t)` (was samples); `lag2`/`lag3`
    with up/down times likewise. Now −132 dB.
  - `varsaw` applied `bi` to an already bipolar shape, so `lfvsaw` spanned −3..1
    (+6 dB). `varsaw` is now the bipolar shape and `vartri` its unipolar form, as in
    tensor-ugens. Now −88 dB.
  - `ring`/`pling` approximated the pole radius as 1 + K/t, not exp(K/t) (−66 dB, and
    −42 dB for a 1 ms attack). Now `decay60dB(t·fs)`: −83 dB, the f32 floor for a pole
    at radius 0.9986 run as a state space.
  - Audio-rate `ring` stays at −41 dB: ours is direct form I, TU's transposed direct
    form II, and the two differ while the frequency moves. Not a bug.
- **Fixed while building it:**
  - `lag(x, u, d)` and its kin never compiled: a lang bug hid every template overload
    of a private function after the first one, when a template from another module
    instantiated it. The scope merge deduplicated by global slot, which is 0 for every
    template. Fixed, with a test.
  - `pling` had a typo (`freq atkTime`).
- **Regression check:** `run_ugen_compare.sh --check` runs the accuracy comparison
  only and fails above −80 dB (−35 dB for audio-rate `ring`); it runs in macOS CI.
  It caught a state space emitted twice that the corpus never exercised.
- **Speed, ns per sample per channel, ours vs TU (final run):**

  | | 1 channel | 4 channels |
  |---|---|---|
  | one-pole (`lag`, `decay`, `leaky`) | 0.21–0.30 vs 0.32–0.42 | 0.75–0.93 vs 0.22–0.38 |
  | `lag3`, `decay2`, `leakdc` | 0.48–0.62 vs 0.63–0.77 | 0.88–1.44 vs 0.29–0.45 |
  | RBJ biquads | 0.43–0.62 vs 0.51–0.67 | 0.92–1.29 vs 0.98–1.19 |
  | `ringz` / `formlet` | 0.85 / 1.71 vs 0.63 / 1.05 | 1.42 / 2.17 vs 0.66 / 0.70 |
  | audio-rate `rlpf` | 3.26 vs 4.74 | 2.14 vs 3.79 |
  | phasor oscillators | 0.98–2.72 vs 0.78–1.77 | 0.67–1.13 vs 0.57–1.58 |
  | `lagud` (nonlinear) | 2.33 vs 2.12 | 1.13 vs 0.67 |
  | noises | 2.2–2.3 vs 1.0–1.3 | 1.4–2.5 vs 0.5–1.2 |

  At one channel every linear filter but `ringz`/`formlet` is now faster than TU
  (they were 1.2–8.5× slower). The 4-channel gap is the interleave copying (M8).
- **How the speed came:**
  - **Serial regions keep state in locals** (`_localizeDelays` in codegen.x): through
    `p`, the compiler couldn't rule out that output stores alias delay state and
    coefficients, so they went through memory every sample (1-channel `leakdc` 7×,
    `brf` 8.5×). A one-channel root delay (a single value or an inline ring of 2–8
    samples) used by one plain region is loaded before its sample loop and stored
    after; values the region only reads are loaded once.
  - **The generalized state space** (above) took `brf`, `ring`, `pling` and `leakdc`
    off the serial path, and with the stride check every one-pole with block-rate
    coefficients.
- **Corpus, block forms against strict recurrences:** 101/118 within −60 dBFS. The
  rest are the previous comparison's set at the same levels (rounding amplified by
  gates, delay times, quantizers and high-Q filters: `fxLoFi`'s −42.1 dBFS is one
  8-bit step), plus `sc2_2`, `sc2_7` and `sc2_12`, which differ between two renders
  of the same code.
- **App benchmarks** (ms of CPU per node-second, start of M7 → now): knobs 0.333 →
  0.300, chorus 1.026 → 1.018, bank 2.330 → 2.359 (noise): their loops are mostly
  multichannel.

**M8: Strategy and fusion optimiser (§4.2 steps 6–8).**
- Serial-loop isolation of short cycles, the cost table, loop formation of all three
  kinds,
  transposes, the buffer arena and in-place reuse.
- **Equivalence-of-forms tests:** each synth compiled with every loop forced to serial,
  forced to channel-vector, forced to sample-vector, and chosen automatically must agree
  within tolerance, at block sizes 64, 1, and
  odd chunks.
- **Benchmark targets:** a mono `sinosc → lpf → out` at least 2× faster per sample than
  `main`; 16-channel no slower than `main`.

Progress (2026-10-26; M8 is done; §4.8 tensor shape operations follow as M8b):
- **Sample-vector loops:** a region whose loops are all one channel, audio rate and
  elementwise (no delays, random draws, buffers, control flow or channel ops) runs 4
  samples a step, then a scalar tail. It reuses the channel-vector emitter: the lanes
  are samples, a per-sample value loads 4 samples from its row (unaligned, `f32x4u`),
  lower rates splat. A block scan's two passes run this way too: its `w` buffer has
  a leading element holding the prior state, so pass 2 reads `w[bi − 1]`.
- **Cost table, first entry:** a state space per channel below 4 channels; at 4 or
  more, the plain recurrence runs 4 channels to a vector, and independent vector
  chains hide its latency. Measured: at 16 channels the channel-vector recurrence
  is 0.61–0.67 ns/sample against 0.86–1.41 for a state space per channel. The dump
  says `channel-vector` for those cycles.
- **State in locals for multichannel delays:** single values as aligned local arrays,
  and inline rings of up to 16 channels.
- **Tap-major rings:** a multichannel inline ring without variable-time readers is
  stored `d[tap][chan]`, so a channel-vector loop reads one tap of 4 channels as one
  vector (plan §4.2 step 8, layout by consumer).
- **Direct ports:** a multichannel port used only by root audio loops through
  elementwise consumers is read and written through the engine's channel-pointer
  table (`r_in<k>[chan][bi]`); its interleaved buffer, gather and scatter are gone.
  The notes input, ports read at other rates, and inputs a reduce or channel op takes
  whole keep the gathered buffer. This removes M7's temporary copy for those ports.
- **Equivalence of forms** (`run_forms_equivalence_test.sh`, `forms_equiv.cpp`, in
  CI on macOS and Linux): every dump-corpus synth built normally and with
  `TZPL_FORMS=serial` (no block forms, no vector loops), run in blocks of 64, of 1
  and in odd chunks, agrees within −50 dB (worst −61 dB, `fxMultiTap`: a lagged
  delay time rounding differently; most below −80). Inputs are sums of sines, and
  random draws are seeded. 158 of 158 pass.
- **Fixes on the way:** `tzpl_simd.hpp` included `<sleef.h>` inside its namespace on
  Linux, which hid the Sleef functions from `tzpl_dsp`: every plugin with a block form
  failed to compile there (CI caught it). Generated code includes `<algorithm>`.
- **Benchmarks against `main`** (ms of CPU per node-second, `main` → now): mono
  `sinosc → lpf` 0.281 → 0.125 (2.24×, target ≥ 2×); 16-channel 0.965 → 0.836
  (1.15×, target ≥ 1×); knobs 0.247 → 0.250; chorus 1.104 → 0.748; bank 3.099 →
  2.350.
- **Reference comparison** (ns/sample/channel, ours vs TU): 1 channel: biquads
  0.44–0.65 vs 0.45–0.73, `sinosc` 1.3 vs 1.8, `sinosc → lpf` 1.8 vs 2.7; 4 channels:
  one-poles 0.24–0.38 vs 0.13–0.32, biquads 0.46–0.58 vs 0.98–1.05; 16 channels:
  biquads 0.56–0.62 vs 0.93–1.05, phasor oscillators 0.22–0.36 vs 0.38–0.62.
- **Port rows (4×4 transposes):** a region on direct ports steps 4 samples at a
  time. Each input port is transposed into 4 rows of channels
  (`tzpl::dsp::portToRows4`, 4 channels per `transpose4`), the region's body runs
  for each row, and output rows are transposed back (`rowsToPort4`); the per-sample
  form runs the tail. At 16 channels a one-pole went from 0.60 to 0.15 ns/sample (TU
  0.19–0.30), a biquad from 0.57 to 0.20 (TU 0.93); 16-channel `sinosc → lpf` is
  1.23× faster than `main` in the app (it was 1.15×), bank 1.28×.
- **Fusion:** consecutive plain regions run as one sample loop (block-cycle regions
  stay apart; sample-vector regions merge only with each other), so values pass
  between them as locals instead of block buffers.
- **Buffer arena:** a block temp's live range runs from the region that writes it to
  the last region that reads it; temps whose ranges don't overlap, with the same
  type and row width, share memory (`p->btB = p->btA` at alloc). Overlapping temps
  never share, so a region's `__restrict` bases stay distinct. After fusion the dump
  corpus has few temps to share (4 of 144); it matters for large synths.
- **Delay times are clamped at 0** in every variable-time delay read
  (`tzpl_delay_time`): converting a negative float to `uint64_t` is undefined; x86-64
  produced a huge value whose fraction indexed the sinc table out of bounds (a crash
  the equivalence test found on Linux).
- **A lang GC bug, found by fusion:** a short-circuit `||`/`&&` left its right
  operand's temporaries allocated and typed; on the skip path they held a previous
  tenant's value, and a later stack map rooted it (`sc2_4` crashed the VM in
  `step_root_frames` marking the Int 8). Fixed in lang codegen (`freeRegsTo` after
  the right operand), with a test.
- **The equivalence test's limit is −40 dB:** `fxMultiTap` measures −61 dB on arm64
  and −47 dB on x86-64 (a 38400-sample delay time held to ~0.002 samples by f32 in any
  form); a wrong form is +30 dB.
- **Port rows only for channel-vector ports:** a direct port narrower than a multiple
  of 4 channels (stereo) keeps per-sample access; transposing it was extra copying.
- **Block-wise delay lines (plan §4.4):**
  - *Value ranges* (`recurrence.x valueRange`): interval arithmetic over constants, the
    sample rate (8–768 kHz), input specs, `+ − × ÷ min max`, `sin`/`cos` and kin,
    compares and selects. An input is assumed to stay within its spec.
  - A runtime-sized root delay line whose written value is computed from outside the
    region without delay reads or random draws (feedforward), and whose every read's
    lower bound puts its kernel's newest tap at least a sample back, is written for
    the whole block first; the region then runs with its write left out. Every
    runtime-sized root ring holds max delay + `maxBlock`, so the block's writes never
    overwrite what a read in the block still needs. Chorus (reads ≥ 146 samples) and
    the granulator qualify; feedback delays (echo, flanger, ping-pong) don't.
  - The read pass then runs sample-vector: lanes are 4 samples of one channel, each
    at its own write head, through the vector interpolation kernels.
- **Sample-vector for 2–3 channels:** regions whose loops have 1 to 3 channels run
  sample-vector, one channel at a time (4 or more run channel-vector).
- **Planar block temps:** a temp whose channel count is not a multiple of 4, written
  channel by channel and read only elementwise, is stored planar (channel c at
  `base + c·maxBlock`); a sample-vector loop reads it contiguously, and so does a
  state space per channel. Channel-vector temps keep interleaved rows (D4).
- **Block-wise feedback delays:** a runtime-sized delay line whose reads, this call,
  all stay `n` samples (plus the kernel's newest tap) behind the write head runs its
  region sample-vector even with feedback: each lane reads older samples and writes its
  own slot. A guard picks the form per call: a static lower bound of the delay time,
  or the block's minimum delay time when it is computable before the region (echo's
  lagged time control). Otherwise the plain form runs.
- **Fix:** the vector interpolation kernel had no case for `Interpolation.none` and
  returned the fraction (a C++ comma expression).
- **The cost table, measured.** `TZPL_FORMS` takes strategies to turn off (`nofuse`,
  `noportrows`, `nosv`, `noss`, `noblockdelay`), and `forms_equiv --time` times every
  dump-corpus synth. Each rule below comes from a synth where the previous rule lost:
  - *Sample-vector* only for work clang can't vectorize from the plain sample loop: a
    math function (`sin`, `exp`, `pow`, ...), or reads through a kernel of 4+ taps of
    a feedforward delay written first. Plain arithmetic, type conversions, and block
    scans' passes are left to clang (one_pole, fxLimiter, if3, bwr: 1.2–1.3× better
    scalar). Not where a multichannel value is an interleaved temp or port (fxBooster:
    each vector became a scatter).
  - *Block-wise delays* only for kernels of 4+ taps (fxDiffuser's uninterpolated reads
    were 1.5× slower vectorized; echo's linear ones even; chorus's cubic ones 1.5×
    faster). A write-first pass is kept only if its region then vectorizes.
  - *Fusion* leaves regions that own a delay line alone: fusing in the delay's written
    value made it unavailable before the region (fxRotary, fxGranulator: 1.3×).
  - *Port rows* only for ports a multiple of 4 channels wide, and not in regions of
    variable-time delay reads (vg, vl: per-lane gathers, 1.1–1.15×).
  - Result over the corpus, each strategy against turning it off: fusion helps 18 synths
    and hurts 1, sample-vector 21 and 2, state space 5 and 1, port rows 11 and 1,
    block-wise delays 3 and 1; the remaining "hurts" are timing noise (the same synth
    under every variant) or under 0.2 ns/sample (d4, tiny).
- **Benchmarks against `main`** (ms of CPU per node-second, `main` → now, side by
  side): mono `sinosc → lpf` 0.341 → 0.132 (2.6×); 16-channel 0.921 → 0.771 (1.19×);
  knobs 0.252 → 0.226 (1.11×); chorus 1.144 → 0.618 (1.85×); bank 3.076 → 2.206
  (1.39×); echo 0.555 → 0.220 (2.52×); ping-pong 0.527 → 0.245 (2.15×).
- **Fix (older bug, both builds):** flat voice mode required a power-of-two channel
  count only of the voicer's output, but its loops split their index with shifts and
  masks for every per-voice value. A voice with 6 resonator modes (`ring_synth`) read
  the wrong channels and voices, and past a per-voice array (non-finite output on the
  macOS CI runner). Flat mode now requires every voice-body value's channel count to
  be a power of two; otherwise voices run AoS. An AddressSanitizer + UBSan sweep of
  every corpus plugin, both builds, through `forms_equiv` (`FORMS_NO_DLCLOSE=1`) is
  clean.
- **Not done:** a register budget (no synth in the corpus showed spills from fused
  regions; the per-ISA budget is M9's). Fixed rules rather than a cost function: the
  measured cases separate cleanly, so a weighted model was not needed yet.

**M8b: Tensor shape operations (§4.8).**
- `Shape.rank` in `synthdef.x`, synthc's IR and `tzpl_Shape`; rank through shape
  inference and broadcasting.
- Axis arguments (outer to inner from 0, negative from the last, named constants) for
  the shape operations in
  `synthdef.x` and the importer, with shape inference.
- Static operations as compile-time index maps folded into consumers' channel tables;
  `reduce` and `scan` per axis as folded op trees.
- `slice` with a varying offset and `rotate` with a varying amount: pointer tables
  rebuilt per parameter-group change at block rate, gathers at audio rate.
- Docs (`Writing_SynthDefs.html` §5) and tests per §4.8.

Progress (2026-10-26):
- **Rank:** `Shape.rank` in `synthdef.x` and synthc; `tzpl_Shape.rank` in the ABI (shape
  equality compares it; `validShape` refuses dims outside it). Signals sized by a
  channel count are a scalar for one channel (`chansShape`, `tzpl::shapeChans`), so
  mono voices and their voicers keep their shapes.
- **Axis operations:** take, drop, stride, stutter, ncyc, slice (constant or integer
  signal offset), rotate, reverse, transpose (reversed axes or a permutation), reshape,
  join, stack, reduce/sum/product/minOf/maxOf along an axis or over everything, scan.
  Shape inference in `types.x` (`vecShape`), errors at import (axis out of range,
  shapes that differ off the axis, a reshape that does not divide, ...). Codegen splits
  an element index into (outer block, index along the axis, inner offset): the static
  operations are index maps into the materialized input, and for a row the split is the
  index itself, so every dump golden was unchanged. Reduce along an axis is
  `reduce_rows<len, inner>` at the element's base; scan is `scan_at<inner>`; join and
  stack along an inner axis copy one slab per outer block.
- **Corpus:** `sum(2)`/`sum(n)` and `transpose(n)` rewritten with axes.
- **Test:** `run_shape_ops_test.sh` (in CI): 208 cases on ranks 1–3 at every axis, and
  in voicers (flat and per-voice structs), each outputting (result − a reference computed
  on flat arrays) = 0 in whole blocks and odd chunks, auto and serial forms; result
  shapes from the analysis dump; the compile errors.
- **Found on the way:** an AoS (per-voice struct) voicer never ran its voice body's
  block-placed loops, so a block-rate value such as a note parameter times a constant
  tensor read 0 — fixed (`_aosVoiceBlockLoops`). `viDiv(1, b)` returned b.
- **Not done:** `at`/`put` along an axis (they still index the flattening); the varying
  `slice`/`rotate` is a per-element `mod` index (no pointer tables rebuilt per
  parameter-group change yet); index maps are not yet folded into consumers' channel
  tables (each operation's input is materialized, as before).

**M9: Voicer block form (§4.6), portability and performance.**
- Channel-vector loops over voices, skipping idle groups, per-voice parameter caches.
- An x86-64 Linux build with Sleef inline headers, FTZ and DAZ on audio and render
  threads, and a per-ISA register budget in the cost table.
- `benchmarks/`: a block-engine suite.

**MF: Inter-node feedback (D1 phase 2).** Any time after M5, and not required for the
merge.
- SCC detection in `sortNodes`.
- Sample-interleaved SCC execution, with input and output pointers offset per sample.
- `feedbackMode = sample | block` per silo.
- Lift the `tzpl_errCyclicConnection` refusal.
- Test against the old one-sample semantics, using a corpus file from M0 that has
  inter-node feedback.

**M10: Docs, cleanup, merge.**
- Rewrite these docs:
  - `Writing_SynthDefs.html` §2 (rates and shapes), §5 (vec ops as views), §7 (inputs
    and specs replace Controls), §9 (delays and feedback in blocks), §12 (voicers);
  - Cookbook `setControl` sections;
  - `Live_Controls_and_Notebooks.html` §3.2 and §3.4;
  - `FFI_Guide.html` §18.7–18.8 and the OSC/NATS tables;
  - `Music_Libraries.html`;
  - `engine/Architecture.md` and `engine/CLAUDE.md`;
  - the root `CLAUDE.md` ("processes one sample at a time" and the synthdef-compiler
    references).
- Delete `synthdef-compiler/` and its CI steps, and delete `shared/tzpl_simd.hpp` if
  nothing still uses it.
- Add a NEWS.md entry (breaking: ABI v3, `setControl` removed).
- Merge to `main`.

**Dependencies:**

```
M0 ─┬─ M1 ───────────────────────────┐
    └─ M2 ─ M3 ─ M4 ─ M5 ─┬─ M6 ─────┤
                          ├──────────┴─ M7 ─ M8 ─ M9 ─ M10
                          └─ MF (optional before merge)
```

---

## 8. Testing strategy

**Kept, adapted:**
- the lang goldens (`lang/tests/run_tests.sh`) for the language;
- `run_*.sh` render tests, with renders compared against the M0 corpus by tolerance,
  not bit-exactly.

**Retired:**
- the byte-for-byte synthc ↔ C++ text diffs (`synthc_*_diff.x`, `test_synthdef_compiler_ffi`'s
  oracle cases);
- `run_synthc_render_ab.sh`, replaced by corpus comparison;
- `synthdef-compiler --test`.

**New:**
1. Support library tests and tensor-ugens reference comparisons (M1, M7).
2. `test_block_engine` (M2).
3. synthc analysis dump goldens (M3).
4. Corpus re-render comparison (M5 onward).
5. Equivalence of forms per synth (M8): forced strategies × block sizes × odd chunks ×
   channel counts {1, 2, 3, 4, 5, 8} × voice counts {1, 3, 12}.
6. Benchmarks (M6, M8, M9).

**Rules:**
- **Silence assertions on every A/B.** Parity tests pass vacuously when both sides are
  silent ([[synthdef-nonpow2-voicer-mask-bug]]).
- **Tolerances everywhere.** Results are not bit-exact across ISAs under fast-math, so
  events such as phase wraps are taken from the code under test (TU §7).
- **CI:** add the support library tests, `test_block_engine` and the corpus check to
  `.github/workflows/tests.yml` on macOS and Linux.

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| synthc rewrite size: about 9.8K LOC, with `codegen.x` alone 3.9K | M4 reuses the existing loop-body emitters inside a sample loop. Fusion (M8) is new code, but runs on a working system with equivalence tests. |
| A sample-interleaved SCC costs per-call overhead × nodes × B (MF) | Only cyclic patches pay it. `block` mode exists. Measure in MF. |
| Code size from scalar tails and the loop-kind variants | Tails are just the plain recurrence. Dual forms only for affine cycles fed by parameter groups. Measure dylib size and compile time in M8. Keep the PCH. |
| Compile time of fused loops at `-O3` (defSynth latency in live coding) | Track per-synth compile time in the benchmarks. Offer `-O2` for live redefinitions. |
| Recurrence classification misses a cycle that has a parallel form (written in an unexpected shape) | It falls back to the General class, which is still correct and still vectorizes everything outside the cycle. The analysis dump names the blocking node. Grow the recogniser from the ugen library and the tensor-ugens comparisons. |
| Changing the voice model (D2) breaks the music and live libraries in subtle ways | It lands in M4 and M5 behind the corpus gate. Voicer tests ported first. |
| Inter-node feedback semantics in `block` mode | The default `sample` mode keeps today's sound. |
| `transpose4` cost on AVX2 | Fusion minimises transposes. Per-ISA cost table (TU §11). |
| Long-lived branch drifting from main | Rebase or merge `main` into the branch at each milestone. Avoid unrelated refactors on the branch. |

---

## 10. Out of scope (possible follow-ups)

- Retiming of the delay-weighted graph (Leiserson–Saxe): moving weight across nodes to
  make a short cycle vector-feasible or to shorten a critical cycle.
- Scans for higher-order affine recurrences with time-varying coefficients (a matrix
  scan); these use the General class for now.
- 8-lane f32 loops on AVX2 and AVX-512 builds.
- Lazy subgraphs triggered a sample at a time (TU run1 use case 1).
- The deferred Max/Pd event-system design ([[tzpl-event-system-deferred-design]]). It
  assumed event rate and needs rethinking: node-ID-as-signal sinks still make sense, and
  event wires may become impulse inputs.

---

## 11. Survey facts this plan relies on (as of 53f3c5f)

**Engine**
- Per-frame loop: `engine/src/tzpl_silo.cpp:174-279`. `runNodes`: `:158-172`.
- One-frame port buffers: `tzpl_node.cpp:44-121`.
- Zero-copy connect: `tzpl_silo.cpp:363`.
- Power-of-two `ChanAdapt`: `tzpl_chanadapt.cpp:29`.
- Cycles produce a one-sample delay through the sort's visiting state:
  `tzpl_silo.cpp:521-565`.
- XFader variants: `tzpl_xfader.cpp:139-221`.
- ABI v2: `shared/tzpl_plugin_abi.h:72`. `SynthFuns`: `:216-240`.
- The voicer is already in `shared/tzpl_voicer.hpp` and takes `now` from
  `Silo::sampleTime_`.
- The task scheduler and actors tick per sample: `bridge/src/tzpl_audio_engine_ffi.cpp:850-894`.

**synthc** (9,828 LOC)
- Per-sample `processAudio` with no frame count: `codegen.x:3492`.
- `chans` rounded by `asChans` (`synthdef.x:107`) and `padPow2` (`ir.x:153`).
- Mask broadcasting in `_indexWrap`: `codegen.x:180`.
- SIMD only across channels, width 4 when `chans % 4 == 0`: `codegen.x:1945-1964`.
- Iso-groups: `passes.x:1214`. They are the ancestor of parameter groups.
- Ugens are Tzopilotl graph code over `delayVar` (`common_ugens.x:1093` phasor,
  `:599-652` lags). Recurrence analysis (D3, §4.7) has to find their parallel forms.
- Compile and link live in `synthdef-compiler/src/synthdef_compile_link.cpp` (D9).

**Consumers**
- 9 `.x` files set controls or notes. 12 integration scripts use `setControl`,
  `noteSetParams` or proxy `set`.
- App: `controls_dispatch.cpp:140-185`, `graph_view.cpp:94-110`, `plugin_browser_window.cpp:49-83`.
- OSC: `tzpl_osc_engine_commands.cpp:315-397`. NATS: `tzpl_nats_engine_commands.cpp:280-386`.

**Pre-existing oddities found** (fix or delete on the branch)
- The legacy `sched_` / `streamTime_` path is unreachable.
- `ClearSchedCmd` doesn't clear the TempoClock queues.
- `Engine::noteOn` and `noteControl` are declared but never defined.
- The second `sortNodes()` in `processFrames` is unexplained ("glitches without it").
- `engine/Architecture.md` is stale in four places (scheduler bins, voicer location,
  buffer API, load protocol).
