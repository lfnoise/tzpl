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
#include <string>
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

// Input values. Every form takes the node, then the input as an int32 port
// index or a string name (resolved against the node's def at bundle submit),
// then its own arguments; trailing floats are the values, broadcast
// cyclically over the addressed channels.
//   /engine/setInput       node port value...
//   /engine/setInputAt     node port first:int32 value...
//   /engine/setInputRamp   node port rampTime:float curve:int32 value...
//   /engine/setInputX      node port value xfadeTime:float curve:int32
//   /engine/impulseInput   node port value...
//   /engine/impulseInputAt node port first:int32 value...

namespace {
struct InputTarget {
    engine::i64 nodeID = 0;
    int port = 0;
    std::string name;   // non-empty: address by name
};

using ArgIt = ::osc::ReceivedMessage::const_iterator;

InputTarget readInputTarget(ArgIt& arg) {
    InputTarget t;
    t.nodeID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    if (arg->IsString()) t.name = arg->AsString();
    else t.port = arg->AsInt32();
    ++arg;
    return t;
}

std::vector<engine::f64> readValues(ArgIt arg, ArgIt end) {
    std::vector<engine::f64> vals;
    for (; arg != end; ++arg) vals.push_back(arg->AsFloat());
    return vals;
}

void sendSetInput(OscDispatcher& d, InputTarget const& t, engine::Region const& region,
                  std::vector<engine::f64> const& vals, engine::f64 time = 0.,
                  engine::FadeCurve curve = engine::fadeLinear) {
    autoBegin(d.engine());
    if (t.name.empty()) engine::setInput({t.nodeID, t.port}, region, vals, time, curve);
    else engine::setInput(t.nodeID, t.name.c_str(), region, vals, time, curve);
    autoGo();
}

void sendImpulse(OscDispatcher& d, InputTarget const& t, engine::Region const& region,
                 std::vector<engine::f64> const& vals) {
    autoBegin(d.engine());
    if (t.name.empty()) engine::impulseInput({t.nodeID, t.port}, region, vals);
    else engine::impulseInput(t.nodeID, t.name.c_str(), region, vals);
    autoGo();
}
} // namespace

static void handleSetInput(const char*, const void* data, int size,
                            const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    InputTarget t = readInputTarget(arg);
    sendSetInput(d, t, engine::Region::all(), readValues(arg, msg.ArgumentsEnd()));
}

static void handleSetInputAt(const char*, const void* data, int size,
                              const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    InputTarget t = readInputTarget(arg);
    int first = arg->AsInt32(); ++arg;
    std::vector<engine::f64> vals = readValues(arg, msg.ArgumentsEnd());
    sendSetInput(d, t, engine::Region::channels(first, (int)vals.size()), vals);
}

static void handleSetInputRamp(const char*, const void* data, int size,
                                const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    InputTarget t = readInputTarget(arg);
    engine::f64 ramp = arg->AsFloat(); ++arg;
    auto curve = static_cast<engine::FadeCurve>(arg->AsInt32()); ++arg;
    sendSetInput(d, t, engine::Region::all(), readValues(arg, msg.ArgumentsEnd()), ramp, curve);
}

static void handleSetInputX(const char*, const void* data, int size,
                             const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    InputTarget t = readInputTarget(arg);
    std::vector<engine::f64> vals{arg->AsFloat()}; ++arg;
    engine::f64 xfade = arg->AsFloat(); ++arg;
    auto curve = static_cast<engine::FadeCurve>(arg->AsInt32());

    autoBegin(d.engine());
    if (t.name.empty())
        engine::setInputX({t.nodeID, t.port}, engine::Region::all(), vals, xfade, curve);
    else
        engine::setInputX(t.nodeID, t.name.c_str(), engine::Region::all(), vals, xfade, curve);
    autoGo();
}

static void handleImpulseInput(const char*, const void* data, int size,
                                const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    InputTarget t = readInputTarget(arg);
    sendImpulse(d, t, engine::Region::all(), readValues(arg, msg.ArgumentsEnd()));
}

static void handleImpulseInputAt(const char*, const void* data, int size,
                                  const SenderInfo&, OscDispatcher& d) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    InputTarget t = readInputTarget(arg);
    int first = arg->AsInt32(); ++arg;
    std::vector<engine::f64> vals = readValues(arg, msg.ArgumentsEnd());
    sendImpulse(d, t, engine::Region::channels(first, (int)vals.size()), vals);
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

// /engine/noteSetParams     node noteID first value...
// /engine/noteSetParamsRamp node noteID first rampTime:float value...
static void noteSetParamsImpl(const void* data, int size, OscDispatcher& d, bool ramped) {
    ::osc::ReceivedMessage msg(::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    auto nodeID = static_cast<engine::i64>(arg->AsInt32()); ++arg;
    int noteID = arg->AsInt32(); ++arg;
    int first = arg->AsInt32(); ++arg;
    engine::f64 ramp = 0.;
    if (ramped) { ramp = arg->AsFloat(); ++arg; }

    std::vector<engine::f32> values;
    while (arg != msg.ArgumentsEnd()) {
        values.push_back(arg->AsFloat());
        ++arg;
    }

    autoBegin(d.engine());
    engine::noteSetParamRange(nodeID, noteID, first,
                               static_cast<int>(values.size()),
                               values.empty() ? nullptr : values.data(), ramp);
    autoGo();
}

static void handleNoteSetParams(const char*, const void* data, int size,
                                 const SenderInfo&, OscDispatcher& d) {
    noteSetParamsImpl(data, size, d, false);
}

static void handleNoteSetParamsRamp(const char*, const void* data, int size,
                                     const SenderInfo&, OscDispatcher& d) {
    noteSetParamsImpl(data, size, d, true);
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
    dispatcher.addHandler("/engine/setInputAt",       wrap(handleSetInputAt));
    dispatcher.addHandler("/engine/setInputRamp",     wrap(handleSetInputRamp));
    dispatcher.addHandler("/engine/impulseInput",     wrap(handleImpulseInput));
    dispatcher.addHandler("/engine/impulseInputAt",   wrap(handleImpulseInputAt));
    dispatcher.addHandler("/engine/noteOn",           wrap(handleNoteOn));
    dispatcher.addHandler("/engine/noteOff",          wrap(handleNoteOff));
    dispatcher.addHandler("/engine/allNotesOff",      wrap(handleAllNotesOff));
    dispatcher.addHandler("/engine/noteSetParams",    wrap(handleNoteSetParams));
    dispatcher.addHandler("/engine/noteSetParamsRamp", wrap(handleNoteSetParamsRamp));
}

} // namespace osc
