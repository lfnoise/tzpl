# Contributing to TZPL

Thanks for your interest in contributing! This document covers how to build,
test, and submit changes.

## Building

Requirements: Clang or GCC 15+ (for `[[clang::musttail]]`), CMake 3.21+,
C++23, macOS (CoreAudio). Build from the project root, not from sub-project
directories:

```sh
./build.sh
```

or manually:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

See the README for the full list of CMake options and build targets.

## Testing

Run the language test suite before submitting changes that touch `lang/`:

```sh
cd lang/tests && bash run_tests.sh
```

Useful flags: `-v` (verbose), `-f "pattern"` (filter), `-u` (update golden
files), `-x` (stop on first failure). Always use this runner rather than
executing test files ad hoc.

Synthdef compiler self-tests:

```sh
./build/synthdef-compiler/synthdef-compiler --test
```

CI (`.github/workflows/tests.yml`) runs the golden-file suite and builds the
apps on every push and pull request.

## Code conventions

- C++23 throughout.
- East const: `Record const&`, not `const Record&`.
- Prefer `std::print`/`std::format` over iostream and string concatenation.
- Namespaces: `lang` (interpreter), `engine` (audio engine), `synthdef`
  (compiler), `sexpr` (S-expression parser).
- Real-time safety is a hard rule: the lang VM, the engine audio thread, and
  generated plugins must never call the system allocator or any potentially
  blocking system call. RT/NRT communication uses lock-free SPSC FIFOs.
- Sub-project conventions are documented in `engine/Architecture.md`,
  `synthdef-compiler/ARCHITECTURE.md`, and `lang/Theory_of_Operation.md`.
- Commit messages: use `--` rather than em dashes.

## Submitting changes

1. Fork the repository and create a branch from `main`.
2. Make your changes, with tests where applicable (lang changes should add or
   update golden-file tests under `lang/tests/`).
3. Make sure the test suites above pass and CI is green.
4. Open a pull request describing what the change does and why.

By submitting a contribution you agree that it is licensed under the
[GPLv3](LICENSE), the same license as the project.

## Reporting bugs and requesting features

Please use the issue templates on GitHub. For security issues, see
[SECURITY.md](SECURITY.md) -- do not open a public issue.
