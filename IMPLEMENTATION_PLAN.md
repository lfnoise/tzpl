# Integration & Product Implementation Plan

This document is a step-by-step plan for integrating the three sub-projects (lang, synthdef-compiler, engine) and building the final live coding application. It is based on an audit of the current state of each project.

**Last updated**: 2026-03-11

---

## Current State Summary

### engine
- Real-time audio engine with CoreAudio/ALSA backends via RtAudio
- Plugin loading via `dlopen`/`dlsym` of `.dylib` files conforming to `tzpl_plugin_abi.h`
- Dynamic graph editing with per-sample topological sort
- Crossfading system (7 curves) for glitch-free connection changes
- Lock-free SPSC FIFOs for RT/NRT communication
- Multi-silo (parallel worker threads) with binary-tree mixdown
- Sample-accurate scheduling queue (hash wheel, 1021 bins)
- Command bundling API (`begin`/`newNode`/`connect`/`go`/`sched`)
- S-expression command parser (text-based)
- Polyphonic voice management (`Voicer` template)
- Safety limiter on master output (lookahead, NaN zapping)
- Builds as static library (`audio_engine_lib`) with install targets
- Full FFI bridge to Tzopilotl (32 functions, 19 marked rtSafe)
- **Remaining**: OSC support, NATS support, buffer operations (declared but not implemented), audio input (infrastructure exists but stream initialized as output-only), MasterGainCmd/ChannelOffsetCmd (empty struct definitions, no handlers), binary s-expression serialization (commented-out skeleton)

### synthdef-compiler
- Two front-ends: S-expression parser and C++ DSL
- ~200 audio operators (oscillators, filters, noise, envelopes, math, delays) + 70 primitive math ops + 47 expression node types
- 14-pass graph analysis pipeline (topology sort, shape/type inference, constant folding, dead code removal, rate scheduling)
- Algebraic rewrite engine (~100 optimization rules)
- C++ code generation targeting `tzpl_plugin_abi.h`
- Full compilation pipeline: parse -> analyze -> codegen -> clang compile -> link -> dlopen
- Hash-consing for common subexpression elimination
- Multi-channel support with power-of-two broadcasting
- FFT/spectral chain processing (`SpectralChainExpr`) with forward/inverse FFT, windowing, overlap-add
- Vector operations (11 ops: take, drop, stride, stutter, ncyc, reverse, transpose, rotate, at, put, join)
- Polyphonic voicer codegen with flat voice mode optimization
- Full subgraph s-expression support (if/switch/for)
- Builds as static library (`synthdef_compiler_lib`) with install targets
- Full FFI bridge to Tzopilotl with compilation caching
- Event/note handling codegen implemented (`genEventFun`, `genHandleEventsFun`, `genNoteFuns`)
- **Remaining**: SIMD codegen (infrastructure present, generation not implemented)

### lang
- Full 5-phase compilation pipeline (lex -> parse -> type check -> codegen -> execute)
- Register-based direct-threaded VM with `[[clang::musttail]]` dispatch
- TLSF O(1) real-time allocator, incremental bounded-pause GC
- Rich type system: Bool, Int, Float, Symbol, String, Fraction, Complex, Array, List, Range, Tuple, Struct, Enum, Ref, Function, Lambda, Coroutine
- Template monomorphization, function overloading, auto-mapping, pattern matching
- C and C++ embedding APIs (`tzpl.h`, `tzpl.hpp`)
- Foreign function interface for registering host-provided C functions
- Module system with all import syntaxes, circular detection, module caching
- Dynamic scoping (`var \`name = expr`) with zero-overhead save/restore
- Infinite lists and generators (lazy evaluation)
- Parser error recovery (synchronization, cascading error suppression)
- Constant folding, register reclamation, tail call optimization
- 317 tests, all passing
- Builds as static library (`tzpl_lib`) with install targets
- `callFunction()` API for host-driven function invocation (event handler infrastructure)
- REPLSession class for interactive evaluation
- Map type with builtins (get, getDefault, contains, keys, values, copy, merge)
- **Remaining**: Event-driven VM (dispatch loop, handler registration, hot-reload), error location refinement, general function inlining, I/O functions

