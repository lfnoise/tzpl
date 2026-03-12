# benchmarks/CMakeLists.txt
# SIMD vs scalar benchmark for synthdef-compiled plugins.

add_executable(bench_simd_vs_scalar
    src/bench_simd_vs_scalar.cpp
)

target_link_libraries(bench_simd_vs_scalar
    PRIVATE synthdef_compiler_lib
)

target_compile_options(bench_simd_vs_scalar PRIVATE -ffast-math)
