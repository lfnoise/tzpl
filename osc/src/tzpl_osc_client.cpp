// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  tzpl_osc_client.cpp
//  osc
//
//  UDP sender for OSC messages.
//

#include "tzpl_osc.hpp"
#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"
#include "ip/IpEndpointName.h"
#include <print>

namespace osc {

void OscClient::send(const char* host, int port, const char* data, int size) {
    try {
        UdpTransmitSocket socket(IpEndpointName(host, port));
        socket.Send(data, size);
    } catch (std::exception const& e) {
        std::print(stderr, "OSC send error: {}\n", e.what());
    }
}

void OscClient::sendMessage(const char* host, int port, const char* address) {
    char buffer[256];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage(address) << ::osc::EndMessage;
    send(host, port, p.Data(), static_cast<int>(p.Size()));
}

void OscClient::sendMessageI(const char* host, int port, const char* address, int32_t value) {
    char buffer[256];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage(address) << value << ::osc::EndMessage;
    send(host, port, p.Data(), static_cast<int>(p.Size()));
}

void OscClient::sendMessageF(const char* host, int port, const char* address, float value) {
    char buffer[256];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage(address) << value << ::osc::EndMessage;
    send(host, port, p.Data(), static_cast<int>(p.Size()));
}

void OscClient::sendMessageS(const char* host, int port, const char* address, const char* value) {
    char buffer[1024];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage(address) << value << ::osc::EndMessage;
    send(host, port, p.Data(), static_cast<int>(p.Size()));
}

void OscClient::sendMessageArgs(const char* host, int port, const char* address,
                                 const float* values, int count) {
    constexpr int kMaxBuffer = 8192;
    char buffer[kMaxBuffer];
    ::osc::OutboundPacketStream p(buffer, kMaxBuffer);
    p << ::osc::BeginMessage(address);
    for (int i = 0; i < count; ++i) {
        p << values[i];
    }
    p << ::osc::EndMessage;
    send(host, port, p.Data(), static_cast<int>(p.Size()));
}

} // namespace osc
