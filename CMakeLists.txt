cmake_minimum_required(VERSION 3.21)
project(jscs LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party/rtaudio)
add_subdirectory(shared)
add_subdirectory(static-lang-3)
add_subdirectory(audio-engine)
add_subdirectory(synthdef-compiler)
