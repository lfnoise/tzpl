# TZPL -- Audio Coding Platform

A cross-platform audio coding platform combining three integrated C++23 sub-projects:

- **`lang/`** -- Tzopilotl, a statically typed, real-time safe interpreted language with a direct-threaded VM, TLSF allocator, and incremental GC.
- **`synthdef-compiler/`** -- Compiles audio signal flow graphs (from S-expressions or C++ DSL) into optimized `.dylib` plugins via a 14-pass analysis pipeline.
- **`engine/`** -- Real-time audio engine with dynamic graph patching, lock-free threading, multi-silo parallelism, crossfading, and sample-accurate scheduling.
- **`bridge/`** -- FFI bridges connecting Tzopilotl to the engine and synthdef-compiler, plus optional OSC and NATS support.
- **`app/`** -- Dear ImGui GUI application integrating all components: multi-tab code editor, output panel, REPL.
- **`shared/`** -- Common headers: plugin ABI (`tzpl_plugin_abi.h`), SIMD, RNG, matrix ops.

## Building

Build from the project root (NOT from sub-project directories):

    ./build.sh

    cmake -B build -DCMAKE_BUILD_TYPE=Release -DTZPL_BUILD_APP=ON
    cmake --build build -j$(sysctl -n hw.ncpu)

AddressSanitizer build:

    cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"
    cmake --build build-asan -j$(sysctl -n hw.ncpu)

The build directory is `build/` at the project root. Key targets: `tzpl`, `tzpl_lib`, `engine`, `audio_engine_lib`, `synthdef-compiler`, `synthdef_compiler_lib`, `tzpl_app`.

**Requirements**: Clang or GCC 15+ (for `[[clang::musttail]]`), CMake 3.21+, C++23, macOS (CoreAudio).

## Testing

    cd lang/tests && bash run_tests.sh

Flags: `-v` (verbose), `-f "pattern"` (filter), `-u` (update golden files), `-x` (stop on failure).

Synthdef compiler tests:

    ./build/synthdef-compiler/synthdef-compiler --test

Always use the official test runner for lang tests -- do not use ad-hoc test loops.

## Tzopilotl Language Quick Reference

Syntax essentials for reading and writing `.x` files:

    -- Line comment
    /* Block comment (nestable) */

    -- Variable declarations
    let x = 42;                    -- immutable
    var y = 0;                     -- mutable local

    -- Functions: no colon between name and type, no -> before return type
    fn add(a Int, b Int) Int { a + b }        -- trailing expr: no semicolon
    fn add2(a Int, b Int) Int = a + b;        -- expression-body form
    fn greet(name String) Void { print(name); }

    -- Statements end with semicolons
    -- A function's return value is its trailing expression, which has NO
    -- semicolon; `{ a + b; }` is a statement, so the body returns Void
    -- Semicolons after struct/enum closing braces are optional

    -- Types: Bool, Int, Float, Symbol, String, Fraction, Complex,
    --        [T], List<T>, Range<T>, Set<T>, Map<K,V>,
    --        Tuple(T1, T2, ...), Struct, Enum, Ref<T>, Function, Coroutine<T>

    -- Mutable containers (Array, Map, Set): heap objects passed by reference.
    -- Mutating builtins idiomatically end in `!`; non-mutating ones return new
    -- values. `!` is part of the identifier -- `push` and `push!` are distinct.
    var a = [1, 2, 3];
    a[0] = 100;                    -- indexed assignment (cyclic, like reads)
    a push!(4);                    -- mutating builtin
    let b = a copy;                -- shallow copy for independent alias

    var m = ["x": 1];
    m["y"] = 2;                    -- insert-or-update

    var s = Set(1, 2);
    s insert!(3);
    let popped = s pop!;           -- mutating; returns one element

    -- Auto-mapping: functions automatically apply over arrays/lists
    let xs = [1, 2, 3];
    let ys = xs add(10);           -- [11, 12, 13]

    -- @ operator for explicit mapping and Cartesian products
    [[1,2], [3,4]] @ reverse;      -- [[2,1], [4,3]]

    -- Pattern matching
    match value {
        case .Some(x) => x;
        case .None => 0;
    }

    -- Modules
    import math.*;
    import utils.foo, utils.bar;

    -- Structs and enums
    struct Point { x Float; y Float; }
    enum Option<T> { case Some(T); case None; }

    -- Lambdas
    let f = |x Int| x * 2;

    -- Template functions
    fn identity<T>(x T) T { x }

## C++ Coding Conventions

- **C++23** standard throughout.
- **East const**: `Record const&` not `const Record&`.
- **Formatting**: Prefer `std::print`/`std::format` over iostream/string concatenation.
- **Namespaces**: `lang` (interpreter), `engine` (audio engine), `synthdef` (compiler), `sexpr` (S-expression parser).
- **File naming**: Sub-project prefixes (`tzpl_` for engine, `synthdef_` for compiler). Snake_case filenames.
- **Memory**: TLSF allocator in lang VM (never call system allocator during execution). Arena allocator in synthdef-compiler.
- **Error handling**: `std::expected<T, std::string>` for recoverable errors in synthdef-compiler. Diagnostic system in lang.
- **No em dashes in commit messages** -- use `--` instead.

