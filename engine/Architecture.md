
# Audio Engine Architecture

## Overview

The audio engine is a real-time system for instantiating, connecting, and processing
a dynamic graph of audio processing nodes. It supports:

- Dynamic loading of native synth plugins (`.dylib`)
- Multi-channel connections between nodes with type checking
- Cross-fading on connect, disconnect, and parameter changes (7 curve types)
- Polyphonic voice management with voice stealing
- Sample-accurate command scheduling
- Parallel processing across multiple silos
- A safety limiter on the master output

The engine is designed for integration with a scripting language and a synthdef compiler
that generates native plugin code from graph descriptions.


## System Diagram

```
Client Code (any thread)
    |
    begin(engine, silo) / newNode() / connect() / go()
    |
    v
thread-local CmdBundle
    |
    v  push to AtomicFifo
+-------------------------------------------+
|  RT Audio Thread                          |
|  audioCallback()                          |
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
- **RtAudio instance**: Manages the platform audio I/O (CoreAudio on macOS).
- **Safety limiter**: A lookahead peak limiter applied to the final mixed output.
- **Background threads**: An NRT command thread and a dead-node deletion thread.

### Engine Lifecycle

1. `newEngine(config, streamParams)` — Allocates the engine, creates silos, opens the
   audio device, defines the built-in "Audio Out" node, and starts worker threads.
2. `startAudio(e)` — Starts the RtAudio stream. The audio callback begins firing.
3. Client code sends commands via `begin()`/`go()`/`sched()`.
4. `stopAudio(e)` — Stops the audio stream.
5. `freeEngine(e)` — Tears everything down.

### Safety Limiter

The `SafetyLimiter` is a lookahead brickwall limiter on the master output:

- **Zap**: Replaces NaN, denormal, infinite, and very large values with zero.
- **Lookahead**: Uses a one-block delay to anticipate peaks.
- **Hold**: Holds the limiting gain for 250ms after a peak to avoid pumping.
- **Recovery**: Linear ramp back to unity gain (0.04 per block).
- **Interpolation**: Gain is linearly interpolated across the block for smooth transitions.

Applied after all silos are mixed down but before the output reaches the audio device.


## 2. Silos (`tzpl_silo.hpp/cpp`)

A **Silo** is an independent, parallel audio processing context. Each silo has its own:

- **Node graph**: A set of nodes with connections between them.
- **Worker thread**: Silo 0 runs on the audio callback thread; silos 1..N-1 each have
  a dedicated thread running at real-time priority (SCHED_RR, priority 63).
- **Command queues**: Lock-free SPSC FIFOs for RT/NRT communication.
- **Scheduler**: A hash-wheel scheduler for time-ordered command dispatch.
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

Per audio callback (one buffer of N frames):

1. **Process RT commands**: Pop command lists from `from_nrt_` FIFO. Immediate commands
   execute directly. Scheduled commands are inserted into the scheduler queue.
2. **Per-sample inner loop** (for each frame):
   a. `processScheduledEvents()` — Pop and execute commands due at the current sample time.
   b. `sortNodes()` — Re-run topological sort if connections changed.
   c. `runNodes()` — Walk the sorted list, calling `processAudio()` on each node.
   d. Copy the output node's input data to the silo's output buffer.
   e. Increment `sampleTime_`.
3. **Mixdown**: Binary-tree reduction — even-indexed silos wait for their odd-indexed
   sibling and sum the output buffers. This cascades until silo 0 has the final mix.

### Topological Sort

Nodes are sorted via depth-first traversal starting from the `outputNode_`:

- Follow each InPort's `srcPort_` connection upstream to the source node.
- Recurse on the source node's inputs.
- Append to the sorted list in post-order (dependencies before dependents).
- **Cycle handling**: If a node is encountered while already being visited (`sortVisiting`
  state), the recursion stops. This means cycles introduce a one-sample delay rather than
  causing an error — the feedback signal is simply one sample old.

The sort only runs when `needsSort_` is true (set by connect/disconnect operations).

### Inter-Silo Synchronization

- `dispatch_semaphore_signal(start_sem_)` wakes each worker silo.
- Each worker signals `done_sem_` when finished.
- During mixdown, even-indexed silos wait on the odd sibling's `done_sem_` and accumulate.


## 3. Nodes (`tzpl_node.hpp/cpp`)

A **Node** is an instance of a synth plugin within a silo. It contains:

- **`synth`**: A `tzpl_SynthData*` — the plugin instance. Plugins extend this struct with
  custom fields (C-style inheritance).
- **`nodeID`**: An integer identifier. 0 = output node, 1 = input node, -1 = crossfader
  sub-node. User nodes have arbitrary positive IDs.
- **`def`**: Pointer to the `NodeDef` describing this node type.
- **`funs`**: The `tzpl_SynthFuns` function table (alloc, free, init, processAudio, etc.).
- **`ins`**: Vector of `InPort` objects.
- **`outs`**: Vector of `OutPort` objects.
- **`controls`**: Vector of `Control` objects.

### Ports

**`InPort`** (input port):
- `dataBuffer_`: Heap-allocated buffer holding a constant/default value. When disconnected,
  the plugin reads from this buffer.
- `srcPort_`: Pointer to the connected `OutPort`, or `nullptr` if disconnected.
- `prev_`, `next_`: Doubly-linked list pointers — an InPort is linked into the source
  OutPort's destination list for fan-out tracking.

**`OutPort`** (output port):
- `dataBuffer_`: Heap-allocated buffer where the plugin writes its output.
- `dstList_`: Head of a doubly-linked list of connected `InPort*` objects. This tracks
  all destinations (fan-out).

### Zero-Copy Connection Mechanism

When `connect(outPort, inPort)` executes:
```
inPort->srcPort_ = outPort;
inPort->node_->synth->inlets[inPort->index_] = outPort->dataBuffer_;
```

The inlet pointer is redirected to point directly at the source's output buffer.
No data is copied during audio processing — the downstream node reads directly from the
upstream node's output buffer.

When disconnected, the inlet pointer reverts to the InPort's own `dataBuffer_`:
```
inPort->srcPort_ = nullptr;
inPort->node_->synth->inlets[inPort->index_] = inPort->dataBuffer_;
```

### Node Lifecycle

1. **Creation** (NRT): `Node::setupSynth()` allocates the `tzpl_SynthData` via
   `funs.alloc()`, sets up the inlet/outlet/control pointer arrays, creates port objects
   with their own data buffers, calls `funs.init()`, and inserts the node into the NRT
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
- `numIns`, `numOuts`, `numControls`: Port and control counts.
- `ins`, `outs`: Arrays of `PortInfo` (name + signal type).
- `controls`: Array of `ControlInfo` (name + signal type + controlID + spec).
- `controlMap_`: Maps controlID to control index for O(1) lookup.

### Plugin ABI (`../shared/tzpl_plugin_abi.h`)

The plugin ABI is a pure C interface shared between the engine and the synthdef compiler.
It defines:

- **`tzpl_SynthData`**: The base struct for plugin instances. Contains the function table,
  engine/node pointers, inlet/outlet/control arrays, sample rate, and sample duration.
  Plugins extend this by placing `tzpl_SynthData` as the first member of their own struct.

- **`tzpl_SynthFuns`**: The plugin function table:
  | Function | Required | Purpose |
  |----------|----------|---------|
  | `alloc` | Yes | Allocate a new instance |
  | `free` | Yes | Deallocate an instance |
  | `init` | Yes | Initialize after allocation (sample rate is set) |
  | `uninit` | No | Cleanup before deallocation |
  | `reset` | No | Reset state |
  | `processAudio` | Yes | Process one sample frame |
  | `processEvents` | No | Process pending events |
  | `event` | No | Handle an event |
  | `noteOn` | No | Start a polyphonic note |
  | `noteOff` | No | Release a note |
  | `allNotesOff` | No | Release all notes |
  | `noteSetParams` | No | Set note parameters by index-value pairs |
  | `noteSetParamRange` | No | Set a range of note parameters |

- **`tzpl_SignalType`**: Describes a port's data format: element type (`i32`/`f32`/`i64`/`f64`),
  rate (`const`/`init`/`reset`/`event`/`audio`), and channel count.

- **`tzpl_SynthDef`**: The static description of a plugin type, provided by the plugin's
  `load` function.

### Plugin Loading

Plugins are `.dylib` shared libraries. The loading protocol:

1. Engine scans a directory for files matching `*_synth.dylib`.
2. `dlopen` loads the library.
3. `dlsym` looks up a symbol named `"load"` of type `void (*)(Engine*)`.
4. The load function calls `addNodeDef(engine, info)` to register the plugin.

A single plugin can also be loaded by name via `loadDef(engine, dirPath, defName)`.

### Signal Types and Type Checking

Connections require type compatibility:

- **Strict** (`compatibleTypes`): Exact match of rate, element type, and channel count.
- **Relaxed** (`relaxedCompatibleTypes`): Allows channel count mismatch. Used for
  connections to the output node (ID 0). Extra channels are zero-filled; fewer channels
  are truncated.


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

Commands are accumulated in a thread-local `CmdBundle`:

1. `begin(engine, silo)` — Start a new bundle targeting a specific silo.
2. Zero or more command calls (`newNode()`, `connect()`, etc.) — Each creates a Command
   object and appends it to the bundle.
3. `go()` — Dispatch immediately (sugar for `sched(0., schedImmediate)`).
4. `sched(time, policy)` — Dispatch at a specific stream time.

The entire bundle is pushed atomically through the `from_nrt_` FIFO, ensuring that a group
of related commands (e.g., create a node and connect it) execute together.

### Scheduling

- **`schedImmediate`**: Execute as soon as the RT thread picks up the command.
- **`schedBetterLateThanNever`**: Convert stream time to sample time. If late, execute anyway.
- **`schedOnTimeOnly`**: If late, set error and discard.

The `SchedulerQueue` is a hash wheel with 1021 bins. Each bin holds a `TimeSortedCommandList`
(doubly-linked, sorted by sample time). At each sample frame, `processScheduledEvents()`
pops commands due at the current `sampleTime_`.

### Command Types

| Command | RT Action |
|---------|-----------|
| `AddNodeCmd` | Insert node into RT node table |
| `RemoveNodeCmd` | Remove node from RT table; NRT stage deletes it |
| `RemoveAllNodesCmd` | Remove all nodes except input/output |
| `ConnectCmd` | Connect ports, optionally with crossfade |
| `ReconnectOutputCmd` | Move all connections from one output to another |
| `DisconnectInputCmd` | Disconnect an input, optionally with fade-out |
| `DisconnectOutputCmd` | Disconnect all destinations from an output |
| `DisconnectNodeCmd` | Disconnect all ports of a node |
| `SetInputCmd<T>` | Set input to a constant value, optionally with crossfade |
| `SetControlCmd<T>` | Set a control parameter on a node |
| `NoteOnCmd` | Trigger a polyphonic note |
| `NoteOffCmd` | Release a note |
| `AllNotesOffCmd` | Release all notes |
| `NoteSetParamRangeCmd` | Set a range of note parameters |
| `NoteSetParamsCmd` | Set note parameters by index-value pairs |


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
| `XFadeIn` | Constant (normalled value) | New source (connected) | Fading in from silence/default |
| `XFadeOut` | Old source (connected) | Constant (normalled value) | Fading out to silence/default |
| `XFadeSet` | Old constant | New constant | Changing a `setInput` value smoothly |

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


## 7. Polyphonic Voice Management (`main.cpp` — Voicer template)

The `Voicer<MaxVoices, NumParams, RowsOrCols>` template class provides polyphonic voice
allocation for plugins that support `noteOn`/`noteOff`:

- **Voice stealing**: When all voices are active, steals the oldest released voice. If none
  are released, steals the oldest active voice.
- **Note ID lookup**: A hash table maps noteID to voice index for O(1) access.
- **Memory layouts**: `VoicesInColumns` (SIMD-friendly, parameters are contiguous across
  voices) or `VoicesInRows` (scalar-friendly, voices are contiguous).
- **Parameter setting**: `noteSetParamRange` and `noteSetParams` update per-voice parameters.

The `VoicerTest` in `main.cpp` demonstrates a 32-voice FM/waveshaping synthesizer using
this system.


## 8. Threading Model

### Threads

| Thread | Role | Priority |
|--------|------|----------|
| Audio callback | Signals workers, processes silo 0, mixdown, safety limiter | Real-time (system) |
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
- **NaN/denormal protection**: The safety limiter's `zap()` function eliminates bad
  floating-point values from the output buffer.


## 9. S-Expression Parser (`tzpl_sexpr.hpp/cpp`)

The engine includes an s-expression parser for text-based command input. An s-expression
is parsed into `sexpr::Item` values — a variant of `bool`, `int64_t`, `double`, `Symbol`,
`std::string`, or `std::vector<Item>`.

Example command format:
```lisp
(sched 100
  (newNode sinosc 101)
  (setInput (101 0) (300))
  (connect (101 0) (0 0)))
