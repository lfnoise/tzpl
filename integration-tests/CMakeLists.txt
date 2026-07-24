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
    PRIVATE tzpl_audio_engine_bridge tzpl_clock_bridge
)

target_compile_definitions(test_audio_engine_ffi PRIVATE
    SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
    MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
)

add_executable(test_graph_shadow
    src/test_graph_shadow.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/tzpl_test_plugins.cpp
)

target_link_libraries(test_graph_shadow
    PRIVATE audio_engine_lib
)

if(TARGET tzpl_synthdef_compiler_bridge)
    add_executable(test_synthdef_compiler_ffi
        src/test_synthdef_compiler_ffi.cpp
        ${CMAKE_SOURCE_DIR}/engine/src/tzpl_test_plugins.cpp
    )

    target_link_libraries(test_synthdef_compiler_ffi
        PRIVATE tzpl_synthdef_compiler_bridge
        PRIVATE tzpl_audio_engine_bridge
        PRIVATE tzpl_clock_bridge
    )

    target_compile_definitions(test_synthdef_compiler_ffi PRIVATE
        SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
        LANG_MODULES_DIR="${CMAKE_SOURCE_DIR}/lang/modules"
        EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
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

if(TARGET tzpl_nats_bridge)
    add_executable(test_nats
        src/test_nats.cpp
        ${CMAKE_SOURCE_DIR}/engine/src/tzpl_test_plugins.cpp
    )

    target_link_libraries(test_nats
        PRIVATE tzpl_nats_bridge
        PRIVATE tzpl_audio_engine_bridge
        PRIVATE tzpl_clock_bridge
    )

    target_compile_definitions(test_nats PRIVATE
        SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
        MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
    )
endif()
