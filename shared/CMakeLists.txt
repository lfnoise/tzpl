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
# shared headers needs it. Built shared so runtime-compiled plugins can link
# the same copy: synthdef_compile_link.cpp stages sleef.h and libsleef into
# the plugin build dir using the TZPL_SLEEF_* definitions exported here.
if(UNIX AND NOT APPLE)
    include(FetchContent)
    FetchContent_Declare(sleef
        GIT_REPOSITORY https://github.com/shibatch/sleef.git
        GIT_TAG 3.7
    )
    set(SLEEF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SLEEF_BUILD_BENCH OFF CACHE BOOL "" FORCE)
    set(SLEEF_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sleef)
    target_link_libraries(shared INTERFACE sleef)
    target_include_directories(shared INTERFACE
        $<BUILD_INTERFACE:${sleef_BINARY_DIR}/include>
    )
    target_compile_definitions(shared INTERFACE
        TZPL_SLEEF_INCLUDE_DIR="${sleef_BINARY_DIR}/include"
        TZPL_SLEEF_LIB="$<TARGET_FILE:sleef>"
    )
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