```

A binary serialization format (`Builder`/`ListBuilder`) is partially implemented for
compact encoding of s-expressions.


## 10. Built-In Plugins (`main.cpp`)

Several plugins are defined directly in `main.cpp` for testing:

| Plugin | Description | Inputs | Outputs |
|--------|-------------|--------|---------|
| `VoicerTest` | 32-voice FM/waveshaping synth with pan, envelope, drive | None | 2ch (stereo) f32 audio |
| `SinOsc` | Sine oscillator | 2: freq (f32), amp (f32) | 1: out (2ch f32 audio) |
| `AddOp` | Addition operator | 2: a (2ch f32 audio), b (2ch f32 audio) | 1: out (2ch f32 audio) |
| `MulOp` | Multiplication operator | 2: a (2ch f32 audio), b (2ch f32 audio) | 1: out (2ch f32 audio) |


## 11. Client API Summary (`tzpl_client_interface.hpp`)

### Engine Lifecycle
- `newEngine(config, streamParams)` / `freeEngine(e)`
- `startAudio(e)` / `stopAudio(e)`
- `loadDefs(e, dirPath)` / `loadDef(e, dirPath, defName)`

### Command Bundling
- `begin(e, silo)` — Start a command bundle for a silo.
- `go()` — Dispatch immediately.
- `sched(time, policy)` — Dispatch at a scheduled time.

### Graph Mutation
- `newNode(defName, nodeID)` / `freeNode(nodeID)` / `freeAllNodes()`
- `connect(src, dst, xfadeTime, curve)`
- `disconnectInput(dst, xfadeTime, curve)` / `disconnectOutput(src)` / `disconnectNode(nodeID)`
- `reconnectOutput(oldSrc, newSrc, xfadeTime, curve)`
- `replaceNode(oldNodeID, newNodeID, xfadeTime, curve)`

### Parameter Control
- `setInput(inPort, numValues, values, xfadeTime, curve)` — Set input to constant.
- `setControl(nodeID, controlID, numValues, values)` — Set a control parameter.

### Polyphonic Notes
- `noteOn(nodeID, noteID, length, paramValues)`
- `noteOff(nodeID, noteID)`
- `allNotesOff(nodeID)`
- `noteSetParams(nodeID, noteID, n, params)`
- `noteSetParamRange(nodeID, noteID, first, length, values)`

### Buffers (declared, not yet implemented)
- `newBuffer` / `freeBuffer` / `resizeBuffer` / `loadBuffer` / `zeroBuffer`
