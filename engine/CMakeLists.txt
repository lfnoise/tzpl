cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_PROJECT_NAME STREQUAL "tzpl")
    project(engine LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

# Require Clang (ext_vector_type swizzle syntax is Clang-only)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    message(WARNING "This project requires Clang. Current compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# --- audio_engine_lib static library (everything except main.cpp) ---

set(LIB_SOURCES
    src/tzpl_sexpr.cpp
    src/tzpl_command.cpp
    src/tzpl_audio_backend_rtaudio.cpp
    src/tzpl_client_interface.cpp
    src/tzpl_silo.cpp
    src/tzpl_engine.cpp
    src/tzpl_node.cpp
    src/tzpl_xfader.cpp
    src/tzpl_mixer.cpp
    src/tzpl_chanadapt.cpp
    src/tzpl_test_plugins.cpp
    src/tzpl_audio_file_writer.cpp
)

add_library(audio_engine_lib STATIC ${LIB_SOURCES})
set_target_properties(audio_engine_lib PROPERTIES OUTPUT_NAME audio_engine)

target_include_directories(audio_engine_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(audio_engine_lib PUBLIC shared rtaudio)
target_compile_definitions(audio_engine_lib PUBLIC HAVE_GETTIMEOFDAY=1)

if(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    target_link_libraries(audio_engine_lib PUBLIC ${AUDIOTOOLBOX_FRAMEWORK})
elseif(UNIX)
    # libsndfile backs the audio-file reader (tzpl_audio_file.hpp) that
    # AudioToolbox provides on macOS. Sleef comes in via the shared target.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
    target_link_libraries(audio_engine_lib PUBLIC PkgConfig::SNDFILE)
endif()

# --- engine executable ---

add_executable(engine src/main.cpp)
target_link_libraries(engine PRIVATE audio_engine_lib)

# --- install targets ---

include(GNUInstallDirs)

install(TARGETS audio_engine_lib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    src/tzpl_audio_backend.hpp
    src/tzpl_audio_backend_rtaudio.hpp
    src/tzpl_client_interface.hpp
    src/tzpl_node.hpp
    src/tzpl_engine.hpp
    src/tzpl_common.hpp
    src/tzpl_hash.hpp
    src/tzpl_sexpr.hpp
    src/tzpl_command.hpp
    src/tzpl_command_subclasses.hpp
    src/tzpl_atomic_fifo.hpp
    src/tzpl_random.hpp
    src/tzpl_complex.hpp
    src/tzpl_silo.hpp
    src/tzpl_xfader.hpp
    src/tzpl_mixer.hpp
    src/tzpl_test_plugins.hpp
    src/tzpl_audio_file_writer.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tzpl
)
install(TARGETS engine
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
