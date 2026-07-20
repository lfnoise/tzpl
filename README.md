# TZPL — Audio Coding Platform

An audio coding platform for composing, experimenting with, and performing music, combining a statically typed programming language, an audio signal graph compiler, and a real-time audio engine. Currently macOS-only; cross-platform support is planned.

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

### Synthdef Compiler (`synthdef-compiler/`)

An audio signal flow graph compiler that takes graph descriptions and compiles them into optimized native plugins (`.dylib`). Features include:

- Two front-ends: S-expression parser and C++ DSL
- ~200 audio operators (oscillators, filters, noise, envelopes, math, delays)
- 14-pass graph analysis pipeline (topology sort, shape/type inference, constant folding, dead code removal, rate scheduling)
- Algebraic rewrite engine (~100 optimization rules)
- C++ code generation targeting the `tzpl_plugin_abi` interface
- Full pipeline: parse → analyze → codegen → clang compile → link → dlopen

### Audio Engine (`engine/`)

A real-time audio engine that loads native synth plugins and supports dynamic patching. Features include:

- Plugin loading via `dlopen` of `.dylib` files conforming to `tzpl_plugin_abi.h`
- Dynamic graph editing with on-demand topological sort (re-sorted only when connections change)
- Crossfading system (7 curves) for glitch-free connection changes
- Lock-free SPSC FIFOs for RT-safe inter-thread communication
- Multi-silo parallel worker threads with binary-tree mixdown
- Sample-accurate scheduling queue
- Command bundling API and S-expression command parser
- Polyphonic voice management (`Voicer` template)
- Safety limiter on master output (lookahead, NaN zapping)

### Shared Headers (`shared/`)

Common headers used by multiple sub-projects:

- `tzpl_plugin_abi.h` — Pure C plugin ABI (the interface between engine and synthdef-compiler)
- `tzpl_simd.hpp` — SIMD abstraction
- `tzpl_random.hpp` — xoroshiro128++ PRNG (scalar and SIMD)
- `tzpl_matrix_transform.hpp` — Compile-time matrix operations

### FFI Bridge (`bridge/`)

Connects Tzopilotl to the engine and synthdef-compiler via the language's foreign function interface, with optional OSC and NATS messaging bridges. Includes Tzopilotl module files (e.g., `audio_engine.x`) that expose native functions to scripts.

### Application (`app/`)

A Dear ImGui desktop application integrating all the sub-projects: multi-tab code editor with syntax highlighting, output panel, REPL, and live documents (notebooks) with interactive UI widgets. A JUCE-based port (`tzpl_app_juce`, in `app/juce/`) is in progress.

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
├── synthdef-compiler/          Signal graph → plugin compiler
│   └── src/                    Compiler source
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
│   └── juce/                   JUCE application port (in progress)
├── examples/                   Example scripts and live documents
├── integration-tests/          Cross-project integration tests
├── benchmarks/                 Performance benchmarks
├── packaging/                  Distribution: DMG build, signing, notarization
├── tools/                      Standalone developer tools
├── docs/                       Project-level design documents
├── third_party/
│   ├── oscpack/                OSC message encoding/decoding
│   └── rtaudio/                Audio I/O library
└── IMPLEMENTATION_PLAN.md      Detailed integration roadmap
```

## Prerequisites

- **Compiler**: Clang or GCC 15+ (required for `[[clang::musttail]]`)
- **CMake**: 3.21+
- **C++ Standard**: C++23
- **Platform**: macOS (CoreAudio). Linux support planned.
- **Frameworks** (macOS): CoreAudio, CoreFoundation, AudioToolbox (found automatically)

## Building

### Quick Build

```sh
./build.sh
```

This configures a Release build and compiles with all available cores.

### Manual Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `TZPL_BUILD_LANG` | `ON` | Build the Tzopilotl interpreter |
| `TZPL_BUILD_AUDIO_ENGINE` | `ON` | Build the audio engine |
| `TZPL_BUILD_SYNTHDEF_COMPILER` | `ON` | Build the synthdef compiler |
| `TZPL_BUILD_BRIDGE` | `ON` | Build the FFI bridge libraries |
| `TZPL_BUILD_APP` | `OFF` | Build the audio coding application |
| `TZPL_BUILD_APP_JUCE` | `OFF` | Build the JUCE GUI application (`tzpl_app_juce`) |
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
| `synthdef-compiler` | Synthdef compiler executable |
| `synthdef_compiler_lib` | Synthdef compiler as a static library |
| `tzpl_audio_engine_bridge` | FFI bridge: Tzopilotl ↔ audio engine |
| `tzpl_synthdef_compiler_bridge` | FFI bridge: Tzopilotl ↔ synthdef compiler |
| `tzpl_app` | Dear ImGui desktop application (requires `-DTZPL_BUILD_APP=ON`) |
| `tzpl_app_juce` | JUCE desktop application (requires `-DTZPL_BUILD_APP_JUCE=ON`) |
| `test_audio_engine_ffi` | Audio engine FFI test executable |
| `test_synthdef_compiler_ffi` | Synthdef compiler FFI test executable |

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

### Synthdef Compiler Tests

```sh
./build/synthdef-compiler/synthdef-compiler --test
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

- [Synthdef Compiler Architecture](synthdef-compiler/ARCHITECTURE.md) — Compilation pipeline, expression graph, type system, and code generation
- [Audio Engine Architecture](engine/Architecture.md) — Engine design, silos, commands, plugin ABI, and thread safety
- [Tzopilotl Theory of Operation](lang/Theory_of_Operation.md) — Detailed design document for the language implementation
- [Integration Plan](IMPLEMENTATION_PLAN.md) — Step-by-step roadmap for integrating the sub-projects

### Editor Support

Syntax highlighting for Tzopilotl is available for:

- **VS Code** — `lang/editors/vscode`
- **TextMate / Sublime Text** — `lang/editors/Tzopilotl.tmbundle`
- **Tree-sitter grammar** — `lang/editors/tree-sitter-tzpl`

## License

Copyright (C) 2026 James McCartney.

This project is licensed under the [GNU General Public License v3.0](LICENSE). Vendored third-party code (`third_party/`, `lang/third_party/`, `app/vendor/`) retains its original permissive licenses; see the LICENSE files in those directories.
