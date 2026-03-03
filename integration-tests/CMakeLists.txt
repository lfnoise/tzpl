# integration-tests/CMakeLists.txt
# Integration tests for FFI bridges.

add_executable(test_audio_engine_ffi
    src/test_audio_engine_ffi.cpp
    ${CMAKE_SOURCE_DIR}/audio-engine/src/jscs_test_plugins.cpp
)

target_link_libraries(test_audio_engine_ffi
    PRIVATE langx_audio_engine_bridge
)

target_compile_definitions(test_audio_engine_ffi PRIVATE
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
)

if(TARGET langx_synthdef_compiler_bridge)
    add_executable(test_synthdef_compiler_ffi
        src/test_synthdef_compiler_ffi.cpp
        ${CMAKE_SOURCE_DIR}/audio-engine/src/jscs_test_plugins.cpp
    )

    target_link_libraries(test_synthdef_compiler_ffi
        PRIVATE langx_synthdef_compiler_bridge
        PRIVATE langx_audio_engine_bridge
    )
endif()