### bridge/
- `tzpl_audio_engine_ffi.cpp` — 32 FFI functions wrapping audio engine commands (including `listSynthDefs`)
- `tzpl_synthdef_compiler_ffi.cpp` — 2 FFI functions for compile and compile-and-load
- `modules/audio_engine.x` — Tzopilotl enum definitions (Enable, SchedPolicy, FadeCurve, Err)
- Both bridges build as OBJECT libraries, linked into the app

### shared/
- `tzpl_plugin_abi.h` — Pure C plugin ABI (used by both engine and synthdef-compiler)
- `tzpl_simd.hpp` — Cross-platform SIMD abstraction (Apple/Linux)
- `tzpl_random.hpp` — xoroshiro128++ PRNG (scalar and SIMD)
- `tzpl_matrix_transform.hpp` — Compile-time matrix operations
- `tzpl_voicer.hpp` — Polyphonic voice management template
- `tzpl_fft.hpp` — Cross-platform FFT wrapper (vDSP/Accelerate on Apple, PFFFT planned for Linux)
- `synthdef_plugin_interface.hpp` — Plugin interface definitions
- Builds as CMake INTERFACE library with install targets

### app/
- CLI application (`tzpl`) that links all three libraries via FFI bridges
- Runs Tzopilotl scripts with full audio engine and synthdef compiler access
- Supports project directories with config files and pre-compiled plugin loading
- No GUI — currently command-line only

---

## Phase 0: Project Organization & Build Infrastructure — DONE

**Goal**: Establish a unified build system and repository structure.

### 0.1 Repository structure decision — DONE

Single monorepo with top-level `CMakeLists.txt` (project name: `tzpl`, C++23).

```
A-new-project/
├── CMakeLists.txt              (top-level, adds subdirectories)
├── build.sh                    (convenience build script)
├── shared/                     (CMake INTERFACE library)
├── engine/
│   └── CMakeLists.txt          (builds audio_engine_lib + engine executable)
├── synthdef-compiler/
│   └── CMakeLists.txt          (builds synthdef_compiler_lib + synthdef-compiler executable)
├── lang/
│   └── CMakeLists.txt          (builds tzpl_lib + tzpl executable)
├── bridge/
│   └── CMakeLists.txt          (builds FFI bridge OBJECT libraries)
├── app/
│   └── CMakeLists.txt          (builds tzpl CLI application)
└── integration-tests/
    └── CMakeLists.txt          (test_foreign_modules, test_audio_engine_ffi, test_synthdef_compiler_ffi)
```

**Completed tasks**:
1. ~~Create top-level `CMakeLists.txt` that adds each sub-project via `add_subdirectory()`.~~ Done. Build options: `TZPL_BUILD_AUDIO_ENGINE`, `TZPL_BUILD_SYNTHDEF_COMPILER`, `TZPL_BUILD_LANG`, `TZPL_BUILD_BRIDGE`, `TZPL_BUILD_APP` (OFF by default), `TZPL_BUILD_TESTS` (OFF by default).
2. ~~Refactor each sub-project's CMakeLists.txt to produce a library target.~~ Done. Each sub-project also supports standalone builds via `if(NOT CMAKE_PROJECT_NAME STREQUAL "tzpl")` guards.
3. ~~Define proper `target_include_directories(PUBLIC ...)` on each library.~~ Done.
4. ~~Move `shared/` into a proper CMake interface library target.~~ Done.
5. ~~Clean up legacy/duplicate headers.~~ Done.
6. ~~Ensure all three projects build successfully from the top level.~~ Done.
7. ~~Add a `build.sh` convenience script at the root.~~ Done.

### 0.2 Cross-platform build support — PARTIAL

**Completed tasks**:
1. ~~Verify macOS ARM64 builds for all three.~~ Done.
3. ~~Define CMake options for optional features.~~ Done.

**Remaining tasks**:
2. Add Linux build CI (GitHub Actions or similar).

---

## Phase 1: Library-ify Each Project — DONE

**Goal**: Each project becomes a linkable library with a clean public API.

### 1.1 engine as a library — DONE

