# Integration & Product Implementation Plan

This document is a step-by-step plan for integrating the three sub-projects (static-lang-3, synthdef-compiler, audio-engine) and building the final live coding application. It is based on an audit of the current state of each project.

---

## Current State Summary

### audio-engine (most complete)
- Real-time audio engine with CoreAudio/ALSA backends via RtAudio
- Plugin loading via `dlopen`/`dlsym` of `.dylib` files conforming to `jscs_plugin_abi.h`
- Dynamic graph editing with per-sample topological sort
- Crossfading system (7 curves) for glitch-free connection changes
- Lock-free SPSC FIFOs for RT/NRT communication
- Multi-silo (parallel worker threads) with binary-tree mixdown
- Sample-accurate scheduling queue (hash wheel, 1021 bins)
- Command bundling API (`begin`/`newNode`/`connect`/`go`/`sched`)
- S-expression command parser (text-based)
- Polyphonic voice management (`Voicer` template)
- Safety limiter on master output (lookahead, NaN zapping)
- **Missing**: OSC support, NATS support, buffer operations, audio input, binary s-expression serialization (partial)

### synthdef-compiler (most complete)
- Two front-ends: S-expression parser and C++ DSL
- ~200 audio operators (oscillators, filters, noise, envelopes, math, delays)
- 14-pass graph analysis pipeline (topology sort, shape/type inference, constant folding, dead code removal, rate scheduling)
- Algebraic rewrite engine (~100 optimization rules)
- C++ code generation targeting `jscs_plugin_abi.h`
- Full compilation pipeline: parse -> analyze -> codegen -> clang compile -> link -> dlopen
- Hash-consing for common subexpression elimination
- Multi-channel support with power-of-two broadcasting
- **Missing**: SIMD codegen (infrastructure present), full subgraph s-expression support, event handling stubs

### static-lang-3 (core complete, integration features not started)
- Full 5-phase compilation pipeline (lex -> parse -> type check -> codegen -> execute)
- Register-based direct-threaded VM with `[[clang::musttail]]` dispatch
- TLSF O(1) real-time allocator, incremental bounded-pause GC
- Rich type system: Bool, Int, Float, Symbol, String, Fraction, Complex, Array, List, Range, Tuple, Struct, Enum, Ref, Function, Lambda, Coroutine
- Template monomorphization, function overloading, auto-mapping, pattern matching
- C and C++ embedding APIs (`langx.h`, `langx.hpp`)
- Foreign function interface for registering host-provided C functions
- 170+ tests across all features
- **Missing**: Methods/OO, event-driven VM, dynamic scoping, optimizations, error handling improvements

### shared/
- `jscs_plugin_abi.h` — Pure C plugin ABI (used by both audio-engine and synthdef-compiler)
- `jscs_simd.hpp` — Cross-platform SIMD abstraction (Apple/Linux)
- `jscs_random.hpp` — xoroshiro128++ PRNG (scalar and SIMD)
- `jscs_matrix_transform.hpp` — Compile-time matrix operations
- Legacy headers: `sapf_plugin_interface.hpp`, `sapf_client_interface.hpp`, `sapf_plugin_utils.hpp`

---

## Phase 0: Project Organization & Build Infrastructure

**Goal**: Establish a unified build system and repository structure.

### 0.1 Repository structure decision

Current state: Each project has its own CMakeLists.txt, no top-level build. The `shared/` directory is a sibling referenced via `../shared`.

**Recommended**: Single monorepo with a top-level CMakeLists.txt.

```
A-new-project/
├── CMakeLists.txt              (top-level, adds subdirectories)
├── shared/                     (shared headers, remains here)
├── audio-engine/
│   └── CMakeLists.txt          (builds libAudioEngine static/shared lib)
├── synthdef-compiler/
│   └── CMakeLists.txt          (builds libSynthdefCompiler static/shared lib)
├── static-lang-3/
│   └── CMakeLists.txt          (builds libLangX static/shared lib)
├── app/
│   └── CMakeLists.txt          (the live coding application)
└── integration-tests/
    └── CMakeLists.txt
```

