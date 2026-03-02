cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_PROJECT_NAME STREQUAL "jscs")
    project(synthdef-compiler LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# --- synthdef_compiler_lib static library (core compiler, no tests/examples/audio) ---

set(LIB_SOURCES
    src/synthdef_arena.cpp
    src/synthdef_builtin_ops.cpp
    src/synthdef_common_ops.cpp
    src/synthdef_cpp_codegen.cpp
    src/synthdef_expr.cpp
    src/synthdef_expr_visitor.cpp
    src/synthdef_from_sexpr.cpp
    src/synthdef_hash.cpp
    src/synthdef_math_ops.cpp
    src/synthdef_matrix.cpp
    src/synthdef_matrix_transform.cpp
    src/synthdef_plugin_interface.cpp
    src/synthdef_pretty_print.cpp
    src/synthdef_rewrite.cpp
    src/synthdef_sexpr.cpp
    src/synthdef_signal_type.cpp
    src/synthdef_str_util.cpp
    src/synthdef_synth.cpp
    src/synthdef_value.cpp
)

add_library(synthdef_compiler_lib STATIC ${LIB_SOURCES})
set_target_properties(synthdef_compiler_lib PROPERTIES OUTPUT_NAME synthdef_compiler)

target_include_directories(synthdef_compiler_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(synthdef_compiler_lib PUBLIC shared)

target_compile_options(synthdef_compiler_lib PRIVATE -ffast-math)

# --- synthdef-compiler executable (tests, examples, audio playback) ---

set(EXE_SOURCES
    src/main.cpp
    src/synthdef_compile.cpp
    src/synthdef_audio_io.cpp
    src/synthdef_examples.cpp
    src/synthdef_tests.cpp
    src/synthdef_from_sexpr_test.cpp
)

add_executable(synthdef-compiler ${EXE_SOURCES})
target_link_libraries(synthdef-compiler PRIVATE synthdef_compiler_lib rtaudio)
target_compile_options(synthdef-compiler PRIVATE -ffast-math)
