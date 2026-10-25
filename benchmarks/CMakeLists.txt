# benchmarks/CMakeLists.txt

# SIMD vs scalar benchmark for synthdef-compiled plugins.
if(TARGET synthdef_compiler_lib)
    add_executable(bench_simd_vs_scalar
        src/bench_simd_vs_scalar.cpp
    )

    target_link_libraries(bench_simd_vs_scalar
        PRIVATE synthdef_compiler_lib
    )

    target_compile_options(bench_simd_vs_scalar PRIVATE -ffast-math)
endif()

# tensor-ugens reference UGens and their benchmark (block engine plan §5).
# Apple only: the copied sources use <simd/simd.h>.
if(APPLE)
    add_subdirectory(tensor_ugens_ref)
    add_subdirectory(ugen_compare)
endif()