**Tasks**:
1. Create top-level `CMakeLists.txt` that adds each sub-project via `add_subdirectory()`.
2. Refactor each sub-project's CMakeLists.txt to produce a library target (static and/or shared) in addition to any executable.
3. Define proper `target_include_directories(PUBLIC ...)` on each library so consumers automatically get the right include paths.
4. Move `shared/` into a proper CMake interface library target (`add_library(shared INTERFACE)`).
5. Clean up legacy/duplicate headers (e.g., `sapf_*` headers in shared/, duplicate `jscs_random.hpp` in audio-engine/src/).
6. Ensure all three projects build successfully from the top level with a single `cmake --build build` command.
7. Add a `build.sh` convenience script at the root.

### 0.2 Cross-platform build support

**Tasks**:
1. Verify macOS ARM64 builds for all three.
2. Add Linux build CI (GitHub Actions or similar) — audio-engine already has Linux stubs (ALSA).
3. Define CMake options for optional features (e.g., `-DBUILD_APP=ON`, `-DBUILD_TESTS=ON`).

---

## Phase 1: Library-ify Each Project

**Goal**: Each project becomes a linkable library with a clean public API.

### 1.1 audio-engine as a library

Current state: Builds an executable. The client API is in `jscs_client_interface.hpp/cpp`.

**Tasks**:
1. Split `main.cpp` — move built-in test plugins (SinOsc, AddOp, MulOp, VoicerTest) to a separate test file.
2. Create `libAudioEngine` (static library) from all src/ files except the main entry point.
3. Define a public header set: `jscs_client_interface.hpp` + `jscs_plugin_abi.h`.
4. Build a separate `audio-engine` executable that links `libAudioEngine`.
5. Add `install()` targets for headers and library.

### 1.2 synthdef-compiler as a library

Current state: Builds an executable. Has a clear compilation API in `synthdef_compile.hpp`.

**Tasks**:
1. Split `main.cpp` — separate test/example code from entry point.
2. Create `libSynthdefCompiler` (static library) from all src/ files except main.
3. Define a public header set: `synthdef_compile.hpp`, `synthdef_synth.hpp`, `synthdef_sexpr.hpp`, `synthdef_from_sexpr.hpp`.
4. Build a separate `synthdef-compiler` executable that links `libSynthdefCompiler`.
5. Add `install()` targets.

### 1.3 static-lang-3 as a library

Current state: Already builds `langx_lib` as a static library with public C/C++ APIs.

**Tasks**:
1. Verify `langx_lib` can be linked by external projects.
2. Ensure `include/langx.h` and `include/langx.hpp` are the complete public API surface.
3. Add `install()` targets for headers and library.

---

## Phase 2: Finish Critical Language Features

**Goal**: Complete the static-lang-3 features needed before integration.

### 2.1 Module system — DONE

The module system is fully implemented and tested. All three import syntaxes work (whole, wildcard, named with aliases), qualified access (`module.func(args)`) works including in space pipeline syntax, module file resolution supports relative and include-path-based lookup, circular import detection and module caching are in place, and all export types (functions, variables, structs, enums, templates, type aliases) are supported. No further work needed.

### 2.2 Event-driven VM (Phase 12 in lang's plan)

Critical for real-time audio integration — the VM must respond to events and then return control.

**Tasks**:
1. Implement an event dispatch loop: VM receives an event, executes the handler function, stack collapses, control returns to host.
2. Define event types: timer tick, OSC message received, note trigger, control change, etc.
3. Integrate with audio-engine's scheduling — language event handlers can be scheduled as engine commands.
4. Ensure the VM can be "stepped" from a host loop (process one event, return).

### 2.3 Methods (Phase 10 in lang's plan)

Useful for ergonomic API design (e.g., `synth.setControl("freq", 440)`).

**Tasks**:
1. Implement method declaration syntax and dispatch on receiver type.
2. Support `this` keyword in method bodies.
3. Methods on built-in types (e.g., Array methods).