- `audio_engine_lib` static library with all src/ files except main.cpp
- Public headers exposed via `target_include_directories(PUBLIC)`
- Separate `engine` executable links `audio_engine_lib`
- `install()` targets for library, headers (15+ headers), and executable

### 1.2 synthdef-compiler as a library — DONE

- `synthdef_compiler_lib` static library (21 source files)
- Separate `synthdef-compiler` executable links `synthdef_compiler_lib`
- `install()` targets for library, headers (22+ headers), and executable

### 1.3 lang as a library — DONE

- `tzpl_lib` static library (16 source files)
- Public API: `include/tzpl.h` (C) and `include/tzpl.hpp` (C++)
- Separate `tzpl` executable with linenoise for CLI
- `install()` targets for library, headers, and executable

---

## Phase 2: FFI Bindings for Audio Engine — DONE

**Goal**: Register engine client functions as callable from Tzopilotl.

### 2.1 Design the language-side audio API — DONE

Implemented in `bridge/src/tzpl_audio_engine_ffi.cpp` (447 lines). Functions are registered under the `audio_engine` foreign module namespace. The API exceeds the original specification with 32 functions (vs. 17 planned).

**Implemented API surface** (Tzopilotl syntax):

```
-- Engine lifecycle
fn engineStart() Void;
fn engineStop() Void;
fn isAudioRunning() Bool;
fn getStreamTime() Float;
fn masterGain(gain Float) Void;
fn safetyLimiter(on Bool) Void;

-- Plugin management
fn loadPlugins(path String) Bool;
fn loadPlugin(path String, name String) Bool;

-- Command bundling/scheduling
fn begin(silo Int) Int;
fn go() Int;
fn sched(time Float) Int;
fn schedPolicy(time Float, policy Int) Int;

-- Node operations
fn newNode(defName String, nodeID Int) Int;
fn freeNode(nodeID Int) Int;
fn freeAllNodes() Int;

-- Connections (8 variants including crossfade and reconnect)
fn connect(srcNode Int, srcPort Int, dstNode Int, dstPort Int) Int;
fn connectX(srcNode Int, srcPort Int, dstNode Int, dstPort Int, xfade Float, curve Int) Int;
fn disconnectInput(dstNode Int, dstPort Int) Int;
fn disconnectInputX(dstNode Int, dstPort Int, xfade Float, curve Int) Int;
fn disconnectOutput(srcNode Int, srcPort Int) Int;
fn disconnectNode(nodeID Int) Int;
fn reconnectOutput(oldSrcNode Int, oldSrcPort Int, newSrcNode Int, newSrcPort Int, xfade Float, curve Int) Int;
fn replaceNode(oldNodeID Int, newNodeID Int, xfade Float, curve Int) Int;

-- Parameter control
fn setInput(nodeID Int, portIndex Int, value Float) Int;
fn setInputX(nodeID Int, portIndex Int, value Float, xfade Float, curve Int) Int;
fn setControl(nodeID Int, controlID Int, value Float) Int;

-- Notes
fn noteOn(nodeID Int, noteID Int, params Array[Float]) Int;
fn noteOff(nodeID Int, noteID Int) Int;
fn allNotesOff(nodeID Int) Int;
fn noteSetParams(nodeID Int, noteID Int, firstParam Int, values Array[Float]) Int;

-- Introspection
fn listSynthDefs() Array[String];

-- Utility
fn sleep(seconds Float) Void;
```

### 2.2 Implement the FFI bridge — DONE

1. ~~C++ bridge layer wrapping each function into FFI signature.~~ Done.
2. ~~Each wrapper extracts arguments from VM registers.~~ Done.
3. ~~Register all functions with the VM's FFI system.~~ Done (foreign module namespace `audio_engine`).
4. ~~Engine pointer via VM user data pointer.~~ Done (`setEngineOnVM()`).
5. ~~Mark scheduling functions as `rtSafe`.~~ Done (19 functions marked rtSafe).

### 2.3 Test the FFI bridge — DONE

Integration tests exist in `integration-tests/` (`test_audio_engine_ffi`). End-to-end audio playback verified.

---

## Phase 3: FFI Bindings for Synthdef Compiler — DONE

**Goal**: Allow Tzopilotl to compile synth definitions at runtime.

### 3.1 Design the language-side synthdef API — DONE

