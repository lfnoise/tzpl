# integration-tests/CMakeLists.txt
# Integration tests for FFI bridges.

add_executable(test_audio_engine_ffi
    src/test_audio_engine_ffi.cpp
)

target_link_libraries(test_audio_engine_ffi
    PRIVATE langx_audio_engine_bridge
)