### 2.4 Error handling improvements (Phase 15)

Needed for a usable live coding environment — errors must not crash the VM.

**Tasks**:
1. Runtime error trapping (division by zero, index out of bounds, etc.) — return error to host instead of crashing.
2. Better parser error recovery for live coding feedback.
3. Source location tracking in error messages.

---

## Phase 3: FFI Bindings for Audio Engine

**Goal**: Register audio-engine client functions as callable from Language X.

### 3.1 Design the language-side audio API

Define a set of Language X functions that map to audio-engine commands. These are registered as foreign functions via the existing FFI.

**Proposed API surface** (Language X syntax):

```
// Engine lifecycle
fn engineStart() -> Void
fn engineStop() -> Void

// Plugin management
fn loadPlugins(path: String) -> Void
fn loadPlugin(path: String, name: String) -> Void

// Node operations
fn newNode(defName: String, nodeID: Int) -> Void
fn freeNode(nodeID: Int) -> Void
fn freeAllNodes() -> Void

// Connections
fn connect(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int) -> Void
fn connectX(srcNode: Int, srcPort: Int, dstNode: Int, dstPort: Int, xfade: Float, curve: Int) -> Void
fn disconnect(dstNode: Int, dstPort: Int) -> Void

// Parameter control
fn setInput(nodeID: Int, portIndex: Int, value: Float) -> Void
fn setControl(nodeID: Int, controlID: Int, value: Float) -> Void

// Scheduling
fn bundle() -> Void          // begin command bundle
fn send() -> Void            // send bundle immediately
fn sched(time: Float) -> Void // schedule bundle at time

// Notes
fn noteOn(nodeID: Int, noteID: Int, params: Array[Float]) -> Void
fn noteOff(nodeID: Int, noteID: Int) -> Void
fn allNotesOff(nodeID: Int) -> Void
```

### 3.2 Implement the FFI bridge

**Tasks**:
1. Write a C++ bridge layer that wraps each `jscs_client_interface` function into the `void (*cfun)(VM&, u16 result_reg, u16 argc, u16 arg_base)` FFI signature expected by langx.
2. Each wrapper extracts arguments from VM registers, calls the corresponding engine function, and stores the result.
3. Register all functions with the VM's FFI system at initialization.
4. The bridge holds a pointer to the `Engine*` via the VM's user data pointer.
5. Mark all scheduling functions as `rtSafe` so they can be called from event handlers on the audio thread.

### 3.3 Test the FFI bridge

**Tasks**:
1. Write Language X test scripts that create nodes, connect them, set parameters.
2. Verify audio output (e.g., create a SinOsc node, connect to output, verify sound).
3. Test error cases (invalid node ID, port out of range, etc.).

---

## Phase 4: FFI Bindings for Synthdef Compiler

**Goal**: Allow Language X to compile synth definitions at runtime.

### 4.1 Design the language-side synthdef API

```
// Compile a synthdef from s-expression text
fn compileSynthDef(name: String, sexpr: String) -> Void

// Compile and load into running engine
fn compileSynthDefAndLoad(name: String, sexpr: String) -> Void

// Query available synthdefs
fn listSynthDefs() -> Array[String]
```

### 4.2 Implement the FFI bridge

**Tasks**:
1. Write a C++ bridge that calls `synthdef::synthFromSExprText()`, `synthdef::cppCodeGen()`, and `synthdef::compileAndLink()`.
2. After compilation, call `loadDef()` on the engine to make the new plugin available.
3. Handle compilation errors gracefully — return error strings to the language.
4. Consider caching: if a synthdef with the same name/hash already exists, skip recompilation.

### 4.3 Higher-level DSL in Language X (future)

Once the basic s-expression bridge works, a more ergonomic Language X DSL could be built that generates s-expressions:

```
let sine = SynthDef("sine") {
    let freq = control("freq", 20..20000, 440)
    let phase = phasor(freq / sampleRate)
    outlet(sin(phase) * 0.1, "out")
}
sine.compile()
```