```
-- Compile a synthdef from s-expression text (returns "" on success, error message on failure)
fn compileSynthDef(sexpr String) String;

-- Compile and load into running engine (returns "" on success, error message on failure)
fn compileSynthDefAndLoad(sexpr String) String;

-- Query available synthdefs (implemented in audio_engine FFI)
fn listSynthDefs() Array[String];
```

Note: The name parameter was removed vs. the original plan — the synth name is extracted from the s-expression itself, avoiding redundancy.

### 3.2 Implement the FFI bridge — DONE

Implemented in `bridge/src/tzpl_synthdef_compiler_ffi.cpp` (192 lines). The bridge:
1. ~~Calls `synthdef::synthFromSExprText()`, `synthdef::cppCodeGen()`, and `synthdef::compileAndLink()`.~~ Done.
2. ~~After compilation, calls `engine::addSynthDef()` to register the def with the engine.~~ Done.
3. ~~Returns error strings to the language instead of crashing.~~ Done (comprehensive error reporting across all pipeline stages).
4. ~~Caches compilation results keyed by name + s-expression hash.~~ Done.

### 3.3 Higher-level DSL in Tzopilotl — DONE

A comprehensive Tzopilotl module (`lang/modules/synthdef.x`, 1038 lines) provides a high-level DSL for creating synth definitions. It generates s-expressions and provides convenience functions like `play()`, `stop()`, `playFor()`. Imports the `audio_engine` module for playback. Additional modules: `common_ugens.x`, `dsp_math.x`, `example_synthdefs.x`, `filters.x` (biquad filters), `test_vec_ops.x` (vector operation tests).

---

## Phase 4: Finish Critical Language Features — PARTIAL

**Goal**: Complete the lang features needed for deeper integration.

### 4.1 Module system — DONE

The module system is fully implemented and tested. All three import syntaxes work (whole, wildcard, named with aliases), qualified access (`module.func(args)`) works including in space pipeline syntax, module file resolution supports relative and include-path-based lookup, circular import detection and module caching are in place, and all export types (functions, variables, structs, enums, templates, type aliases) are supported. Cascading errors on failed imports are suppressed. 13 module-specific tests. No further work needed.

### 4.2 Event-driven VM (Phase 12 in lang's plan) — NOT STARTED

Critical for real-time audio integration — the VM must respond to events and then return control. There are two classes of event handler with different constraints:

**Real-time event handlers** run integrated into the engine's `Silo` class. They must not call the system allocator or any blocking functions. They never handle I/O directly. Events include: timer tick, note trigger, control change, per-sample or per-buffer callbacks.

**Non-real-time event handlers** run on a separate thread with fewer restrictions. They handle OSC and NATS I/O, file operations, and other tasks that may block or allocate.

**All VMs** (RT and NRT) must support receiving code install updates (hot-reloading new function definitions or event handlers).

**Infrastructure already in place**:
- `VM::callFunction(CodeBlock* block, const Word* args, u16 argc)` — host can invoke compiled functions and get return values
- Direct-threaded dispatch naturally returns control after function completion
- Global variables persist across calls (designed for event-driven use)
- TLSF allocator and lock-free FIFOs available for RT-safe operation

**Remaining tasks**:
1. Implement an event dispatch loop: VM receives an event, executes the handler function, stack collapses, control returns to host.
2. Define event types and which are RT vs NRT: RT events (timer tick, note trigger, control change, code update), NRT events (OSC message, NATS message, code update).
3. Integrate RT event handlers into the engine's Silo — the VM runs within the Silo's processing loop using only the RT-safe allocator and lock-free communication.
4. Implement NRT event loop for handling I/O protocols (OSC, NATS) and dispatching to handler functions.
5. Implement code install updates — all VMs can receive and apply new function/handler definitions while running.
6. Ensure the VM can be "stepped" from a host loop (process one event, return).

### 4.3 Error handling improvements — PARTIAL

**Parser error recovery — DONE**: Synchronization implemented in `parser.cpp` — after encountering an error, the parser skips to the next statement boundary (`Semicolon`, `Fn`, `Let`, `Var`, `Const`, `Struct`, `Enum`, `Import`, etc.). Progress-check mechanism prevents infinite loops. Cascading errors from failed module imports are suppressed.

