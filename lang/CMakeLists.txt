cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_PROJECT_NAME STREQUAL "tzpl")
    project(lang C CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Require Clang or GCC 15+ for [[clang::musttail]] support
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # All Clang versions with C++23 support are fine
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "15")
    # GCC 15+ supports [[clang::musttail]]
else()
    message(WARNING "This project requires Clang or GCC 15+ for [[clang::musttail]] support. "
                    "Use -DCMAKE_CXX_COMPILER=clang++ or g++-15 to configure.")
endif()

# Common compiler flags
set(TZPL_COMPILE_OPTIONS
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    # Note: exceptions are used for initialization errors only.
    # The real-time execution path does not throw.
)

# --- tzpl_lib static library (everything except main.cpp) ---

set(LIB_SOURCES
    src/vm.cpp
    src/tracing_gc.cpp
    src/symbol.cpp
    src/compiler.cpp
    src/value.cpp
    src/value_graph.cpp
    src/graph_equal.cpp
    src/graph_hash.cpp
    src/pretty_print.cpp
    src/value_serialize.cpp
    src/persistent_vector.cpp
    src/persistent_map.cpp
    src/type_system.cpp
    src/type_universe.cpp
    src/opcodes.cpp
    src/lexer.cpp
    src/parser.cpp
    src/type_checker.cpp
    src/type_checker_decls.cpp
    src/type_checker_stmts.cpp
    src/type_checker_infer.cpp
    src/type_checker_exprs.cpp
    src/type_checker_calls.cpp
    src/type_checker_types.cpp
    src/type_checker_constraints.cpp
    src/type_checker_overload.cpp
    src/codegen.cpp
    src/builtins.cpp
    src/builtins_math.cpp
    src/builtins_array.cpp
    src/builtins_listgen.cpp
    src/builtins_bytes.cpp
    src/builtins_actors.cpp
    src/disassemble.cpp
    src/module_compiler.cpp
    src/diagnostic.cpp
    src/repl_session.cpp
    src/incremental_compiler.cpp
    src/nrt_scheduler.cpp
    src/nrt_tempo_scheduler.cpp
    src/tzpl_c.cpp
    src/tzpl_cpp.cpp
)

add_library(tzpl_lib STATIC ${LIB_SOURCES})
set_target_properties(tzpl_lib PROPERTIES OUTPUT_NAME tzpl)
target_include_directories(tzpl_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# shared/ provides tempo_ramp.hpp (and other common headers).
target_link_libraries(tzpl_lib PUBLIC shared)
target_compile_options(tzpl_lib PRIVATE ${TZPL_COMPILE_OPTIONS})

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tzpl_lib PRIVATE -g -O0)
endif()

# Release flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(tzpl_lib PRIVATE -O2)
endif()

# --- Linenoise line-editing library (vendored C, CLI only) ---

add_library(linenoise STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/linenoise/linenoise.c)
target_include_directories(linenoise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/linenoise)

# --- tzpl executable (CLI + REPL) ---

add_executable(tzpl src/main.cpp)
target_link_libraries(tzpl PRIVATE tzpl_lib linenoise)
target_compile_options(tzpl PRIVATE ${TZPL_COMPILE_OPTIONS})

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tzpl PRIVATE -g -O0)
endif()

# Release flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(tzpl PRIVATE -O2)
endif()

# --- install targets ---

include(GNUInstallDirs)

install(TARGETS tzpl_lib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/tzpl.h
    include/tzpl.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tzpl
)
install(TARGETS tzpl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
