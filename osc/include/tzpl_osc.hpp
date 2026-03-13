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
//  tzpl_osc.hpp
//  osc
//
//  Open Sound Control support: server (UDP listener), client (UDP sender),
//  and dispatcher (address routing with engine command mapping).
//

#ifndef tzpl_osc_hpp
#define tzpl_osc_hpp

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <vector>

namespace engine { struct Engine; }

namespace osc {

// ---------------------------------------------------------------------------
// OscClient -- sends OSC messages over UDP
// ---------------------------------------------------------------------------

class OscClient {
public:
    // Send a raw OSC packet to the given host:port.
    void send(const char* host, int port, const char* data, int size);

    // Convenience: send a message with no args.
    void sendMessage(const char* host, int port, const char* address);

    // Convenience: send a message with a single int arg.
    void sendMessageI(const char* host, int port, const char* address, int32_t value);

    // Convenience: send a message with a single float arg.
    void sendMessageF(const char* host, int port, const char* address, float value);

    // Convenience: send a message with a single string arg.
    void sendMessageS(const char* host, int port, const char* address, const char* value);

    // Convenience: send a message with an array of floats.
    void sendMessageArgs(const char* host, int port, const char* address,
                         const float* values, int count);
};

// ---------------------------------------------------------------------------
// OscDispatcher -- routes OSC addresses to handler functions
// ---------------------------------------------------------------------------

// Handler function signature: receives address, arg data pointer, arg data size,
// and the sender's endpoint info for replies.
struct SenderInfo {
    std::string host;
    int port = 0;
};

using OscHandler = std::function<void(const char* address, const void* argData,
                                       int argSize, const SenderInfo& sender)>;

class OscDispatcher {
public:
    // Register a handler for an OSC address pattern.
    void addHandler(const std::string& address, OscHandler handler);

    // Remove a handler.
    void removeHandler(const std::string& address);

    // Dispatch a raw OSC packet (message or bundle).
    // engine is used for begin/go/sched on bundle boundaries.
    void dispatch(const char* data, int size, const SenderInfo& sender);

    // Set the engine pointer for command dispatch.
    void setEngine(engine::Engine* engine) { engine_ = engine; }
    engine::Engine* engine() const { return engine_; }

    // Set the client for sending replies.
    void setClient(OscClient* client) { client_ = client; }
    OscClient* client() const { return client_; }

private:
    void dispatchMessage(const char* data, int size, const SenderInfo& sender);
    void dispatchBundle(const char* data, int size, const SenderInfo& sender);

    std::unordered_map<std::string, OscHandler> handlers_;
    std::mutex handlersMutex_;
    engine::Engine* engine_ = nullptr;
    OscClient* client_ = nullptr;
};

// ---------------------------------------------------------------------------
// OscServer -- UDP listener thread
// ---------------------------------------------------------------------------

class OscServer {
public:
    explicit OscServer(OscDispatcher& dispatcher);
    ~OscServer();

    // Start listening on the given port. Returns true on success.
    bool start(int port);

    // Stop listening and join the listener thread.
    void stop();

    // Returns the port the server is listening on, or 0 if not running.
    int port() const { return port_; }

    // Returns true if the server is running.
    bool isRunning() const { return running_.load(); }

private:
    void listenerLoop();

    OscDispatcher& dispatcher_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    int socket_ = -1;
    int port_ = 0;
};

// ---------------------------------------------------------------------------
// Engine command registration
// ---------------------------------------------------------------------------

// Thread-local flag: true when inside a bundle dispatch.
// Graph command handlers check this to decide whether to auto-wrap in begin/go.
extern thread_local bool tInsideBundle;

// Register all /engine/* and /lang/* handlers on the dispatcher.
void registerEngineHandlers(OscDispatcher& dispatcher);

} // namespace osc

#endif /* tzpl_osc_hpp */