## Architecture Overview

### Integration Flow

    Tzopilotl code (.x) --> lang VM compiles + evaluates
        --> synthdef-compiler FFI generates .dylib plugins
        --> engine loads plugins, builds audio graph
        --> real-time audio callback processes graph

### Key Design Decisions

- **Real-time safety**: The lang VM, engine audio thread, and generated plugins never call the system allocator or any potentially blocking system call.
- **Lock-free communication**: All RT/NRT thread communication uses SPSC atomic FIFOs.
- **Sample-accurate scheduling**: The engine processes one sample at a time, dispatching scheduled commands between samples.
- **Untagged values**: The VM stores values in 64-bit `Word` unions; types are statically known at compile time.
- **Direct-threaded dispatch**: VM opcodes are function pointers; each handler tail-calls the next via `[[clang::musttail]]`.
- **Engines/silos have independent tempos** -- do not assume or enforce synchronization between them.
- **GC heartbeat runs on a timed interval**, not just after function calls.

### Plugin ABI

The plugin interface (`shared/tzpl_plugin_abi.h`) is a pure C ABI shared between the engine and synthdef-compiler. Plugins are `.dylib` files loaded via `dlopen`. Any plugin conforming to this ABI can be loaded, not just those generated by the synthdef-compiler.

## Sub-Project Documentation

Each sub-project has its own CLAUDE.md with detailed file layouts and conventions:

- `engine/CLAUDE.md` -- Engine architecture, thread safety rules, plugin ABI
- `engine/Architecture.md` -- Detailed engine design
- `synthdef-compiler/CLAUDE.md` -- Compiler structure, code conventions
- `synthdef-compiler/ARCHITECTURE.md` -- 14-pass pipeline, expression graph, codegen
- `lang/Theory_of_Operation.md` -- Detailed language design and implementation
- `lang/docs/Tzopilotl_by_Example.html` -- Comprehensive syntax guide
- `lang/docs/Builtin_Functions.html` -- Built-in function reference
- `lang/docs/Standard_Library.html` -- Standard library module reference (std.* namespace)
- `lang/docs/Music_Libraries.html` -- Music library reference (music.* namespace: events, tunings, four composition dialects)
- `lang/docs/Tzopilotl_Music_Cookbook.html` -- Task-oriented recipes for making sound and music (ch. 11 tours the music libraries)
- `lang/docs/FFI_Guide.html` -- Foreign function interface guide (includes the audio engine FFI)
- `lang/docs/Live_Controls_and_Notebooks.html` -- User guide: the `ui` widget module and notebook documents in the app

## Key Source Files

### Lang (largest component, ~42K LOC)
- `lang/src/codegen.cpp` -- Bytecode generation (largest single file)
- `lang/src/vm.cpp` / `vm.hpp` -- Virtual machine execution
- `lang/src/type_checker*.cpp` -- Type checking (split across 9 files)
- `lang/src/compiler.cpp` -- Main compiler driver
- `lang/src/builtins*.cpp` -- Built-in functions (math, array, list operations)
- `lang/src/module_compiler.cpp` -- Module system with caching and dependency tracking
- `lang/src/value.cpp` / `value.hpp` -- Object representation, CodeBlock
- `lang/src/repl_session.cpp` -- REPL infrastructure (pimpl idiom)

### Engine (~8K LOC)
- `engine/src/tzpl_silo.cpp` -- Parallel processing units, audio processing loop
- `engine/src/tzpl_client_interface.cpp` -- Public API, plugin loading, command bundling
- `engine/src/tzpl_command_subclasses.hpp` -- All concrete command types
- `engine/src/tzpl_xfader.cpp` -- Crossfader system (7 curves)

### Synthdef Compiler (~20K LOC)
- `synthdef-compiler/src/synthdef_cpp_codegen.cpp` -- C++ code generation (2nd largest file)
- `synthdef-compiler/src/synthdef_from_sexpr.cpp` -- S-expression graph builder
- `synthdef-compiler/src/synthdef_synth.cpp` -- 14-pass analysis pipeline
- `synthdef-compiler/src/synthdef_common_ops.cpp` -- ~200 audio operators

### Bridge
- `bridge/src/tzpl_audio_engine_ffi.cpp` -- Tzopilotl to engine bridge
- `bridge/src/tzpl_synthdef_compiler_ffi.cpp` -- Tzopilotl to synthdef-compiler bridge
- `bridge/modules/*.x` -- Tzopilotl module files exposing native functions

### App
- `app/src/main.cpp` -- Application entry point, config parsing, REPL/GUI mode
- `app/src/app_gui.mm` -- macOS Metal GUI backend
- `app/src/editor_panel.cpp` -- Multi-tab code editor with syntax highlighting
