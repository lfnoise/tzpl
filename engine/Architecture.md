
# Audio Engine Architecture

## Overview

The audio engine is a real-time system for instantiating, connecting, and processing
a dynamic graph of audio processing nodes. It supports:

- Dynamic loading of native synth plugins (`.dylib`, `.so` on Linux; plugin ABI v3)
- Block processing (default 64 samples) over tensor-shaped signals (up to 4 dimensions)
- Feedback between nodes (cycles run a sample at a time, or a block at a time)
- Multi-channel connections between nodes with type checking and broadcasting
- Inputs that hold values: stepped, ramped, impulsed, or crossfaded per channel
- Cross-fading on connect, disconnect, and input changes (7 curve types)
- Polyphonic voice management with voice stealing
- Sample-accurate command scheduling
- Parallel processing across multiple silos
- A safety limiter on the master output

The engine is designed for integration with a scripting language (Tzopilotl, through
`bridge/`) and a synth compiler that generates native plugin code from graph descriptions
(synthc, `lang/modules/synthc/`; compile and link in `bridge/src/tzpl_synth_build.cpp`).


## System Diagram

```
Client Code (any thread)
    |
    begin(engine) / newNode() / connect() / go(silo)
    |
    v
thread-local CmdBundle
    |
    v  push to AtomicFifo
+-------------------------------------------+
|  RT Audio Thread                          |
|  processAudioBlock()                      |
|    |-- signal worker silos to start       |
|    |-- process silo 0                     |
|    |-- binary-tree mixdown across silos   |
|    |-- apply safety limiter               |
+-------------------------------------------+
    |                         ^
    v  (completed cmds)       |  (dead nodes)
+------------------+    +------------------+
| NRT Cmd Thread   |    | Dead Node Thread |
| doNRT() cleanup  |    | delete nodes     |
| (polls 25ms)     |    | (polls 25ms)     |
+------------------+    +------------------+
```


## 1. Engine (`tzpl_engine.hpp/cpp`)

The `Engine` is the top-level object. It owns:

- **Silos** (`silos_`): A vector of `Silo` objects — independent parallel processing units.
- **NodeDef table** (`defs_`): A hash table (2048 bins, separate chaining) mapping
  hashed names to `NodeDef` pointers. All registered plugin types live here.
- **Audio backend** (`backend_`, an `AudioBackend`): opens the device and calls
  `processAudioBlock()` from its callback. The default is RtAudio (CoreAudio on macOS,
  ALSA/JACK/Pulse on Linux); hosts can supply their own (the JUCE app does). An NRT
  engine has none.
- **Safety limiter**: A lookahead peak limiter applied to the final mixed output.
- **Background threads**: An NRT command thread and a dead-node deletion thread.

### Engine Lifecycle

1. `newEngine(config, streamParams, backend = nullptr)` — Allocates the engine, creates
   silos, opens the audio device, defines the built-in "Audio Out" and "Audio In" nodes, and
   starts worker threads. `EngineConfig` sets `numSilos` (default 4), `numTempoClocks`
   (beat clock slots per silo, default 1) and `blockSize` (default 64, a multiple of 4, at
   most 1024).
2. `startAudio(e)` — Starts the backend's stream. The audio callback begins firing.
3. Client code sends commands via `begin()`/`go()`/`sched()`.
4. `stopAudio(e)` — Stops the audio stream.
5. `freeEngine(e)` — Tears everything down.

For offline rendering, `newEngineNRT(config, streamParams)` opens no device; the caller
calls `renderNRTBlock(e, out)` to render `streamParams.bufferFrames` frames at a time.

### Safety Limiter

The `SafetyLimiter` is a lookahead brickwall limiter on the master output:

- **Zap**: Replaces NaN, denormal, infinite, and very large values with zero.
- **Lookahead**: Delays the output by one device buffer to anticipate peaks.
- **Hold**: Holds the limiting gain for 250ms after a peak to avoid pumping.
- **Recovery**: Linear ramp back to unity gain (0.04 per device buffer).
- **Interpolation**: Gain is linearly interpolated across the buffer for smooth transitions.

The limiter works on whole device buffers (`bufferFrames`), not on engine blocks.

Applied after all silos are mixed down but before the output reaches the audio device.


## 2. Silos (`tzpl_silo.hpp/cpp`)

A **Silo** is an independent, parallel audio processing context. Each silo has its own:

- **Node graph**: A set of nodes with connections between them.
- **Worker thread**: Silo 0 runs on the audio callback thread; silos 1..N-1 each have
  a dedicated thread running at real-time priority (SCHED_RR, priority 63).
