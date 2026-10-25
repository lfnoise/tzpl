# benchmarks/ugen_compare/CMakeLists.txt
# Our common_ugens.x / filters.x ugens against their tensor-ugens references
# (block engine plan M7). run_ugen_compare.sh builds the plugins and runs it.

add_executable(bench_ugen_compare bench_ugen_compare.cpp)
target_include_directories(bench_ugen_compare PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../tensor_ugens_ref
    ${PROJECT_SOURCE_DIR}/shared)
target_link_libraries(bench_ugen_compare PRIVATE tensor_ugens_ref)
