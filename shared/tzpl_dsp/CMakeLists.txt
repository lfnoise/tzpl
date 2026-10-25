# shared/tzpl_dsp: the block-engine runtime support library (header only;
# block engine plan §5) and its tests.

add_library(tzpl_dsp INTERFACE)
# Included as "tzpl_dsp/<header>.hpp", like the other shared headers.
target_include_directories(tzpl_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
# On Linux the Sleef vmath backend uses the Sleef that shared/ fetches.
if(TARGET sleef)
    target_link_libraries(tzpl_dsp INTERFACE sleef)
    target_include_directories(tzpl_dsp INTERFACE $<BUILD_INTERFACE:${sleef_BINARY_DIR}/include>)
endif()

option(TZPL_BUILD_DSP_TESTS "Build the tzpl_dsp support library tests" ON)
if(TZPL_BUILD_DSP_TESTS)
    set(_tzpl_dsp_warnings -Wall -Wextra -Wdouble-promotion)

    # The platform's backend: Apple simd on macOS, Sleef on Linux.
    add_executable(tzpl_dsp_tests tests/tzpl_dsp_tests.cpp)
    target_link_libraries(tzpl_dsp_tests PRIVATE tzpl_dsp)
    target_compile_options(tzpl_dsp_tests PRIVATE ${_tzpl_dsp_warnings})

    # The libm lane fallback, forced, so it is tested on every platform.
    add_executable(tzpl_dsp_tests_libm tests/tzpl_dsp_tests.cpp)
    target_link_libraries(tzpl_dsp_tests_libm PRIVATE tzpl_dsp)
    target_compile_definitions(tzpl_dsp_tests_libm PRIVATE TZPL_DSP_VMATH_LIBM)
    target_compile_options(tzpl_dsp_tests_libm PRIVATE ${_tzpl_dsp_warnings})

    # Generated synth code is compiled with -O3 -ffast-math; the library must
    # meet its contracts there too (tensor-ugens ARCHITECTURE.md §7).
    add_executable(tzpl_dsp_tests_fastmath tests/tzpl_dsp_tests.cpp)
    target_link_libraries(tzpl_dsp_tests_fastmath PRIVATE tzpl_dsp)
    target_compile_options(tzpl_dsp_tests_fastmath PRIVATE ${_tzpl_dsp_warnings} -ffast-math)
endif()
