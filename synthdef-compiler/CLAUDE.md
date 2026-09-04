# Synthdef Compiler

An audio signal flow graph compiler that takes graph descriptions (via s-expressions or a C++ DSL) and compiles them into optimized C++ dynamic library plugins conforming to the `tzpl_plugin_abi` interface.

See `ARCHITECTURE.md` for a detailed description of the compilation pipeline, expression graph, type system, and code generation.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Platform: macOS (CoreAudio/AudioToolbox frameworks, found automatically by CMake) and Linux (Sleef via the shared target; see `docs/LINUX.md`). Requires C++23 and Clang.

The compiler itself invokes the C++ compiler as a subprocess to compile generated C++ into `.dylib` (macOS) / `.so` (Linux) plugins. Build output goes to `~/tzpl-build/` or the path in `$TZPL_BUILD`.

## Usage

```
# Compile .sexpr files to .dylib plugins
./synthdef-compiler file1.sexpr file2.sexpr ...

# Run tests
./synthdef-compiler --test
```

## Project Structure

All source files are in `src/`. The shared plugin ABI header is at `../../shared/tzpl_plugin_abi.h` (relative to `src/`).

Key areas:
- **Expression graph**: `synthdef_expr`, `synthdef_value`, `synthdef_expr_visitor`
- **Graph analysis pipeline** (14 passes): `synthdef_synth`
- **Algebraic rewriter** (~100 rules): `synthdef_rewrite`
- **Code generation**: `synthdef_cpp_codegen`
- **S-expression front-end**: `synthdef_sexpr`, `synthdef_from_sexpr`
- **C++ DSL front-end**: `synthdef_builtin_ops`, `synthdef_common_ops`
- **Compilation/linking**: `synthdef_compile`
- **CLI entry point**: `main.cpp`

## Code Conventions

- **Namespace**: All code in `namespace synthdef`. S-expression parser in `namespace sexpr`.
- **Naming**: Files use `synthdef_` prefix with snake_case. Functions and variables mix snake_case and camelCase. Classes/structs use PascalCase. Single-letter names `S` (signal handle) and `D` (delay buffer handle) are intentional and pervasive.
- **Headers**: Use `#pragma once`. Local headers before standard library headers in most files.
- **Memory**: Arena allocator (`ArenaObj` base class) with thread-local storage. Context managed via RAII guards: `PushSynth`, `PushGraph`, `PushArena`.
- **Error handling**: `std::expected<T, std::string>` for recoverable errors. Exceptions for unrecoverable failures. `assert()` in tests.
- **Types**: `synthdef_types.hpp` defines short aliases (`u8`..`u64`, `i8`..`i64`, `f32`, `f64`, `usize`, `isize`). `synthdef_types2.hpp` aliases STL types (`string`, `vector`, `optional`, `variant`).
- **Patterns**: Visitor pattern for expression traversal. Hash-consing for expression deduplication. Constant folding at graph construction time.

## Tests

Tests use `assert()` and `printf()` (no test framework). Run with `--test` flag. Test functions are in `synthdef_tests.cpp` and `synthdef_from_sexpr_test.cpp`. The `all_tests()` function is the master test runner.

## Third-Party Code

- `RtAudio.h/.cpp` -- Real-time audio I/O library (vendored, do not modify)
