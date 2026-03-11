# Tzopilotl

A statically typed, real-time safe interpreted language designed for audio and signal processing.

Tzopilotl combines functional and imperative programming with static type inference, immutable-by-default data, and a runtime that never calls the system allocator or blocks. It is suitable for use within a real-time audio thread.

## Quick Example

```
-- Functions, auto-mapping, and pipelines
fn double(x Int) Int = x * 2;

42 double println;              -- 84
[1, 2, 3, 4, 5] double println; -- [2, 4, 6, 8, 10]

-- Closures
let offset = 10;
let shift = fn(x Int) Int { x + offset };
[1, 2, 3] shift println;       -- [11, 12, 13]

-- Enums and pattern matching
enum Shape {
    circle Float,
    rect (Float, Float)
}

fn area(s Shape) Float = match s {
    Shape.circle(r) => 3.14159 * r * r,
    Shape.rect(w, h) => w * h,
};

Shape.circle(5.0) area println; -- 78.53975

-- The @ operator maps at a chosen depth
[[1, 2, 3], [4, 5, 6]] @ reverse println; -- [[3, 2, 1], [6, 5, 4]]
```

## Features

- **Static type inference** -- types flow from source to sink; type annotations can be omitted in many cases.
- **Auto-mapping** -- pass an array where a scalar is expected and the function maps over it automatically.
- **`@` operator** -- explicit depth control for mapping, Cartesian products, and data construction.
- **Immutable by default** -- `let` bindings, arrays, tuples, maps, and structs are immutable. Use `var` for mutable locals. Use `Ref` for mutable slots in data structures.
- **Real-time safe runtime** -- custom TLSF allocator, real-time garbage collector, no system calls that block.
- **Direct-threaded VM** -- opcode functions dispatch via `[[clang::musttail]]` tail calls; no bytecode switch loop.
- **Rich type system** -- tuples, structs, enums (sum types), templates, functions, lazy lists, refs.
- **Space and pipe pipelines** -- `x f g` or `a + b |> f` for left-to-right data flow.
- **Function overloading** -- dispatch on static argument types.
- **Pattern matching** -- destructuring in `match` and `let`.
- **Module system** -- file-based modules with selective imports and visibility control.

## Building

Requires **Clang** or **GCC 15+** (for `[[clang::musttail]]`) and **CMake 3.20+** with C++23 support.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Usage

```sh
# Run a file
./build/tzpl program.x

# Start the REPL
./build/tzpl

# Add module search paths
./build/tzpl -I lib:vendor program.x
```

## Running Tests

```sh
./tests/run_tests.sh
```

## Documentation

- [Tzopilotl by Example](docs/Tzopilotl_by_Example.html) -- comprehensive syntax guide
- [Built-in Functions](docs/Builtin_Functions.html) -- reference for all built-in functions
- [Coroutines](docs/Coroutines.html) -- coroutine system design and usage
- [FFI Guide](docs/FFI_Guide.html) -- calling C from Tzopilotl

## Editor Support

Syntax highlighting packages are available in the `editors/` directory:

- **VS Code** -- `editors/vscode`
- **Zed** -- `editors/zed-tzpl`
- **TextMate / Sublime Text** -- `editors/Tzopilotl.tmbundle`
- **Tree-sitter grammar** -- `editors/tree-sitter-tzpl`
