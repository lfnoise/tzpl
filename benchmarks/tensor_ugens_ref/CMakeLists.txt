# benchmarks/tensor_ugens_ref/CMakeLists.txt
# The tensor-ugens UGens as test oracles and benchmark baselines (block engine
# plan §5). The sources in src/ are copied from tensor-ugens unchanged; see
# README.md. They use <simd/simd.h> and arc4random, so this builds on Apple
# platforms only.

add_library(tensor_ugens_ref STATIC
    src/filter.cpp
    src/graph.cpp
    src/noise.cpp
    src/ops.cpp
    src/phasor.cpp
    src/shape.cpp
)
target_include_directories(tensor_ugens_ref PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The upstream code is written for gnu++20 and is not warning-clean under the
# tree's flags; keep it as it is rather than patch it.
target_compile_options(tensor_ugens_ref PRIVATE -w)

add_executable(bench_tensor_ugens bench_tensor_ugens.cpp)
target_link_libraries(bench_tensor_ugens PRIVATE tensor_ugens_ref)
