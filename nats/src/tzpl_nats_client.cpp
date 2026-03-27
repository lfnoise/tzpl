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
//  tzpl_nats_client.cpp
//  nats
//
//  NATS client: connection management, publish, and request/reply.
//

#include "tzpl_nats.hpp"
#include <nats/nats.h>
#include <print>

namespace nats {

NatsClient::NatsClient() = default;

NatsClient::~NatsClient() {
    disconnect();
}

bool NatsClient::connect(const char* url) {
    if (conn_) disconnect();

    natsStatus s = natsConnection_ConnectTo(&conn_, url);
    if (s != NATS_OK) {
        std::print(stderr, "NATS: failed to connect to {}: {}\n",
                   url, natsStatus_GetText(s));
        conn_ = nullptr;
        return false;
    }

    url_ = url;
    std::print("NATS: connected to {}\n", url);
    return true;
}

void NatsClient::disconnect() {
    if (!conn_) return;

    natsConnection_Close(conn_);
    natsConnection_Destroy(conn_);
    conn_ = nullptr;
    std::print("NATS: disconnected\n");
    url_.clear();
}

bool NatsClient::isConnected() const {
    if (!conn_) return false;
    return natsConnection_Status(conn_) == NATS_CONN_STATUS_CONNECTED;
}

bool NatsClient::publish(const char* subject, const char* data) {
    if (!conn_) return false;
    natsStatus s = natsConnection_PublishString(conn_, subject, data);
    if (s != NATS_OK) {
        std::print(stderr, "NATS publish error: {}\n", natsStatus_GetText(s));
        return false;
    }
    return true;
}

bool NatsClient::publish(const char* subject, const char* data, int len) {
    if (!conn_) return false;
    natsStatus s = natsConnection_Publish(conn_, subject, data, len);
    if (s != NATS_OK) {
        std::print(stderr, "NATS publish error: {}\n", natsStatus_GetText(s));
        return false;
    }
    return true;
}

bool NatsClient::publishRequest(const char* subject, const char* reply,
                                 const char* data, int len) {
    if (!conn_) return false;
    natsStatus s = natsConnection_PublishRequest(conn_, subject, reply, data, len);
    if (s != NATS_OK) {
        std::print(stderr, "NATS publish request error: {}\n",
                   natsStatus_GetText(s));
        return false;
    }
    return true;
}

std::string NatsClient::request(const char* subject, const char* data, int len,
                                 int timeoutMs) {
    if (!conn_) return "";

    natsMsg* reply = nullptr;
    natsStatus s = natsConnection_Request(&reply, conn_, subject, data, len,
                                           static_cast<int64_t>(timeoutMs));
    if (s != NATS_OK || !reply) {
        return "";
    }

    std::string result(natsMsg_GetData(reply), natsMsg_GetDataLength(reply));
    natsMsg_Destroy(reply);
    return result;
}

// Closure passed through the nats subscription callback void* pointer.
struct AsyncRequestCtx {
    NatsClient::ReplyCallback callback;
};

static void asyncReplyHandler(natsConnection*, natsSubscription*,
                               natsMsg* msg, void* closure) {
    auto* ctx = static_cast<AsyncRequestCtx*>(closure);
    const char* data = natsMsg_GetData(msg);
    int len = natsMsg_GetDataLength(msg);
    ctx->callback(data, len);
    natsMsg_Destroy(msg);
    // Sub auto-unsubscribes after 1 message; clean up the context.
    delete ctx;
}

void NatsClient::asyncRequest(const char* subject, const char* data, int len,
                                int /*timeoutMs*/, ReplyCallback callback) {
    if (!conn_) {
        callback("", 0);
        return;
    }

    // Create a unique inbox subject for this request.
    natsInbox* inbox = nullptr;
    natsStatus s = natsInbox_Create(&inbox);
    if (s != NATS_OK) {
        callback("", 0);
        return;
    }

    // Subscribe to the inbox. The callback fires on the nats.c delivery thread.
    auto* ctx = new AsyncRequestCtx{std::move(callback)};
    natsSubscription* sub = nullptr;
    s = natsConnection_Subscribe(&sub, conn_, inbox, asyncReplyHandler, ctx);
    if (s != NATS_OK || !sub) {
        delete ctx;
        natsInbox_Destroy(inbox);
        return;
    }

    // Auto-unsubscribe after receiving 1 message.
    natsSubscription_AutoUnsubscribe(sub, 1);

    // Publish the request with the inbox as reply subject.
    s = natsConnection_PublishRequest(conn_, subject, inbox, data, len);
    natsInbox_Destroy(inbox);

    if (s != NATS_OK) {
        natsSubscription_Unsubscribe(sub);
        natsSubscription_Destroy(sub);
    }
}

} // namespace nats
