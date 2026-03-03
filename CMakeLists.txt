cmake_minimum_required(VERSION 3.21)
project(jscs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Options ----------------------------------------------------------------

option(JSCS_BUILD_AUDIO_ENGINE      "Build the audio-engine library and executable"      ON)
option(JSCS_BUILD_SYNTHDEF_COMPILER "Build the synthdef-compiler library and executable"  ON)
option(JSCS_BUILD_LANG              "Build the static-lang-3 library and executable"      ON)
option(JSCS_BUILD_BRIDGE            "Build the FFI bridge libraries"                      ON)
option(JSCS_BUILD_APP               "Build the live coding application"                   OFF)
option(JSCS_BUILD_TESTS             "Build integration tests"                             OFF)

# --- Core dependencies (always needed) --------------------------------------

add_subdirectory(third_party/rtaudio)
add_subdirectory(shared)

# --- Sub-projects ------------------------------------------------------------

if(JSCS_BUILD_LANG)
    add_subdirectory(static-lang-3)
endif()

if(JSCS_BUILD_AUDIO_ENGINE)
    add_subdirectory(audio-engine)
endif()

if(JSCS_BUILD_SYNTHDEF_COMPILER)
    add_subdirectory(synthdef-compiler)
endif()

if(JSCS_BUILD_BRIDGE AND JSCS_BUILD_LANG AND JSCS_BUILD_AUDIO_ENGINE)
    add_subdirectory(bridge)
endif()

if(JSCS_BUILD_APP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt)
    add_subdirectory(app)
endif()

if(JSCS_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration-tests/CMakeLists.txt)
    add_subdirectory(integration-tests)
endif()
