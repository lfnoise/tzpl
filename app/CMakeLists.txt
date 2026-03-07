# app/CMakeLists.txt
# Live coding application: langx + audio engine + synthdef compiler

add_executable(jscs
    src/main.cpp
)

target_link_libraries(jscs
    PRIVATE langx_synthdef_compiler_bridge
    PRIVATE langx_audio_engine_bridge
    PRIVATE linenoise
)

target_compile_definitions(jscs PRIVATE
    MODULES_DIR="${CMAKE_SOURCE_DIR}/bridge/modules"
)
