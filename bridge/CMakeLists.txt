# bridge/CMakeLists.txt
# FFI bridge libraries connecting Tzopilotl to the engine and synthdef-compiler.

add_library(tzpl_audio_engine_bridge OBJECT
    src/tzpl_audio_engine_ffi.cpp
    src/tzpl_nrt_render.cpp
    src/tzpl_ui_state.cpp
    src/tzpl_ui_ffi.cpp
    src/tzpl_ui_node_controls.cpp
)

target_include_directories(tzpl_audio_engine_bridge
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(tzpl_audio_engine_bridge
    PUBLIC  tzpl_lib
    PUBLIC  audio_engine_lib
)

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

# --- Synthdef-compiler FFI bridge ---

if(TARGET synthdef_compiler_lib)
    add_library(tzpl_synthdef_compiler_bridge OBJECT
        src/tzpl_synthdef_compiler_ffi.cpp
    )

    target_include_directories(tzpl_synthdef_compiler_bridge
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(tzpl_synthdef_compiler_bridge
        PUBLIC  tzpl_lib
        PUBLIC  synthdef_compiler_lib
        PUBLIC  audio_engine_lib
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(tzpl_synthdef_compiler_bridge PRIVATE -O1)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(tzpl_synthdef_compiler_bridge PRIVATE -O2)
    endif()
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