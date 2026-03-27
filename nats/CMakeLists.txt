# nats/CMakeLists.txt
# Reusable NATS library: client, dispatcher, and engine command mapping.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CNATS IMPORTED_TARGET libnats)
endif()

if(NOT CNATS_FOUND)
    # Try finding the library directly (e.g., from Homebrew)
    find_path(CNATS_INCLUDE_DIR nats/nats.h
        HINTS /opt/homebrew/include /usr/local/include)
    find_library(CNATS_LIBRARY NAMES nats nats_static
        HINTS /opt/homebrew/lib /usr/local/lib)

    if(CNATS_INCLUDE_DIR AND CNATS_LIBRARY)
        set(CNATS_FOUND TRUE)
    else()
        message(FATAL_ERROR
            "NATS C client library not found.\n"
            "Install it with: brew install cnats\n"
            "Or set CNATS_INCLUDE_DIR and CNATS_LIBRARY manually.")
    endif()
endif()

add_library(nats_lib STATIC
    src/tzpl_nats_client.cpp
    src/tzpl_nats_dispatcher.cpp
    src/tzpl_nats_engine_commands.cpp
)

target_include_directories(nats_lib
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(TARGET PkgConfig::CNATS)
    target_link_libraries(nats_lib PUBLIC PkgConfig::CNATS)
else()
    target_include_directories(nats_lib PUBLIC ${CNATS_INCLUDE_DIR})
    target_link_libraries(nats_lib PUBLIC ${CNATS_LIBRARY})
endif()

target_link_libraries(nats_lib
    PUBLIC  audio_engine_lib
)

# Match tzpl_lib optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(nats_lib PRIVATE -O1)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(nats_lib PRIVATE -O2)
endif()
