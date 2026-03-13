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
//  tzpl_osc_dispatcher.cpp
//  osc
//
//  Address routing and bundle handling for OSC packets.
//

#include "tzpl_osc.hpp"
#include "osc/OscReceivedElements.h"
#include "tzpl_client_interface.hpp"
#include <print>
#include <cstring>

namespace osc {

void OscDispatcher::addHandler(const std::string& address, OscHandler handler) {
    std::lock_guard lock(handlersMutex_);
    handlers_[address] = std::move(handler);
}

void OscDispatcher::removeHandler(const std::string& address) {
    std::lock_guard lock(handlersMutex_);
    handlers_.erase(address);
}

void OscDispatcher::dispatch(const char* data, int size, const SenderInfo& sender) {
    if (size < 4) return;

    // OSC bundles start with "#bundle\0", messages start with "/"
    if (data[0] == '#') {
        dispatchBundle(data, size, sender);
    } else if (data[0] == '/') {
        dispatchMessage(data, size, sender);
    }
}

void OscDispatcher::dispatchMessage(const char* data, int size, const SenderInfo& sender) {
    try {
        ::osc::ReceivedMessage msg(::osc::ReceivedPacket(data, size));
        const char* address = msg.AddressPattern();

        std::lock_guard lock(handlersMutex_);
        auto it = handlers_.find(address);
        if (it != handlers_.end()) {
            it->second(address, data, size, sender);
        }
    } catch (::osc::Exception const& e) {
        std::print(stderr, "OSC dispatch error: {}\n", e.what());
    }
}

void OscDispatcher::dispatchBundle(const char* data, int size, const SenderInfo& sender) {
    try {
        ::osc::ReceivedBundle bundle(::osc::ReceivedPacket(data, size));

        // Check for /engine/silo as first element to override silo index
        int silo = 0;
        bool hasSiloOverride = false;

        // Peek at first element
        auto it = bundle.ElementsBegin();
        if (it != bundle.ElementsEnd() && !it->IsBundle()) {
            ::osc::ReceivedMessage firstMsg(*it);
            if (std::strcmp(firstMsg.AddressPattern(), "/engine/silo") == 0) {
                auto argIt = firstMsg.ArgumentsBegin();
                silo = argIt->AsInt32();
                hasSiloOverride = true;
            }
        }

        // Convert NTP timetag to engine time
        // Timetag 0x1 means "immediately"
        ::osc::uint64 timetag = bundle.TimeTag();
        bool immediate = (timetag <= 1);

        // Begin a command bundle on the engine
        if (engine_) {
            engine::begin(engine_, silo);
        }

        // Set bundle flag so graph handlers don't auto-wrap
        bool wasBundled = tInsideBundle;
        tInsideBundle = true;

        // Dispatch all messages in the bundle
        for (auto elemIt = bundle.ElementsBegin(); elemIt != bundle.ElementsEnd(); ++elemIt) {
            if (elemIt->IsBundle()) {
                // Nested bundle -- recurse (gets its own begin/sched pair)
                dispatchBundle(elemIt->Contents(), static_cast<int>(elemIt->Size()), sender);
            } else {
                // Skip the /engine/silo message we already processed
                if (hasSiloOverride && elemIt == bundle.ElementsBegin()) continue;

                ::osc::ReceivedMessage msg(*elemIt);
                const char* address = msg.AddressPattern();

                std::lock_guard lock(handlersMutex_);
                auto handlerIt = handlers_.find(address);
                if (handlerIt != handlers_.end()) {
                    handlerIt->second(address, elemIt->Contents(),
                                      static_cast<int>(elemIt->Size()), sender);
                }
            }
        }

        // Restore bundle flag
        tInsideBundle = wasBundled;

        // Finalize the bundle
        if (engine_) {
            if (immediate) {
                engine::go();
            } else {
                // Convert NTP timetag to seconds since epoch, then to engine stream time.
                // NTP epoch is 1900-01-01, Unix epoch is 1970-01-01.
                // For now, treat the timetag as raw seconds offset from engine start.
                // A proper implementation would anchor NTP time to engine time at startup.
                double seconds = static_cast<double>(timetag >> 32) +
                                 static_cast<double>(timetag & 0xFFFFFFFF) / 4294967296.0;
                engine::sched(seconds);
            }
        }
    } catch (::osc::Exception const& e) {
        std::print(stderr, "OSC bundle error: {}\n", e.what());
    }
}

} // namespace osc
