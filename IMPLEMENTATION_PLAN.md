# Integration & Product Implementation Plan

> **Working roadmap.** Phase statuses reflect the date of the last update; completed phases are retained as a design record, and later phases may be reordered or dropped.

This document is a step-by-step plan for integrating the three sub-projects (lang, synthdef-compiler, engine) and building the final audio coding application. It is based on an audit of the current state of each project.

**Last updated**: 2026-04-01

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
- OSC support via vendored oscpack library (UDP server/client, engine command dispatch, bundle/timetag scheduling, FFI bridge with local and remote send, reply routing)
- NATS support via `nats_lib` (cnats C client): NatsClient, NatsDispatcher, engine command mapping, VM eval/call handlers, FFI bridge with pub/sub/request
- Sample buffers: declared inside a synthdef, filled by the engine's resize/load/replace commands via the plugin's `swapBuffer` (no engine-side buffer pool); audio file loading via ExtAudioFile / libsndfile
- Binary message serialization (TZB) shared with the language and the app's `.tzd` documents
- **Remaining**: buffer readback and buffer-to-file writing (Phase 7.1); Linux audio file loading is unbuilt (no libsndfile in CMake)

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
- 318 tests, all passing
- Builds as static library (`tzpl_lib`) with install targets
- `callFunction()` API for host-driven function invocation (event handler infrastructure)
- REPLSession class for interactive evaluation
- Map type with builtins (get, getDefault, contains, keys, values, copy, merge)
- Event-driven VM infrastructure: cross-thread ARC deletion, NRT VM with mutex serialization, RT VM on Silo, NRT wall-clock scheduler, tempo-based NRT and RT schedulers with TempoRamp. See `EVENT_DRIVEN_VM_PLAN.md`.
- Clock FFI module: `sched`, `schedAbs`, `after`, `at`, `cancel`, `setTempo`, `getTempo`, `getBeats`, `getBeatDur`, `schedTempoChange`, `setLatency`, `getLatency`. App migrated from bare VM to NRTVM with tempo scheduler.
- **Remaining**: OSC handler dispatch, `rt.onNote`/`rt.onControl` FFI, error location refinement, general function inlining, I/O functions

### bridge/
- `tzpl_audio_engine_ffi.cpp` — 32 FFI functions wrapping audio engine commands (including `listSynthDefs`)
- `tzpl_synthdef_compiler_ffi.cpp` — 2 FFI functions for compile and compile-and-load
- `tzpl_clock_ffi.cpp` — 12 FFI functions for tempo-based scheduling (`clock` module)
- `tzpl_rt_tempo_scheduler.cpp` — RT tempo scheduler with pre-allocated entry pool (1024 entries), sample-accurate beat timing
- `tzpl_vm_commands.hpp` — Engine command subclasses for VM event dispatch, code hot-reload, RT tempo scheduling
- `modules/audio_engine.x` — Tzopilotl enum definitions (Enable, SchedPolicy, FadeCurve, Err)
- Bridges build as OBJECT libraries, linked into the app

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
- Audio coding application (`tzpl_app`) that links all three libraries via FFI bridges
- GUI mode (default): Dear ImGui with GLFW + Metal backend (macOS), bundled DejaVu Sans Mono font with runtime size switching (Cmd+=/-)
- Headless mode (`--nogui`): runs scripts and/or interactive REPL with linenoise
- Supports project directories with config files and pre-compiled plugin loading
- OSC and NATS listeners run in both modes
- Build option: `TZPL_BUILD_GUI` (default ON); Dear ImGui and GLFW fetched via CMake FetchContent

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
1. Create top-level `CMakeLists.txt` that adds each sub-project via `add_subdirectory()`. Done. Build options: `TZPL_BUILD_AUDIO_ENGINE`, `TZPL_BUILD_SYNTHDEF_COMPILER`, `TZPL_BUILD_LANG`, `TZPL_BUILD_BRIDGE`, `TZPL_BUILD_APP` (OFF by default), `TZPL_BUILD_TESTS` (OFF by default).
2. Refactor each sub-project's CMakeLists.txt to produce a library target. Done. Each sub-project also supports standalone builds via `if(NOT CMAKE_PROJECT_NAME STREQUAL "tzpl")` guards.
3. Define proper `target_include_directories(PUBLIC ...)` on each library. Done.
4. Move `shared/` into a proper CMake interface library target. Done.
5. Clean up legacy/duplicate headers. Done.
6. Ensure all three projects build successfully from the top level. Done.
7. Add a `build.sh` convenience script at the root. Done.

### 0.2 Cross-platform build support — PARTIAL

**Completed tasks**:
1. Verify macOS ARM64 builds for all three. Done.
3. Define CMake options for optional features. Done.

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
fn noteOn(nodeID Int, noteID Int, params [Float]) Int;
fn noteOff(nodeID Int, noteID Int) Int;
fn allNotesOff(nodeID Int) Int;
fn noteSetParams(nodeID Int, noteID Int, firstParam Int, values [Float]) Int;

-- Introspection
fn listSynthDefs() [String];

-- Utility
fn sleep(seconds Float) Void;
```

### 2.2 Implement the FFI bridge — DONE

1. C++ bridge layer wrapping each function into FFI signature. Done.
2. Each wrapper extracts arguments from VM registers. Done.
3. Register all functions with the VM's FFI system. Done (foreign module namespace `audio_engine`).
4. Engine pointer via VM user data pointer. Done (`setEngineOnVM()`).
5. Mark scheduling functions as `rtSafe`. Done (19 functions marked rtSafe).

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
fn listSynthDefs() [String];
```

Note: The name parameter was removed vs. the original plan — the synth name is extracted from the s-expression itself, avoiding redundancy.

### 3.2 Implement the FFI bridge — DONE

Implemented in `bridge/src/tzpl_synthdef_compiler_ffi.cpp` (192 lines). The bridge:
1. Calls `synthdef::synthFromSExprText()`, `synthdef::cppCodeGen()`, and `synthdef::compileAndLink()`. Done.
2. After compilation, calls `engine::addSynthDef()` to register the def with the engine. Done.
3. Returns error strings to the language instead of crashing. Done (comprehensive error reporting across all pipeline stages).
4. Caches compilation results keyed by name + s-expression hash. Done.

### 3.3 Higher-level DSL in Tzopilotl — DONE

A comprehensive Tzopilotl module (`lang/modules/synthdef.x`, 1038 lines) provides a high-level DSL for creating synth definitions. It generates s-expressions and provides convenience functions like `play()`, `stop()`, `playFor()`. Imports the `audio_engine` module for playback. Additional modules: `common_ugens.x`, `dsp_math.x`, `example_synthdefs.x`, `filters.x` (biquad filters), `test_vec_ops.x` (vector operation tests).

---

## Phase 4: Finish Critical Language Features — DONE

**Goal**: Complete the lang features needed for deeper integration.

### 4.1 Module system — DONE

The module system is fully implemented and tested. All three import syntaxes work (whole, wildcard, named with aliases), qualified access (`module.func(args)`) works including in space pipeline syntax, module file resolution supports relative and include-path-based lookup, circular import detection and module caching are in place, and all export types (functions, variables, structs, enums, templates, type aliases) are supported. Cascading errors on failed imports are suppressed. 13 module-specific tests. No further work needed.

### 4.2 Event-driven VM (Phase 12 in lang's plan) — DONE

Core infrastructure implemented. See `EVENT_DRIVEN_VM_PLAN.md` for the full design.

**What was implemented**:

1. **Cross-thread ARC deletion** (`lang/src/gc.hpp`, `arc.hpp`, `tlsf_allocator.hpp`, `vm.hpp/cpp`): Lock-free MPSC `ForeignDeleteQueue` (Treiber stack) per VM. Objects whose last reference is dropped on a foreign thread are enqueued on the home VM's foreign delete queue and freed during `gcHeartbeat()`.

2. **NRT VM with mutex serialization** (`lang/src/nrt_vm.hpp`, `nrt_scheduler.hpp/cpp`): `NRTVM` wrapper provides `call()`, `callCallable()`, `compileAndInstall()`, `execute()` -- all acquire a per-VM mutex, call `makeCurrent()`, and run `gcHeartbeat()`. Any thread (OSC server, NATS client, scheduler, UI) can call in. `NRTScheduler` runs on its own thread with wall-clock timing and logical time for drift-free scheduling.

3. **Tempo-based NRT scheduler** (`lang/src/tempo_ramp.hpp`, `nrt_tempo_scheduler.hpp/cpp`): Beat-based scheduler using `TempoRamp` for beat/second conversion. Tempo ramps are linear in beats (exponential in seconds). Queue is sorted by beat position (invariant under tempo changes). Fires events early by a user-specified latency so commands reach the RT thread in time. Tempo changes are scheduled as queue events. If a handler returns a positive finite number, it is rescheduled that many beats later (SuperCollider `Routine` convention). API: `sched(deltaBeats, handler)`, `schedAbs(beat, handler)`, `schedTempoChange(beat, targetTempo, rampBeats)`, `setTempo(bps)`, `setTempoBPM(bpm)`. Default 120 BPM, 50ms latency.

