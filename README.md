# TZPL — Audio Coding Platform

An audio coding platform for composing, experimenting with, and performing music, combining a statically typed programming language, an audio signal graph compiler, and a real-time audio engine. Runs on macOS (CoreAudio) and Linux (ALSA/JACK/PulseAudio).

## Sub-Projects

### Tzopilotl (`lang/`)

A statically typed, real-time safe interpreted language designed for audio and signal processing. Features include:

- Static type inference (source-to-sink)
- Auto-mapping: pass an array where a scalar is expected and the function maps automatically
- `@` operator for explicit depth control, Cartesian products, and data construction
- Immutable by default (`let`), mutable locals with `var`, mutable slots with `Ref`
- Direct-threaded VM using `[[clang::musttail]]` tail-call dispatch
- TLSF O(1) real-time allocator
- Incremental tri-color snapshot-at-the-beginning (SATB) tracing garbage collector with stack-map-based precise roots and a bounded per-step budget (sub-millisecond pauses suitable for audio-rate workloads)
- Rich type system: Bool, Int, Float, Symbol, String, Fraction, Complex, Bytes, Array, List, Range, Set, Map, Tuple, Struct, Enum, Ref, Function, Lambda, Coroutine
- Template monomorphization, function overloading, pattern matching
- File-based module system with selective imports
- C/C++ embedding APIs and foreign function interface

### Synthdef Compiler (`synthc`, in `lang/modules/synthc/`)

A signal-graph compiler written in Tzopilotl: synth graphs built with the `synthdef` module compile to native plugins (`.dylib`). Features include:

- Tensor-shaped signals (scalars to rank-4), broadcasting, and shape operations along any axis
- Graph analysis: shape and type inference, constant folding, rewrites, rate placement (constant, init, reset, block, audio), parameter groups that recompute only when their inputs change
- Recurrences (`delayVar` feedback) recognised and run as block scans, block state space, or per-sample loops, with sample- and channel-vector SIMD forms
- Voicers with idle-voice skipping and voice groups
- C++ code generation targeting the `tzpl_plugin_abi` interface, compiled and linked by the system clang (`bridge/src/tzpl_synth_build.cpp`)

### Audio Engine (`engine/`)

A real-time audio engine that loads native synth plugins and supports dynamic patching. Features include:

- Plugin loading via `dlopen` of `.dylib` files conforming to `tzpl_plugin_abi.h`
- Block processing (`process(offset, n)`, 64 samples by default) with sample-accurate events: value changes render into input buffers at their sample, and node start, free and voice resets split a node's block
- Dynamic graph editing with on-demand topological sort (re-sorted only when connections change); connections may form cycles, run a sample at a time (or a block at a time) like the old per-sample engine
- Crossfading system (7 curves) for glitch-free connection changes
- Lock-free SPSC FIFOs for RT-safe inter-thread communication
- Multi-silo parallel worker threads with binary-tree mixdown
- Sample-accurate scheduling queue
- Command bundling API and S-expression command parser
- Polyphonic voice allocation in the engine, writing each voiced synth's `notes` input
- Safety limiter on master output (lookahead, NaN zapping)

### Shared Headers (`shared/`)

Common headers used by multiple sub-projects:

- `tzpl_plugin_abi.h` — Pure C plugin ABI (the interface between the engine and compiled synths)
- `tzpl_simd.hpp` — SIMD abstraction
- `tzpl_random.hpp` — xoroshiro128++ PRNG (scalar and SIMD)
- `tzpl_matrix_transform.hpp` — Compile-time matrix operations

### FFI Bridge (`bridge/`)

