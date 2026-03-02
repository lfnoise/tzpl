cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_PROJECT_NAME STREQUAL "jscs")
    project(audio-engine LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Require Clang (ext_vector_type swizzle syntax is Clang-only)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    message(WARNING "This project requires Clang. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# Platform-specific: Sleef on Linux
if(UNIX AND NOT APPLE)
    include(FetchContent)
    FetchContent_Declare(sleef
        GIT_REPOSITORY https://github.com/shibatch/sleef.git
        GIT_TAG 3.7
    )
    set(SLEEF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SLEEF_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sleef)
endif()

# --- audio_engine_lib static library (everything except main.cpp) ---

set(LIB_SOURCES
    src/jscs_sexpr.cpp
    src/jscs_sexpr_binary_buffer.cpp
    src/jscs_command.cpp
    src/jscs_client_interface.cpp
    src/jscs_silo.cpp
    src/jscs_engine.cpp
    src/jscs_node.cpp
    src/jscs_xfader.cpp
)

add_library(audio_engine_lib STATIC ${LIB_SOURCES})
set_target_properties(audio_engine_lib PROPERTIES OUTPUT_NAME audio_engine)

target_include_directories(audio_engine_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(audio_engine_lib PUBLIC shared rtaudio)
target_compile_definitions(audio_engine_lib PUBLIC HAVE_GETTIMEOFDAY=1)

if(UNIX AND NOT APPLE)
    target_link_libraries(audio_engine_lib PUBLIC sleef)
endif()

# --- audio-engine executable ---

add_executable(audio-engine src/main.cpp)
target_link_libraries(audio-engine PRIVATE audio_engine_lib)