**Error location refinement — NOT DONE**: SourceRange/SourceLoc structures exist, but the parser still highlights the token where it detects the failure rather than the actual cause. The parser may consume several tokens through a rule before returning failure, so the highlighted location can be far from the real problem.

---

## Phase 5: OSC (Open Sound Control) Support — NOT STARTED

**Goal**: Control both the audio engine and language VM via OSC messages.

### 5.1 Choose an OSC library

Options:
- **liblo** — mature, C, widely used
- **oscpack** — C++, header-only, simple
- **Custom** — minimal implementation (OSC is a simple protocol)

Recommendation: **oscpack** or a minimal custom implementation to avoid external dependencies and maintain real-time safety.

### 5.2 OSC server for engine

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

### 5.3 OSC server for Tzopilotl VM

**Tasks**:
1. Add an OSC listener that can dispatch events to the VM.
2. Map OSC messages to VM events (ties into Phase 4.2 event-driven VM).
3. `/eval <code>` — compile and execute a string of Tzopilotl code.
4. `/call <functionName> <args...>` — call a named function.

### 5.4 OSC client (sending)

**Tasks**:
1. Add OSC send capability as Tzopilotl built-in functions.
2. `fn oscSend(host String, port Int, address String, args Array[Any]) Void;`

---

## Phase 6: NATS Support — NOT STARTED

**Goal**: Enable networked control and distributed messaging via NATS.

### 6.1 NATS client library

**Tasks**:
1. Evaluate NATS C client (`nats.c`) for real-time safety. It uses pthreads and malloc internally, so it must run on a non-RT thread.
2. Integrate NATS client as an NRT service — messages received on NATS are converted to commands and pushed to the engine via the existing FIFO.
3. Subscribe to subjects for engine commands (similar mapping to OSC addresses).

### 6.2 NATS for Tzopilotl

**Tasks**:
1. Add `fn natsPub(subject String, data String) Void;` as FFI function.
2. Add NATS subscription support: incoming messages trigger VM events.

### 6.3 NATS for distributed engines

**Tasks**:
1. Multiple engine instances can communicate via NATS subjects.
2. Useful for multi-machine performances or networked collaboration.

---

## Phase 7: Finish Remaining Engine Features — NOT STARTED

**Goal**: Complete engine functionality gaps.

### 7.1 Buffer operations

The engine declares but doesn't implement: `newBuffer`, `freeBuffer`, `resizeBuffer`, `loadBuffer`, `zeroBuffer`. Function signatures exist in `tzpl_client_interface.hpp` but no implementation in the .cpp file.

**Tasks**:
1. Implement a buffer pool (pre-allocated memory blocks for audio data).
2. Implement buffer loading from audio files (libsndfile or similar).
3. Add buffer read/write operations accessible from plugins.
4. Add buffer-related commands to the command system.

### 7.2 Audio input support -- DONE

**Completed tasks**:
1. ~~Enable input streams in RtAudio configuration.~~ Done. Supports three modes: duplex (same device for I/O), separate input device (second RtAudio instance with staging buffer), and macOS aggregate devices. `AudioStreamParameters` has `inputDeviceName`, `inputChannels`, `firstInputChannel` fields.
2. ~~Route hardware input to a special input node per silo.~~ Done. "Audio In" node (nodeID=1) created per silo with an output port. `processFrames()` copies hardware input samples to the input node's outlet each sample, before running the node graph.
3. ~~Plugins can receive live audio via inlet connections.~~ Done. Connect any plugin's input to the input node's output (nodeID=1, port 0) to receive live audio. The input node participates in topological sort like any other node.
4. App CLI supports `--input-channels`, `--input-device`, `--first-input-channel` flags and `inputDevice` config key.
5. FFI bridge exposes `inputChannels()` for querying active input channel count.

### 7.3 MasterGainCmd and ChannelOffsetCmd

Struct definitions exist in `tzpl_command_subclasses.hpp` but no command handler logic in Silo processing or command dispatch.

**Tasks**:
1. Implement master gain control (applied after safety limiter or integrated into it).
2. Implement channel offset for routing to specific hardware output channels.

