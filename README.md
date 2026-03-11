# JSCS — Live Coding Audio Platform

A cross-platform live coding platform for creating live music performances, combining a statically typed programming language, an audio signal graph compiler, and a real-time audio engine.

## Sub-Projects

### Language X (`lang/`)

A statically typed, real-time safe interpreted language designed for audio and signal processing. Features include:

- Static type inference (source-to-sink)
- Auto-mapping: pass an array where a scalar is expected and the function maps automatically
- `@` operator for explicit depth control, Cartesian products, and data construction
- Immutable by default (`let`), mutable locals with `var`, mutable slots with `Ref`
- Direct-threaded VM using `[[clang::musttail]]` tail-call dispatch
- TLSF O(1) real-time allocator and incremental bounded-pause GC
- Rich type system: Bool, Int, Float, Symbol, String, Fraction, Complex, Array, List, Range, Tuple, Struct, Enum, Ref, Function, Lambda, Coroutine
- Template monomorphization, function overloading, pattern matching
- File-based module system with selective imports
- C/C++ embedding APIs and foreign function interface

### Synthdef Compiler (`synthdef-compiler/`)

An audio signal flow graph compiler that takes graph descriptions and compiles them into optimized native plugins (`.dylib`). Features include:

- Two front-ends: S-expression parser and C++ DSL
- ~200 audio operators (oscillators, filters, noise, envelopes, math, delays)
- 14-pass graph analysis pipeline (topology sort, shape/type inference, constant folding, dead code removal, rate scheduling)
- Algebraic rewrite engine (~100 optimization rules)
- C++ code generation targeting the `jscs_plugin_abi` interface
- Full pipeline: parse → analyze → codegen → clang compile → link → dlopen

### Audio Engine (`engine/`)

A real-time audio engine that loads native synth plugins and supports dynamic patching. Features include:

- Plugin loading via `dlopen` of `.dylib` files conforming to `jscs_plugin_abi.h`
- Dynamic graph editing with on-demand topological sort (re-sorted only when connections change)
- Crossfading system (7 curves) for glitch-free connection changes
- Lock-free SPSC FIFOs for RT-safe inter-thread communication
- Multi-silo parallel worker threads with binary-tree mixdown
- Sample-accurate scheduling queue (hash wheel, 1021 bins)
- Command bundling API and S-expression command parser
- Polyphonic voice management (`Voicer` template)
- Safety limiter on master output (lookahead, NaN zapping)

### Shared Headers (`shared/`)

Common headers used by multiple sub-projects:

- `jscs_plugin_abi.h` — Pure C plugin ABI (the interface between engine and synthdef-compiler)
- `jscs_simd.hpp` — Cross-platform SIMD abstraction
- `jscs_random.hpp` — xoroshiro128++ PRNG (scalar and SIMD)
- `jscs_matrix_transform.hpp` — Compile-time matrix operations

### FFI Bridge (`bridge/`)

Connects Language X to the engine and synthdef-compiler via the language's foreign function interface. Includes Language X module files (e.g., `audio_engine.x`) that expose native functions to scripts.

## Directory Structure

```
A-new-project/
├── CMakeLists.txt              Top-level build configuration
├── build.sh                    Quick build script
├── shared/                     Shared headers (plugin ABI, SIMD, RNG)
├── lang/              Language X interpreter
│   ├── src/                    Compiler and VM source
│   ├── tests/                  Test suite (226 tests)
│   ├── modules/                Standard library modules
│   ├── docs/                   Language documentation (HTML)
│   └── editors/                Editor support packages
├── synthdef-compiler/          Signal graph → plugin compiler
│   └── src/                    Compiler source
├── engine/               Real-time audio engine
│   └── src/                    Engine source
├── bridge/                     FFI bridges between sub-projects
│   ├── src/                    Bridge implementations
│   ├── include/                Bridge headers
│   └── modules/                Language X bridge modules
├── integration-tests/          Cross-project integration tests
├── third_party/
│   └── rtaudio/                Cross-platform audio I/O library
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
| `JSCS_BUILD_LANG` | `ON` | Build the Language X interpreter |
| `JSCS_BUILD_AUDIO_ENGINE` | `ON` | Build the audio engine |
| `JSCS_BUILD_SYNTHDEF_COMPILER` | `ON` | Build the synthdef compiler |
| `JSCS_BUILD_BRIDGE` | `ON` | Build the FFI bridge libraries |
| `JSCS_BUILD_APP` | `OFF` | Build the live coding application |
| `JSCS_BUILD_TESTS` | `OFF` | Build integration tests |

Example — build only the language:

```sh
cmake -B build -DJSCS_BUILD_LANG=ON -DJSCS_BUILD_AUDIO_ENGINE=OFF -DJSCS_BUILD_SYNTHDEF_COMPILER=OFF
cmake --build build
```

### Build Targets

| Target | Description |
|--------|-------------|
| `langx` | Language X interpreter executable |
| `langx_lib` | Language X as a static library |
| `engine` | Audio engine executable |
| `audio_engine_lib` | Audio engine as a static library |
| `synthdef-compiler` | Synthdef compiler executable |
| `synthdef_compiler_lib` | Synthdef compiler as a static library |
| `langx_audio_engine_bridge` | FFI bridge: Language X ↔ audio engine |
| `langx_synthdef_compiler_bridge` | FFI bridge: Language X ↔ synthdef compiler |
| `test_audio_engine_ffi` | Audio engine FFI test executable |
| `test_synthdef_compiler_ffi` | Synthdef compiler FFI test executable |

Build a specific target:

```sh
cmake --build build --target langx
```

## Running Language X

```sh
# Run a script
./build/lang/langx program.x

# Start the REPL
./build/lang/langx

# Add module search paths
./build/lang/langx -I lib:vendor program.x
```

## Running Tests

### Language X Tests

The language has a comprehensive test suite with 226 tests covering arithmetic, auto-mapping, builtins, control flow, coroutines, data structures, destructuring, errors, expressions, FFI, functions, modules, operators, type system, and more.

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

### Language X

- [Language X by Example](lang/docs/Language_X_by_Example.html) — Comprehensive syntax and feature guide
- [Built-in Functions](lang/docs/Builtin_Functions.html) — Reference for all built-in functions
- [Coroutines](lang/docs/Coroutines.html) — Coroutine system design and usage
- [FFI Guide](lang/docs/FFI_Guide.html) — Calling C functions from Language X

### Architecture

- [Synthdef Compiler Architecture](synthdef-compiler/ARCHITECTURE.md) — Compilation pipeline, expression graph, type system, and code generation
- [Audio Engine Architecture](engine/Architecture.md) — Engine design, silos, commands, plugin ABI, and thread safety
- [Language X Theory of Operation](lang/Theory_of_Operation.md) — Detailed design document for the language implementation
- [Integration Plan](IMPLEMENTATION_PLAN.md) — Step-by-step roadmap for integrating the sub-projects

### Editor Support

Syntax highlighting for Language X is available for:

- **VS Code** — `lang/editors/vscode`
- **Zed** — `lang/editors/zed-langx`
- **TextMate / Sublime Text** — `lang/editors/LangX.tmbundle`
- **Tree-sitter grammar** — `lang/editors/tree-sitter-langx`

## License

TBD
