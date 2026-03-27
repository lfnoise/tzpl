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
//  tzpl_nats_dispatcher.cpp
//  nats
//
//  Subject routing for NATS messages: maps subjects to handler functions,
//  manages subscriptions on the NATS connection.
//

#include "tzpl_nats.hpp"
#include <nats/nats.h>
#include <print>

namespace nats {

NatsDispatcher::NatsDispatcher() = default;

NatsDispatcher::~NatsDispatcher() {
    unsubscribeAll();
}

void NatsDispatcher::addHandler(const std::string& subject, NatsHandler handler) {
    std::lock_guard lock(handlersMutex_);
    handlers_[subject] = std::move(handler);
}

void NatsDispatcher::removeHandler(const std::string& subject) {
    std::lock_guard lock(handlersMutex_);
    handlers_.erase(subject);

    // Also unsubscribe from NATS if we have an active subscription
    auto sit = subscriptions_.find(subject);
    if (sit != subscriptions_.end()) {
        natsSubscription_Unsubscribe(sit->second);
        natsSubscription_Destroy(sit->second);
        subscriptions_.erase(sit);
    }
}

// Static callback invoked by nats.c on its delivery thread.
// The closure is the NatsDispatcher pointer.
static void onMessage(natsConnection*, natsSubscription*, natsMsg* msg, void* closure) {
    auto* dispatcher = static_cast<NatsDispatcher*>(closure);
    const char* subject = natsMsg_GetSubject(msg);
    const char* data = natsMsg_GetData(msg);
    int dataLen = natsMsg_GetDataLength(msg);
    const char* reply = natsMsg_GetReply(msg);

    dispatcher->dispatch(subject, data, dataLen, reply);

    natsMsg_Destroy(msg);
}

void NatsDispatcher::subscribeAll(NatsClient& client) {
    if (!client.conn()) return;

    std::lock_guard lock(handlersMutex_);
    for (auto& [subject, handler] : handlers_) {
        // Skip if already subscribed
        if (subscriptions_.count(subject)) continue;

        natsSubscription* sub = nullptr;
        natsStatus s = natsConnection_Subscribe(&sub, client.conn(),
                                                 subject.c_str(),
                                                 nats::onMessage, this);
        if (s == NATS_OK && sub) {
            subscriptions_[subject] = sub;
        } else {
            std::print(stderr, "NATS: failed to subscribe to '{}': {}\n",
                       subject, natsStatus_GetText(s));
        }
    }
}

void NatsDispatcher::unsubscribeAll() {
    for (auto& [subject, sub] : subscriptions_) {
        natsSubscription_Unsubscribe(sub);
        natsSubscription_Destroy(sub);
    }
    subscriptions_.clear();
}

void NatsDispatcher::dispatch(const char* subject, const char* data,
                               int dataLen, const char* reply) {
    std::lock_guard lock(handlersMutex_);
    auto it = handlers_.find(subject);
    if (it != handlers_.end()) {
        it->second(subject, data, dataLen, reply);
    }
}

} // namespace nats
