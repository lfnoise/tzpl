# integration-tests/CMakeLists.txt
# Integration tests for FFI bridges.

add_executable(test_foreign_modules
    src/test_foreign_modules.cpp
)

target_link_libraries(test_foreign_modules
    PRIVATE tzpl_lib
)

add_executable(test_audio_engine_ffi
    src/test_audio_engine_ffi.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/tzpl_test_plugins.cpp
)

target_link_libraries(test_audio_engine_ffi
    PRIVATE tzpl_audio_engine_bridge
)

target_compile_definitions(test_audio_engine_ffi PRIVATE
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
)

if(TARGET tzpl_synthdef_compiler_bridge)
    add_executable(test_synthdef_compiler_ffi
        src/test_synthdef_compiler_ffi.cpp
        ${CMAKE_SOURCE_DIR}/engine/src/tzpl_test_plugins.cpp
    )

    target_link_libraries(test_synthdef_compiler_ffi
        PRIVATE tzpl_synthdef_compiler_bridge
        PRIVATE tzpl_audio_engine_bridge
    )

    target_compile_definitions(test_synthdef_compiler_ffi PRIVATE
        MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
    )
endif()

if(TARGET tzpl_osc_bridge)
    add_executable(test_osc
        src/test_osc.cpp
        ${CMAKE_SOURCE_DIR}/engine/src/tzpl_test_plugins.cpp
    )

    target_link_libraries(test_osc
        PRIVATE tzpl_osc_bridge
        PRIVATE tzpl_audio_engine_bridge
        PRIVATE tzpl_clock_bridge
    )

    target_compile_definitions(test_osc PRIVATE
        SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
    )
endif()
