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
//  tzpl_osc_engine_commands.cpp
//  osc
//
//  Maps OSC addresses to engine:: client interface calls.
//  Graph commands received as single (non-bundled) messages are
//  auto-wrapped in begin()/go(0).
//

#include "tzpl_osc.hpp"
#include "osc/OscReceivedElements.h"
#include "osc/OscOutboundPacketStream.h"
#include "tzpl_client_interface.hpp"
#include <print>
#include <cstring>
#include <vector>

namespace osc {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Defined here, declared extern in tzpl_osc.hpp.
// When false, graph commands are auto-wrapped in begin()/go(0).
thread_local bool tInsideBundle = false;

static void autoBegin(engine::Engine* eng) {
    if (!tInsideBundle) {
        engine::begin(eng);
    }
}

static void autoGo() {
    if (!tInsideBundle) {
        engine::go(0);
    }
}

// ---------------------------------------------------------------------------
// Engine lifecycle handlers (non-bundled, immediate)
// ---------------------------------------------------------------------------

static void handleStartAudio(const char*, const void*, int,
                              const SenderInfo&, OscDispatcher& d) {
    engine::startAudio(d.engine());
}

static void handleStopAudio(const char*, const void*, int,
                             const SenderInfo&, OscDispatcher& d) {
    engine::stopAudio(d.engine());
}

static void handleMasterGain(const char*, const void* data, int size,
                              const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    float gain = arg->AsFloat();
    engine::masterGain(d.engine(), gain);
}

static void handleSafetyLimiter(const char*, const void* data, int size,
                                 const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    int onoff = arg->AsInt32();
    engine::safetyLimiter(d.engine(), onoff ? engine::kOn : engine::kOff);
}

static void handleLoadDefs(const char*, const void* data, int size,
                            const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    const char* path = arg->AsString();
    engine::loadDefs(d.engine(), path);
}

static void handleLoadDef(const char*, const void* data, int size,
                           const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    const char* path = arg->AsString(); ++arg;
    const char* name = arg->AsString();
    engine::loadDef(d.engine(), path, name);
}

// ---------------------------------------------------------------------------
// Query handlers (reply sent to sender)
// ---------------------------------------------------------------------------

static void handleGetStreamTime(const char*, const void*, int,
                                 const SenderInfo& sender, OscDispatcher& d) {
    double t = engine::getStreamTime(d.engine());
    if (d.client()) {
        d.client()->sendMessageF(sender.host.c_str(), sender.port,
                                  "/reply/getStreamTime", static_cast<float>(t));
    }
}

static void handleListNodeDefs(const char*, const void*, int,
                                const SenderInfo& sender, OscDispatcher& d) {
    std::vector<std::string> names;
    engine::listNodeDefs(d.engine(), names);
    if (d.client()) {
        // Send each name as a separate string arg
        constexpr int kMaxBuf = 8192;
        char buffer[kMaxBuf];
        ::osc::OutboundPacketStream p(buffer, kMaxBuf);
        p << ::osc::BeginMessage("/reply/listNodeDefs");
        for (auto const& name : names) {
            p << name.c_str();
        }
        p << ::osc::EndMessage;
        d.client()->send(sender.host.c_str(), sender.port,
                          p.Data(), static_cast<int>(p.Size()));
    }
}

static void handleIsAudioRunning(const char*, const void*, int,
                                  const SenderInfo& sender, OscDispatcher& d) {
    bool running = engine::isAudioRunning(d.engine());
    if (d.client()) {
        d.client()->sendMessageI(sender.host.c_str(), sender.port,
                                  "/reply/isAudioRunning", running ? 1 : 0);
    }
}

// ---------------------------------------------------------------------------
// Graph command handlers (auto-bundled)
// ---------------------------------------------------------------------------

static void handleNewNode(const char*, const void* data, int size,
                           const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    const char* defName = arg->AsString(); ++arg;
    auto nodeID = static_cast<engine::i64>(arg->AsInt32());

    autoBegin(d.engine());
    engine::newNode(defName, nodeID);
    autoGo();
}

static void handleFreeNode(const char*, const void* data, int size,
                            const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32());

    autoBegin(d.engine());
    engine::freeNode(nodeID);
    autoGo();
}

static void handleFreeAllNodes(const char*, const void*, int,
                                const SenderInfo&, OscDispatcher& d) {
    autoBegin(d.engine());
    engine::freeAllNodes();
    autoGo();
}

