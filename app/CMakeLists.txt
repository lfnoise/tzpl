# app/CMakeLists.txt
# Live coding application: tzpl + audio engine + synthdef compiler

add_executable(tzpl_app
    src/main.cpp
)

target_link_libraries(tzpl_app
    PRIVATE tzpl_synthdef_compiler_bridge
    PRIVATE tzpl_audio_engine_bridge
    PRIVATE linenoise
)

target_compile_definitions(tzpl_app PRIVATE
    MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
)