This is lower priority and can be built incrementally on top of the s-expression bridge.

---

## Phase 5: OSC (Open Sound Control) Support

**Goal**: Control both the audio engine and language VM via OSC messages.

### 5.1 Choose an OSC library

Options:
- **liblo** — mature, C, widely used
- **oscpack** — C++, header-only, simple
- **Custom** — minimal implementation (OSC is a simple protocol)

Recommendation: **oscpack** or a minimal custom implementation to avoid external dependencies and maintain real-time safety.

### 5.2 OSC server for audio-engine

**Tasks**:
1. Add an OSC listener thread (UDP socket) to the engine.
2. Map OSC addresses to engine commands:
   - `/node/new <defName> <nodeID>` -> `newNode()`
   - `/node/free <nodeID>` -> `freeNode()`
   - `/connect <srcNode> <srcPort> <dstNode> <dstPort>` -> `connect()`
   - `/node/set <nodeID> <controlName> <value>` -> `setControl()`
   - `/note/on <nodeID> <noteID> <params...>` -> `noteOn()`
   - `/note/off <nodeID> <noteID>` -> `noteOff()`
   - `/bundle <time> <messages...>` -> `begin()`/`sched()`
3. OSC messages are parsed on the listener thread and converted to engine commands via the existing NRT command path (lock-free FIFO to RT thread).

### 5.3 OSC server for Language X VM

**Tasks**:
1. Add an OSC listener that can dispatch events to the VM.
2. Map OSC messages to VM events (ties into Phase 2.2 event-driven VM).
3. `/eval <code>` — compile and execute a string of Language X code.
4. `/call <functionName> <args...>` — call a named function.

### 5.4 OSC client (sending)

**Tasks**:
1. Add OSC send capability as Language X built-in functions.
2. `fn oscSend(host: String, port: Int, address: String, args: Array[Any]) -> Void`

---

## Phase 6: NATS Support

**Goal**: Enable networked control and distributed messaging via NATS.

### 6.1 NATS client library

**Tasks**:
1. Evaluate NATS C client (`nats.c`) for real-time safety. It uses pthreads and malloc internally, so it must run on a non-RT thread.
2. Integrate NATS client as an NRT service — messages received on NATS are converted to commands and pushed to the engine via the existing FIFO.
3. Subscribe to subjects for engine commands (similar mapping to OSC addresses).

### 6.2 NATS for Language X

**Tasks**:
1. Add `fn natsPub(subject: String, data: String) -> Void` as FFI function.
2. Add NATS subscription support: incoming messages trigger VM events.

### 6.3 NATS for distributed engines

**Tasks**:
1. Multiple engine instances can communicate via NATS subjects.
2. Useful for multi-machine performances or networked collaboration.

---

## Phase 7: Finish Remaining Engine Features

**Goal**: Complete audio-engine functionality gaps.

### 7.1 Buffer operations

The audio-engine declares but doesn't implement: `newBuffer`, `freeBuffer`, `resizeBuffer`, `loadBuffer`, `zeroBuffer`.

**Tasks**:
1. Implement a buffer pool (pre-allocated memory blocks for audio data).
2. Implement buffer loading from audio files (libsndfile or similar).
3. Add buffer read/write operations accessible from plugins.
4. Add buffer-related commands to the command system.

### 7.2 Audio input support

**Tasks**:
1. Enable input streams in RtAudio configuration.
2. Route hardware input to a special input node per silo.
3. Plugins can receive live audio via inlet connections.

### 7.3 MasterGainCmd and ChannelOffsetCmd

These are declared but not implemented.

**Tasks**:
1. Implement master gain control (applied after safety limiter or integrated into it).
2. Implement channel offset for routing to specific hardware output channels.

### 7.4 Binary s-expression serialization

Partially implemented. Complete it for efficient network transport of commands (useful with NATS).

---

## Phase 8: Finish Remaining Compiler Features

**Goal**: Complete synthdef-compiler gaps.

