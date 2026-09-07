# third_party/oscpack/CMakeLists.txt
# Vendored oscpack: Open Sound Control packet manipulation library.
# MIT license -- see LICENSE in this directory.

if(WIN32)
    set(OSCPACK_IP_SOURCES ip/win32/NetworkingUtils.cpp ip/win32/UdpSocket.cpp)
else()
    set(OSCPACK_IP_SOURCES ip/posix/NetworkingUtils.cpp ip/posix/UdpSocket.cpp)
endif()

add_library(oscpack STATIC
    osc/OscTypes.cpp
    osc/OscReceivedElements.cpp
    osc/OscOutboundPacketStream.cpp
    osc/OscPrintReceivedElements.cpp
    ip/IpEndpointName.cpp
    ${OSCPACK_IP_SOURCES}
)

target_include_directories(oscpack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(oscpack PUBLIC ws2_32 winmm)
endif()

# Suppress warnings in vendored code
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(oscpack PRIVATE /w)
else()
    target_compile_options(oscpack PRIVATE -w)
endif()
