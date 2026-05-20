# Tzopilotl -- Statically Typed Real-Time Interpreted Language

A statically typed, real-time safe interpreted language designed for audio and signal processing. Features a direct-threaded register-based VM, TLSF O(1) allocator, incremental bounded-pause GC, source-to-sink type inference, auto-mapping, and a file-based module system.

See `Theory_of_Operation.md` for detailed design rationale.
See `docs/Tzopilotl_by_Example.html` for comprehensive syntax documentation.

## Building

Built as part of the top-level CMake project. From the project root:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

Targets: `tzpl` (CLI executable), `tzpl_lib` (static library).

## Testing

```sh
cd lang/tests && bash run_tests.sh
```

~332 tests in `.x` files with `.expected` golden outputs, organized across 38 subdirectories. Flags: `-v` (verbose), `-f "pattern"` (filter), `-u` (update golden files), `-x` (stop on first failure).

## Running

```sh
./build/lang/tzpl              # Start REPL
./build/lang/tzpl program.x    # Run a script
./build/lang/tzpl -I path      # Add module search paths
```

## Source Layout (`src/`)

### Compiler Frontend
| File | Role |
|------|------|
| `lexer.cpp/hpp` | Hand-written scanner, ~75 token kinds |
| `parser.cpp/hpp` | Recursive descent with Pratt precedence climbing, ~65 AST node types |
| `ast.hpp` | AST node definitions |
| `diagnostic.cpp/hpp` | Error/warning reporting with source context |

### Type System
| File | Role |
|------|------|
| `type_system.cpp/hpp` | Type representation and operations |
| `type_universe.cpp/hpp` | Global type interning and caching |
| `type_checker.cpp/hpp` | Core type checking infrastructure |
| `type_checker_calls.cpp` | Function call type checking and inference (most complex) |
| `type_checker_constraints.cpp` | Generic constraint solving |
| `type_checker_decls.cpp` | Declaration checking |
| `type_checker_exprs.cpp` | Expression type checking |
| `type_checker_infer.cpp` | Return type inference |
| `type_checker_overload.cpp` | Overload resolution (best-match scoring) |
| `type_checker_stmts.cpp` | Statement checking |
| `type_checker_types.cpp` | Type expression resolution |

### Code Generation and Execution
| File | Role |
|------|------|
| `codegen.cpp` | Bytecode generation -- largest file (~8000 lines) |
| `compiler.cpp/hpp` | Main compiler driver, orchestrates all phases |
| `vm.cpp/hpp` | Direct-threaded VM using `[[clang::musttail]]` tail calls |
| `opcodes.cpp/hpp` | ~100+ opcode handler functions |
| `disassemble.cpp/hpp` | Bytecode disassembler for debugging |

### Built-in Functions
| File | Role |
|------|------|
| `builtins.cpp/hpp` | Registration, dispatch, higher-order functions (map, filter, fold, etc.) |
| `builtins_math.cpp` | Math operations (trig, log, power, rounding, special functions) |
| `builtins_array.cpp` | Array operations (indexing, slicing, aggregation, transformation) |
| `builtins_listgen.cpp` | Lazy list generators and combinators (zip, cross, transpose) |

### Values and Memory
| File | Role |
|------|------|
| `value.cpp/hpp` | Object representation, CodeBlock, Word union, type-aware printing |
| `gc.hpp` | `GCObj` base class: color bits, gcTag, immortal flag, all-objects-list link |
| `tracing_gc.hpp/cpp` | Incremental tri-color snapshot-at-the-beginning (SATB) tracing GC; bounded mark/sweep budget driven by `op_safepoint` polls and host rtTick/nrtTick |
| `tlsf_allocator.hpp` | TLSF O(1) real-time allocator |
| `stl_allocator.hpp` | STL-compatible allocator wrapper for TLSF |
| `vm_allocator.hpp` | VM-specific allocator configuration |
| `symbol.cpp/hpp` | Symbol interning (pointer-equality comparison) |

