add_library(shared INTERFACE)
target_include_directories(shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(shared INTERFACE TZPL_SHARED_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(shared INTERFACE Threads::Threads ${CMAKE_DL_LIBS})

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
