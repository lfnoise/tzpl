
# Audio Engine

A real-time audio engine that loads native synth plugins and supports dynamic patching
with multi-channel connections, cross-fading, polyphonic voice management, and
sample-accurate command scheduling.

## Project Layout

All source files live in `src/` with the `tzpl_` prefix.
The shared plugin ABI header lives in `../shared/tzpl_plugin_abi.h`.

| File(s) | Role |
|---------|------|
| `tzpl_client_interface.hpp/cpp` | Public API: engine lifecycle, command bundling, plugin loading |
| `tzpl_audio_backend.hpp` | AudioBackend interface (device open/start/stop/streamTime) + `processAudioBlock()` |
| `tzpl_audio_backend_rtaudio.hpp/cpp` | Default RtAudio backend (CoreAudio/ALSA), separate-input-device staging, macOS sample-rate listener |
| `tzpl_engine.hpp/cpp` | Engine struct, safety limiter, built-in node defs, NRT/dead-node threads |
| `tzpl_silo.hpp/cpp` | Parallel processing unit: node tables, topological sort, audio processing, command dispatch |
| `tzpl_node.hpp/cpp` | Node, InPort, OutPort, Control, NodeDef — the graph data model |
| `tzpl_command.hpp`, `tzpl_command_subclasses.hpp` | Command base class, scheduler queue, all concrete command types |
| `tzpl_xfader.hpp/cpp` | Crossfader node: 7 fade curves, automatic splice-in/splice-out |
| `tzpl_atomic_fifo.hpp` | Lock-free SPSC FIFO for RT-safe inter-thread communication |
| `tzpl_sexpr.hpp/cpp` | S-expression parser for text-based commands |
| `tzpl_common.hpp` | Type aliases (`f32`, `i64`, SIMD types) |
| `tzpl_hash.hpp` | 64-bit hash function for node/def lookup |
| `tzpl_random.hpp` | xoroshiro128++ PRNG, SIMD-templated |
| `tzpl_complex.hpp` | Complex number utilities |
| `tzpl_plugin_interface.hpp` | Legacy/reference plugin helpers (mostly commented out) |
| `main.cpp` | Test code, built-in plugins (VoicerTest, SinOsc, AddOp, MulOp), Voicer template |
| `RtAudio.h/cpp` | Third-party cross-platform audio I/O (CoreAudio on macOS) |

## Building

- Currently uses Xcode. A CMakeLists.txt also exists.
- Requires C++23 or later.
- macOS only (CoreAudio). Uses `<simd/simd.h>` for SIMD types.
- Links CoreAudio and CoreFoundation frameworks.

## C++ Coding Style

- East const: `Record const&` not `const Record&`.
- Prefer `std::print` over iostream.
- Prefer `std::format` over string concatenation with `+`.
- Everything is in the `engine` namespace (except the C ABI header).

## Key Architectural Concepts

- **Silos**: Independent parallel processing units, each with its own node graph, worker thread, and command queue. Silo 0 runs on the audio callback thread; silos 1..N-1 run on dedicated worker threads. Outputs are summed via binary-tree reduction.
- **Two-stage commands**: Stage 1 (`doRT`) runs on the RT thread; stage 2 (`doNRT`) runs on a background thread for cleanup/deallocation. Commands are bundled atomically via `begin()`/`go(silo)`/`sched(silo, ...)` -- the target silo is chosen at submit, where the whole bundle is validated and materialized; an invalid bundle is discarded in its entirety (atomic abort).
- **Lock-free FIFOs**: All RT <-> NRT communication uses `AtomicFifo` (SPSC). No locks or allocations on the audio thread.
- **Zero-copy connections**: Connecting an InPort to an OutPort redirects the inlet pointer to the outlet's buffer. No data copying during audio processing.
- **Sample-by-sample processing**: Nodes process one sample at a time, enabling sample-accurate scheduling.
- **Crossfaders**: Temporary nodes spliced into the signal graph for smooth transitions. They self-remove when the fade completes.
- **Topological sort**: Depth-first from the output node. Re-sorted only when connections change. Cycles cause one-sample delay rather than failure.

## Plugin ABI

Plugins are `.dylib` files exporting a `load` function. The stable C ABI is defined in
`../shared/tzpl_plugin_abi.h`. Plugins extend `tzpl_SynthData` with custom fields and
provide a `tzpl_SynthFuns` function table (alloc, free, init, processAudio, noteOn, etc.).

## Thread Safety Rules

- Never allocate or free memory on the RT thread.
- Use `AtomicFifo` for all cross-thread data passing.
- NRT operations are protected by `nrt_lock_` mutex.
- Dead nodes are pushed to a FIFO and deleted by a background thread.
- Worker threads run at SCHED_RR priority 63.