### 7.4 Binary s-expression serialization

File `tzpl_sexpr_binary_buffer.hpp` exists but contains only commented-out skeleton code. Text-based s-expression parsing is fully implemented.

**Tasks**:
1. Complete the binary parser/serializer for efficient network transport of commands (useful with NATS).

---

## Phase 8: Finish Remaining Compiler Features — MOSTLY DONE

**Goal**: Complete synthdef-compiler gaps.

### 8.1 SIMD code generation — NOT STARTED

Infrastructure exists: `max_simd_width = 4`, `unroll_by = 4` in `synthdef_cpp_codegen.cpp`, SIMD type definitions (`f32x4`, `f64x2`, `f64x4`) in `synthdef_types.hpp`. But no SIMD loop body generation code — currently defaults to scalar codegen.

**Tasks**:
1. Generate SIMD loop bodies for multi-channel synths where channels align to SIMD widths (2, 4, 8).
2. Use `f64x2`/`f64x4` types from `tzpl_simd.hpp`.
3. Generate scalar remainder loops for non-aligned channel counts.
4. Benchmark against scalar codegen to validate speedup.

### 8.2 Full s-expression subgraph support — DONE

All control flow subgraph parsing from s-expressions is now implemented:
- IfExpr subgraph parsing and analysis
- SwitchExpr subgraph parsing (`parseSwitchExpr()` -- selector input, multiple case subgraphs)
- ForLoopExpr subgraph parsing (`parseForLoopExpr()` -- count and body graph)
- All three have full code generation support in `synthdef_cpp_codegen.cpp`

### 8.3 Event and note handling — DONE

Previously listed as stubs, now fully implemented:
- `genEventFun()` generates control event routing via memcpy to control arrays
- `genHandleEventsFun()` implements iso-group activation and event loop processing
- `genNoteFuns()` generates `_noteOn()` and `_noteOff()` with voicer integration, per-voice state reset, delay buffer init, RNG reseeding
- Both functions properly mark controls as active and manage state

### 8.4 FFT/spectral chain processing — DONE

Not in the original plan but now implemented:
- `SpectralChainExpr` and `SpectralFrameInput` expression types
- Forward/inverse FFT via `tzpl_fft_forward()`/`tzpl_fft_inverse()` (shared `tzpl_fft.hpp`)
- Windowing (Hann, sqrt-Hann) and overlap-add for real-time processing
- Ring buffer management, hop counter, per-channel FFT operations
- S-expression parsing via `parseSpectralChainExpr()` and `parseSpectralFrameInput()`
- Full code generation in `synthdef_cpp_codegen.cpp`

### 8.5 Vector operations — DONE

Not in the original plan but now implemented (11 operations):
- `VecTakeExpr`, `VecDropExpr`, `VecStrideExpr`, `VecStutterExpr`, `VecNCycExpr`
- `VecReverseExpr`, `VecTransposeExpr`, `VecRotateExpr`
- `VecAtExpr`, `VecPutExpr`, `VecJoinExpr`
- All have full s-expression parsing, type/shape inference, and C++ code generation
- Implicit binary op auto-mapping for multi-channel operations

---

## Phase 9: Finish Remaining Language Features — MOSTLY DONE

**Goal**: Complete lower-priority lang features.

### 9.1 Infinite lists and generators — DONE

Fully implemented with lazy evaluation support in Range type system. No further work needed.

### 9.2 Dynamic scoping (Phase 11 in lang's plan) — DONE

Fully implemented across all compiler phases. Backtick-prefixed variables (`var \`name = expr`) use dynamic (call-chain) scoping instead of lexical scoping.

Implementation details:
- Lexer: `DynamicVar` token type
- Type checker: Pre-scan registers dynamic vars before body checking, shared registry on Compiler
- Codegen: `op_load_dynamic`, `op_store_dynamic`, `op_dynscope_push` opcodes
- VM: Separate `dynVars_` table with save/restore stack, GC integration
- Zero overhead on normal function calls (one u32 write on pushFrame, one comparison on popFrame)

Test file: `lang/tests/dynamic_scope.x`. Module example: `lang/modules/dynvar.x`.

