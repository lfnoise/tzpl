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
option(TZPL_BUILD_BENCHMARKS        "Build benchmarks"                                    OFF)
option(TZPL_BUILD_OSC               "Build OSC (Open Sound Control) support"              OFF)
option(TZPL_BUILD_NATS              "Build NATS messaging support"                       OFF)
option(TZPL_BUILD_GUI               "Build with GUI support (Dear ImGui + GLFW)"         ON)

# --- Core dependencies (always needed) --------------------------------------

add_subdirectory(third_party/rtaudio)
if(TZPL_BUILD_OSC)
    add_subdirectory(third_party/oscpack)
endif()
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

if(TZPL_BUILD_OSC AND TZPL_BUILD_AUDIO_ENGINE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/osc/CMakeLists.txt)
    add_subdirectory(osc)
endif()

if(TZPL_BUILD_NATS AND TZPL_BUILD_AUDIO_ENGINE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/nats/CMakeLists.txt)
    add_subdirectory(nats)
endif()

if(TZPL_BUILD_BRIDGE AND TZPL_BUILD_LANG AND TZPL_BUILD_AUDIO_ENGINE)
    add_subdirectory(bridge)
endif()
# Note: The synthdef-compiler bridge inside bridge/CMakeLists.txt is guarded
# by if(TARGET synthdef_compiler_lib), so it only builds when
# TZPL_BUILD_SYNTHDEF_COMPILER is ON.
# The OSC bridge inside bridge/CMakeLists.txt is guarded
# by if(TARGET osc_lib), so it only builds when TZPL_BUILD_OSC is ON.
# The NATS bridge inside bridge/CMakeLists.txt is guarded
# by if(TARGET nats_lib), so it only builds when TZPL_BUILD_NATS is ON.

if(TZPL_BUILD_APP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt)
    add_subdirectory(app)
endif()

if(TZPL_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration-tests/CMakeLists.txt)
    add_subdirectory(integration-tests)
endif()

if(TZPL_BUILD_BENCHMARKS AND TZPL_BUILD_SYNTHDEF_COMPILER AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt)
    add_subdirectory(benchmarks)
endif()
