cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_PROJECT_NAME STREQUAL "jscs")
    project(static-lang-3 C CXX)
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
set(LANGX_COMPILE_OPTIONS
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    # Note: exceptions are used for initialization errors only.
    # The real-time execution path does not throw.
)

# --- langx_lib static library (everything except main.cpp) ---

set(LIB_SOURCES
    src/vm.cpp
    src/symbol.cpp
    src/compiler.cpp
    src/value.cpp
    src/type_system.cpp
    src/type_universe.cpp
    src/opcodes.cpp
    src/lexer.cpp
    src/parser.cpp
    src/type_checker.cpp
    src/codegen.cpp
    src/builtins.cpp
    src/module_compiler.cpp
    src/diagnostic.cpp
    src/repl_session.cpp
    src/langx_c.cpp
    src/langx_cpp.cpp
)

add_library(langx_lib STATIC ${LIB_SOURCES})
set_target_properties(langx_lib PROPERTIES OUTPUT_NAME langx)
target_include_directories(langx_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(langx_lib PRIVATE ${LANGX_COMPILE_OPTIONS})

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(langx_lib PRIVATE -g -O1)
endif()

# Release flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(langx_lib PRIVATE -O2)
endif()

# --- Linenoise line-editing library (vendored C, CLI only) ---

add_library(linenoise STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/linenoise/linenoise.c)
target_include_directories(linenoise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/linenoise)

# --- langx executable (CLI + REPL) ---

add_executable(langx src/main.cpp)
target_link_libraries(langx PRIVATE langx_lib linenoise)
target_compile_options(langx PRIVATE ${LANGX_COMPILE_OPTIONS})

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(langx PRIVATE -g -O1)
endif()

# Release flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(langx PRIVATE -O2)
endif()