### 9.3 Standard library completion (Phase 13 in lang's plan) — MOSTLY DONE

**Completed**:
1. ~~String functions~~: substring, split, contains, startsWith, endsWith, trim, toUpper, toLower, replace, byte indexing. Tested in `tests/builtins/string_functions.x`.
2. ~~Array/list utility functions~~: Extensively tested.
3. ~~Range operations~~: Working.
4. ~~Map operations~~: `MapObj` class with builtins -- get (returns Option), getDefault, contains, keys, values, copy, merge.

**Remaining tasks**:
1. Add I/O functions (file reading for loading scripts -- NRT only).

### 9.4 Optimizations (Phase 14 in lang's plan) — MOSTLY DONE

**Completed**:
- ~~Register allocation~~ Done (register reclamation, `--no-reg-reclaim` flag to disable).
- ~~Tail call optimization~~ Done (`--no-tco` flag to disable).
- ~~Constant folding~~ Done (AST-level, `--no-const-fold` flag to disable).
- ~~Range loop inlining~~ Done (Int and Fraction range for-loops are inlined to avoid RangeObj allocation).

**Remaining tasks**:
1. General function inlining for small functions (beyond range loops).

---

## Phase 10: Live Coding Application — UI Framework — NOT STARTED

**Goal**: Choose and set up the UI framework.

The app/ directory exists with a CMakeLists.txt and main.cpp, but it is currently a **CLI-only application** (no GUI). It successfully links all three libraries via FFI bridges and can run Tzopilotl scripts with audio.

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
1. ~~Create `app/` directory with CMakeLists.txt.~~ Done (CLI only).
2. Set up Dear ImGui with a Metal backend (macOS) / Vulkan or OpenGL backend (Linux).
3. Create main application window with basic menu bar.
4. ~~Link against `libAudioEngine`, `libSynthdefCompiler`, `libTzopilotl`.~~ Done.
5. ~~Initialize all three systems at startup.~~ Done (in CLI app).

---

## Phase 11: Code Editor & REPL — NOT STARTED

**Goal**: Build the core live coding interface.

**Note**: A `REPLSession` class exists in `lang/src/repl_session.hpp/cpp` providing `eval()`, `queryType()`, `listGlobals()`, `listFunctions()`. This is used by the language's CLI tool but not yet integrated into the GUI app.

### 11.1 Code editor panel

**Tasks**:
1. Integrate ImGuiColorTextEdit (or similar) as the code editor widget.
2. Add Tzopilotl syntax highlighting rules.
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

## Phase 12: Plugin & Module Management — NOT STARTED

**Goal**: Browsing and managing synth plugins and language modules.

### 12.1 Plugin browser panel

**Tasks**:
1. List all loaded synth definitions with their port/control info.
2. Show plugin details: inputs, outputs, controls with specs.
3. One-click instantiation of a plugin as a new node.
4. Search/filter functionality.

### 12.2 Module browser panel

**Tasks**:
1. List available Tzopilotl modules.
2. Show module exports (functions, types).
3. Click-to-import into current editor.

### 12.3 Synthdef compilation UI

**Tasks**:
1. A "Compile" button that sends the current editor content through the synthdef-compiler.
2. Show compilation progress and errors.
3. Newly compiled plugins automatically appear in the plugin browser.

---

## Phase 13: Audio Graph Visualization — NOT STARTED

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

## Phase 14: Metering & Monitoring — NOT STARTED

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
3. Display GC statistics from Tzopilotl VM.
4. Display command queue depth.
5. Alert on audio dropouts (buffer underruns).

---

## Phase 15: Session Management — NOT STARTED

**Goal**: Save and restore the state of a live coding session.

**Note**: The app currently supports project directories with config files and auto-loading of pre-compiled plugins from `<project>/synthdefs/dylib/`, but full session save/restore is not implemented.

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