static void handleReplaceNode(const char*, const void* data, int size,
                               const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto oldID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    auto newID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    float xfade = arg->AsFloat(); ++arg;
    auto curve = static_cast<engine::FadeCurve>(arg->AsInt32());

    autoBegin(d.engine());
    engine::replaceNode(oldID, newID, xfade, curve);
    autoGo();
}

static void handleConnect(const char*, const void* data, int size,
                           const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr src{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    src.index = arg->AsInt32(); ++arg;
    engine::PortAddr dst{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    dst.index = arg->AsInt32();

    autoBegin(d.engine());
    engine::connect(src, dst);
    autoGo();
}

static void handleConnectX(const char*, const void* data, int size,
                            const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr src{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    src.index = arg->AsInt32(); ++arg;
    engine::PortAddr dst{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    dst.index = arg->AsInt32(); ++arg;
    float xfade = arg->AsFloat(); ++arg;
    auto curve = static_cast<engine::FadeCurve>(arg->AsInt32());

    autoBegin(d.engine());
    engine::connect(src, dst, xfade, curve);
    autoGo();
}

static void handleDisconnectInput(const char*, const void* data, int size,
                                   const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr dst{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    dst.index = arg->AsInt32();

    autoBegin(d.engine());
    engine::disconnectInput(dst);
    autoGo();
}

static void handleDisconnectInputX(const char*, const void* data, int size,
                                    const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr dst{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    dst.index = arg->AsInt32(); ++arg;
    float xfade = arg->AsFloat(); ++arg;
    auto curve = static_cast<engine::FadeCurve>(arg->AsInt32());

    autoBegin(d.engine());
    engine::disconnectInput(dst, xfade, curve);
    autoGo();
}

static void handleDisconnectOutput(const char*, const void* data, int size,
                                    const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr src{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    src.index = arg->AsInt32();

    autoBegin(d.engine());
    engine::disconnectOutput(src);
    autoGo();
}

static void handleDisconnectNode(const char*, const void* data, int size,
                                  const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32());

    autoBegin(d.engine());
    engine::disconnectNode(nodeID);
    autoGo();
}

static void handleReconnectOutput(const char*, const void* data, int size,
                                   const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr oldSrc{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    oldSrc.index = arg->AsInt32(); ++arg;
    engine::PortAddr newSrc{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    newSrc.index = arg->AsInt32(); ++arg;
    float xfade = arg->AsFloat(); ++arg;
    auto curve = static_cast<engine::FadeCurve>(arg->AsInt32());

    autoBegin(d.engine());
    engine::reconnectOutput(oldSrc, newSrc, xfade, curve);
    autoGo();
}

static void handleSetInput(const char*, const void* data, int size,
                            const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr port{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    port.index = arg->AsInt32(); ++arg;
    engine::f32 val = arg->AsFloat();

    autoBegin(d.engine());
    engine::setInput(port, 1, &val);
    autoGo();
}

static void handleSetInputX(const char*, const void* data, int size,
                             const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    engine::PortAddr port{static_cast<engine::i64>(arg->AsInt32()), 0}; ++arg;
    port.index = arg->AsInt32(); ++arg;
    engine::f32 val = arg->AsFloat(); ++arg;
    float xfade = arg->AsFloat(); ++arg;
    auto curve = static_cast<engine::FadeCurve>(arg->AsInt32());

    autoBegin(d.engine());
    engine::setInput(port, 1, &val, xfade, curve);
    autoGo();
}

// The control argument is either an int32 controlID or a string control
// name (resolved against the node's def at bundle submit).
static void handleSetControl(const char*, const void* data, int size,
                              const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    if (arg->IsString()) {
        const char* controlName = arg->AsString(); ++arg;
        engine::f32 val = arg->AsFloat();

        autoBegin(d.engine());
        engine::setControl(nodeID, controlName, 1, &val);
        autoGo();
        return;
    }
    auto controlID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    engine::f32 val = arg->AsFloat();

    autoBegin(d.engine());
    engine::setControl(nodeID, controlID, 1, &val);
    autoGo();
}

static void handleNoteOn(const char*, const void* data, int size,
                          const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    int noteID = arg->AsInt32(); ++arg;

    // Remaining args are float params
    std::vector<engine::f32> params;
    while (arg != msg.ArgumentsEnd()) {
        params.push_back(arg->AsFloat());
        ++arg;
    }

    autoBegin(d.engine());
    engine::noteOn(nodeID, noteID, static_cast<int>(params.size()),
                    params.empty() ? nullptr : params.data());
    autoGo();
}

static void handleNoteOff(const char*, const void* data, int size,
                           const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    int noteID = arg->AsInt32();

    autoBegin(d.engine());
    engine::noteOff(nodeID, noteID);
    autoGo();
}

static void handleAllNotesOff(const char*, const void* data, int size,
                               const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32());

    autoBegin(d.engine());
    engine::allNotesOff(nodeID);
    autoGo();
}

static void handleNoteSetParams(const char*, const void* data, int size,
                                 const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    int noteID = arg->AsInt32(); ++arg;
    int first = arg->AsInt32(); ++arg;

    std::vector<engine::f32> values;
    while (arg != msg.ArgumentsEnd()) {
        values.push_back(arg->AsFloat());
        ++arg;
    }

    autoBegin(d.engine());
    engine::noteSetParamRange(nodeID, noteID, first,
                               static_cast<int>(values.size()),
                               values.empty() ? nullptr : values.data());
    autoGo();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerEngineHandlers(OscDispatcher& dispatcher) {
    // Wrap each handler to capture the dispatcher reference.
    auto wrap = [&](auto fn) -> OscHandler {
        return [&dispatcher, fn](const char* addr, const void* data,
                                  int size, const SenderInfo& sender) {
            fn(addr, data, size, sender, dispatcher);
        };
    };

    // Set the bundle flag for the dispatcher's bundle dispatch.
    // The dispatcher sets tInsideBundle before dispatching bundle contents.

    // Engine lifecycle (non-bundled, immediate)
    dispatcher.addHandler("/engine/startAudio",    wrap(handleStartAudio));
    dispatcher.addHandler("/engine/stopAudio",     wrap(handleStopAudio));
    dispatcher.addHandler("/engine/masterGain",    wrap(handleMasterGain));
    dispatcher.addHandler("/engine/safetyLimiter", wrap(handleSafetyLimiter));
    dispatcher.addHandler("/engine/loadDefs",      wrap(handleLoadDefs));
    dispatcher.addHandler("/engine/loadDef",       wrap(handleLoadDef));

    // Queries
    dispatcher.addHandler("/engine/getStreamTime",   wrap(handleGetStreamTime));
    dispatcher.addHandler("/engine/listNodeDefs",    wrap(handleListNodeDefs));
    dispatcher.addHandler("/engine/isAudioRunning",  wrap(handleIsAudioRunning));

    // Graph commands (auto-bundled)
    dispatcher.addHandler("/engine/newNode",          wrap(handleNewNode));
    dispatcher.addHandler("/engine/freeNode",         wrap(handleFreeNode));
    dispatcher.addHandler("/engine/freeAllNodes",     wrap(handleFreeAllNodes));
    dispatcher.addHandler("/engine/replaceNode",      wrap(handleReplaceNode));
    dispatcher.addHandler("/engine/connect",          wrap(handleConnect));
    dispatcher.addHandler("/engine/connectX",         wrap(handleConnectX));
    dispatcher.addHandler("/engine/disconnectInput",  wrap(handleDisconnectInput));
    dispatcher.addHandler("/engine/disconnectInputX", wrap(handleDisconnectInputX));
    dispatcher.addHandler("/engine/disconnectOutput", wrap(handleDisconnectOutput));
    dispatcher.addHandler("/engine/disconnectNode",   wrap(handleDisconnectNode));
    dispatcher.addHandler("/engine/reconnectOutput",  wrap(handleReconnectOutput));
    dispatcher.addHandler("/engine/setInput",         wrap(handleSetInput));
    dispatcher.addHandler("/engine/setInputX",        wrap(handleSetInputX));
    dispatcher.addHandler("/engine/setControl",       wrap(handleSetControl));
    dispatcher.addHandler("/engine/noteOn",           wrap(handleNoteOn));
    dispatcher.addHandler("/engine/noteOff",          wrap(handleNoteOff));
    dispatcher.addHandler("/engine/allNotesOff",      wrap(handleAllNotesOff));
    dispatcher.addHandler("/engine/noteSetParams",    wrap(handleNoteSetParams));
}

} // namespace osc
