# Integration & Product Implementation Plan

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
- **Remaining**: buffer operations (declared but not implemented), binary s-expression serialization (commented-out skeleton), distributed engine communication via NATS (Phase 6.3)

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

## Phase 7: Finish Remaining Engine Features — PARTIAL

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
1. Enable input streams in RtAudio configuration. Done. Supports three modes: duplex (same device for I/O), separate input device (second RtAudio instance with staging buffer), and macOS aggregate devices. `AudioStreamParameters` has `inputDeviceName`, `inputChannels`, `firstInputChannel` fields.
2. Route hardware input to a special input node per silo. Done. "Audio In" node (nodeID=1) created per silo with an output port. `processFrames()` copies hardware input samples to the input node's outlet each sample, before running the node graph.
3. Plugins can receive live audio via inlet connections. Done. Connect any plugin's input to the input node's output (nodeID=1, port 0) to receive live audio. The input node participates in topological sort like any other node.
4. App CLI supports `--input-channels`, `--input-device`, `--first-input-channel` flags and `inputDevice` config key.
5. FFI bridge exposes `inputChannels()` for querying active input channel count.

### 7.3 MasterGainCmd and ChannelOffsetCmd -- DONE

**Completed tasks**:
1. Implement master gain control (applied after safety limiter or integrated into it). Done.
2. Implement channel offset for routing to specific hardware output channels. Done.

### 7.4 Binary s-expression serialization

File `tzpl_sexpr_binary_buffer.hpp` exists but contains only commented-out skeleton code. Text-based s-expression parsing is fully implemented.

**Tasks**:
1. Complete the binary parser/serializer for efficient network transport of commands (useful with NATS).

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
        ├─> Phase 7 (Engine Features)              🟡 PARTIAL ─────────────────┐
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
| 7 | Engine feature completion | 🟡 Partial | Buffers, binary sexpr. Audio input, master gain/channel offset done |
| 8 | Compiler feature completion | ✅ Done | -- |
| 9 | Language feature completion | 🟢 Mostly done | I/O functions, general function inlining |
| 10 | UI framework setup | ✅ Done | -- |
| 11 | Code editor & REPL | ✅ Done | Type info on hover |
| 12 | Plugin/module management | ⬜ Not started | All tasks |
| 13 | Audio graph visualization | ⬜ Not started | All tasks |
| 14 | Metering & monitoring | ⬜ Not started | All tasks |
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
