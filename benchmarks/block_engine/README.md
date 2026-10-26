# Block-engine benchmarks

Whole-app CPU cost of typical synths on the block engine (plan
`devplans/BLOCK_ENGINE_PLAN.md`, M9).

    bash benchmarks/block_engine/run.sh [case...]

Each case in `bench.x` runs as 16 nodes (`COUNT`), rendered with `--nrt` for
61 s and for 1 s, one silo, 64-frame blocks. The difference over 60 s and 16
nodes is the CPU time per node-second (start-up and compilation cancel out);
the best of 3 runs (`REPEAT`) is reported.

To compare against another build, point `APP` at its `tzpl_app` and `MODROOT`
at its source tree (whose `lang/modules` holds the synth compiler). Set
`TZPL_FORMS` to time a code form off (`novgroup`, `noidle`, `nosv`, ...).

For a single plugin's process loop without the engine, see
`forms_equiv --time` (integration-tests/src/forms_equiv.cpp).

## Cases

| case | what it is |
|---|---|
| `knobs` | two resonant lowpasses on noise, every control constant |
| `sinlpf1`, `sinlpf16` | a sine through a one-pole lowpass, 1 and 16 channels |
| `chorus`, `echo`, `pingpong` | the `effects.x` effects |
| `bank` | `resonBank(8)` with four notes held throughout |
| `pluck` | `ksPluck(12)`, a note every 0.5 s (about six strings ringing) |
| `bankseq` | `resonBank(8)`, a note every second held for 0.5 s |
| `cycle`, `cycleblk`, `chain` | a node pair (tanh shaper, one-pole) in a feedback loop, sample and block feedback mode, and without the loop; a "node" is one pair. Not in the default list: run them by name. On `main`, give `BENCH_X` a copy of `bench.x` without the `feedbackMode` line |

## Results (2026-10-26, Apple M-series, ms CPU per node-second)

`main` is the sample-at-a-time engine before the block-engine branch; M8b is
`a40f5e8`; M9 adds idle voices, vector compares and selects, and voice groups.

| case | main | M8b | M9 | M9 vs main |
|---|---|---|---|---|
| knobs | 0.259 | 0.199 | 0.206 | 1.25x |
| sinlpf1 | 0.270 | 0.125 | 0.109 | 2.49x |
| sinlpf16 | 0.948 | 0.730 | 0.717 | 1.32x |
| chorus | 1.106 | 0.615 | 0.574 | 1.93x |
| echo | 0.557 | 0.207 | 0.159 | 3.51x |
| pingpong | 0.500 | 0.255 | 0.250 | 2.00x |
| bank | 2.456 | 2.175 | 1.469 | 1.67x |
| pluck | 0.652 | 0.526 | 0.260 | 2.51x |
| bankseq | 1.427 | 1.249 | 0.644 | 2.22x |

Feedback between nodes (MF): `chain` 0.153, `cycle` 0.782, `cycleblk` 0.114
(`main`: `chain` 0.451, `cycle` 0.508).