## Phase 16: Future Extensions (Lower Priority) — NOT STARTED

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
Phase 0 (Build Infrastructure)       ✅ DONE
  └─> Phase 1 (Library-ify)          ✅ DONE
        ├─> Phase 2 (FFI: Audio)     ✅ DONE
        │     └─> Phase 3 (FFI: SD)  ✅ DONE
        │           └─> Phase 4 (Lang Features)  🟡 PARTIAL (event-driven VM remaining)
        │                 └─> Phase 5 (OSC)       ⬜ NOT STARTED
        │                       └─> Phase 6 (NATS) ⬜ NOT STARTED
        ├─> Phase 7 (Engine Features)              🟡 PARTIAL ─────────────────┐
        ├─> Phase 8 (Compiler Features)            🟢 MOSTLY DONE ─────────┤
        └─> Phase 9 (Language Features cont.)      🟢 MOSTLY DONE ───────────┤
                                                                              v
                                                                  Phase 10 (UI Framework)  ⬜ NOT STARTED
                                                                    └─> Phase 11 (Editor)  ⬜ NOT STARTED
                                                                          └─> Phase 12     ⬜ NOT STARTED
                                                                                └─> Phase 13  ⬜ NOT STARTED
                                                                                      └─> Phase 14  ⬜ NOT STARTED
                                                                                            └─> Phase 15  ⬜ NOT STARTED
                                                                                                  └─> Phase 16  ⬜ NOT STARTED
```

**Notes on parallelism**:
- Phases 7, 8, 9 can proceed in parallel with Phases 2-6.
- Phase 10 can begin as soon as Phase 1 is complete (UI framework setup doesn't depend on FFI work).
- Phases 11-15 are sequential but can overlap (e.g., start Phase 12 while finishing Phase 11).

---

## Progress Summary

| Phase | Description | Status | Remaining Work |
|-------|-------------|--------|----------------|
| 0 | Build infrastructure | ✅ Done | CI setup |
| 1 | Library-ify projects | ✅ Done | — |
| 2 | FFI: engine | ✅ Done | — |
| 3 | FFI: synthdef-compiler | ✅ Done | — |
| 4 | Critical language features | 🟡 Partial | Event-driven VM, error location refinement |
| 5 | OSC support | ⬜ Not started | All tasks |
| 6 | NATS support | ⬜ Not started | All tasks |
| 7 | Engine feature completion | 🟡 Partial | Buffers, master gain, binary sexpr. Audio input done |
| 8 | Compiler feature completion | 🟢 Mostly done | SIMD codegen. FFT/spectral, vector ops, switch/for subgraphs all done |
| 9 | Language feature completion | 🟢 Mostly done | I/O functions, general function inlining. Map ops done |
| 10 | UI framework setup | ⬜ Not started | All tasks (CLI app exists) |
| 11 | Code editor & REPL | ⬜ Not started | All tasks (REPLSession exists) |
| 12 | Plugin/module management | ⬜ Not started | All tasks |
| 13 | Audio graph visualization | ⬜ Not started | All tasks |
| 14 | Metering & monitoring | ⬜ Not started | All tasks |
| 15 | Session management | ⬜ Not started | All tasks |
| 16 | Future extensions | ⬜ Not started | All tasks |

---

## Key Risks & Decisions

1. **Event-driven VM design** (Phase 4.2): This is the most architecturally significant remaining work. The VM must be able to process an event handler and return control to the host without blocking. This needs careful design to work with the engine's sample-accurate scheduling. Infrastructure (`callFunction()`, TLSF allocator, lock-free FIFOs) is in place, but the event management layer is not.

2. **UI framework choice** (Phase 10): Dear ImGui is recommended but the project description mentions Qt as an alternative. This should be decided before Phase 10 begins.

3. **Real-time safety across boundaries**: When Tzopilotl calls engine functions via FFI, the call chain must remain real-time safe. The bridge functions must not allocate memory or block. The existing TLSF allocator and lock-free FIFOs make this feasible, but it needs careful validation.

4. **Compilation latency for synthdef**: Calling clang at runtime to compile synth definitions takes time (100ms-1s+). This must happen on a background thread with the compiled plugin loaded asynchronously. The UI should show compilation status.

5. **Cross-platform audio**: RtAudio handles CoreAudio (macOS) and ALSA (Linux). Windows support via WASAPI/ASIO would be needed for full cross-platform coverage.

6. **Plugin ABI stability**: The `tzpl_plugin_abi.h` interface is the contract between all three projects. Changes to it require coordinated updates. Consider versioning the ABI.
