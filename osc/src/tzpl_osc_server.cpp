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
//  tzpl_osc_server.cpp
//  osc
//
//  UDP listener thread for receiving OSC packets.
//

#include "tzpl_osc.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <print>
#include <cstring>

namespace osc {

OscServer::OscServer(OscDispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

OscServer::~OscServer() {
    stop();
}

bool OscServer::start(int port) {
    if (running_.load()) return false;

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        std::print(stderr, "OSC: failed to create socket\n");
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::print(stderr, "OSC: failed to bind to port {}\n", port);
        ::close(socket_);
        socket_ = -1;
        return false;
    }

    port_ = port;
    running_.store(true);
    thread_ = std::thread(&OscServer::listenerLoop, this);

    std::print("OSC: listening on port {}\n", port);
    return true;
}

void OscServer::stop() {
    if (!running_.load()) return;

    running_.store(false);

    // Close socket to unblock recvfrom
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    port_ = 0;
    std::print("OSC: server stopped\n");
}

void OscServer::listenerLoop() {
    constexpr int kMaxPacketSize = 65536;
    char buffer[kMaxPacketSize];

    while (running_.load()) {
        sockaddr_in senderAddr{};
        socklen_t senderLen = sizeof(senderAddr);

        ssize_t bytesRead = ::recvfrom(socket_, buffer, kMaxPacketSize, 0,
                                        reinterpret_cast<sockaddr*>(&senderAddr),
                                        &senderLen);

        if (bytesRead <= 0) {
            if (!running_.load()) break;  // Socket closed for shutdown
            continue;
        }

        // Build sender info for replies
        char hostBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &senderAddr.sin_addr, hostBuf, sizeof(hostBuf));

        SenderInfo sender;
        sender.host = hostBuf;
        sender.port = ntohs(senderAddr.sin_port);

        dispatcher_.dispatch(buffer, static_cast<int>(bytesRead), sender);
    }
}

} // namespace osc
