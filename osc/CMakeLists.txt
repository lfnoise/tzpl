# osc/CMakeLists.txt
# Reusable OSC library: server, client, dispatcher, and engine command mapping.

add_library(osc_lib STATIC
    src/tzpl_osc_server.cpp
    src/tzpl_osc_client.cpp
    src/tzpl_osc_dispatcher.cpp
    src/tzpl_osc_engine_commands.cpp
)

target_include_directories(osc_lib
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(osc_lib
    PUBLIC  oscpack
    PUBLIC  audio_engine_lib
)

# Match tzpl_lib optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(osc_lib PRIVATE -O1)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(osc_lib PRIVATE -O2)
endif()