### 8.1 SIMD code generation

Infrastructure exists (max_simd_width, unroll_by, SIMD type definitions).

**Tasks**:
1. Generate SIMD loop bodies for multi-channel synths where channels align to SIMD widths (2, 4, 8).
2. Use `f64x2`/`f64x4` types from `jscs_simd.hpp`.
3. Generate scalar remainder loops for non-aligned channel counts.
4. Benchmark against scalar codegen to validate speedup.

### 8.2 Full s-expression subgraph support

S-expression parsing for `if`, `switch`, `for` subgraphs is marked TODO.

**Tasks**:
1. Implement subgraph parsing for control flow nodes in s-expressions.
2. Test round-tripping: C++ DSL -> s-expression -> parse -> compile.

### 8.3 Event and note handling

Currently stubs.

**Tasks**:
1. Generate proper `event()` function bodies.
2. Connect to the engine's note allocation system.

---

## Phase 9: Finish Remaining Language Features

**Goal**: Complete lower-priority static-lang-3 features.

### 9.1 Infinite lists and generators (Phase 7 in lang's plan)

**Tasks**:
1. Implement `ord` built-in (infinite list of integers).
2. Implement lazy `to()` for generating ranges as infinite lists.
3. Auto-mapping over infinite lists.

### 9.2 Dynamic scoping (Phase 11 in lang's plan)

Useful for implicit context passing (e.g., current silo, current bundle).

**Tasks**:
1. Implement dynamic scope variable declaration and lookup.
2. Dynamic variables are scoped to the current event handler invocation.

### 9.3 Standard library completion (Phase 13 in lang's plan)

**Tasks**:
1. Complete string functions: charAt, substring, indexOf, split, replace, etc.
2. Complete array/list utility functions.
3. Add Map operations.
4. Add I/O functions (file reading for loading scripts — NRT only).

### 9.4 Optimizations (Phase 14 in lang's plan)

**Tasks**:
1. Register allocation optimization (minimize register pressure).
2. Constant folding in the compiler.
3. Function inlining for small functions.
4. Tail call optimization.

---

## Phase 10: Live Coding Application — UI Framework

**Goal**: Choose and set up the UI framework.

### 10.1 Framework evaluation

| Criterion | Dear ImGui | Qt | JUCE |
|-----------|-----------|-----|------|
| License | MIT | GPL/Commercial | GPL/Commercial |
| Code editor | Need to add (ImGuiColorTextEdit) | QScintilla/QTextEdit | Built-in CodeEditorComponent |
| GPU rendering | Native (OpenGL/Metal/Vulkan) | QML has GPU, Widgets are CPU | OpenGL |
| Audio integration | None (bring your own) | QtMultimedia (not great) | Excellent built-in |
| Cross-platform | Yes | Yes | Yes |
| Footprint | Very small | Large | Medium |
| Custom widgets | Easy (immediate mode) | Medium (retained mode) | Medium |
| Node graph editor | imnodes / ImNodes libraries | Qt Node Editor | None built-in |
| Learning curve | Low | High | Medium |

**Recommendation**: **Dear ImGui** — lightweight, MIT license, excellent for real-time applications, easy to embed, growing ecosystem of widgets. Node graph editors (imnodes) already exist. Code editors exist (ImGuiColorTextEdit). Pairs well with a custom audio engine.

### 10.2 Application scaffold

**Tasks**:
1. Create `app/` directory with CMakeLists.txt.
2. Set up Dear ImGui with a Metal backend (macOS) / Vulkan or OpenGL backend (Linux).
3. Create main application window with basic menu bar.
4. Link against `libAudioEngine`, `libSynthdefCompiler`, `libLangX`.
5. Initialize all three systems at startup.

---

## Phase 11: Code Editor & REPL

**Goal**: Build the core live coding interface.

### 11.1 Code editor panel

**Tasks**:
1. Integrate ImGuiColorTextEdit (or similar) as the code editor widget.
2. Add Language X syntax highlighting rules.
3. Add line numbers, current line highlighting, bracket matching.
4. Support multiple editor tabs for different files/modules.

