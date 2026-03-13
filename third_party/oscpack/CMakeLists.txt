# third_party/oscpack/CMakeLists.txt
# Vendored oscpack: Open Sound Control packet manipulation library.
# MIT license -- see LICENSE in this directory.

add_library(oscpack STATIC
    osc/OscTypes.cpp
    osc/OscReceivedElements.cpp
    osc/OscOutboundPacketStream.cpp
    osc/OscPrintReceivedElements.cpp
    ip/IpEndpointName.cpp
    ip/posix/NetworkingUtils.cpp
    ip/posix/UdpSocket.cpp
)

target_include_directories(oscpack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Suppress warnings in vendored code
target_compile_options(oscpack PRIVATE -w)
