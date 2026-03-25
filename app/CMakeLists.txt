# app/CMakeLists.txt
# Live coding application: tzpl + audio engine + synthdef compiler + OSC

add_executable(tzpl_app
    src/main.cpp
)

target_link_libraries(tzpl_app
    PRIVATE tzpl_synthdef_compiler_bridge
    PRIVATE tzpl_audio_engine_bridge
    PRIVATE tzpl_clock_bridge
    PRIVATE linenoise
)

if(TARGET tzpl_osc_bridge)
    target_link_libraries(tzpl_app PRIVATE tzpl_osc_bridge)
    target_compile_definitions(tzpl_app PRIVATE TZPL_HAS_OSC=1)
endif()

target_compile_definitions(tzpl_app PRIVATE
    MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
)
