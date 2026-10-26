# TZPL -- Audio Coding Platform

A cross-platform audio coding platform combining these integrated sub-projects (C++23, plus the synth compiler written in Tzopilotl):

- **`lang/`** -- Tzopilotl, a statically typed, real-time safe interpreted language with a direct-threaded VM, TLSF allocator, and incremental GC.
- **`lang/modules/synthc/`** -- synthc, the synth compiler, written in Tzopilotl: compiles signal graphs built with the `synthdef` module (tensor-shaped signals, recurrences, voicers) into C++ that the system clang builds into `.dylib` plugins.
- **`engine/`** -- Real-time block audio engine with dynamic graph patching (cycles allowed), lock-free threading, multi-silo parallelism, crossfading, and sample-accurate scheduling.
- **`bridge/`** -- FFI bridges connecting Tzopilotl to the engine and to plugin building (compile and link of synthc's output), plus optional OSC and NATS support.
- **`app/`** -- GUI application integrating all components: multi-tab code editor, output panel, REPL, notebooks. The JUCE version (`app/juce/`, target `tzpl_app_juce`) is preferred; the original Dear ImGui version (`tzpl_app`) is still available.
- **`shared/`** -- Common headers: plugin ABI (`tzpl_plugin_abi.h`), SIMD, RNG, matrix ops.

## Building

Build from the project root (NOT from sub-project directories):

    ./build.sh

    cmake -B build -DCMAKE_BUILD_TYPE=Release -DTZPL_BUILD_APP=ON
    cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

AddressSanitizer build:

    cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"
    cmake --build build-asan -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

The build directory is `build/` at the project root. Key targets: `tzpl`, `tzpl_lib`, `engine`, `audio_engine_lib`, `tzpl_app`, `tzpl_app_juce`.

**Requirements**: Clang 19+ (Clang required: `ext_vector_type` swizzles and `[[clang::musttail]]`), CMake 3.21+, C++23. macOS (CoreAudio) or Linux (ALSA/JACK/Pulse; see `docs/LINUX.md`).

## Testing

    cd lang/tests && bash run_tests.sh

Flags: `--verbose`, `--filter <pattern>` (substring match on test path), `--update` (update golden files), `--stop-on-fail`.

synthc (the synth compiler) analysis-dump goldens, compiled plugins in every
code form, and the block engine:

    bash integration-tests/run_synthc_dump_test.sh        # --update to regenerate
    bash integration-tests/run_forms_equivalence_test.sh  # needs build/integration-tests/forms_equiv
    ./build/integration-tests/test_block_engine

Benchmarks: `bash benchmarks/block_engine/run.sh` (see its README).

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
    a clear!;                      -- empty any Array/Map/Set in place
    a append!(b);                  -- bulk-append an array/list in place ($ analogue)
    a isEmpty;                     -- true when any collection has no elements

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
    let f = fn(x Int) Int { x * 2 };

    -- Template functions
    fn identity<T>(x T) T { x }

## C++ Coding Conventions

- **C++23** standard throughout.
- **East const**: `Record const&` not `const Record&`.
- **Formatting**: Prefer `std::print`/`std::format` over iostream/string concatenation.
- **Namespaces**: `lang` (interpreter), `engine` (audio engine), `synthdef` (helpers generated plugins use, in `shared/`), `sexpr` (S-expression parser).
- **File naming**: `tzpl_` prefix for engine and shared files. Snake_case filenames.
- **Memory**: TLSF allocator in lang VM (never call system allocator during execution).
- **Error handling**: `std::expected<T, std::string>` for recoverable errors in the bridge's plugin building. Diagnostic system in lang.
- **No em dashes in commit messages** -- use `--` instead.

## Architecture Overview

### Integration Flow

    Tzopilotl code (.x) --> lang VM compiles + evaluates
        --> synthc (lang/modules/synthc) emits C++; the bridge compiles and links it into .dylib plugins
        --> engine loads plugins, builds audio graph
        --> real-time audio callback processes graph

### Key Design Decisions

- **Real-time safety**: The lang VM, engine audio thread, and generated plugins never call the system allocator or any potentially blocking system call.
- **Lock-free communication**: All RT/NRT thread communication uses SPSC atomic FIFOs.
- **Sample-accurate scheduling**: The engine processes blocks (default 64 samples; plugin ABI v3). Scheduled commands are applied in time order before the block's nodes run: value changes are rendered into input buffers at their sample, and discrete events (node start/free, voice reset) split the node's block there.
- **Untagged values**: The VM stores values in 64-bit `Word` unions; types are statically known at compile time.
- **Direct-threaded dispatch**: VM opcodes are function pointers; each handler tail-calls the next via `[[clang::musttail]]`.
- **Feedback between nodes**: a connection may close a cycle; the silo runs the cycle's nodes a sample at a time with the closing connection a sample late (`feedbackMode` sample, default) or a block at a time (`block`).
- **Engines/silos have independent tempos** -- do not assume or enforce synchronization between them.
- **GC heartbeat runs on a timed interval**, not just after function calls.

### Plugin ABI

The plugin interface (`shared/tzpl_plugin_abi.h`) is a pure C ABI between the engine and plugins (ABI v3: `process(synth, offset, n)` over per-channel port buffers, `resetVoice`). Plugins are `.dylib` files loaded via `dlopen`. Any plugin conforming to this ABI can be loaded, not just those synthc generates.

## Sub-Project Documentation

Each sub-project has its own CLAUDE.md with detailed file layouts and conventions:

- `engine/CLAUDE.md` -- Engine architecture, thread safety rules, plugin ABI
- `engine/Architecture.md` -- Detailed engine design
- `devplans/BLOCK_ENGINE_PLAN.md` -- Block engine and synthc design, with what landed per milestone
- `lang/Theory_of_Operation.md` -- Detailed language design and implementation
- `lang/docs/Tzopilotl_by_Example.html` -- Comprehensive syntax guide
- `lang/docs/Builtin_Functions.html` -- Built-in function reference
- `lang/docs/Standard_Library.html` -- Standard library module reference (std.* namespace)
- `lang/docs/Music_Libraries.html` -- Music library reference (music.* namespace: events, tunings, four composition dialects)
- `lang/docs/Writing_SynthDefs.html` -- SynthDef authoring guide (rates, element types, channel broadcasting, delays, buffers, voicers) + `synthdef`/`common_ugens`/`filters` reference
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

### Synth Compiler (synthc, Tzopilotl, ~12K lines)
- `lang/modules/synthdef.x` -- The front end: signals, shapes, inputs, delays, voicers
- `lang/modules/synthc/importer.x` -- Graph to IR
- `lang/modules/synthc/passes.x` -- Analysis passes (types, shapes, rates, cuts)
- `lang/modules/synthc/recurrence.x`, `cycles.x`, `paramgroups.x` -- Recurrence forms, delay cycles, parameter groups
- `lang/modules/synthc/codegen.x` -- C++ code generation (largest file)
- `bridge/src/tzpl_synth_build.cpp` -- Compile and link of the generated C++

### Bridge
- `bridge/src/tzpl_audio_engine_ffi.cpp` -- Tzopilotl to engine bridge
- `bridge/src/tzpl_synthdef_compiler_ffi.cpp` -- Tzopilotl to plugin building (synthc's output)
- `bridge/modules/*.x` -- Tzopilotl module files exposing native functions

### App
- `app/src/main.cpp` -- Application entry point, config parsing, REPL/GUI mode
- `app/src/app_gui.mm` -- macOS Metal GUI backend
- `app/src/editor_panel.cpp` -- Multi-tab code editor with syntax highlighting