- **Command queues**: Lock-free SPSC FIFOs for RT/NRT communication.
- **TempoClock slots** (`tempoClocks_`, `tzpl_tempo_clock.hpp`): `numTempoClocks` beat
  clocks, each mapping beats to samples through a tempo ramp and holding a beat-sorted queue
  of scheduled bundles. Slot k is kept in sync across silos (tempo changes are broadcast to
  every silo's slot k).
- **Task hooks** (`taskNextFn_` / `taskRunFn_`): the bridge's silo task scheduler (silo VM
  coroutines and actor timers), merged with the clock queues in time order once per block.
- **Feedback mode** (`feedbackMode_`): how the silo runs cycles of nodes (see *Topological
  Sort*).
- **Output buffer**: Accumulated into the final mix via binary-tree reduction.

### Dual Node Tables

Each silo maintains two parallel hash tables (2048 bins each):

- `nrt_nodeTable_`: Modified only by the NRT/client thread. Used for command validation
  (e.g., looking up a node by ID before creating a connect command).
- `rt_nodeTable_`: Modified only by the RT thread. Used during audio processing.

Nodes are added to the NRT table immediately when a `newNode` command is created, and
added to the RT table when the `AddNodeCmd` executes on the RT thread. This two-table
design avoids any locking on the RT thread.

### Audio Processing Pipeline

Per audio callback (one device buffer of N frames), `processFrames()` processes the buffer
in chunks of up to one block (`blockSize_`, default 64; the last chunk may be shorter, and
synths handle any n, so this adds no latency). `processBlock(off, n)`:

1. **Process RT commands**: Pop command lists from `from_nrt_` FIFO. Immediate commands
   execute directly. Scheduled commands are queued on their TempoClock slot.
2. **Collect and run the block's events** (`collectAndRunEvents`): every clock-scheduled
   command due in `[T0, T0+n)` and every RT task wakeup, in time order. Each is resolved
   to a (fractional) sample time in the block and applied to the state it targets:
   input changes go to the input's per-channel value stages; node start/free and voice
   resets go to the node's per-block event list.
3. Deinterleave the hardware input into the input node's outlet.
4. `sortNodes()` — Re-run topological sort if connections changed.
5. `runNodes()` — Walk the sorted list; for each node, render its input stages
   (steps, ramps, impulses written into the input's own buffers; `isConst`/`version`
   set per channel), then call `process(off, len)` for each segment between the node's
   discrete events.
6. Taps; interleave the output node's input into the silo's output buffer.
7. `finishBlock()`: retire finished crossfaders, remove nodes freed in this block, and
   hand the block's commands to the NRT thread; then `sampleTime_ += n`.

After the last chunk, the GC heartbeat of an attached silo VM runs (once per device
buffer).

Then **Mixdown**: binary-tree reduction — even-indexed silos wait for their odd-indexed
sibling and sum the output buffers. This cascades until silo 0 has the final mix.

### Topological Sort

Nodes are sorted via depth-first traversal starting from the `outputNode_`:

- Follow each InPort's `srcPort_` connection upstream to the source node.
- Recurse on the source node's inputs.
- Append to the sorted list in post-order (dependencies before dependents).
- **Cycles** (feedback between nodes): the walk is Tarjan's strongly connected
  components. A cycle's nodes come out together in the sorted list, the first holding
  the group's length (`sccLen_`); an input whose source is still on the depth-first path
  closes the cycle (`InPort::fbEdge_`) and reads its own buffers instead of the source's.
  `Silo::runCycle` runs a cycle per the silo's `feedbackMode`: `sample` (default) runs its
  nodes a sample at a time (`Node::runSpan`, events at their sample) and fills each
  closing input with its source's previous sample, the per-sample engine's one-sample
  delay; `block` runs whole blocks with the closing inputs a block late.

The sort only runs when `needsSort_` is true (set by connect/disconnect operations). It is
generation-stamped (`sortGen_`) and never walks the previous sorted list, which may hold
hidden nodes the NRT side has already deleted.

### Inter-Silo Synchronization

- `start_sem_.signal()` (from `processAudioBlock`) wakes each worker silo.
- Each worker signals `done_sem_` when finished.
- During mixdown, even-indexed silos wait on the odd sibling's `done_sem_` and accumulate.


## 3. Nodes (`tzpl_node.hpp/cpp`)

A **Node** is an instance of a synth plugin within a silo. It contains:

- **`synth`**: A `tzpl_SynthData*` — the plugin instance. Plugins extend this struct with
  custom fields (C-style inheritance).
