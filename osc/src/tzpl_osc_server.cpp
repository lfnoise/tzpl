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
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mutex>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
#endif
#include <print>
#include <cstring>

namespace osc {

// Socket-API differences, kept to one place. OscSocket is the handle type
// declared in tzpl_osc.hpp (SOCKET on Windows, int elsewhere).
namespace {
#ifdef _WIN32
constexpr OscSocket kInvalidSocket = INVALID_SOCKET;
inline bool socketValid(OscSocket s) { return s != INVALID_SOCKET; }
inline void closeSocket(OscSocket s) { ::closesocket(s); }
using SockLen = int;
using RecvLen = int;
inline void ensureWinsock() {
    static std::once_flag once;
    std::call_once(once, [] { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); });
}
#else
constexpr OscSocket kInvalidSocket = -1;
inline bool socketValid(OscSocket s) { return s >= 0; }
inline void closeSocket(OscSocket s) { ::close(s); }
using SockLen = socklen_t;
using RecvLen = ssize_t;
inline void ensureWinsock() {}
#endif
} // namespace

OscServer::OscServer(OscDispatcher& dispatcher)
    : dispatcher_(dispatcher) {}

OscServer::~OscServer() {
    stop();
}

bool OscServer::start(int port) {
    if (running_.load()) return false;

    ensureWinsock();
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (!socketValid(socket_)) {
        std::print(stderr, "OSC: failed to create socket\n");
        return false;
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char const*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::print(stderr, "OSC: failed to bind to port {}\n", port);
        closeSocket(socket_);
        socket_ = kInvalidSocket;
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
    if (socketValid(socket_)) {
        closeSocket(socket_);
        socket_ = kInvalidSocket;
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
        SockLen senderLen = sizeof(senderAddr);

        RecvLen bytesRead = ::recvfrom(socket_, buffer, kMaxPacketSize, 0,
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
