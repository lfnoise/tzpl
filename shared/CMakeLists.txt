add_library(shared INTERFACE)
target_include_directories(shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(shared INTERFACE TZPL_SHARED_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Host-side platform shims (dynamic loading, process spawning, well-known
# paths). One small static library so every consumer of the shared headers
# gets them; never part of generated plugin code.
add_library(tzpl_platform STATIC
    tzpl_dynlib.cpp
    tzpl_process.cpp
    tzpl_paths.cpp
)
target_include_directories(tzpl_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tzpl_platform PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

target_link_libraries(shared INTERFACE tzpl_platform)

# Sleef supplies the vector math that <simd/simd.h> provides on macOS
# (tzpl_simd.hpp includes <sleef.h> on non-Apple), so every consumer of the
# shared headers needs it. On Linux it is built shared so runtime-compiled
# plugins can link the same copy: synthdef_compile_link.cpp stages sleef.h
# and libsleef into the plugin build dir using the TZPL_SLEEF_* definitions
# exported here. On Windows the host copy is static (clang-cl); plugins get
# their own static build from the llvm-mingw toolchain below.
if(NOT APPLE)
    include(FetchContent)
    FetchContent_Declare(sleef
        GIT_REPOSITORY https://github.com/shibatch/sleef.git
        GIT_TAG 3.7
    )
    set(SLEEF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SLEEF_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    set(SLEEF_BUILD_DFT OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(SLEEF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    else()
        set(SLEEF_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(sleef)
    target_link_libraries(shared INTERFACE sleef)
    target_include_directories(shared INTERFACE
        $<BUILD_INTERFACE:${sleef_BINARY_DIR}/include>
    )
    if(NOT WIN32)
        target_compile_definitions(shared INTERFACE
            TZPL_SLEEF_INCLUDE_DIR="${sleef_BINARY_DIR}/include"
            TZPL_SLEEF_LIB="$<TARGET_FILE:sleef>"
        )
    endif()
endif()

# Windows: the Sleef that runtime-compiled plugins link is built with the
# same llvm-mingw toolchain that compiles them (a clang-cl static library
# cannot be linked by the GNU driver). Static, so plugin DLLs import only
# the system CRT. Skipped when no plugin toolchain is configured; the
# distribution's lib/ then has to supply libsleef.a (make_dist_win.ps1
# checks).
if(WIN32 AND TZPL_PLUGIN_TOOLCHAIN_DIR)
    include(ExternalProject)
    set(TZPL_PLUGIN_SLEEF_PREFIX "${CMAKE_BINARY_DIR}/plugin-sleef")
    ExternalProject_Add(sleef_plugin
        GIT_REPOSITORY https://github.com/shibatch/sleef.git
        GIT_TAG 3.7
        GIT_SHALLOW TRUE
        CMAKE_GENERATOR Ninja
        CMAKE_ARGS
            -DCMAKE_C_COMPILER=${TZPL_PLUGIN_TOOLCHAIN_DIR}/bin/clang.exe
            -DCMAKE_CXX_COMPILER=${TZPL_PLUGIN_TOOLCHAIN_DIR}/bin/clang++.exe
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${TZPL_PLUGIN_SLEEF_PREFIX}
            -DSLEEF_BUILD_SHARED_LIBS=OFF
            -DSLEEF_BUILD_TESTS=OFF
            -DSLEEF_BUILD_BENCH=OFF
            -DSLEEF_BUILD_DFT=OFF
        BUILD_BYPRODUCTS "${TZPL_PLUGIN_SLEEF_PREFIX}/lib/libsleef.a"
    )
    add_dependencies(tzpl_platform sleef_plugin)
    target_compile_definitions(shared INTERFACE
        TZPL_SLEEF_INCLUDE_DIR="${TZPL_PLUGIN_SLEEF_PREFIX}/include"
        TZPL_PLUGIN_SLEEF_LIB="${TZPL_PLUGIN_SLEEF_PREFIX}/lib/libsleef.a"
    )
    install(FILES "${TZPL_PLUGIN_SLEEF_PREFIX}/lib/libsleef.a"
            DESTINATION lib COMPONENT dist)
    install(FILES "${TZPL_PLUGIN_SLEEF_PREFIX}/include/sleef.h"
            DESTINATION include COMPONENT dist)
endif()

# --- install targets ---

include(GNUInstallDirs)

# The distribution folder carries the plugin headers as include/ beside
# modules/: generated synthdef code #includes them at runtime, and a user's
# machine has no source tree (synthdef_compile_link.cpp resolves the
# distribution's include/ after $TZPL_SHARED_INCLUDE). Sleef goes with them
# where plugins link it.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION include
    COMPONENT dist
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)
if(UNIX AND NOT APPLE)
    install(TARGETS sleef LIBRARY DESTINATION lib COMPONENT dist
            NAMELINK_COMPONENT dist)
    install(FILES ${sleef_BINARY_DIR}/include/sleef.h
            DESTINATION include COMPONENT dist)
endif()

install(FILES
    tzpl_plugin_abi.h
    tempo_ramp.hpp
    tzpl_simd.hpp
    tzpl_random.hpp
    tzpl_matrix_transform.hpp
    tzpl_voicer.hpp
    tzpl_fft.hpp
    synthdef_plugin_interface.hpp
    synthdef_types.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tzpl
)