- **`nodeID`**: An integer identifier. 0 = output node, 1 = input node, -1 = crossfader
  sub-node. User nodes have arbitrary positive IDs.
- **`def`**: Pointer to the `NodeDef` describing this node type.
- **`funs`**: The `tzpl_SynthFuns` function table (alloc, free, init, process, resetVoice, etc.).
- **`ins`**: Vector of `InPort` objects. There are no separate controls: every parameter
  is an input, and a "control" is an input whose def carries a `tzpl_ControlSpec`.
- **`outs`**: Vector of `OutPort` objects.
- **`events_`**: The discrete events (start, free, resetVoice, reset) in the current block.
- **`voicer_`** / **`notesPort_`**: For voiced defs, the engine-side voice allocator and
  the index of the `notes` input.

### Ports

Every port channel is its own buffer of `maxBlock` samples. A synth sees each port as a
`tzpl_InPort` / `tzpl_OutPort`: an array of per-channel pointers plus per-channel
`isConst` and `version` flags.

**`InPort`** (input port):
- `chanPtrs_`: The per-channel pointers the synth reads. Each points either at the input's
  own buffer (`ownBufs_`) or at a source outlet channel.
- `stages_`: One `ChanStage` per channel, the value an unconnected channel holds: a
  constant, a running ramp (`rampTo`), or a pending one-sample impulse. `render()` writes
  them into the input's own buffers before the node runs, and sets `isConst_` /
  `version_`. A channel that holds still reports `isConst` and keeps its `version`, so
  generated code can skip recomputing what depends only on it.
- `srcPort_`: Pointer to the connected `OutPort`, or `nullptr` if disconnected.
- `srcMap_`: The source channel each channel reads when connected (broadcasting).
- `prev_`, `next_`: Doubly-linked list pointers — an InPort is linked into the source
  OutPort's destination list for fan-out tracking.
- `mixerNode_`: Hidden fan-in mixer, when two or more sources feed the input.

An unconnected input holds its spec's `init` (or 0) until something sets it.

**`OutPort`** (output port):
- `bufs_`: Per-channel buffers the plugin writes, plus the producer's `isConst_` /
  `version_` report.
- `dstList_`: Head of a doubly-linked list of connected `InPort*` objects. This tracks
  all destinations (fan-out).

### Zero-Copy Connection Mechanism

When `connect(outPort, inPort)` executes, each input channel's pointer is pointed at the
outlet channel it broadcasts from (per dimension, input index mod source size):
```
inPort->srcPort_ = outPort;
inPort->chanPtrs_[c] = outPort->bufs_[inPort->srcMap_[c]];
```

No data is copied during audio processing, and broadcasting a narrower source over a
wider input costs nothing: several channel pointers alias one buffer. When disconnected,
the channel pointers revert to the input's own buffers, refilled from its stages
(`restoreOwnBuffers`).

### Node Lifecycle

1. **Creation** (NRT): `Node::setupSynth()` allocates the `tzpl_SynthData` via
   `funs.alloc(maxBlock)`, creates the port objects with their per-channel buffers and
   value stages, sets up the `tzpl_InPort`/`tzpl_OutPort` arrays the synth sees, calls `funs.init()`, and inserts the node into the NRT
   node table.
2. **Activation** (RT): `AddNodeCmd::doRT()` inserts the node into the RT node table and
   sets `rt_active = true`.
3. **Deactivation** (RT): `RemoveNodeCmd::doRT()` removes the node from the RT node table.
4. **Deletion** (NRT): The node is either deleted in `RemoveNodeCmd::doNRT()` or pushed
   to the `dead_nodes_` FIFO for the dead-node thread to delete.


## 4. NodeDefs and the Plugin System

### NodeDef (`tzpl_node.hpp`)

A `NodeDef` describes a type of node. It is stored in the engine's `defs_` hash table.
Key fields:

- `name`: The def name (e.g., `"SinOsc"`, `"VoicerTest"`).
- `funs`: The `tzpl_SynthFuns` function table.
- `num_ins`, `num_outs`: Port counts.
- `ins`, `outs`: Arrays of `PortInfo` (name + signal type, plus an optional
  `tzpl_ControlSpec` on inputs: UI range, warp, kind, and the `init` value an unconnected
  input holds).
- `voiced`, `maxVoices`, `noteDefaults`: For voiced defs, which have a `notes` input of
  shape `{maxVoices, 1+N}` (column 0 the gate, columns 1..N the note parameters).

### Plugin ABI (`../shared/tzpl_plugin_abi.h`)

