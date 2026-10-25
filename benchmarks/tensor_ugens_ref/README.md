# tensor-ugens reference UGens

Test oracles and benchmark baselines for the block engine (`devplans/BLOCK_ENGINE_PLAN.md` §5).

`src/` is copied unchanged from `~/Dropbox/dev/tensor-ugens/tensor-ugens/` at commit
`062f75e` ("Start pulse1 cycles high and drop the NaN previous phase"), plus that tree's
uncommitted edit removing `Shape::rank` from `shape.hpp` / `shape.cpp`. `main.cpp` and the
Xcode project are not copied. The design is described in that repo's `ARCHITECTURE.md`
(cited in the plan as TU §n).

- `tensor_ugens_ref`: a static library of the UGens: phasors and waveshaper oscillators,
  the filter family (first order, RBJ biquads, lags, decays, leakdc, resonators), the
  noises, lfnoise and the draw noises, with the graph, ops and shape support they need.
- `bench_tensor_ugens`: prints ns per sample per channel for each UGen at 1, 2, 4 and 16
  channels, block 64, f64. Compare with TU §8. An optional argument filters the cases by
  name.

The code uses `<simd/simd.h>` and `arc4random_buf`, so it builds on Apple platforms only.
Noise generators are seeded from `arc4random`, so noise outputs are not reproducible run
to run; oracle tests should compare statistics, or reseed the state.

Build (off by default):

    cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DTZPL_BUILD_BENCHMARKS=ON
    cmake --build build-bench --target bench_tensor_ugens
    ./build-bench/benchmarks/tensor_ugens_ref/bench_tensor_ugens
