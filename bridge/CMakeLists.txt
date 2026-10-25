# bridge/CMakeLists.txt
# FFI bridge libraries connecting Tzopilotl to the engine and synthdef-compiler.

add_library(tzpl_audio_engine_bridge OBJECT
    src/tzpl_audio_engine_ffi.cpp
    src/tzpl_nrt_render.cpp
    src/tzpl_ui_state.cpp
    src/tzpl_ui_ffi.cpp
    src/tzpl_ui_node_controls.cpp
    src/tzpl_ui_taps.cpp
    src/tzpl_spectrum.cpp
)

target_include_directories(tzpl_audio_engine_bridge
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tzpl_audio_engine_bridge
    PUBLIC  tzpl_lib
    PUBLIC  audio_engine_lib
)

# Spectrum widgets run shared/tzpl_fft.hpp host-side; on Apple that is vDSP.
# PUBLIC so every consumer of this object library gets the framework too.
if(APPLE)
    target_link_libraries(tzpl_audio_engine_bridge PUBLIC "-framework Accelerate")
endif()

# Match tzpl_lib optimization flags to avoid ODR/inline mismatches
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tzpl_audio_engine_bridge PRIVATE -O1)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(tzpl_audio_engine_bridge PRIVATE -O2)
endif()

# --- Clock (tempo scheduler) FFI bridge ---

add_library(tzpl_clock_bridge OBJECT
    src/tzpl_clock_ffi.cpp
    src/tzpl_rt_tempo_scheduler.cpp
)

target_include_directories(tzpl_clock_bridge
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tzpl_clock_bridge
    PUBLIC  tzpl_lib
    PUBLIC  audio_engine_lib
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tzpl_clock_bridge PRIVATE -O1)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(tzpl_clock_bridge PRIVATE -O2)
endif()

# --- Synth build FFI bridge (synthc compile, link, load) ---
#
# The target keeps its old name; it no longer links the frozen C++
# synthdef-compiler (block-engine plan D9): tzpl_synth_build.cpp holds the
# compile-and-link step synthc uses.

add_library(tzpl_synthdef_compiler_bridge OBJECT
    src/tzpl_synthdef_compiler_ffi.cpp
    src/tzpl_synth_build.cpp
)

target_include_directories(tzpl_synthdef_compiler_bridge
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tzpl_synthdef_compiler_bridge
    PUBLIC  tzpl_lib
    PUBLIC  audio_engine_lib
)

# The runtime plugin compiler shells out to a C++ compiler; bake in the one
# this build was configured with so the PATH lookup is only a fallback
# ($TZPL_CC still overrides at runtime).
target_compile_definitions(tzpl_synthdef_compiler_bridge PRIVATE
    TZPL_PLUGIN_CXX="${CMAKE_CXX_COMPILER}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(tzpl_synthdef_compiler_bridge PRIVATE -O1)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(tzpl_synthdef_compiler_bridge PRIVATE -O2)
endif()

# --- OSC FFI bridge ---

if(TARGET osc_lib)
    add_library(tzpl_osc_bridge OBJECT
        src/tzpl_osc_ffi.cpp
        src/tzpl_osc_vm_handlers.cpp
    )

    target_include_directories(tzpl_osc_bridge
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(tzpl_osc_bridge
        PUBLIC  tzpl_lib
        PUBLIC  osc_lib
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(tzpl_osc_bridge PRIVATE -O1)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(tzpl_osc_bridge PRIVATE -O2)
    endif()
endif()

# --- NATS FFI bridge ---

if(TARGET nats_lib)
    add_library(tzpl_nats_bridge OBJECT
        src/tzpl_nats_ffi.cpp
        src/tzpl_nats_vm_handlers.cpp
    )

    target_include_directories(tzpl_nats_bridge
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(tzpl_nats_bridge
        PUBLIC  tzpl_lib
        PUBLIC  nats_lib
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(tzpl_nats_bridge PRIVATE -O1)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(tzpl_nats_bridge PRIVATE -O2)
    endif()
endif()