The plugin ABI is a pure C interface between the engine and plugins (those synthc
generates, the hand-written test plugins, or any other conforming library). Its version,
`TZPL_PLUGIN_ABI_VERSION`, is 3 (block processing); loaders refuse anything older.
It defines:

- **`tzpl_SynthData`**: The base struct for plugin instances. Contains the function table,
  engine/node pointers, `maxBlock`, the `tzpl_InPort`/`tzpl_OutPort` arrays, sample rate,
  and sample duration.
  Plugins extend this by placing `tzpl_SynthData` as the first member of their own struct.

- **`tzpl_SynthFuns`**: The plugin function table:
  | Function | Required | Purpose |
  |----------|----------|---------|
  | `alloc(maxBlock)` | Yes | Allocate a new instance |
  | `free` | Yes | Deallocate an instance |
  | `init` | Yes | Initialize after allocation (sample rate is set) |
  | `uninit` | No | Cleanup before deallocation |
  | `reset` | No | Reset state |
  | `process(offset, n)` | Yes | Compute samples `[offset, offset+n)` of every output |
  | `resetVoice(voice, offset)` | Voiced only | A new note starts on `voice` at `offset` |
  | `swapBuffer` | No | Replace a buffer slot, returning the old buffer for NRT cleanup |

  Notes are not plugin calls: the engine allocates voices and writes the rows of the
  `notes` input itself (§7).

- **`tzpl_InPort` / `tzpl_OutPort`**: What a synth sees of a port: `chans` (one pointer per
  channel to a buffer of `maxBlock` samples) plus per-channel `isConst` and `version`.
  The engine refreshes an input's flags before every `process` call. A synth may set its
  outputs' flags to report a channel constant for the block; the engine passes that on to
  the inputs the output feeds.

- **`tzpl_SignalType`**: Describes a port's data format: element type (`i32`/`i64`/`f32`/`f64`, in that enum order),
  rate (`const`/`init`/`reset`/`audio`; informational on ports, every input is audio
  rate at the ABI), and `tzpl_Shape`: up to 4 dimensions, row-major, unused leading
  dimensions 1. Channel counts are exact (no power-of-two rounding).

- **`tzpl_SynthDef`**: The static description of a plugin type, returned by the plugin's
  `load` function: name, function table, input and output `tzpl_PortDef`s (name, type, and
  for inputs an optional `tzpl_ControlSpec`), and for voiced synths `voiced`, `maxVoices`
  and `noteDefaults`.

### Plugin Loading

Plugins are shared libraries (`.dylib` on macOS, `.so` on Linux). The loading protocol
(`loadOneDef` in `tzpl_client_interface.cpp`):

1. `dlopen` loads the library.
2. `dlsym("tzpl_abi_version")` reads the ABI version stamp. A library without one, or with
   a version older than 3 or newer than the engine's, is refused.
3. `dlsym("load")`, of type `tzpl_SynthDef (*)(void)` (`tzpl_LoadSynthDefFun`), returns the
   def by value.
4. Optional symbols are probed: `loadBufferDefs`, `loadTags`, `loadSampleBankDefs`,
   `swapSampleBank`. A plugin that reads the shared-input table exports
   `tzpl_sharedInput`, which the engine points at its table.
5. The counts, arrays, shapes and required functions (`alloc`, `free`, `process`) are
   validated; a malformed def is refused.
6. `addSynthDef(engine, def, handle, ...)` registers it. A def with the name of an existing
   one supersedes it: nodes already running keep the old def (it is refcounted), new nodes
   use the new one.

