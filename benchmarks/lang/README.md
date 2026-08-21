# Language Benchmarks

Cross-language micro-benchmarks comparing Tzopilotl (`tzpl/`), C++ (`cpp/`),
and Lua (`lua/`) implementations of the same programs. Run with `run.sh`.

Most of these programs (`binary_trees`, `fannkuch_redux`, `fasta`,
`mandelbrot`, `nbody`, `spectral_norm`) are reimplementations of the classic
benchmark set from the [Computer Language Benchmarks
Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/) (Isaac
Gouy et al., BSD-3-Clause). The versions here are independent ports written
for TZPL, kept deliberately simple and structurally parallel across the three
languages rather than tuned for maximum speed. `fannkuch_redux` follows Mike
Pall's algorithm. `fib`, `matmul`, and `mandelbrot_complex` are original
additions.

Like the rest of the repository, these files are covered by the root GPLv3
[LICENSE](../../LICENSE).