### 11.2 REPL / output panel

**Tasks**:
1. Add a scrolling output panel for print/println output from the VM.
2. Add a single-line REPL input field for interactive evaluation.
3. Show compilation errors inline in the editor (underlines, margin markers).
4. Show type information on hover or in a status bar.

### 11.3 Execute-on-keystroke

**Tasks**:
1. Evaluate selected code block on Cmd+Enter (or configurable key).
2. Evaluate current line on Shift+Enter.
3. Evaluate entire file on Cmd+Shift+Enter.
4. Show flash/highlight on evaluated lines for visual feedback.

---

## Phase 12: Plugin & Module Management

**Goal**: Browsing and managing synth plugins and language modules.

### 12.1 Plugin browser panel

**Tasks**:
1. List all loaded synth definitions with their port/control info.
2. Show plugin details: inputs, outputs, controls with specs.
3. One-click instantiation of a plugin as a new node.
4. Search/filter functionality.

### 12.2 Module browser panel

**Tasks**:
1. List available Language X modules.
2. Show module exports (functions, types).
3. Click-to-import into current editor.

### 12.3 Synthdef compilation UI

**Tasks**:
1. A "Compile" button that sends the current editor content through the synthdef-compiler.
2. Show compilation progress and errors.
3. Newly compiled plugins automatically appear in the plugin browser.

---

## Phase 13: Audio Graph Visualization

**Goal**: Visual representation of the running audio graph.

### 13.1 Node graph display

**Tasks**:
1. Integrate an ImGui node graph library (e.g., imnodes).
2. Display running nodes as boxes with input/output ports.
3. Display connections as wires between ports.
4. Update in real-time as nodes are added/removed/connected.

### 13.2 Interactive editing

**Tasks**:
1. Drag to create new connections (generates `connect` commands).
2. Click to delete connections (generates `disconnect` commands).
3. Right-click context menu to add/remove nodes.
4. Double-click a node to open its controls.

### 13.3 Control surfaces

**Tasks**:
1. For each node, show its controls as sliders/knobs.
2. Changes immediately sent as `setControl` commands.
3. Support MIDI mapping to controls (future).

---

## Phase 14: Metering & Monitoring

**Goal**: Audio level meters, scope displays, and performance monitoring.

### 14.1 Level meters

**Tasks**:
1. Add peak/RMS metering on the master output.
2. Per-node output level meters (opt-in, since it adds overhead).
3. Visual meter widgets in ImGui.

### 14.2 Oscilloscope / waveform display

**Tasks**:
1. Ring buffer capture of audio output.
2. Waveform display widget (time domain).
3. Optional FFT spectrum display (frequency domain).

### 14.3 Performance monitoring

**Tasks**:
1. Display audio thread CPU usage.
2. Display per-silo load.
3. Display GC statistics from Language X VM.
4. Display command queue depth.
5. Alert on audio dropouts (buffer underruns).

---

## Phase 15: Session Management

**Goal**: Save and restore the state of a live coding session.

### 15.1 Project files

**Tasks**:
1. Define a project file format (JSON or custom) that stores:
   - Open editor tabs with content
   - Audio graph state (nodes, connections, control values)
   - Loaded plugins and modules
   - Engine configuration (sample rate, buffer size, silo count)
2. Save/load functionality.

### 15.2 Undo/redo

**Tasks**:
1. Command history for audio graph operations.
2. Undo/redo stack.
3. Editor undo/redo (typically built into the editor widget).

---

## Phase 16: Future Extensions (Lower Priority)

These are longer-term goals mentioned in the project description.

### 16.1 Video synthesis via shader composition
- Integrate GPU shader pipeline (Metal/Vulkan compute shaders)
- Audio-reactive shader parameters
- Shader editor in the app

### 16.2 Visual node graph editor for synthdef construction
- Drag-and-drop UGen palette
- Visual patching of signal flow
- Generates s-expressions from the visual graph
- Feeds into synthdef-compiler

