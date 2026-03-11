cmake_minimum_required(VERSION 3.21)
project(tzpl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Options ----------------------------------------------------------------

option(TZPL_BUILD_AUDIO_ENGINE      "Build the engine library and executable"             ON)
option(TZPL_BUILD_SYNTHDEF_COMPILER "Build the synthdef-compiler library and executable"  ON)
option(TZPL_BUILD_LANG              "Build the lang library and executable"                ON)
option(TZPL_BUILD_BRIDGE            "Build the FFI bridge libraries"                      ON)
option(TZPL_BUILD_APP               "Build the live coding application"                   OFF)
option(TZPL_BUILD_TESTS             "Build integration tests"                             OFF)

# --- Core dependencies (always needed) --------------------------------------

add_subdirectory(third_party/rtaudio)
add_subdirectory(shared)

# --- Sub-projects ------------------------------------------------------------

if(TZPL_BUILD_LANG)
    add_subdirectory(lang)
endif()

if(TZPL_BUILD_AUDIO_ENGINE)
    add_subdirectory(engine)
endif()

if(TZPL_BUILD_SYNTHDEF_COMPILER)
    add_subdirectory(synthdef-compiler)
endif()

if(TZPL_BUILD_BRIDGE AND TZPL_BUILD_LANG AND TZPL_BUILD_AUDIO_ENGINE)
    add_subdirectory(bridge)
endif()
# Note: The synthdef-compiler bridge inside bridge/CMakeLists.txt is guarded
# by if(TARGET synthdef_compiler_lib), so it only builds when
# TZPL_BUILD_SYNTHDEF_COMPILER is ON.

if(TZPL_BUILD_APP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt)
    add_subdirectory(app)
endif()

if(TZPL_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration-tests/CMakeLists.txt)
    add_subdirectory(integration-tests)
endif()