`loadDefs(engine, dirPath)` loads every plugin library under a directory, recursively.
`loadDef(engine, dirPath, defName)` searches it for `<defName>_synth.dylib` (`.so`). synthc
builds name their libraries `<name>_synth_rN.dylib`, a new revision for each build, and the
bridge loads them by path. `getPluginFileDesc` reads a library's def without registering it
(the app's plugin browser).

### Signal Types and Type Checking

Connections require type compatibility:

- **Strict** (`compatibleTypes`): Exact match of element type and shape.
  Applies to the direct port-to-port links the engine builds internally.
- **Connectable** (`connectableTypes`): Element type only. This is what `connect()`
  requires of a user connection. Where the source is no larger than the input in every
  dimension it broadcasts (`broadcastsTo`: pointer remapping, no node); where it is
  larger in some dimension, a fold adapter is spliced in (see *Channel Adaptation*).

Type checking happens before any link is broken, so a rejected connect leaves whatever
was already feeding the inlet in place.


## 5. Commands (`tzpl_command.hpp`, `tzpl_command_subclasses.hpp`)

### Command Model

All mutations to the audio graph go through the command system. Commands have a two-stage
execution model:

- **Stage 1 — `doRT()`**: Runs on the RT thread. Performs the actual graph mutation
  (add/remove node, connect/disconnect, set values, trigger notes).
- **Stage 2 — `doNRT()`**: Runs on the NRT command thread. Handles cleanup that involves
  memory allocation/deallocation (e.g., deleting removed nodes). Returns `true` when the
  command itself can be deleted.

### Command Bundling

Commands are accumulated in a thread-local `CmdBundle`; the target silo is chosen
at submit time:

1. `begin(engine)` — Start a new bundle.
2. Zero or more command calls (`newNode()`, `connect()`, etc.) — Each records a
   deferred op (`BundleOp`) into the bundle. No silo is touched yet; the only error
   a builder can return is `errNoActiveBundle`.
3. `go(silo)` — Dispatch immediately (sugar for `sched(silo, 0, 0., schedImmediate)`).
4. `sched(silo, clock, beat, policy)` — Dispatch bound to a beat on a TempoClock slot.

At submit, the recorded ops are replayed in order against the chosen silo: node IDs
and ports are validated against the silo's NRT node-table mirror, connections are
type-checked, and the silo-bound objects (nodes, hidden mixers, crossfaders) are
allocated. The bundle is atomic: on the first invalid op everything already
materialized is freed (never-run Commands free what they own via `stage_ == 0`
destructor guards), the bundle is discarded, and the submit call returns the error.
Submit always closes the bundle.

On success the entire bundle is pushed atomically through the `from_nrt_` FIFO,
ensuring that a group of related commands (e.g., create a node and connect it)
execute together. Beat-scheduled bundles are re-validated on the RT thread when
they fire; a command whose target no longer (or does not yet) exist is silently
dropped.

### Scheduling

- **`schedImmediate`**: Execute at the start of the next block the RT thread processes.
- **`schedBetterLateThanNever`** (the default for `sched`): Fire when the clock slot reaches
  the beat; if the beat has already passed, execute at the start of the next block.
- **`schedOnTimeOnly`**: If the beat has already passed, set `tzpl_errTooLate` and discard.

Each TempoClock slot keeps a beat-sorted command queue. Once per block,
`collectAndRunEvents()` pops every command due in the block from every clock, resolves
each to its exact (fractional) sample time, and runs them, merged with RT task wakeups,
in time order (see *Audio Processing Pipeline*).

### Command Types

| Command | RT Action |
|---------|-----------|
| `AddNodeCmd` | Insert node into RT node table |
| `RemoveNodeCmd` | Remove node from RT table; NRT stage deletes it |
| `RemoveAllNodesCmd` | Remove all nodes except input/output |
| `ConnectCmd` | Connect ports, optionally with crossfade |
| `ReconnectOutputCmd` | Move all connections from one output to another |
| `DisconnectInputCmd` | Disconnect an input, optionally with fade-out |
| `DisconnectSourceCmd` | Disconnect one specific source from an input |
| `DisconnectOutputCmd` | Disconnect all destinations from an output |
| `DisconnectNodeCmd` | Disconnect all ports of a node |
| `SetInputCmd` | Value-stage operation on some channels of an input: step, ramp, impulse, or (on a connected input) a crossfade to constants via an XFader |
| `NoteOnCmd` | Allocate a voice, split the node at the onset (resetVoice), write its `notes` row |
| `NoteOffCmd` | Drop the voice's gate |
| `AllNotesOffCmd` | Release all notes |
| `NoteSetParamsCmd` | Step or ramp note parameters (index-value pairs) |
| `AllNotesOffAllCmd` | Release all notes on every voiced node in the silo |
| `ClearSchedCmd` | Drop every bundle still waiting on the silo's TempoClock slots |
| `ChannelOffsetCmd` | Set where the silo's output lands in the device channels |
| `ResizeBufferCmd` / `LoadBufferCmd` / `ReplaceBufferCmd` | Swap a buffer into a node's buffer slot; NRT stage frees the old one |
| `ReplaceSampleBankCmd` | Swap a sample bank into a node's bank slot |
| `TapOutletCmd` / `TapMasterCmd` / `UntapCmd` | Install or remove a signal tap |
| `SetClockTempoCmd` / `SchedClockTempoRampCmd` | Set or ramp a TempoClock slot's tempo; one is sent to every silo |


## 6. Cross-Fading (`tzpl_xfader.hpp/cpp`)

The crossfader system enables smooth transitions when connections change. It works by
creating temporary nodes that are transparently spliced into the signal graph.

### How It Works

When a command requests a crossfade (e.g., `connect(src, dst, xfadeTime)`):

1. A temporary XFader node is created (nodeID = -1, not in any hash table).
2. It is spliced into the graph:
   - Old source connects to XFader input 0.
   - New source connects to XFader input 1.
   - XFader output connects to the destination.
3. The XFader interpolates between old and new over `xfadeTime` seconds.
4. When the fade completes:
   - The XFader disconnects itself.
   - The surviving source is connected directly to the destination.
   - The XFader node is pushed to the dead-node queue for deletion.

This entire lifecycle is automatic and happens within the RT thread.

### XFader Variants

| Variant | Input 0 | Input 1 | Use Case |
|---------|---------|---------|----------|
| `XFadeTwo` | Old source (connected) | New source (connected) | Replacing one connection with another |
| `XFadeIn` | Constant (the input's held value) | New source (connected) | Fading in from silence/default |
| `XFadeOut` | Old source (connected) | Constant (the input's held value) | Fading out to silence/default |
| `XFadeSet` | Old constant | New constant | Crossfading between constants |

A `setInputX` on an *unconnected* input needs no fader: a crossfade between two
constants is a ramp in the input's value stages. On a connected input, an XFader fades
the signal out to the new value, which the input then holds. Fades compose: a crossfade
started while another runs takes the running fader's output as its old input. Fade
starts are sub-sample (a fractional start time), and the curves are shared with ramps
(`tzpl_fade_curves.hpp`).

### Fade Curves

Seven interpolation curves are available:

| Curve | Description |
|-------|-------------|
| `fadeLinear` | Straight line: `a + x*(b-a)` |
| `fadeExponential` | Exponential: `a * pow(b/a, x)` |
| `fadeSmoothstep` | Hermite S-curve: `x*x*(3-2*x)` |
| `fadeEqualPower` | Constant-power crossfade (cubic approximation) |
| `fadeOutIn` | Fade out first, then fade in (V-shaped dip) |
| `fadeEaseInCubic` | Slow start, fast end |
| `fadeEaseOutCubic` | Fast start, slow end |


## 6a. Hidden Helper Nodes

Besides crossfaders, two other node kinds are created by the engine itself. All of them
have `nodeID == -1`, live in no hash table, and never appear in the topology shadow --
the graph view shows the user's connection, not the machinery behind it.

### Fan-In Mixers (`tzpl_mixer.hpp/cpp`)

Connecting a second source to an occupied inlet splices in a mixer that sums its inputs
and drives the destination; `InPort::mixerNode_` points at it. A mixer has a fixed number
of input slots (4). When they are all taken, a further connect *chains* another mixer in
front as the new head, holding the old head in one of its slots -- fan-in is unbounded.
Chaining never moves an existing link, which is what makes it safe while crossfaders are
running into slots. When one source is left the chain collapses back to a direct
connection (deferred while a fade is still in flight, since the fader reads a slot's
buffer).

### Channel Adaptation (`tzpl_chanadapt.hpp/cpp`)

Any outlet can drive any inlet of the same element type. Shapes combine per dimension:

- **Broadcasting** (source no larger than the input in every dimension): no node at all.
  Each input channel points at the source channel it maps to (input index mod source size,
  per dimension). A mono source fills every destination channel; stereo into a quad inlet
  lands as (L, R, L, R).
- **Folding** (source larger in some dimension): a fold adapter node is spliced in
  immediately after the source, so everything downstream of it -- crossfader, mixer,
  destination -- sees a shape that broadcasts. Each output channel sums every source
  channel that lands on it (source index mod destination size, per dimension). An
  8-channel source into a stereo inlet mixes down to (a0+a2+a4+a6, a1+a3+a5+a7).

Channel counts are exact; nothing is rounded to a power of two.

An adapter belongs to exactly one connection and retires itself (disconnects and goes to
the dead-node queue) as soon as either end is unlinked. `Silo::unlink` is the single
funnel where that is detected, which imposes one rule on the rest of the engine: when
re-routing a source, **connect the new link before breaking the old one**, or an adapter
in that source's path will retire out from under the rewire.


## 7. Polyphonic Voice Management (`tzpl_note_voicer.hpp`)

Voices are allocated by the engine, not the plugin. A voiced def has an input named
`notes` of shape `{maxVoices, 1+N}`: column 0 is the gate, columns 1..N the note
parameters. `NoteVoicer` only decides which row a note gets; the silo then writes the row
as input-stage steps at the command's sample:

- **`noteOn`**: Allocate a voice, add a `resetVoice` event at the onset (the node's block
  is split there, and the plugin re-initializes that voice), step the gate to 1 and the
  parameters to the note's values. Parameters the note does not give take the def's
  `noteDefaults`.
- **`noteOff`**: Step the voice's gate to 0.
- **`noteSetParams`**: Step or ramp parameter columns of the note's row.
- **Voice stealing**: Take the voice released longest ago, else steal the one started
  longest ago, scanning from a rotating start so voices freed at the same time are handed
  out in turn (policy shared with `shared/tzpl_voicer.hpp`).

Silo tasks reach the same operations without a bundle (`rtNoteOn`, `rtNoteOff`,
`rtNoteSetParams`), at the task's wake time.


## 8. Threading Model

### Threads

| Thread | Role | Priority |
|--------|------|----------|
| Audio callback (or the NRT render thread) | Signals workers, processes silo 0, mixdown, safety limiter | Real-time (system) |
| Worker threads (1..N-1) | Each processes one silo | SCHED_RR, priority 63 |
| NRT command thread | Runs `doNRT()` on completed commands | Normal (polls 25ms) |
| Dead node thread | Deletes nodes pushed from the RT thread | Normal (polls 25ms) |
| Client threads | Send commands via `begin()`/`go()`/`sched()` | Normal |

### Inter-Thread Communication

```
Client thread  --[AtomicFifo: from_nrt_]--> RT thread
RT thread      --[AtomicFifo: to_nrt_]----> NRT cmd thread
RT thread      --[AtomicFifo: dead_nodes_]-> Dead node thread
```

All three FIFOs are lock-free SPSC (single producer, single consumer) queues based on
the Le, Guatto, Cohen, Pop algorithm (SBAC-PAD 2013).

### Real-Time Safety Guarantees

- **No allocation on the RT thread**: All objects (nodes, commands) are allocated on
  NRT threads and passed to the RT thread via FIFOs.
- **No locks on the RT thread**: Communication uses only lock-free FIFOs and semaphores.
- **Deferred deletion**: Nodes removed in RT are pushed to a FIFO and deleted by a
  background thread.
- **Exception guarding**: The audio callback wraps processing in try/catch to prevent
  exceptions from crashing the audio thread.
- **NaN/denormal protection**: Every silo worker thread, and the thread calling
  `processAudioBlock` (device callback or NRT renderer), sets flush-to-zero
  (`shared/tzpl_denormals.hpp`), so plugins never compute on subnormals. The safety
  limiter's `zap()` function eliminates bad floating-point values from the output buffer.


## 9. S-Expression Parser (`tzpl_sexpr.hpp/cpp`)

The engine includes an s-expression parser. An s-expression is parsed into `sexpr::Item`
values — a variant of `bool`, `int64_t`, `double`, `Symbol`, `std::string`, or
`std::vector<Item>`. Nothing in the engine executes s-expression commands; only the old
manual test driver (`main.cpp`) uses the parser. The binary message format used over NATS
is `shared/tzpl_sexpr_bin.hpp`.


## 10. Built-In Plugins (`tzpl_test_plugins.cpp`)

Several hand-written ABI v3 plugins are defined in `tzpl_test_plugins.cpp` for testing:

| Plugin | Description | Inputs | Outputs |
|--------|-------------|--------|---------|
| `voicer` | 32-voice sine voicer (params: pitch, amp) | `notes` (32x3 f32) | 1: out (2ch f32) |
| `sinosc` | Sine oscillator | 2: freq, amp (f32, with specs) | 1: out (2ch f32) |
| `+` / `*` | Addition / multiplication | 2: a, b (2ch f32) | 1: out (2ch f32) |
| `counter` | Sample counter | none | 1: out (1ch f32) |
| `vtest` | Voiced test def | `notes` (4x2 f32) | 1: out (4ch f32) |
| pass / rec defs | Pass-through and recorder, any shape | 1: in | 1: out |

The bridge also registers native `_wireN` / `_gainN` defs (and shaped forms such as
`_wire2x3`) on demand (`tzpl_builtin_defs.cpp`).


## 11. Client API Summary (`tzpl_client_interface.hpp`)

### Engine Lifecycle
- `newEngine(config, streamParams, backend = nullptr)` / `newEngineNRT(config, streamParams)` / `freeEngine(e)`
- `startAudio(e)` / `stopAudio(e)` / `isAudioRunning(e)`
- `renderNRTBlock(e, out)` — NRT engines: render one buffer.
- `masterGain(e, gain)` / `masterMute(e, mute)` / `safetyLimiter(e, onoff)`
- `feedbackMode(e, silo, FeedbackMode::sample | block)` — how a silo runs cycles.

### Plugins and Introspection
- `loadDefs(e, dirPath)` / `loadDef(e, dirPath, defName)` / `loadOneDef(e, path)`
- `addSynthDef(e, def, dlHandle, ...)` — register a def directly (built-in and synthc defs).
- `copyNodeDefs(from, to)` — give a new engine (an NRT render) the same defs.
- `listNodeDefs` / `getDefDesc` / `listDefDescs` — `DefDesc` lists inputs (with shape and
  spec), outputs, buffers, sample banks, `voiced` and `maxVoices`.
- `listPluginFiles` / `getPluginFileDesc` — find and describe plugin files without loading them.
- `graphGeneration(e)` / `getGraphDesc(e, silo, out)` — the topology shadow (graph view).

### Command Bundling
- `begin(e)` — Start a command bundle.
- `go(silo)` — Dispatch to a silo immediately.
- `sched(silo, clock, beat, policy)` — Dispatch to a silo at a beat on a TempoClock slot.

### Tempo Clocks
- `setTempo(e, clock, bpm)` / `schedTempoChange(e, clock, atBeat, targetBPM, rampBeats)` —
  applied to slot `clock` on every silo; not bundled.
- `clockBeats(e, clock)` / `clockTempoBPM(e, clock)` / `clockQuery(...)`

### Graph Mutation
- `newNode(defName, nodeID)` / `freeNode(nodeID)` / `freeAllNodes()`
- `connect(src, dst, xfadeTime, curve)` -- broadcasts or folds differing shapes; may close a cycle
- `disconnectInput(dst, xfadeTime, curve)` / `disconnectSource(src, dst, xfadeTime, curve)` /
  `disconnectOutput(src)` / `disconnectNode(nodeID)`
- `reconnectOutput(oldSrc, newSrc, xfadeTime, curve)`
- `replaceNode(oldNodeID, newNodeID, xfadeTime, curve)`
- `channelOffset(offset)` — where the silo's output lands in the device channels.

### Input Values
Inputs are addressed by port index or by name (resolved at submit), and a `Region`
(the whole port, a flat channel range, or a box: offset and extent per dimension) picks the
channels. Values are broadcast cyclically over the region. A region that does not fit the
shape aborts the bundle with `tzpl_errRegionOutOfRange`.
- `setInput(port, region, values, rampTime = 0, curve)` — Step, or ramp from the current value.
- `setInputX(port, region, values, xfadeTime, curve)` — Crossfade to the values (a connected
  input fades its signal out and is left holding them; whole port only).
- `impulseInput(port, region, values)` — The values for one sample, then back.
- `setInput(port, numValues, values, xfadeTime, curve)` — older whole-port forms (f32, f64,
  i32, i64 values; channels `[0, numValues)`, no broadcasting).
- `setSharedInput(slot, value)` / `getSharedInput(slot)` — the process-global shared-input
  table plugins read directly; not bundled, not scheduled.

### Polyphonic Notes
- `noteOn(nodeID, noteID, length, paramValues)`
- `noteOff(nodeID, noteID)`
- `allNotesOff(nodeID)` / `allNotesOffAll()` (every voiced node in the silo)
- `noteSetParams(nodeID, noteID, n, params, rampTime = 0)`
- `noteSetParamRange(nodeID, noteID, first, length, values, rampTime = 0)`
- `rtNoteOn` / `rtNoteOff` / `rtNoteSetParams(silo, ...)` — from silo tasks on the RT
  thread, at the task's event time, without a bundle.
- `clearSched()` — drop every beat-scheduled bundle still pending in the silo.

### Buffers and Sample Banks (bundled)
- `resizeBuffer(nodeID, bufID, numChannels, length)` — an empty buffer.
- `loadBuffer(nodeID, bufID, path, channelOffset, frameOffset, numFrames)` — reads the file
  when the command is recorded.
- `replaceBuffer(nodeID, bufID, buffer)` — a buffer the caller built.
- `loadSampleBank(nodeID, bankID, zones)` / `replaceSampleBank(nodeID, bankID, bank)`

The swap onto the node happens on the RT thread; the replaced buffer or bank is freed on
the NRT thread.

### Taps and Metering
- `tapOutlet(nodeID, outlet, tapID, mode, ...)` / `tapMaster(tapID, mode, ...)` / `untap(tapID)` (bundled)
- `tapPeak` / `tapRms` / `tapChans` / `tapShape` / `tapDrain`, and `rtTap*` for silo code
- `masterPeak` / `masterRms` / `masterPeakHold` / `masterClipCount`
- `getEngineStats` / `resetEngineStats`