4. **RT tempo scheduler** (`bridge/include/tzpl_rt_tempo_scheduler.hpp`, `bridge/src/tzpl_rt_tempo_scheduler.cpp`): Same `TempoRamp` math as NRT, but uses sample time as the time base -- no latency compensation, events fire at the exact sample. Pre-allocated pool of 1024 entries (no RT allocation). Sorted doubly-linked list. Polled each sample via a callback on the Silo (`tempoSchedFn_`). Engine commands (`RTTempoSchedCmd`, `RTTempoChangeCmd`, `RTSetTempoCmd`, `AttachRTTempoSchedulerCmd`) deliver events and tempo changes from NRT to RT via the existing FIFO.

5. **RT VM on Silo** (`bridge/include/tzpl_vm_commands.hpp`, `engine/src/tzpl_silo.hpp`): `Silo::vm_` opaque pointer for attaching a VM. Engine command subclasses (`VMEventCmd`, `VMCallableCmd`, `CodeInstallCmd`, `AttachVMCmd`, `DetachVMCmd`) flow through the existing FIFO/scheduler.

6. **VM::callCallable()** (`lang/src/vm.hpp/cpp`): Calls a Lambda or Primitive from C++ host code, handling free variable setup for closures.

7. **Clock FFI module** (`bridge/src/tzpl_clock_ffi.cpp`): 12 FFI functions registered in the `"clock"` module: `sched`, `schedAbs` (reschedulable, `Fn() Float` handler), `after`, `at` (one-shot, `Fn() Void` handler), `cancel`, `setTempo`, `getTempo`, `getBeats`, `getBeatDur`, `schedTempoChange`, `setLatency`, `getLatency`. Documented in `lang/docs/FFI_Guide.html` Section 10.

8. **App migration**: The CLI app (`app/src/main.cpp`) migrated from bare `VM` to `NRTVM` with a `NRTTempoScheduler` (120 BPM, 50ms latency). Registered `clock` FFI alongside existing engine and synthdef-compiler FFIs.

**Remaining wiring tasks** (not core infrastructure):
- ~~Wire OSC handler dispatch to NRT VM (`osc.onMessage()` FFI)~~ Done (Phase 5).
- RT event handlers (`rtOnNote`, `rtOnNoteOff`, `rtOnControl`) and RT-to-NRT reply (`rtReply`) were removed. A more general inter-VM messaging scheme is planned, potentially transparent over NATS.

### 4.3 Error handling improvements — DONE

**Parser error recovery — DONE**: Synchronization implemented in `parser.cpp` -- after encountering an error, the parser skips to the next statement boundary (`Semicolon`, `Fn`, `Let`, `Var`, `Const`, `Struct`, `Enum`, `Import`, etc.). Progress-check mechanism prevents infinite loops. Cascading errors from failed module imports are suppressed.

**Error location refinement — DONE**: Three improvements made: (1) `expect()` now reports the actual token found (e.g. "Expected ')', got '{'"). (2) All closing-delimiter errors use `expectClosing()` which attaches a `DiagnosticNote` pointing to the opening delimiter (e.g. "to match ( here" at line 5). (3) `formatError()` renders notes with source context and caret underlining. Added `tokenKindString()` to produce readable names for all token kinds.

---

## Phase 5: OSC (Open Sound Control) Support — DONE

**Goal**: Control both the audio engine and language VM via OSC messages.

### 5.1 Choose an OSC library — DONE

Selected **oscpack** (vendored in `third_party/oscpack/`, MIT license). Provides packet serialization/parsing without external dependencies.

CMake option: `TZPL_BUILD_OSC` (default OFF). Requires `TZPL_BUILD_AUDIO_ENGINE` to be enabled.

### 5.2 OSC server for engine — DONE

Implemented in `osc/src/` as three components:

1. **OscServer** (`tzpl_osc_server.cpp`): UDP listener thread (POSIX sockets, configurable port, 65KB max packet, clean start/stop). Done.
2. **OscDispatcher** (`tzpl_osc_dispatcher.cpp`): Address-pattern routing to handler functions with mutex-protected registry. Supports bundles with timetag scheduling (NTP to engine stream time conversion), nested bundles, silo selection via `/engine/silo <int>`, and auto-wrapping single commands in `begin()/go()` transactions. Done.
3. **Engine command handlers** (`tzpl_osc_engine_commands.cpp`): Full mapping of OSC addresses to engine commands. Done.

**Implemented OSC addresses**:
- Lifecycle: `/engine/startAudio`, `/engine/stopAudio`, `/engine/masterGain`, `/engine/safetyLimiter`, `/engine/loadDefs`, `/engine/loadDef`
- Queries (with reply routing): `/engine/getStreamTime`, `/engine/listNodeDefs`, `/engine/isAudioRunning`
- Graph manipulation: `/engine/newNode`, `/engine/freeNode`, `/engine/freeAllNodes`, `/engine/replaceNode`, `/engine/connect`, `/engine/connectX`, `/engine/disconnectInput`, `/engine/disconnectInputX`, `/engine/disconnectOutput`, `/engine/disconnectNode`, `/engine/reconnectOutput`, `/engine/setInput`, `/engine/setInputX`, `/engine/setControl`
- Notes: `/engine/noteOn`, `/engine/noteOff`, `/engine/allNotesOff`, `/engine/noteSetParams`

App integration: `--osc-port <port>` CLI flag, `oscPort` config file key. Server started after audio engine init, clean shutdown on Ctrl-C.

### 5.3 OSC server for Tzopilotl VM — DONE

**Built-in OSC handlers** (`bridge/src/tzpl_osc_vm_handlers.cpp`):
- `/tzpl/eval <source_string>` -- compile and execute Tzopilotl source code received via OSC. Replies with `/tzpl/eval/ok` on success or `/tzpl/eval/error <message>` on failure.
- `/tzpl/call <address> [args...]` -- invoke a user-registered handler by OSC address, passing OSC args (int/float/string) as Tzopilotl values.

**User handler registration FFI** (added to `bridge/src/tzpl_osc_ffi.cpp`):
- `osc.onMessage(address String, handler fn() Void) Void` -- register a no-arg handler
- `osc.onMessageI(address String, handler fn(Int) Void) Void` -- int arg handler
- `osc.onMessageF(address String, handler fn(Float) Void) Void` -- float arg handler
- `osc.onMessageS(address String, handler fn(String) Void) Void` -- string arg handler
- `osc.onMessageArgs(address String, handler fn([Float]) Void) Void` -- float array handler
- `osc.removeHandler(address String) Void` -- remove a registered handler

Handlers are retained via ARC, stored in NRTVM's HandlerTable, and dispatched via OscDispatcher. String and array args are created inside the NRTVM mutex to ensure correct allocator usage. AppContext extended with `nrtvm`, `compiler`, `moduleCompiler`, and `target` pointers for full VM access from OSC handlers.

### 5.4 OSC client (sending) — DONE

**OscClient** (`tzpl_osc_client.cpp`): UDP sender with typed message variants.

FFI bridge (`bridge/src/tzpl_osc_ffi.cpp`) exposes both remote and local send functions:

**Remote send** (over UDP):
- `oscSend(host String, port Int, address String) Void`
- `oscSendI(host String, port Int, address String, value Int) Void`
- `oscSendF(host String, port Int, address String, value Float) Void`
- `oscSendS(host String, port Int, address String, value String) Void`
- `oscSendArgs(host String, port Int, address String, args [Float]) Void`

**Local send** (in-process dispatch, bypasses network):
- `oscSendLocal(address String) Void`
- `oscSendLocalI(address String, value Int) Void`
- `oscSendLocalF(address String, value Float) Void`
- `oscSendLocalS(address String, value String) Void`
- `oscSendLocalArgs(address String, args [Float]) Void`

**Server control**:
- `oscServerStart(port Int) Bool`
- `oscServerStop() Void`
- `oscServerPort() Int`

### 5.5 Testing — DONE

C++ integration tests (`integration-tests/src/test_osc.cpp`): server lifecycle, client sending, engine command dispatch, local dispatch, FFI compilation. Tzopilotl script test (`integration-tests/scripts/test_osc.x`).

---

## Phase 6: NATS Support — DONE

**Goal**: Enable networked control and distributed messaging via NATS.

### 6.1 NATS client library -- DONE

**Completed tasks**:
1. Evaluated NATS C client (`nats.c`, Apache-2.0, via `brew install cnats`). Uses pthreads and malloc -- runs on NRT thread only. Subscription callbacks are delivered on a nats.c-managed thread; dispatch acquires the handler mutex or NRTVM mutex as needed.
2. `nats/` library (`nats_lib` static library) with `NatsClient` (connect/disconnect/publish/request), `NatsDispatcher` (subject->handler routing with per-subject subscription management), and engine command mapping (20 engine commands: lifecycle, queries, graph operations, notes).
3. Engine command subjects use dot-separated names (e.g., `engine.newNode`, `engine.setControl`). Payloads are space-separated text arguments. Query handlers reply via NATS reply subjects.
4. `tzpl.eval` and `tzpl.call` NATS handlers for remote code execution and handler invocation.
5. Build controlled by `TZPL_BUILD_NATS` CMake option. Requires `cnats` system library.
6. 22 integration tests (all passing): client connect/disconnect, pub/sub, request/reply, engine command dispatch, VM eval, FFI registration, handler registration/invocation/removal.

