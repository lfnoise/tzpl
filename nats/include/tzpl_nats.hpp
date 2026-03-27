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
//  tzpl_nats.hpp
//  nats
//
//  NATS messaging support: client (connect/publish/subscribe),
//  dispatcher (subject routing with engine command mapping).
//

#ifndef tzpl_nats_hpp
#define tzpl_nats_hpp

#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>

// nats.c uses typedef struct __natsXxx natsXxx, so forward-declare
// the internal structs and replicate the typedefs.
struct __natsConnection;
typedef struct __natsConnection natsConnection;
struct __natsSubscription;
typedef struct __natsSubscription natsSubscription;

namespace engine { struct Engine; }

namespace nats {

// ---------------------------------------------------------------------------
// NatsHandler -- callback for received messages
// ---------------------------------------------------------------------------

using NatsHandler = std::function<void(const char* subject, const char* data,
                                        int dataLen, const char* reply)>;

// ---------------------------------------------------------------------------
// NatsClient -- connects to a NATS server, publishes and subscribes
// ---------------------------------------------------------------------------

class NatsClient {
public:
    NatsClient();
    ~NatsClient();

    // Connect to a NATS server. url is e.g. "nats://127.0.0.1:4222".
    // Returns true on success.
    bool connect(const char* url);

    // Disconnect from the server.
    void disconnect();

    // Returns true if connected.
    bool isConnected() const;

    // Returns the URL we connected to, or "" if not connected.
    const std::string& url() const { return url_; }

    // Publish a string payload to a subject.
    bool publish(const char* subject, const char* data);

    // Publish raw bytes to a subject.
    bool publish(const char* subject, const char* data, int len);

    // Publish with a reply subject.
    bool publishRequest(const char* subject, const char* reply,
                        const char* data, int len);

    // Synchronous request/reply: publish and wait for a response (timeout in ms).
    // Returns the reply payload, or empty string on timeout/error.
    // NOTE: This blocks the calling thread. Not suitable for the VM thread.
    std::string request(const char* subject, const char* data, int len,
                        int timeoutMs = 1000);

    // Async request/reply: creates an inbox subscription, publishes the
    // request, and invokes the callback on the nats.c delivery thread when
    // the reply arrives (or "" on timeout). Non-blocking.
    using ReplyCallback = std::function<void(const char* data, int len)>;
    void asyncRequest(const char* subject, const char* data, int len,
                      int timeoutMs, ReplyCallback callback);

    // Access the underlying nats connection (for subscriptions).
    natsConnection* conn() const { return conn_; }

private:
    natsConnection* conn_ = nullptr;
    std::string url_;
};

// ---------------------------------------------------------------------------
// NatsDispatcher -- routes NATS subjects to handler functions
// ---------------------------------------------------------------------------

class NatsDispatcher {
public:
    NatsDispatcher();
    ~NatsDispatcher();

    // Register a handler for a NATS subject.
    void addHandler(const std::string& subject, NatsHandler handler);

    // Remove a handler.
    void removeHandler(const std::string& subject);

    // Subscribe to all registered subjects on the given client.
    // Must be called after addHandler and after client.connect().
    void subscribeAll(NatsClient& client);

    // Unsubscribe all active subscriptions.
    void unsubscribeAll();

    // Dispatch a message (called from the NATS callback thread).
    void dispatch(const char* subject, const char* data, int dataLen,
                  const char* reply);

    // Set the engine pointer for command dispatch.
    void setEngine(engine::Engine* engine) { engine_ = engine; }
    engine::Engine* engine() const { return engine_; }

    // Set the client for sending replies.
    void setClient(NatsClient* client) { client_ = client; }
    NatsClient* client() const { return client_; }

private:
    std::unordered_map<std::string, NatsHandler> handlers_;
    std::unordered_map<std::string, natsSubscription*> subscriptions_;
    std::mutex handlersMutex_;
    engine::Engine* engine_ = nullptr;
    NatsClient* client_ = nullptr;
};

// ---------------------------------------------------------------------------
// Engine command registration
// ---------------------------------------------------------------------------

// Register all engine.* handlers on the dispatcher.
// If engineName is non-empty, also registers under engines.{name}.* and
// engines.all.* for namespaced and broadcast addressing.
void registerEngineHandlers(NatsDispatcher& dispatcher,
                             const std::string& engineName = "");

} // namespace nats

#endif /* tzpl_nats_hpp */
