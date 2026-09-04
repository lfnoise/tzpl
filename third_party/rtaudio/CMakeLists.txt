add_library(rtaudio STATIC RtAudio.cpp RtAudio.h)
target_include_directories(rtaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(APPLE)
    target_compile_definitions(rtaudio PUBLIC __MACOSX_CORE__)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(rtaudio PUBLIC
        ${COREAUDIO_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(rtaudio PUBLIC __LINUX_ALSA__)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    target_include_directories(rtaudio PUBLIC ${ALSA_INCLUDE_DIRS})
    target_link_libraries(rtaudio PUBLIC
        ${ALSA_LIBRARIES}
        pthread
    )
    # Optional backends, compiled in when the dev packages are present.
    # The engine opens RtAudio with UNSPECIFIED on Linux, which picks the
    # first compiled-in API that reports devices (JACK, then Pulse, then ALSA).
    pkg_check_modules(JACK jack)
    if(JACK_FOUND)
        target_compile_definitions(rtaudio PUBLIC __UNIX_JACK__)
        target_include_directories(rtaudio PUBLIC ${JACK_INCLUDE_DIRS})
        target_link_libraries(rtaudio PUBLIC ${JACK_LIBRARIES})
    endif()
    pkg_check_modules(PULSE libpulse-simple)
    if(PULSE_FOUND)
        target_compile_definitions(rtaudio PUBLIC __LINUX_PULSE__)
        target_include_directories(rtaudio PUBLIC ${PULSE_INCLUDE_DIRS})
        target_link_libraries(rtaudio PUBLIC ${PULSE_LIBRARIES})
    endif()
endif()