### 6.2 NATS for Tzopilotl -- DONE

**Completed tasks**:
1. FFI functions in `nats` module: `natsConnect(url)`, `natsDisconnect()`, `natsIsConnected()`, `natsUrl()`, `natsPub(subject, data)`, `natsPubI(subject, value)`, `natsPubF(subject, value)`, `natsRequest(subject, data, timeoutMs, handler)` (async callback-based).
2. Subscription handlers: `onMessage(subject, handler)`, `onMessageS(subject, handler)`, `onMessageI(subject, handler)`, `onMessageF(subject, handler)`, `removeHandler(subject)`. Handlers are retained `Obj*` stored in NRTVM's `HandlerTable.natsHandlers` map.
3. App CLI supports `--nats-url <url>` and config file `natsUrl` key.
4. Conditional compilation via `TZPL_HAS_NATS` (set when `tzpl_nats_bridge` target exists).

### 6.3 NATS for distributed engines -- DONE

**Goal**: Multiple application instances can communicate via NATS for multi-machine performances and networked collaboration. The initial scope is language-level messaging between nodes and immediate-mode engine commands. Timing synchronization is explicitly deferred -- each engine/silo maintains its own independent tempo (multi-tempo music is a design goal).

**Completed tasks**:
1. **Engine naming**: `--engine-name` CLI option and `engineName` config key. When set, all engine command and VM handlers are registered under three subject prefixes: flat (`engine.*`), namespaced (`engines.{name}.engine.*`), and broadcast (`engines.all.engine.*`). `natsEngineName()` FFI function exposes the name to Tzopilotl code.
2. **Language-to-language messaging**: Works via existing `natsPub` / `onMessageS` etc. from 6.2. Convention: `nodes.{engineName}.{topic}` for addressing scripts on specific machines.
3. **Immediate-mode engine commands via named subjects**: All 20 engine commands and `tzpl.eval`/`tzpl.call` are addressable via `engines.{name}.*` subjects.
4. **Broadcast commands**: All commands addressable via `engines.all.*` for coordinated actions across all engines.
5. 26 integration tests (4 new namespaced/broadcast tests).

**Deferred**:
- Tempo/clock synchronization across engines (multi-tempo is intentional).
- State snapshot/restore for late-joining engines.
- Cross-engine audio routing.
- Latency-compensated scheduling.

---

## Phase 7: Finish Remaining Engine Features — DONE

**Goal**: Complete engine functionality gaps.

### 7.1 Buffer operations -- DONE

Shipped under a different design than the original tasks assumed. **A buffer is internal to a synthdef, not an engine-owned resource.** The synthdef declares it (`bufferVar()` in `lang/modules/synthdef.x`), the compiler turns it into a `tzpl_Buffer*` field on the generated plugin struct (null until filled), and the plugin advertises its slots through the optional `loadBufferDefs` symbol. The engine owns no buffers and has no buffer pool or registry: it only swaps a pointer into a plugin instance via `funs.swapBuffer(synth, bufID, newBuf)`, addressed by `(nodeID, bufID)`. The old pointer comes back for the NRT thread to free. Allocation and file I/O happen at record/submit time on the caller's thread, so the RT thread does nothing but a pointer store.

Consequently the originally listed `newBuffer` / `freeBuffer` / `zeroBuffer` engine entry points no longer exist and are not coming: `resizeBuffer` covers zeroing (`tzpl_createBuffer` callocs), and freeing a slot is `replaceBuffer` with a null pointer.

**Completed tasks**:
1. ~~Buffer pool~~ -- obsolete under the swap design; there is nothing to pool.
2. Loading from audio files. Done. `engine/src/tzpl_audio_file.hpp`: ExtAudioFile (AudioToolbox) on macOS, libsndfile on Linux, honoring `channelOffset` / `frameOffset` / `numFrames` and converting to non-interleaved f64.
3. Buffer read/write operations accessible from plugins. Done. `BufFixRead`, `BufVarRead` (interpolated), `BufWrite`, `BufLength` -- expression graph, s-expression parsing, and C++ codegen including SIMD paths. Reads compile to `buf ? ... : 0.0` and writes are wrapped in `if (buf)`, so an unfilled slot is safe. DSL surface: `bufferVar` / `read` / `vread` / `write` / `length`; mirrored in synthc; 7 tests in `synthdef-compiler --test`.
4. Buffer commands in the command system. Done. `ResizeBufferCmd`, `ReplaceBufferCmd`, `LoadBufferCmd` -- all two-stage, freeing the displaced buffer on the NRT thread.
5. Beyond the original scope: `resizeBuffer` / `loadBuffer` exposed to Tzopilotl via the FFI bridge; the plugin browser shows a Buffers section per def; node control panels get a per-slot "load <buffer>" button feeding the `waveform` widget (see 13.3).

**Remaining gaps** (none blocking; buffers work end to end on macOS):
- No readback. Nothing reads buffer contents back out of the engine -- the waveform overview re-reads the file from the path the app recorded at load time. No buffer-to-file write either, so a buffer written by a synth cannot be saved.
- `replaceBuffer` is C++-only (it takes a `tzpl_Buffer*`), and clearing a slot by swapping in null is neither exposed nor tested, though the null guards make it safe.
- The FFI `loadBuffer` is 3-arg (`nodeID`, `bufID`, `path`) -- it does not expose the engine function's channel/frame offsets.
- Linux would not link: `tzpl_audio_file.hpp` includes `<sndfile.h>` on non-Apple, but no CMakeLists finds or links libsndfile.
- Undocumented from the language: the FFI Guide's audio-engine section never mentions `loadBuffer` / `resizeBuffer`, no example `.x` calls them, and there is no end-to-end test that loads a file and plays it (`graph_layout_test` covers submit plus frame count only).

### 7.2 Audio input support -- DONE

**Completed tasks**:
1. Enable input streams in RtAudio configuration. Done. Supports three modes: duplex (same device for I/O), separate input device (second RtAudio instance with staging buffer), and macOS aggregate devices. `AudioStreamParameters` has `inputDeviceName`, `inputChannels`, `firstInputChannel` fields.
2. Route hardware input to a special input node per silo. Done. "Audio In" node (nodeID=1) created per silo with an output port. `processFrames()` copies hardware input samples to the input node's outlet each sample, before running the node graph.
3. Plugins can receive live audio via inlet connections. Done. Connect any plugin's input to the input node's output (nodeID=1, port 0) to receive live audio. The input node participates in topological sort like any other node.
4. App CLI supports `--input-channels`, `--input-device`, `--first-input-channel` flags and `inputDevice` config key.
5. FFI bridge exposes `inputChannels()` for querying active input channel count.

### 7.3 MasterGainCmd and ChannelOffsetCmd -- DONE

**Completed tasks**:
1. Implement master gain control (applied after safety limiter or integrated into it). Done.
2. Implement channel offset for routing to specific hardware output channels. Done.

### 7.4 Binary s-expression serialization -- DONE

Shipped as the **TZB binary message format**, not in the originally planned file. The `Msg` type (renamed from `SExpr` on 2026-06-29) is the serialized value. The originally planned `engine/src/tzpl_sexpr_binary_buffer.{hpp,cpp}` never got past a commented-out skeleton and has been deleted.

**Completed tasks**:
0. **Documentation**: `lang/docs/FFI_Guide.html` §15 (Binary Messages) is the reference for TZB -- §15.5 the wire format, §15.7 the C++ API. `shared/tzpl_sexpr_bin.hpp`'s header comment is the normative layout definition that both implementations follow.
1. `shared/tzpl_sexpr_bin.hpp` -- the canonical wire layout (`namespace tzpl::sbin`, magic `TZB`, version 2, little-endian). Provides a zero-copy bounds-checked `Reader` (random access, no allocation -- safe for the silo / consumer side), plus `Writer`/`encode` over a small `Value` variant. Parallel tag/payload arrays give O(1) child access into vectors.
2. Tzopilotl side: `Bytes` type (`lang/src/builtins_bytes.cpp`), `lang/modules/std/message.x` (the `Msg` type) and `lang/modules/std/messageEncoding.x` (`encode`/`decode`/`Reader`), mirroring the C++ layout byte for byte.
3. NATS transport: `natsPubMsg(subject, Bytes)` and `onMessageMsg(subject, fn(Bytes))` in `bridge/src/tzpl_nats_ffi.cpp`; `bridge/modules/nats.x` wraps them to deliver messages into an actor mailbox.
4. Cross-language conformance test: `integration-tests/scripts/sexpr_bin_interop.sh` encodes the same value with the C++ encoder and with `messageEncoding.x` and diffs the byte dumps (passing). Also `tools/sexpr_bin_selftest.cpp`.
5. Second consumer beyond networking: the app's content-addressed notebook history hashes canonical sbin bytes (`app/src/content_hash.hpp`, `app/src/document.cpp`), with `app/tests/doc_roundtrip_test.cpp` covering it.

