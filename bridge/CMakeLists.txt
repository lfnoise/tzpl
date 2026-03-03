# bridge/CMakeLists.txt
# FFI bridge libraries connecting Language X to the audio-engine and synthdef-compiler.

add_library(langx_audio_engine_bridge OBJECT
    src/langx_audio_engine_ffi.cpp
)

target_include_directories(langx_audio_engine_bridge
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(langx_audio_engine_bridge
    PUBLIC  langx_lib
    PUBLIC  audio_engine_lib
)

# Match langx_lib optimization flags to avoid ODR/inline mismatches
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(langx_audio_engine_bridge PRIVATE -O1)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(langx_audio_engine_bridge PRIVATE -O2)
endif()