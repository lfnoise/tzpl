# nats/CMakeLists.txt
# Reusable NATS library: client, dispatcher, and engine command mapping.

# Prefer linking the NATS client and its OpenSSL dependency statically:
# distributed binaries (the `dist` DMG) must not reference Homebrew dylib
# paths that don't exist on a user's machine.
if(APPLE)
    set(_tzpl_nats_static_default ON)
else()
    set(_tzpl_nats_static_default OFF)
endif()
option(TZPL_NATS_STATIC "Link the NATS client and OpenSSL statically"
       ${_tzpl_nats_static_default})

set(CNATS_STATIC_FOUND FALSE)
if(TZPL_NATS_STATIC)
    find_path(CNATS_INCLUDE_DIR nats/nats.h
        HINTS /opt/homebrew/include /usr/local/include)
    find_library(CNATS_STATIC_LIBRARY NAMES libnats_static.a nats_static
        HINTS /opt/homebrew/lib /usr/local/lib)
    find_library(TZPL_SSL_STATIC_LIBRARY NAMES libssl.a
        HINTS /opt/homebrew/lib /opt/homebrew/opt/openssl@3/lib
              /usr/local/lib /usr/local/opt/openssl@3/lib)
    find_library(TZPL_CRYPTO_STATIC_LIBRARY NAMES libcrypto.a
        HINTS /opt/homebrew/lib /opt/homebrew/opt/openssl@3/lib
              /usr/local/lib /usr/local/opt/openssl@3/lib)
    if(CNATS_INCLUDE_DIR AND CNATS_STATIC_LIBRARY
       AND TZPL_SSL_STATIC_LIBRARY AND TZPL_CRYPTO_STATIC_LIBRARY)
        set(CNATS_STATIC_FOUND TRUE)
    else()
        message(STATUS
            "TZPL_NATS_STATIC: static cnats/OpenSSL not found; "
            "falling back to dynamic linking")
    endif()
endif()

if(NOT CNATS_STATIC_FOUND)
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
endif()

add_library(nats_lib STATIC
    src/tzpl_nats_client.cpp
    src/tzpl_nats_dispatcher.cpp
    src/tzpl_nats_engine_commands.cpp
)

target_include_directories(nats_lib
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CNATS_STATIC_FOUND)
    target_include_directories(nats_lib PUBLIC ${CNATS_INCLUDE_DIR})
    target_link_libraries(nats_lib PUBLIC
        ${CNATS_STATIC_LIBRARY}
        ${TZPL_SSL_STATIC_LIBRARY}
        ${TZPL_CRYPTO_STATIC_LIBRARY}
    )
elseif(TARGET PkgConfig::CNATS)
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