### 16.3 Piano roll editor
- MIDI-style piano roll for note event editing
- Generates `noteOn`/`noteOff` commands on the scheduling queue

### 16.4 Event list editor
- Spreadsheet-like editor for scheduled events
- Time, command type, parameters per row
- Direct manipulation of the scheduling queue

### 16.5 MIDI I/O
- MIDI input for playing synths from external controllers
- MIDI learn for mapping controllers to node controls
- MIDI output for sequencing external gear

---

## Dependency Graph (Phases)

```
Phase 0 (Build Infrastructure)
  └─> Phase 1 (Library-ify)
        ├─> Phase 2 (Language Features)
        │     └─> Phase 3 (FFI: Audio Engine)
        │           └─> Phase 4 (FFI: Synthdef Compiler)
        │                 └─> Phase 5 (OSC)
        │                       └─> Phase 6 (NATS)
        ├─> Phase 7 (Engine Features) ──────────────────┐
        ├─> Phase 8 (Compiler Features) ────────────────┤
        └─> Phase 9 (Language Features cont.) ──────────┤
                                                        v
                                              Phase 10 (UI Framework)
                                                └─> Phase 11 (Editor & REPL)
                                                      └─> Phase 12 (Plugin/Module Mgmt)
                                                            └─> Phase 13 (Graph Viz)
                                                                  └─> Phase 14 (Metering)
                                                                        └─> Phase 15 (Sessions)
                                                                              └─> Phase 16 (Future)
```

**Notes on parallelism**:
- Phases 7, 8, 9 can proceed in parallel with Phases 2-6.
- Phase 10 can begin as soon as Phase 1 is complete (UI framework setup doesn't depend on FFI work).
- Phases 11-15 are sequential but can overlap (e.g., start Phase 12 while finishing Phase 11).

---

## Estimated Effort Summary

| Phase | Description | Effort |
|-------|-------------|--------|
| 0 | Build infrastructure | Small |
| 1 | Library-ify projects | Small |
| 2 | Critical language features | Large (event-driven VM is the biggest piece) |
| 3 | FFI: audio-engine | Medium |
| 4 | FFI: synthdef-compiler | Medium |
| 5 | OSC support | Medium |
| 6 | NATS support | Medium |
| 7 | Engine feature completion | Medium |
| 8 | Compiler feature completion | Medium |
| 9 | Language feature completion | Large |
| 10 | UI framework setup | Small-Medium |
| 11 | Code editor & REPL | Medium |
| 12 | Plugin/module management | Medium |
| 13 | Audio graph visualization | Large |
| 14 | Metering & monitoring | Medium |
| 15 | Session management | Medium |
| 16 | Future extensions | Very Large (ongoing) |

---

## Key Risks & Decisions

1. **Event-driven VM design** (Phase 2.2): This is the most architecturally significant remaining work. The VM must be able to process an event handler and return control to the host without blocking. This needs careful design to work with the audio-engine's sample-accurate scheduling.

2. **UI framework choice** (Phase 10): Dear ImGui is recommended but the project description mentions Qt as an alternative. This should be decided before Phase 10 begins.

3. **Real-time safety across boundaries**: When Language X calls audio-engine functions via FFI, the call chain must remain real-time safe. The bridge functions must not allocate memory or block. The existing TLSF allocator and lock-free FIFOs make this feasible, but it needs careful validation.

4. **Compilation latency for synthdef**: Calling clang at runtime to compile synth definitions takes time (100ms-1s+). This must happen on a background thread with the compiled plugin loaded asynchronously. The UI should show compilation status.

5. **Cross-platform audio**: RtAudio handles CoreAudio (macOS) and ALSA (Linux). Windows support via WASAPI/ASIO would be needed for full cross-platform coverage.

6. **Plugin ABI stability**: The `jscs_plugin_abi.h` interface is the contract between all three projects. Changes to it require coordinated updates. Consider versioning the ABI.