**Not done** (deliberate): engine command payloads over NATS are still space-separated text (see 6.1/6.3). Moving them onto TZB is a possible follow-up, not a gap in the serializer.

---

## Phase 8: Finish Remaining Compiler Features — DONE

**Goal**: Complete synthdef-compiler gaps.

### 8.1 SIMD code generation — DONE

Fully implemented in `synthdef_cpp_codegen.cpp`. The `genLoop()` function generates SIMD loop bodies using `simdLoad()`, `simdStore()`, and `simdSplat()` helpers. Two code paths: single vector op (no loop needed) or stride loop with `i += width`. SIMD type definitions in `shared/tzpl_simd.hpp`.

**Completed tasks**:
0. SIMD code generation is optional. `--simd-2` CLI flag enables 2-channel SIMD (minimum width = 2 instead of default 4). Default OFF for stability; scalar code used unless explicitly enabled. Done.
1. SIMD loop bodies generated for multi-channel synths with power-of-two channel counts. Done.
2. Both integer and float operations vectorized. Done.
3. Instance variables and local variables containing SIMD vectors are aligned. Done.
4. SIMD vs scalar benchmarks added. Done.

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

### 9.3 Standard library completion (Phase 13 in lang's plan) — DONE

**Completed**:
1. String functions: substring, split, contains, startsWith, endsWith, trim, toUpper, toLower, replace, byte indexing, codePoints (lazy `List<Int>` of Unicode code points from UTF-8 string), indexOf/lastIndexOf, strict parseInt/parseFloat (Option returns). Tested in `tests/builtins/string_functions.x`, `tests/builtins/codepoints.x`, `tests/stdlib/`.
2. Array/list utility functions: Extensively tested.
3. Range operations: Working.
4. Map operations: `MapObj` class with builtins -- get (returns Option), getDefault, contains, keys, values, copy, merge.
5. File/OS IO builtins (July 2026, `builtins_io.cpp`, NRT-gated via `rtSafe=false`): readFile/readFileBytes/writeFile/appendFile, fileExists/isDirectory/fileSize/fileModTime, listDir/makeDir/removeFile/renameFile, getEnv/programArgs/currentDir.
6. `std.*` module namespace (`lang/modules/std/`): strings, path, fs, result, test, json, message, messageEncoding, futures, thunk. Documented in `lang/docs/Standard_Library.html`; expansion roadmap in lang's plan §13.5.

### 9.4 Optimizations (Phase 14 in lang's plan) — MOSTLY DONE

**Completed**:
- Register allocation. Done (register reclamation, `--no-reg-reclaim` flag to disable).
- Tail call optimization. Done (`--no-tco` flag to disable).
- Constant folding. Done (AST-level, `--no-const-fold` flag to disable).
- Range loop inlining. Done (Int and Fraction range for-loops are inlined to avoid RangeObj allocation).

**Remaining tasks**:
1. General function inlining for small functions (beyond range loops).

---

## Phase 10: Application — UI Framework — DONE

**Goal**: Choose and set up the UI framework.

### 10.1 Framework evaluation — DONE

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

**Decision**: **Dear ImGui** — lightweight, MIT license, excellent for real-time applications, easy to embed, growing ecosystem of widgets. Node graph editors (imnodes) already exist. Code editors exist (ImGuiColorTextEdit). Pairs well with a custom audio engine.

### 10.2 Application scaffold — DONE

**Completed tasks**:
1. Create `app/` directory with CMakeLists.txt. Done.
2. Set up Dear ImGui with GLFW + Metal backend (macOS). Done. Dear ImGui v1.91.0 and GLFW 3.4 fetched via CMake FetchContent. OpenGL3 backend prepared for Linux. Build option: `TZPL_BUILD_GUI` (default ON).
3. Create main application window. Done (1280x800, "Tzopilotl" title). ImGui demo window shown as scaffold UI.
4. Link against all libraries via FFI bridges. Done.
5. Initialize all three systems at startup. Done.
6. Headless mode (`--nogui`): runs scripts with full engine access, drops to interactive REPL (linenoise, multi-line input, `:help`/`:quit`/`:type`/`:globals`/`:functions`/`:memory`/`:gc` commands). If OSC/NATS listeners are active, stays alive as a headless node. Done.
7. Font setup: bundled DejaVu Sans Mono with runtime font search (exe-relative for deployment, compile-time path for dev builds, Monaco system fallback on macOS). Three pre-rasterised sizes (14/16/18pt) switchable at runtime via Cmd+=/-. Retina-crisp rendering via scaled atlas + FontGlobalScale. Done.
8. Trackpad scroll dampening (25% of raw GLFW values) via chained scroll callback. Done.
9. Native macOS menu bar replacing Dear ImGui menu bar. Objective-C `TzplMenuHandler` class bridges menu events to main loop via global state flags. Menus: App (About, Quit), File (New, Open, Save, Save As, Save a Copy As, Close Tab), View (font size Cmd+=/-). Done.
10. Native file dialogs via `NSOpenPanel` / `NSSavePanel` with `.x` file type filtering. Integrated with File menu operations. Done.

---

## Phase 11: Code Editor & REPL — DONE

**Goal**: Build the core code editor and REPL interface.

### 11.1 Code editor panel — DONE

**Completed tasks**:
1. Integrated ImGuiColorTextEdit (BalazsJako, via FetchContent) as the code editor widget. Done.
2. Tzopilotl syntax highlighting: 34 keywords, `--` line comments, `/* */` block comments, hex/float/imaginary number literals, `"string"` literals, `'symbol` literals. Done.
3. Line numbers, current line highlighting built into TextEditor widget. Done.
4. Multiple editor tabs: tab bar with + button and close buttons, each tab has its own TextEditor instance. Starts with `scratch.x`. Done.

New files: `app/src/editor_panel.hpp`, `app/src/editor_panel.cpp`.

### 11.2 REPL / output panel — DONE

**Completed tasks**:
1. Scrolling output panel with color-coded lines (white=print output, green=eval results, red=errors, blue=info). VM `print`/`println` output captured via pipe (`setPrintOutput` + non-blocking read each frame). Done.
2. Single-line REPL input field with Enter-to-submit and Up/Down arrow command history. Done.
3. Compilation errors set as red line markers on the editor via `TextEditor::SetErrorMarkers()`. Cleared on next successful evaluation. Done.
4. Type information display deferred to a later phase (hover/status bar).

New files: `app/src/output_panel.hpp`, `app/src/output_panel.cpp`, `app/src/gui_state.hpp`, `app/src/gui_state.cpp`.

### 11.3 Execute-on-keystroke — DONE

**Completed tasks**:
1. Cmd+Enter evaluates selected text, or current block (contiguous non-empty lines around cursor) if no selection. Done.
2. Shift+Enter evaluates current line. Done.
3. Cmd+Shift+Enter evaluates entire file. Done.
4. Eval flash: brief blue highlight overlay on evaluated lines, fades over ~0.33s. Done.

Keyboard shortcuts intercepted at the GLFW key callback layer (`GLFW_MOD_SUPER`) to avoid macOS Cmd key conflicts. All evaluation goes through `REPLSession` with `NRTVM` mutex for thread safety with concurrent OSC/NATS handlers.

### 11.4 File operations — DONE

File operations integrated via native macOS menu bar (see Phase 10.2):
- New tab, open file (native file dialog with `.x` filter), save, save as, save a copy as, close tab.
- Untitled tabs prompt Save As on first save.

### Layout

Vertical split: editor panel on top, output+REPL panel on bottom, with a draggable horizontal splitter. Sizes computed from `ImGui::GetContentRegionAvail()` to fit within window bounds. `REPLSession` created inside `runGui()` using the `AppContext`'s compiler, NRTVM, and target.

---

## Phase 12: Plugin & Module Management — PARTIAL

**Goal**: Browsing and managing synth plugins and language modules.

### 12.1 Plugin browser panel — MOSTLY DONE (2026-07-23)

**Completed tasks**:
1. List all loaded synth definitions with their port/control info. Done. Both apps: floating "Plugins" window (ImGui: View menu / Cmd+Shift+P; JUCE: View menu / Cmd+Shift+B) listing every registered def with inlet/outlet/control/buffer counts. Auto-refreshes ~1 Hz while open. A second "Available" section lists loadable-but-not-loaded plugins found in the search paths (`AppContext::pluginSearchPaths`: project `synthdefs/dylib` + the synthdef compile cache), with a Load button. The directory scan never dlopens; selecting an available plugin introspects it once via `getPluginFileDesc`, cached engine-side by path + mtime (failures too) so the same plugin is never reloaded.
2. Show plugin details: inputs, outputs, controls with specs. Done. Selecting a def shows Inlets/Outlets/Controls/Buffers sections with name, channel count, element type, and rate; controls add lo/hi/init from the ControlSpec; buffers add the bufID.
4. Search/filter functionality. Done (case-insensitive substring filter on the def name).

