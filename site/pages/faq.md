# Frequently Asked Questions

## The language

**Why does `push` not modify my array, while `push!` does?**
The trailing `!` is part of the identifier -- `push` and `push!` resolve to
different functions. By convention, builtins ending in `!` mutate an
argument in place (`push!`, `put!`, `insert!`, `pop!`); the plain forms are
non-mutating and return a new container. `Array`, `Map`, and `Set` are heap
objects passed by reference, so in-place writes (`a[0] = x`, `a push!(y)`)
are visible through every alias; use `copy` when you need an independent
one.

**I passed an array to a function expecting a scalar and it just... worked?**
That is auto-mapping: functions expecting scalars automatically apply over
arrays and lists, element by element -- `[1, 2, 3] add(10)` is
`[11, 12, 13]`. The `@` operator gives explicit control over mapping depth
and Cartesian products. See the
[Auto-mapping chapter](By_Example-auto-mapping.html) of Tzopilotl by
Example.

**Why does my function return `Void` when the body clearly computes a value?**
A function returns its *trailing expression*, which must have **no**
semicolon. `{ a + b; }` is a statement -- the body evaluates it, discards
it, and returns `Void`. Write `{ a + b }`, or use the expression-body form
`fn add(a Int, b Int) Int = a + b;`.

**What does "NRT" mean in the library docs?**
Non-real-time. Functions marked NRT perform blocking syscalls (file IO,
process launch, ...) and are rejected by the type checker in real-time
contexts, where the VM must never block or call the system allocator. Each
module's documentation states whether it is RT-safe.

## Sound & synthdefs

**`defSynthX` fails with a compiler error about clang.**
Synthdef compilation emits C++ and compiles it with the system toolchain,
so a working `clang` must be on `PATH`. On macOS:
`xcode-select --install`.

**I redefined a synth while it was playing -- why didn't the sound change
immediately?**
Compilation is asynchronous: the clang step runs in the background so music
that is already playing never pauses. Players hot-swap to the new version
when it finishes loading. Use `await` only when the *next* line needs the
def (its first play).

**Do the different engines/silos stay in sync?**
Engines and silos have independent tempos by design -- nothing assumes or
enforces synchronization between them.

## Project

**What do "TZPL" and "Tzopilotl" each refer to?**
Tzopilotl is the programming language (*tzopilotl* is Nahuatl for vulture
-- hence the emblem). TZPL is the whole platform: the language, the
synthdef compiler, the real-time engine, and the desktop app.

**Is it really GPL? What about the JUCE app?**
The project is GPLv3. The optional JUCE app target (`tzpl_app_juce`) links
the JUCE framework under its AGPLv3 option -- JUCE is downloaded at build
time, not included in the repository -- and binaries built from that target
combine GPLv3 and AGPLv3 code as each license permits. Vendored third-party
code keeps its original permissive licenses; see
[THIRD_PARTY_NOTICES.md](https://github.com/lfnoise/tzpl/blob/main/THIRD_PARTY_NOTICES.md).

**Will it run on Linux or Windows?**
Currently macOS only (the engine speaks CoreAudio). Cross-platform support
is planned; the interpreter and compilers are portable C++23, so the audio
backend is the main porting surface.