Connects Tzopilotl to the engine and to plugin building (compile and link of synthc's output) via the language's foreign function interface, with optional OSC and NATS messaging bridges. Includes Tzopilotl module files (e.g., `audio_engine.x`) that expose native functions to scripts.

### Application (`app/`)

A desktop application integrating all the sub-projects: multi-tab code editor with syntax highlighting, output panel, REPL, and live documents (notebooks) with interactive UI widgets. The preferred version is the JUCE-based app (`tzpl_app_juce`, in `app/juce/`); the original Dear ImGui app (`tzpl_app`) is still available.

## Directory Structure

```
tzpl/
├── CMakeLists.txt              Top-level build configuration
├── build.sh                    Quick build script
├── shared/                     Shared headers (plugin ABI, SIMD, RNG)
├── lang/                       Tzopilotl interpreter
│   ├── src/                    Compiler and VM source
│   ├── tests/                  Test suite
│   ├── modules/                Standard library modules
│   ├── docs/                   Language documentation (HTML)
│   └── editors/                Editor support packages
├── engine/                     Real-time audio engine
│   └── src/                    Engine source
├── bridge/                     FFI bridges between sub-projects
│   ├── src/                    Bridge implementations
│   ├── include/                Bridge headers
│   └── modules/                Tzopilotl bridge modules
├── osc/                        OSC (Open Sound Control) support library
├── nats/                       NATS messaging support library
├── app/                        Desktop application
│   ├── src/                    Dear ImGui application source
│   └── juce/                   JUCE application source (preferred version)
├── examples/                   Example scripts and live documents
├── integration-tests/          Cross-project integration tests
├── benchmarks/                 Performance benchmarks
├── packaging/                  Distribution: DMG build, signing, notarization
├── tools/                      Standalone developer tools
├── docs/                       Project-level design documents
├── devplans/                   Development roadmaps and plans
└── third_party/
    ├── oscpack/                OSC message encoding/decoding
    └── rtaudio/                Audio I/O library
```

## Prerequisites

- **Compiler**: Clang (AppleClang on macOS; Clang 19+ on Linux). GCC is not
  supported: the engine uses Clang-only `ext_vector_type` swizzles, and the
  VM uses `[[clang::musttail]]`
- **CMake**: 3.21+
- **C++ Standard**: C++23
- **Platform**: macOS (CoreAudio) and Linux (ALSA/JACK/PulseAudio; see `docs/LINUX.md`).
- **Frameworks** (macOS): CoreAudio, CoreFoundation, AudioToolbox (found automatically)
- **Packages** (Linux): `libasound2-dev` and `libsndfile1-dev` required; JACK,
  PulseAudio, and the JUCE GUI need more — see `docs/LINUX.md` for the full
  list, a ready-made Docker dev environment, and real-time setup

## Building

### Quick Build

```sh
./build.sh
```

This configures a Release build and compiles with all available cores.

### Manual Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `TZPL_BUILD_LANG` | `ON` | Build the Tzopilotl interpreter |
| `TZPL_BUILD_AUDIO_ENGINE` | `ON` | Build the audio engine |
| `TZPL_BUILD_SYNTHDEF_COMPILER` | `OFF` | Build the frozen C++ synthdef compiler (superseded by synthc) |
| `TZPL_BUILD_BRIDGE` | `ON` | Build the FFI bridge libraries |
| `TZPL_BUILD_APP` | `OFF` | Build the Dear ImGui application (`tzpl_app`) |
| `TZPL_BUILD_APP_JUCE` | `OFF` | Build the JUCE GUI application (`tzpl_app_juce`, preferred) |
| `TZPL_BUILD_GUI` | `ON` | Build with GUI support (Dear ImGui + GLFW) |
| `TZPL_BUILD_OSC` | `OFF` | Build OSC (Open Sound Control) support |
| `TZPL_BUILD_NATS` | `OFF` | Build NATS messaging support |
| `TZPL_BUILD_TESTS` | `OFF` | Build integration tests |
| `TZPL_BUILD_BENCHMARKS` | `OFF` | Build benchmarks |

Example — build only the language:

```sh
cmake -B build -DTZPL_BUILD_LANG=ON -DTZPL_BUILD_AUDIO_ENGINE=OFF -DTZPL_BUILD_SYNTHDEF_COMPILER=OFF
cmake --build build
```

### Build Targets

| Target | Description |
|--------|-------------|
| `tzpl` | Tzopilotl interpreter executable |
| `tzpl_lib` | Tzopilotl as a static library |
| `engine` | Audio engine executable |
| `audio_engine_lib` | Audio engine as a static library |
| `tzpl_audio_engine_bridge` | FFI bridge: Tzopilotl ↔ audio engine |
| `tzpl_synthdef_compiler_bridge` | FFI bridge: compile and link synthc's generated plugins |
| `tzpl_app` | Dear ImGui desktop application (requires `-DTZPL_BUILD_APP=ON`) |
| `tzpl_app_juce` | JUCE desktop application, the preferred version (requires `-DTZPL_BUILD_APP_JUCE=ON`) |
| `test_audio_engine_ffi` | Audio engine FFI test executable |
| `test_synthdef_compiler_ffi` | Plugin-building FFI test executable |
| `test_block_engine` | Block engine tests (hand-written plugins) |

Build a specific target:

```sh
cmake --build build --target tzpl
```

## Running Tzopilotl

```sh
# Run a script
./build/lang/tzpl program.x

# Start the REPL
./build/lang/tzpl

# Add module search paths
./build/lang/tzpl -I lib:vendor program.x
```

## Running Tests

### Tzopilotl Tests

The language has a comprehensive test suite covering arithmetic, auto-mapping, builtins, control flow, coroutines, data structures, destructuring, errors, expressions, FFI, functions, modules, operators, type system, and more.

```sh
cd lang/tests && bash run_tests.sh
```

The test runner supports several flags:

```sh
bash run_tests.sh -v              # Verbose output
bash run_tests.sh -f "pattern"    # Filter tests by name
bash run_tests.sh -u              # Update golden files
bash run_tests.sh -x              # Stop on first failure
```

### Synthdef Compiler and Engine Tests

```sh
bash integration-tests/run_synthc_dump_test.sh        # analysis goldens (--update to regenerate)
bash integration-tests/run_forms_equivalence_test.sh  # every code form agrees with the plain loops
./build/integration-tests/test_block_engine           # the block engine
```

## Documentation

### Tzopilotl

The language and library guides are rendered at **<https://lfnoise.github.io/tzpl/>** (sources in [`lang/docs/`](lang/docs/)):

- [Getting Started](https://lfnoise.github.io/tzpl/Getting_Started.html) — Build the platform, run the app, and make your first sounds
- [Tzopilotl by Example](https://lfnoise.github.io/tzpl/Tzopilotl_by_Example.html) — Comprehensive syntax and feature guide
- [Built-in Functions](https://lfnoise.github.io/tzpl/Builtin_Functions.html) — Reference for all built-in functions
- [Standard Library](https://lfnoise.github.io/tzpl/Standard_Library.html) — The `std.*` module reference
- [Music Libraries](https://lfnoise.github.io/tzpl/Music_Libraries.html) — The `music.*` namespace: events, tunings, and four composition dialects
- [Tzopilotl Music Cookbook](https://lfnoise.github.io/tzpl/Tzopilotl_Music_Cookbook.html) — Recipes for making music with the platform
- [Live Controls and Notebooks](https://lfnoise.github.io/tzpl/Live_Controls_and_Notebooks.html) — The `ui` widget module and notebook documents in the app
- [Coroutines](https://lfnoise.github.io/tzpl/Coroutines.html) — Coroutine system design and usage
- [FFI Guide](https://lfnoise.github.io/tzpl/FFI_Guide.html) — Calling C functions from Tzopilotl

### Architecture

- [Audio Engine Architecture](engine/Architecture.md) — Engine design, silos, commands, plugin ABI, and thread safety
- [Tzopilotl Theory of Operation](lang/Theory_of_Operation.md) — Detailed design document for the language implementation
- [Integration Plan](devplans/IMPLEMENTATION_PLAN.md) — Step-by-step roadmap for integrating the sub-projects

### Editor Support

Syntax highlighting for Tzopilotl is available for:

- **VS Code** — `lang/editors/vscode`
- **TextMate / Sublime Text** — `lang/editors/Tzopilotl.tmbundle`
- **Tree-sitter grammar** — `lang/editors/tree-sitter-tzpl`

## License

Copyright (C) 2026 James McCartney.

This project is licensed under the [GNU General Public License v3.0](LICENSE). Files that do not carry an explicit license header (including the Tzopilotl `.x` modules, examples, and build/test scripts) are covered by that license.

Vendored third-party code (`third_party/`, `lang/third_party/`, `app/vendor/`, and a few other locations) retains its original permissive licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the LICENSE files in those directories.

**Note on the optional JUCE application:** the `tzpl_app_juce` target (built only when `TZPL_BUILD_APP_JUCE` is enabled) downloads and links against the JUCE framework, which this project uses under the GNU Affero General Public License, version 3 (AGPLv3) -- not the commercial JUCE licence. JUCE is not included in this repository. Binaries built from that target combine GPLv3-covered code with AGPLv3-covered code, as permitted by section 13 of each licence; the AGPLv3's requirements concerning interaction through a network apply to such combined binaries.