### Module System and REPL
| File | Role |
|------|------|
| `module_compiler.cpp/hpp` | Multi-file compilation, dependency tracking, caching |
| `repl_session.cpp/hpp` | REPL infrastructure (pimpl idiom) |
| `nrt_scheduler.cpp/hpp` | Non-real-time scheduler |
| `nrt_tempo_scheduler.cpp/hpp` | Tempo-aware scheduling |

### FFI and Embedding
| File | Role |
|------|------|
| `tzpl_c.cpp` | C language bindings |
| `tzpl_cpp.cpp` | C++ language bindings |
| `main.cpp` | CLI entry point (REPL, file execution) |

## Standard Library Modules (`modules/`)

- `common_ugens.x` -- Audio unit generators (oscillators, noise, envelopes)
- `synthdef.x` -- SynthDef integration framework
- `dsp_math.x` -- DSP math utilities
- `filters.x` -- IIR/FIR filter implementations
- `example_synthdefs.x` -- Example synthesizer definitions
- `sexprs.x` -- S-expression utilities

## Language Design Principles

- **Immutable by default**: `let` bindings are immutable. Use `var` for mutable locals, `Ref[T]` for mutable scalar/struct slots.
- **Mutable containers**: `Array`, `Map`, and `Set` are heap objects passed by reference. They support in-place writes via `a[i] = x`, `m[k] = v`, and the bang-suffixed builtins `push!` / `pop!` (arrays), `put!` / `remove!` (maps), and `insert!` / `remove!` / `pop!` (sets). The plain `push` / `pop` / `add` / `put` / `remove` etc. remain non-mutating and return new containers. Use `copy` for an independent shallow copy.
- **Trailing `!` on identifiers**: the `!` is part of the identifier &mdash; `foo` and `foo!` resolve to different functions. Idiomatically the `!` marks a mutating function, but the convention is not enforced.
- **Source-to-sink inference**: Types propagate forward, not bidirectionally.
- **Untagged values**: 64-bit `Word` union; types are statically known at compile time.
- **Real-time safe**: TLSF allocator, no system allocator calls, no blocking syscalls during execution.
- **Single-threaded VM**: No atomics or mutexes internally. GC runs incrementally within the same thread.
- **Auto-mapping**: Functions expecting scalars automatically apply over arrays/lists.
- **`@` operator**: Postfix operator for explicit depth control, Cartesian products, and data construction.

## Syntax Quick Reference

```
-- Comments use double dashes
/* Block comments are nestable */

-- No colon between param name and type; no arrow before return type
fn foo(x Int, y Float) String { ... }

-- Statements end with semicolons
let a = 10;
var b = 20;

-- Template functions
fn identity<T>(x T) T { x; }

-- Structs (optional trailing semicolon)
struct Point { x Float; y Float; }

-- Enums (sum types)
enum Option[T] { case Some(T); case None; }

-- Private functions start with underscore
fn _helper(x Int) Int { x * 2; }

-- Modules
import math.*;
import utils.foo, utils.bar;

-- Mutable containers: in-place writes
var a = [1, 2, 3];
a[0] = 100;
a push!(4);
var m = ["x": 1];
m["y"] = 2;
var s = Set(1, 2);
s insert!(3);
```

## C++ Coding Conventions

- **Namespace**: `lang` (except FFI exports).
- **East const**: `Record const&` not `const Record&`.
- **Formatting**: `std::print`/`std::format` over iostream.
- **Memory**: Never call system allocator from VM execution path. TLSF for all runtime allocations.
- **Error handling**: `CompileError` struct with message + source location. Diagnostic system for formatted output.

## Editor Support (`editors/`)

- VS Code: `editors/vscode`
- Zed: `editors/zed-tzpl`
- TextMate/Sublime: `editors/Tzopilotl.tmbundle`
- Tree-sitter: `editors/tree-sitter-tzpl`