Supporting work:
- New engine introspection API: `engine::DefDesc` + `getDefDesc()` / `listDefDescs()` in `tzpl_client_interface.hpp` (copies under `nrt_lock_`, GUI-thread safe). `ControlDesc` gained a `tzpl_SignalType type` field. On-disk discovery: `listPluginFiles()` (filename-stem scan, newest revision per name, earlier dirs shadow later), `getPluginFileDesc()` (transient dlopen introspection with path+mtime cache), and `loadOneDef()` made public for single-file loading.
- Buffer metadata added to the plugin ABI via the optional `loadBufferDefs` symbol (`tzpl_BufferDef` / `tzpl_BufferDefList` in `tzpl_plugin_abi.h`) — a separate symbol rather than a `tzpl_SynthDef` extension so old plugins/engines stay compatible in both directions. Emitted by both the C++ codegen and the synthc port (byte-identical, differential-tested); stored as `BufferInfo` in `NodeDefInfo`.
- Plugin ABI version stamp: `TZPL_PLUGIN_ABI_VERSION` (currently 1) in `tzpl_plugin_abi.h`; generated plugins export `extern "C" int64_t tzpl_abi_version`. Missing symbol = version 0 (pre-versioning, layout-compatible). All three loaders (`engine::loadOneDef`, `synthdef::loadDef`, `getPluginFileDesc`) refuse plugins stamped newer than the header they were built against. Bump only for layout/calling-convention breaks; additive changes keep using optional-symbol probing.
- Heterarchical category tags (three additive layers, union-merged):
  1. **Embedded**: `(Tags "test" ...)` sexpr clause -> `Synth::tags` -> optional plugin symbol `loadTags` (`tzpl_TagList`, no version bump), emitted byte-identically by both codegens (differential-tested), surfaced as `DefDesc::tags`. Lang side: `defSynth`/`defSynthX` take an optional `tags [String]` argument; `TZPL_DEFAULT_TAGS` (comma-separated env var, read by `defaultSynthTags()` in synthdef.x) is appended to everything compiled in the session -- the test harnesses set `TZPL_DEFAULT_TAGS=test` so their synthdefs are born tagged.
  2. **Name-pattern rules**: globs implying tags (default `test_*` => test) for legacy untagged dylibs.
  3. **User-local tags**: added in the browser, keyed by SYNTHDEF NAME (not path -- names are the identity the engine/cache/browser already use, so tags survive folder moves and recompiles).
  Layers 2-3 + the per-tag filter states persist in one per-user, human-editable file shared by both apps (`~/Library/Application Support/Tzopilotl/plugin_tags.txt`; `app/src/plugin_tags.hpp` `PluginTagStore`). Filtering is tri-state per tag (show / hide / don't-care): hide wins, and a non-empty show set acts as a whitelist -- "show all fx except distortion" = fx:show + distortion:hide; with no show tags, everything not hidden is visible (default: hide `test`). Browser UI: tag chips in the details pane (local tags removable, add-tag input), a "Tags..." dropdown setting each tag's tri-state (with per-tag plugin counts), and a "show hidden (N)" momentary reveal toggle.
- Fixed `engine::loadOneDef()`: it called the plugin's `load()` through a stale `void(*)(Engine*)` signature (mismatched sret call, def never registered). Precompiled-dylib preloading (`loadPlugins`/project `synthdefs/dylib/`) now actually registers defs.

**Remaining tasks**:
3. One-click instantiation of a plugin as a new node.

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

## Phase 13: Audio Graph Visualization — IN PROGRESS

**Goal**: Visual representation of the running audio graph. Implemented as a **third center-pane mode** of the app alongside the text editor and notebook. JUCE-first: the core (engine snapshot, view-model, layout) is toolkit-free; an ImGui/imnodes view can follow later if wanted.

### 13.1 Node graph display — DONE (JUCE, 2026-07-23)

**Engine: NRT topology shadow + snapshot API.** Connection state previously existed only in RT-thread-owned port linked lists with no NRT mirror or change notification. Added:
- `GraphShadow` per Silo (`tzpl_silo.hpp`): `nodeID -> defName` map + `ShadowConn` list, guarded by a new `Engine::shadowMtx_` (NOT `nrt_lock_`: inline command execution runs while `sched()` holds `nrt_lock_`; lock order is `shadowMtx_` inside `nrt_lock_`, never the reverse). Seeded with nodes 0/1 in the Engine ctor.
- **Journal at submit, commit at execution**: topology-mutating `BundleOp`s record `ShadowEdit`s during submit-time validation (atomic abort discards them); `sched()` appends a `ShadowCommitCmd` as the bundle's last command, whose `doNRT` (stage 2, after the bundle actually executed on RT) applies the edits and bumps `Engine::graphGeneration_`. The shadow therefore follows RT **execution order** — correct even when bundles are scheduled to run out of submission order or tempo changes on the per-silo clocks reorder clock-scheduled bundles (late-bound beat scheduling; the commit rides with its bundle). A `fired_` flag set in `doRT` keeps late-dropped `schedOnTimeOnly` bundles from committing. Fan-in shows as duplicate conns; hidden mixers/xfaders (nodeID -1) never appear; `commitShadowEdits` mirrors RT's silent no-ops for stale scheduled commands.
- Snapshot API (`tzpl_client_interface.hpp`, DefDesc copy-under-lock pattern): `GraphDesc {generation, nodes, conns}`, `getGraphDesc(e, silo, out)`, lock-free `graphGeneration(e)` for cheap dirty-polling, `numSilos(e)`. `addSynthDef` also bumps the generation (def hot-reload changes port metadata).
- Fixed a pre-existing bug found during this work: the NRT drain loops ran `run()` on stage-0 commands whose `err_` was already set (late-dropped `schedOnTimeOnly`), executing them on the NRT thread; they are now destroyed unrun.
- Tests: `integration-tests/src/test_graph_shadow.cpp` (43 checks) — per-op shadow effects, generation gating, atomic abort, per-silo isolation, and an NRT-rendered out-of-submission-order scheduling test.

**Toolkit-free core** (`app/src/`, no GUI includes): `graph_model.{hpp,cpp}` — `GraphPoller` (generation-gated poll, DefDesc join with def cache, defMissing pin synthesis) + `buildViewModel` split out for testing; `graph_layout.{hpp,cpp}` — `LayoutStore` (session positions keyed by (silo, nodeID)) + `autoLayout` (longest-path layering toward Audio Out via reverse DFS with cycle-neutral back edges, per-column x from max widths, one barycenter row-ordering pass, deterministic). Tests: `app/tests/graph_layout_test.cpp` (`tzpl_graph_tests`, 20 checks).

**JUCE UI**: `app/juce/graph_view.{hpp,cpp}` — canvas with world transform (pan/zoom: drag background, wheel, cmd-wheel + pinch about cursor, zoom-to-fit on first show), node boxes (title + #nodeID, inlet pins left / outlet pins right with name/chans/elem-rate glyphs, Audio In/Out tinted, defMissing dimmed), bezier wires, click-select node/edge, drag nodes (session-sticky via LayoutStore), toolbar (silo selector, Re-layout, Fit), 8 Hz poll timer while showing (one atomic read per tick when unchanged), empty-state hint. Theme colors via LookAndFeel ids.

**Mode switching**: `MainComponent`'s `bool notebookVisible_` replaced by `enum CenterMode { editor, notebook, graph }` + `setCenterMode()`; `showNotebook(bool)` kept as a wrapper so call sites didn't change. Graph mode overlays a *document* mode: save/eval/undo/revert route via `lastDocMode_` (`docModeIsNotebook()`), and closing the graph returns to it. Command: `cmd::toggleGraphView`, View menu, **Cmd+Shift+\\** (pairs with Cmd+\\ notebook toggle).

**Remaining 13.1 ideas**: ImGui/imnodes view over the same core; resync-from-engine hook if shadow drift is ever observed in practice.

### 13.2 Interactive editing — MOSTLY DONE (2026-07-24)

**Completed**:
1. Drag-to-connect. Done. Drag from any pin (either direction); every type-compatible opposite pin shows an accent ring (rules mirror `compatibleTypes`/`relaxedCompatibleTypes` — relaxed channel count for Audio Out; unknown defs pass and the engine decides), the hovered target snaps the rubber band. Drop submits `begin/connect/go` on the message thread; no optimistic UI — the accepted bundle's shadow commit bumps the generation and the view re-snapshots.
2. Delete connections. Done. Click a wire to select, Delete/Backspace sends `disconnectSource`. Delete on a selected node (id >= 2) frees it; Esc clears selection.
3. Right-click menus. Done. Background: "New Node" palette from `listDefDescs` (Audio In/Out excluded), new node gets the smallest unused nodeID >= 2 and drops at the click point (LayoutStore pre-seeded). Node: Disconnect All / Free Node (Free disabled for nodes 0/1).
3b. Click-free audio: every UI connect/disconnect passes a 0.1 s crossfade (`graph::kUIXFadeTime`); the engine splices an xfader only for float-element ports, so integer/event connections stay hard. "Disconnect All" fades too -- it bundles per-wire `disconnectSource` commands built from the view (`disconnectAllWires`) instead of the hard-cut `disconnectNode`. Free Node remains an immediate cut.
4. Submission helpers are toolkit-free in `app/src/graph_edits.{hpp,cpp}` (`canConnect`, `nextFreeNodeID`, `errText`, connect/disconnect/create/free/disconnectNode submitters) for a future ImGui view. Errors surface in the app console via `GraphView::onLog`.
5. Tests: `tzpl_graph_tests` grew unit checks (`canConnect` rule matrix, `nextFreeNodeID`) and a live-engine end-to-end pass driving every submitter through `GraphPoller` (50 checks total).

**Remaining**:
- Double-click a node to open its controls (lands with 13.3).

### 13.3 Control surfaces — DONE (2026-07-24, MIDI deferred)

Implemented by reusing the live-controls widget system rather than building a parallel panel: double-click a node in the graph view (or right-click > "Controls...") materializes the node's control interface as engine-bound `UIState` widgets in a panel named "<def> #<id>", and the existing floating `ControlsWindow` machinery displays it. Everything downstream was already built: per-ControlSpec widget kinds and warps, coalesced `setControl` delivery via `ControlsDispatcher` (~30 Hz while active), presets, arrange mode, key bindings, and both apps' renderers.

**Completed**:
1. Shared bridge helper `bridge/tzpl_ui_node_controls.{hpp,cpp}`: `specFromControl` (ABI spec -> UISpec, warp ordinal mapping), `widgetKindForControl` (Continuous -> Slider, Trigger -> Button, Boolean -> Toggle, Select -> Number), `bindControlWidget`, and `materializeNodeControls(ui, engine, panel, nodeID, silo, defName)`. `ui.control`/`ui.controls` in `tzpl_ui_ffi.cpp` now use the same helpers (previously file-static duplicates); the graph-view path resolves the def name from the topology shadow, so it works for nodes created by any client (the FFI path still uses `AppContext::nodeDefNames`).
2. `GraphView::onOpenNodeControls` -> `MainComponent::openNodeControls()`: materialize, `dispatcher_.ensureRunning()`, `refreshControlsWindows()` immediately, window to front. Closing the window removes the panel's widgets (existing behavior). No-control and unknown-def cases report to the console.
3. Write-only, as planned: widgets start at each spec's `init` (an already-open panel keeps its tweaked values -- `UIState::upsert` semantics); no engine readback of current control values yet. Readback pairs naturally with Phase 14 metering (tap-like mirror or a getControl command).
3b. Buffers: defs with sample buffers get a "load <buffer>" button per slot in the node's panel. Buttons carry a new `UIWidget::hostAction` (host-side click action, invoked async off `ui->mtx` by the JUCE renderer; never serialized) that opens a file dialog; the chosen file is read on the message thread (`graph::loadBufferFile` -- pre-reads via `tzpl_loadAudioFile` so bad files report synchronously, unlike the engine's `loadBuffer` command which only fails on RT) and submitted as a `replaceBuffer` bundle. The path is recorded in `AppContext::bufferPaths` (same as `audio_engine.loadBuffer`), and a waveform overview row (shared `bindWaveformWidget`, also now used by `ui.waveform`) appears/refreshes above the button; already-loaded buffers show their waveform when the panel opens.
4. Tests: `tzpl_graph_tests` (65 checks) covers `materializeNodeControls` end-to-end against a registered def -- widget kinds, spec/warp mapping, `(node, controlID, silo)` binding, `dirtyEngine` push flag, idempotent reopen, unknown def.

5. Control kinds are now authorable (2026-07-24). The ABI's `tzpl_ControlKind` (Continuous/Trigger/Boolean/Select) previously had no authoring path -- the codegen hard-coded Continuous. Now: DSL sugar constructors in `synthdef.x` (`control()` = continuous; `trigger(name)` = momentary button; `toggle(name, init)` = latched 0/1 toggle; `choice(name, numChoices, init)` = integer select, step warp), carried as a `ControlKind` on the `SignalExprKind.control` payload, emitted as an optional 6th `(ControlSpec ...)` field (omitted for continuous, so existing defs/goldens are byte-identical), parsed into the compiler's `ControlSpec` (now in its hash/equality), and emitted as `.kind = (tzpl_ControlKind)N` by BOTH codegens (C++ + synthc, differential tests green). Node control panels and `ui.controls` now produce buttons/toggles/number boxes for real. End-to-end test: `test_control_kinds` in `test_synthdef_compiler_ffi` (DSL -> sexpr -> compile -> DefDesc -> widget kinds).

6. Multichannel controls (2026-07-24): a control with `chans > 1` gets ONE widget carrying all channels -- Continuous/Select -> a MultiSlider with one bar per channel (all mapped through the spec, starting at init); Boolean/Trigger -> a 1 x chans ButtonMatrix row (toggle / momentary). One widget per control is structural, not just cosmetic: the dispatcher sends the whole value vector as a single `setControl(node, controlID, N, vals)`, and there is no per-element setControl form for independent per-channel widgets to bind to. (Previously a multichannel control got a single slider that wrote only channel 0.)

**Deferred**: MIDI mapping to controls (per plan); docs for the new constructors in the synthdef guide.

---

## Phase 14: Metering & Monitoring — DONE (2026-07-24)

**Goal**: Audio level meters, scope displays, and performance monitoring.

**Note on prior work**: node-outlet taps (`engine/src/tzpl_tap.hpp`, `Silo::processTaps`, `tapOutlet`/`untap`/`tapPeak`/`tapRms`/`tapDrain`), the `ui.meter`/`ui.scope` lang surface, and Meter/Scope widgets in both frontends predated this phase — they landed alongside the live-controls work. So 14.1.2, 14.1.3, 14.2.1 and 14.2.2 were already done when the phase opened. This phase built the master bus path, the spectrum display, graph-view metering, and all of 14.3.

### 14.0 Tap plumbing (prerequisite) — DONE

- **Dense tap table**: `Silo::rt_taps_` keeps live entries in the prefix `[0, numTaps_)`; removal moves the last entry into the hole. `processTaps` (per **sample**) now only touches live taps, which made raising `kMaxTaps` from 32 to 128 affordable. `Silo::eraseTapAt` is the single removal primitive.
- **Synchronous budget**: `applyTapOutlet` counts a silo's live registry slots and returns the new `tzpl_errResourceLimit` (appended to `tzpl_SErr`; ABI values are append-only) at the cap. Previously a full table let `go()` return `errNone` and handed the caller a tapID that read silence forever.
- **`engine::allocTapID`** (`Engine::nextTapID_`) replaces `UIState::nextTapId`, so widget taps and graph-view meters draw from one process-wide sequence and can never collide.
- **`AtomicFifo::depth()`** replaces the commented-out `numPushed`/`numPopped`, using unsigned subtraction (the counters are monotone and wrap).
- **De-duplication**: the tap-poll loop was byte-identical in `app/src/controls_panel.cpp` and `app/juce/widgets/controls_dispatch.cpp` and had already drifted (only the JUCE copy guarded `values.size() < 2`, so a restored Meter snapshot could write out of bounds in the ImGui path). Extracted to `bridge/tzpl_ui_taps.{hpp,cpp}`: `pollWidgetTaps`, `removePanelWidgets`, `untapWidget`/`untapWidgets` — the latter replacing four copies.
- **Fixed a pre-existing teardown use-after-free**: `~Engine` destroys members in reverse declaration order, so `taps_` (declared after `silos_`) was freed *before* `~Silo` → `removeAllNodes` → `removeNode` → `clearTapsForNode`, which then wrote published levels through dangling `TapSlot` pointers. Latent UB whenever an engine was freed with a live tap; the changed `TapSlot` size made it reproducible. `~Engine` now clears the RT tables and the registry up front. Found with AddressSanitizer.

### 14.1 Level meters — DONE

1. **Master output metering**: `MasterMeter` in the new `engine/src/tzpl_engine_stats.hpp`, accumulated in `processAudioBlock` immediately after `safetyLimiter_->process()` — so it measures exactly what the device plays (post-limiter, post-gain). Per-channel `peak`/`rms`/`peakHold` plus across-channel summaries and a clip counter; RMS uses the same mean-square-across-channels convention as `processTaps`, so master and node meters are comparable. `peakHold` falls over ~1.5 s, which makes the reading independent of the GUI's poll rate and safe for multiple readers (no read-and-reset). Public API: `masterChans` / `masterPeak` / `masterRms` / `masterPeakHold` / `masterClipCount`, all lock-free.
   - `SafetyLimiter::prevMaxPeak` turned out to be useless for this: the limiter's enabled path ends in `std::swap_ranges`, i.e. it carries a block of latency, and `maxAbsPeak` describes the *incoming* pre-gain block. Documented consequence: with the limiter on, the master meter trails node taps by one block (~5.8 ms).
2. Per-node meters: already shipped (`tapOutlet` + `ui.meter`); this phase added the graph-view surface (14.4).
3. Widgets: already shipped in both renderers.

### 14.2 Oscilloscope / spectrum — DONE

1-2. Ring capture and time-domain display: already shipped (`tapScope` + the Scope widget).
3. **Spectrum display**: new `UIWidgetKind::Spectrum` (**appended** — ordinal 15; the enum is persisted as a raw ordinal in `.tzd`, so it is append-only forever, and `document.cpp`'s clamp degrades an unknown kind to Slider so an older build opens a newer document without crashing). Reuses `tapScope` — no new `TapMode`.
   - Analysis lives in `bridge/tzpl_spectrum.{hpp,cpp}` (`SpectrumEngine`), run from the control dispatcher's fixed 30 Hz tick rather than from `paint()`: the decay needs a known tick rate, and repaints happen at whatever rate the toolkit decides. FFT setups and Hann windows are cached **by size**, so N widgets at 2048 share one `vDSP_create_fftsetup`.
   - Normalization is `2/(N·cg)` with Hann coherent gain 0.5 (halved for DC and Nyquist, which have no mirror bin), so a full-scale sine reads 0 dBFS. Peak-with-fall at 0.7 dB/tick (~21 dB/s), floor −96 dB.
   - The **log-frequency axis is the renderer's job**: each pixel column takes the max over the bins it covers, so the top end (dozens of bins per column) doesn't alias to an arbitrary bin. Implemented in both `widget_draw.cpp` and `widget_component.cpp`.
   - **`shared/tzpl_fft.hpp` gained a portable fallback**: the non-Apple branch was a `// TODO: PFFFT` stub, so the widget would have silently drawn a flat line off macOS. Now a radix-2 real FFT (half-length complex transform + untangle) producing the identical packed layout, forward and inverse, no VLAs. Benefits synthdefs too, since they share the header.
4. **Master-bus taps**: `tapMaster(tapID, mode)` routes master capture through the *existing* tap registry rather than a bespoke path — installed into `Engine::rt_masterTaps_` by `TapMasterCmd`, accumulated at **block** rate by `Engine::processMasterTaps`. This is why `tapExists`/`tapPeak`/`tapRms`/`tapChans`/`tapDrain`, the widget poll loop, panel-close untapping and document restore all needed **zero** changes: a master tap is an ordinary registry entry. Silo-0-only by construction (`applyTapMaster` and `applyUntap` reject other silos) — silo 0's thread is the one that runs the post-limiter section.
   - Lang surface: `ui.spectrum(name, node, outlet, silo)`, plus `ui.masterMeter` / `ui.masterScope` / `ui.masterSpectrum`. Dedicated names rather than a `node = -1` overload of `ui.meter` (whose `outlet`/`silo` arguments are meaningless for the master bus), but internally one `makeTapWidget` with a `nodeID < 0` sentinel.

### 14.3 Performance monitoring — DONE

All of this was new: the engine had **zero** instrumentation before.

- `engine/src/tzpl_engine_stats.hpp`: `MasterMeter`, `SiloStats`, `EngineStatsRT`. Every field is single-writer (the thread that owns the block) with any number of lock-free readers, so all accesses are `memory_order_relaxed` — nothing depends on two fields agreeing.
- **Max-since-read without a CAS**: `resetEngineStats` bumps `statsEpoch`; each writer keeps its running maximum in a plain non-atomic member and restarts it when it notices the epoch changed. One relaxed load per block.
- **Timing**: `steady_clock::now()` three times in `Silo::processFrames` and twice around `processAudioBlock`. The split at `mixDown` is deliberate — silo 0's `mixDown` blocks on the worker silos' semaphores, so folding that wait into the DSP figure would make silo 0 look pathologically slow (`mixWaitNanos` is reported separately). Cost is ~5 commpage/vDSO clock reads per block against a multi-millisecond block; gated on `Engine::statsEnabled_` regardless.
- **Queue depths** (`from_nrt_`/`to_nrt_`/`dead_nodes_`) and live tap count sampled once per block.
- **GC statistics**: `HeartbeatFn` became `void(*)(void* vm, Silo* s)` so `rtVMHeartbeat` can republish the collector's counters (`stepCount`, `cyclesCompleted`, and the `GCStepSource::RtTick` bucket — the number that determines RT safety) into `SiloStats`. The counters already existed and the RT thread already owns them, so this is six plain loads and five relaxed stores per block. They stay monotone; the host takes deltas rather than resetting cross-thread. The NRT VM's collector is read directly under `nrtvm.mtx`.
- **Dropout detection**, all previously invisible:
  - RtAudio's `RtAudioStreamStatus` was being discarded in the callback signature; underflow/overflow now increments `dropoutCount`.
  - The JUCE backend's wrong-block-size bail (which silences a whole block) and its `catch (...)` now increment `badBlockSizeCount` / `rtExceptionCount` plus a dropout.
  - Over-budget blocks count too.
  - New `AudioBackend` virtuals `deviceXruns` / `deviceCpu` / `hasTelemetry`, defaulted so both backends compile unchanged; JUCE overrides them from `AudioDeviceManager` (clamping its −1 "unsupported"). Device- and engine-side counters are reported **separately** — they measure different things, and `deviceCpu` includes the backend's own de/interleaving so it reads higher than `loadPercent`.
- **Snapshot API**: `EngineStats` / `SiloStatsSnap` + `getEngineStats` / `resetEngineStats`, copied under `nrt_lock_` following the `getGraphDesc`/`DefDesc` convention. Safe and non-inverting because the audio thread never takes `nrt_lock_`, so a GUI poll cannot block audio.
- **UI**: `app/juce/status_bar.{hpp,cpp}` — a bottom strip in `MainComponent` showing device format, DSP load (colour-coded bar), a master peak-hold meter with a latching clip square, and a **latching** XRUN readout. Expands into a detail grid: per-silo DSP/max/load/mix-wait/taps/queue-depths/GC, an NRT-VM GC row (read with `try_lock` so a long compile never stalls the message thread), and a device row. Clicking XRUN calls `resetEngineStats` — the only way to clear the latch, so a dropout can never quietly disappear. New dropouts also flash the strip and append **one** rate-limited console line. The bar takes no keyboard focus (it sits under a text editor) and is hidden in perform mode; it is not a center-mode component, so mode switching was untouched.

### 14.4 Graph-view metering — DONE

Per-node **opt-in** via the node context menu ("Meter Node", or a "Meter" submenu with per-outlet items when a node has several outlets; non-f32 outlets are greyed because the engine only taps f32).

- Toolkit-free core `app/src/graph_meters.{hpp,cpp}`: `MeterStore` owns the tap lifetime (`enable`/`disable`/`enableNode`/`disableNode`/`prune`/`clearSilo`/`clear`/`poll`). No GUI includes, so a future ImGui graph view can reuse it.
- **Session-only, never persisted**: taps are a scarce RT resource tied to live audio, and restoring them from a document would mean tapping nodes that may not exist yet. Cleared on silo switch and on `GraphView` destruction; **pruned on every topology change**, which is required — the engine clears a dead node's RT tap entry but leaves the registry slot alive reading silence, so nothing else would free it.
- `GraphView` became a `juce::MultiTimer`: topology stays at 8 Hz (a deep snapshot under `shadowMtx_`), meters run at 25 Hz and **only while meters exist**, so an unmetered graph costs exactly what it did before. Repaints are per metered node's screen rect — a full 25 Hz repaint would re-stroke every bezier wire.
- Bars are drawn in a gutter **outside** the node box, at `pinCentre(n, false, port)`, using the same −60..0 dB curve as the widget meters. Inside-the-box bars would require widening metered nodes in `rebuildLayout()`, making the graph jump on every toggle. A dot in the title bar keeps the state legible when zoomed out.

### Tests

- `integration-tests/src/test_metering.cpp` (new target `test_metering`, 85 checks), driven by `renderNRTBlock` so it is fully deterministic with no audio device: tap install/read/remove, scope capture, **tap-table compaction with a middle removal** (the regression guard for 14.0), taps on freed nodes, the per-silo and per-master budgets, master meter post-gain and post-limiter, peak hold, master taps agreeing with the always-on meter, the silo-0 rule, stats plumbing, and the reset epoch handshake. Stats assertions check counts, positivity and finiteness only — never absolute wall times.
- `app/tests/spectrum_test.cpp` (new target `tzpl_spectrum_tests`, 17 checks): bin placement, 0 dBFS / −6 dB levels, silence, de-interleaving and channel-mean, exact per-tick decay and floor clamping, DC/Nyquist unpacking. (A pure Nyquist tone spreads evenly over the top two bins under a Hann window — the test asserts placement *and level* rather than pinning one bin.)
- `tzpl_graph_tests` grew `test_graph_meters` (93 checks total): tap ownership, idempotent enable, per-outlet expansion, prune-after-free, and `clear()`.

### Docs — DONE

`lang/docs/Live_Controls_and_Notebooks.html`: `spectrum` / `masterMeter` / `masterScope` / `masterSpectrum` added to the §3.1 widget catalogue and the §3.7 function reference, with a callout explaining *why* the master constructors exist (node 0 has no outlets, and gain/limiting happen after the graph runs) and the one-block limiter lag. §3.5 gained a master read example and a sharper one-consumer-per-stream warning. New **§6 Monitoring the Engine** documents the status bar (including that the XRUN latch clears only by clicking it) and graph-view node meters; §6 was inserted before Current Limitations, which became §7. Limitations, presets and save/load notes updated for the new kind. Every code example was executed against the built app.

### 14.5 Scriptable taps (`audio_engine`) — DONE

Taps were previously reachable only by creating a `ui` widget. Headless mode does have a `UIState`, so that worked — but it meant allocating a GUI object to read a level, routing through `UIState::mtx` and the `(panel, name)` keyspace, and, more seriously, the `ui` tap functions resolve the engine via `AppContext::engine` rather than `getEngine(vm)`, so during an offline `renderNRT` they target the **live** engine instead of the render's.

- **FFI** (`bridge/src/tzpl_audio_engine_ffi.cpp`): `allocTapID`, `tapOutlet`, `tapMaster`, `untap`, `tapExists`, `tapPeak`, `tapRms`, `tapChans`, `tapSamples`. Create/remove are ordinary bundled commands; everything routes through `getEngine(vm)`, so it follows the render context. Module surface + `TapMode` enum in `bridge/modules/audio_engine.x` (whose `Err` enum also gained the `errResourceLimit` case added to the ABI in 14.0 — it had been missed).
- **Ownership is explicit `untap`.** A script tap has no other owner; the finite per-silo budget is surfaced through `errResourceLimit` at submit and `EngineStats::silos[].numTaps`.
- **RT reads, silo-local.** `Silo::rt_findTap` resolves a tapID through that silo's own RT table: no lock, no map, and no concurrency at all, because the thread reading is the thread that publishes in `processTaps`. That removes the tearing and reclamation problems by construction rather than by machinery — no seqlock, no handle map, no deferred reclamation. `rtTapExists/Peak/Rms/Chans/Drain` in `tzpl_client_interface.hpp` wrap it; the FFI picks the RT path when `gCurrentSilo` is set and the locking path otherwise. All nine functions are registered `rtSafe=true`.
  - **Cross-silo reads are deliberately out of scope**: a tap on another silo reads exactly as an unknown id does. Silo 0 additionally sees master taps, since silo 0's thread runs the post-limiter section. Reading another silo's taps means using the locking accessors from a non-RT thread.
  - `tapSamples` drains into a fixed 4096-sample stack buffer (no heap staging) and allocates its result through the VM's TLSF allocator, so it is RT-safe like the other array-returning builtins.
- **Fixed while testing**: `renderNRTAsync` dereferenced a null `AppContext::nrtvm` and segfaulted in `NRTTempoScheduler`'s constructor. Any host without an NRT VM (a bare test harness) crashed rather than declining; it now reports and returns 0.
- **Tests**: `test_metering` 85 -> 103 (RT reads agree with the locking forms; RT reads are silo-local, including the master-tap-from-silo-0 case). `test_audio_engine_ffi` 60 -> 65: a new `taps_test.x` script covering the whole lang surface and its error codes, plus a compile test asserting the tap API passes the RT-restricted type checker *and* that a non-`rtSafe` engine function is still rejected (so the gate itself is guarded).
- **Docs**: new **FFI Guide §18.9 Signal Taps** — the function table, the one-consumer-per-tap rule, the finite-budget rule, and the silo-local read model.

### 14.6 Tap ownership and bulk cleanup — DONE

Explicit `untap` leaks a finite slot if a script forgets, so taps now record who made them.

- `TapOwnerKind` (`tzpl_tap.hpp`): `tapOwnerHost` / `tapOwnerNRTVM` / `tapOwnerSiloVM` (+ `tapOwnerAny` as a filter), plus `ownerSilo`. Threaded through `tapOutlet`/`tapMaster` as **defaulted** parameters, so the ui widgets and `graph::MeterStore` are unchanged and land on `tapOwnerHost` — being sweepable is opt-in, and the safe answer is what you get by doing nothing.
- `freeTapsByOwner(e, kind, silo)` walks the registry under `nrt_lock_` and submits **one untap bundle per owning silo**. Exposed as `audio_engine.freeVmTaps()` (the caller's own taps: silo VM by silo index, main VM otherwise) and `audio_engine.freeAllTaps()` (everything, app taps included). Both decline and return 0 if the caller has a bundle open, rather than clobbering it — `begin()` already refuses to nest, so the guard is free.
- A silo VM's taps are keyed by **silo index, not VM identity**: `siloLoad` replaces a silo's VM, and keying on the VM would strand its predecessor's taps with nothing able to reclaim them. The trade — a reloaded VM inherits and can free the previous one's taps — is the desirable direction.

**Fixed a second latent use-after-free.** `applyUntap` validated the submitting silo only for master taps, so `untap` of a silo-1 tap submitted to silo 0 succeeded at submit, no-op'd on RT (silo 0's table doesn't hold it), then freed the slot in `doNRT` — leaving silo 1's `rt_taps_` pointing at freed memory. Reachable from a script via the API added in 14.5. `TapSlot::silo` already recorded the owner, so the fix is one line; it also had to be right before the per-silo fan-out above could be. **Reproduced under ASan** with the guard removed (heap-use-after-free in `Silo::processTaps` on the worker thread), then confirmed fixed.

Tests: `test_metering` 103 -> 127 — the wrong-silo rejection, owner-scoped sweeps across five taps and three owners on two silos, the app's tap outliving every `freeVmTaps`, master taps included in a sweep, and the mid-bundle decline. `taps_test.x` covers the lang surface.

---

## Phase 15: Session Management — NOT STARTED

**Goal**: Save and restore session state.

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
        │           └─> Phase 4 (Lang Features)  ✅ DONE
        │                 └─> Phase 5 (OSC)       ✅ DONE
        │                       └─> Phase 6 (NATS) ✅ DONE
        ├─> Phase 7 (Engine Features)              ✅ DONE ────────────────────┐
        ├─> Phase 8 (Compiler Features)            ✅ DONE ────────────────┤
        └─> Phase 9 (Language Features cont.)      🟢 MOSTLY DONE ───────────┤
                                                                              v
                                                                  Phase 10 (UI Framework)  ✅ DONE
                                                                    └─> Phase 11 (Editor)  ✅ DONE
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
| 1 | Library-ify projects | ✅ Done | -- |
| 2 | FFI: engine | ✅ Done | -- |
| 3 | FFI: synthdef-compiler | ✅ Done | -- |
| 4 | Critical language features | ✅ Done | -- |
| 5 | OSC support | ✅ Done | -- |
| 6 | NATS support | ✅ Done | -- |
| 7 | Engine feature completion | ✅ Done | 7.1-7.4 all done. Follow-ups: buffer readback / buffer-to-file, engine commands over TZB instead of text |
| 8 | Compiler feature completion | ✅ Done | -- |
| 9 | Language feature completion | 🟢 Mostly done | I/O functions, general function inlining |
| 10 | UI framework setup | ✅ Done | -- |
| 11 | Code editor & REPL | ✅ Done | Type info on hover |
| 12 | Plugin/module management | 🟡 Partial | 12.1 plugin browser done (both apps) except one-click instantiation; 12.2 module browser, 12.3 compile UI |
| 13 | Audio graph visualization | 🟢 Mostly done | 13.1-13.3 done (JUCE): topology shadow, GraphView mode, interactive editing, node control panels. Future: ImGui view, MIDI mapping, control readback |
| 14 | Metering & monitoring | ✅ Done | 14.1-14.4 done: master meter + master taps, Spectrum widget + portable FFT, graph-view node meters, engine perf counters + JUCE status bar; scriptable taps on `audio_engine` with silo-local RT reads + owner-scoped `freeVmTaps`/`freeAllTaps`; user docs in the Live Controls guide (§6) and FFI Guide (§18.9) |
| 15 | Session management | ⬜ Not started | All tasks |
| 16 | Future extensions | ⬜ Not started | All tasks |

---

## Key Risks & Decisions

1. **Event-driven VM design** (Phase 4.2): Done. Cross-thread ARC deletion, NRT VM with mutex serialization, RT VM on Silo, NRT and RT tempo schedulers with TempoRamp, clock FFI (12 functions), app migrated to NRTVM. OSC and NATS handler dispatch to NRT VM done. See `EVENT_DRIVEN_VM_PLAN.md` for the full design.

2. **UI framework choice** (Phase 10): Dear ImGui selected. GLFW + Metal on macOS, OpenGL3 prepared for Linux. Fetched via CMake FetchContent.

3. **Real-time safety across boundaries**: When Tzopilotl calls engine functions via FFI, the call chain must remain real-time safe. The bridge functions must not allocate memory or block. The existing TLSF allocator and lock-free FIFOs make this feasible, but it needs careful validation.

4. **Compilation latency for synthdef**: Calling clang at runtime to compile synth definitions takes time (100ms-1s+). This must happen on a background thread with the compiled plugin loaded asynchronously. The UI should show compilation status.

5. **Cross-platform audio**: RtAudio handles CoreAudio (macOS) and ALSA (Linux). Windows support via WASAPI/ASIO would be needed for full cross-platform coverage.

6. **Plugin ABI stability**: The `tzpl_plugin_abi.h` interface is the contract between all three projects. Changes to it require coordinated updates. Consider versioning the ABI.
