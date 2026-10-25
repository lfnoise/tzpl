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
//  tzpl_nats_engine_commands.cpp
//  nats
//
//  Maps NATS subjects to engine:: client interface calls.
//  NATS message payloads use simple space-separated text arguments.
//  Single (non-bundled) graph commands are auto-wrapped in begin()/go(0).
//

#include "tzpl_nats.hpp"
#include "tzpl_client_interface.hpp"
#include <print>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <sstream>

namespace nats {

// ---------------------------------------------------------------------------
// Helpers -- parse space-separated text payload
// ---------------------------------------------------------------------------

static std::vector<std::string> splitArgs(const char* data, int dataLen) {
    std::vector<std::string> args;
    if (!data || dataLen <= 0) return args;

    std::string payload(data, dataLen);
    std::istringstream iss(payload);
    std::string token;
    while (iss >> token) {
        args.push_back(std::move(token));
    }
    return args;
}

static int argInt(const std::vector<std::string>& args, size_t idx, int def = 0) {
    if (idx >= args.size()) return def;
    return std::atoi(args[idx].c_str());
}

static float argFloat(const std::vector<std::string>& args, size_t idx, float def = 0.0f) {
    if (idx >= args.size()) return def;
    return std::strtof(args[idx].c_str(), nullptr);
}

static const char* argStr(const std::vector<std::string>& args, size_t idx) {
    if (idx >= args.size()) return "";
    return args[idx].c_str();
}

// True if the token is an optionally signed run of digits. Used where an
// argument may be either a numeric ID or a name (names are never all digits).
static bool isIntToken(std::string const& s) {
    size_t i = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Engine lifecycle handlers
// ---------------------------------------------------------------------------

static void handleStartAudio(const char*, const char*, int, const char*,
                              NatsDispatcher& d) {
    engine::startAudio(d.engine());
}

static void handleStopAudio(const char*, const char*, int, const char*,
                              NatsDispatcher& d) {
    engine::stopAudio(d.engine());
}

static void handleMasterGain(const char*, const char* data, int len, const char*,
                               NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    float gain = argFloat(args, 0, 1.0f);
    engine::masterGain(d.engine(), gain);
}

static void handleSafetyLimiter(const char*, const char* data, int len, const char*,
                                  NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    int onoff = argInt(args, 0, 1);
    engine::safetyLimiter(d.engine(), onoff ? engine::kOn : engine::kOff);
}

static void handleLoadDefs(const char*, const char* data, int len, const char*,
                             NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.empty()) return;
    engine::loadDefs(d.engine(), argStr(args, 0));
}

static void handleLoadDef(const char*, const char* data, int len, const char*,
                            NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    engine::loadDef(d.engine(), argStr(args, 0), argStr(args, 1));
}

// ---------------------------------------------------------------------------
// Query handlers (reply via NATS reply subject)
// ---------------------------------------------------------------------------

static void handleGetStreamTime(const char*, const char*, int, const char* reply,
                                  NatsDispatcher& d) {
    double t = engine::getStreamTime(d.engine());
    if (d.client() && reply && reply[0]) {
        std::string msg = std::to_string(t);
        d.client()->publish(reply, msg.c_str());
    }
}

static void handleListNodeDefs(const char*, const char*, int, const char* reply,
                                 NatsDispatcher& d) {
    std::vector<std::string> names;
    engine::listNodeDefs(d.engine(), names);
    if (d.client() && reply && reply[0]) {
        std::string msg;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) msg += ' ';
            msg += names[i];
        }
        d.client()->publish(reply, msg.c_str());
    }
}

static void handleIsAudioRunning(const char*, const char*, int, const char* reply,
                                   NatsDispatcher& d) {
    bool running = engine::isAudioRunning(d.engine());
    if (d.client() && reply && reply[0]) {
        d.client()->publish(reply, running ? "1" : "0");
    }
}

// ---------------------------------------------------------------------------
// Graph command handlers
// ---------------------------------------------------------------------------

static void handleNewNode(const char*, const char* data, int len, const char*,
                           NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    const char* defName = argStr(args, 0);
    auto nodeID = static_cast<engine::i64>(argInt(args, 1));

    engine::begin(d.engine());
    engine::newNode(defName, nodeID);
    engine::go(0);
}

static void handleFreeNode(const char*, const char* data, int len, const char*,
                             NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.empty()) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));

    engine::begin(d.engine());
    engine::freeNode(nodeID);
    engine::go(0);
}

static void handleFreeAllNodes(const char*, const char*, int, const char*,
                                NatsDispatcher& d) {
    engine::begin(d.engine());
    engine::freeAllNodes();
    engine::go(0);
}

static void handleConnect(const char*, const char* data, int len, const char*,
                           NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 4) return;
    engine::PortAddr src{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};
    engine::PortAddr dst{static_cast<engine::i64>(argInt(args, 2)), argInt(args, 3)};

    engine::begin(d.engine());
    engine::connect(src, dst);
    engine::go(0);
}

static void handleConnectX(const char*, const char* data, int len, const char*,
                             NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 6) return;
    engine::PortAddr src{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};
    engine::PortAddr dst{static_cast<engine::i64>(argInt(args, 2)), argInt(args, 3)};
    float xfade = argFloat(args, 4);
    auto curve = static_cast<engine::FadeCurve>(argInt(args, 5));

    engine::begin(d.engine());
    engine::connect(src, dst, xfade, curve);
    engine::go(0);
}

static void handleDisconnectInput(const char*, const char* data, int len, const char*,
                                    NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    engine::PortAddr dst{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};

    engine::begin(d.engine());
    engine::disconnectInput(dst);
    engine::go(0);
}

static void handleDisconnectOutput(const char*, const char* data, int len, const char*,
                                     NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    engine::PortAddr src{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};

    engine::begin(d.engine());
    engine::disconnectOutput(src);
    engine::go(0);
}

static void handleDisconnectNode(const char*, const char* data, int len, const char*,
                                   NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.empty()) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));

    engine::begin(d.engine());
    engine::disconnectNode(nodeID);
    engine::go(0);
}

static void handleReplaceNode(const char*, const char* data, int len, const char*,
                                NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 4) return;
    auto oldID = static_cast<engine::i64>(argInt(args, 0));
    auto newID = static_cast<engine::i64>(argInt(args, 1));
    float xfade = argFloat(args, 2);
    auto curve = static_cast<engine::FadeCurve>(argInt(args, 3));

    engine::begin(d.engine());
    engine::replaceNode(oldID, newID, xfade, curve);
    engine::go(0);
}

// Input values. Every form takes the node, then the input as a port index
// or a name (resolved against the node's def at bundle submit), then its own
// arguments; trailing numbers are the values, broadcast cyclically over the
// addressed channels.
//   engine.setInput       "node port value..."
//   engine.setInputAt     "node port first value..."
//   engine.setInputRamp   "node port rampTime curve value..."
//   engine.setInputX      "node port value xfadeTime curve"
//   engine.impulseInput   "node port value..."
//   engine.impulseInputAt "node port first value..."

namespace {
enum class InputOp { set, fade, impulse };

std::vector<engine::f64> argValues(std::vector<std::string> const& args, size_t from) {
    std::vector<engine::f64> vals;
    for (size_t i = from; i < args.size(); ++i) vals.push_back(argFloat(args, i));
    return vals;
}

void sendInput(NatsDispatcher& d, std::vector<std::string> const& args,
               engine::Region const& region, std::vector<engine::f64> const& vals,
               engine::f64 time, engine::FadeCurve curve, InputOp op) {
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));
    bool byName = !isIntToken(args[1]);
    engine::PortAddr port{nodeID, byName ? 0 : argInt(args, 1)};
    char const* name = argStr(args, 1);

    engine::begin(d.engine());
    switch (op) {
        case InputOp::set:
            if (byName) engine::setInput(nodeID, name, region, vals, time, curve);
            else engine::setInput(port, region, vals, time, curve);
            break;
        case InputOp::fade:
            if (byName) engine::setInputX(nodeID, name, region, vals, time, curve);
            else engine::setInputX(port, region, vals, time, curve);
            break;
        case InputOp::impulse:
            if (byName) engine::impulseInput(nodeID, name, region, vals);
            else engine::impulseInput(port, region, vals);
            break;
    }
    engine::go(0);
}
} // namespace

static void handleSetInput(const char*, const char* data, int len, const char*,
                             NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 3) return;
    sendInput(d, args, engine::Region::all(), argValues(args, 2), 0., engine::fadeLinear,
              InputOp::set);
}

static void handleSetInputAt(const char*, const char* data, int len, const char*,
                               NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 4) return;
    auto vals = argValues(args, 3);
    sendInput(d, args, engine::Region::channels(argInt(args, 2), (int)vals.size()), vals,
              0., engine::fadeLinear, InputOp::set);
}

static void handleSetInputRamp(const char*, const char* data, int len, const char*,
                                 NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 5) return;
    sendInput(d, args, engine::Region::all(), argValues(args, 4), argFloat(args, 2),
              static_cast<engine::FadeCurve>(argInt(args, 3)), InputOp::set);
}

static void handleSetInputX(const char*, const char* data, int len, const char*,
                              NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 4) return;
    sendInput(d, args, engine::Region::all(), {argFloat(args, 2)}, argFloat(args, 3),
              static_cast<engine::FadeCurve>(argInt(args, 4)), InputOp::fade);
}

static void handleImpulseInput(const char*, const char* data, int len, const char*,
                                 NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 3) return;
    sendInput(d, args, engine::Region::all(), argValues(args, 2), 0., engine::fadeLinear,
              InputOp::impulse);
}

static void handleImpulseInputAt(const char*, const char* data, int len, const char*,
                                   NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 4) return;
    auto vals = argValues(args, 3);
    sendInput(d, args, engine::Region::channels(argInt(args, 2), (int)vals.size()), vals,
              0., engine::fadeLinear, InputOp::impulse);
}

static void handleNoteOn(const char*, const char* data, int len, const char*,
                           NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));
    int noteID = argInt(args, 1);

    std::vector<engine::f32> params;
    for (size_t i = 2; i < args.size(); ++i) {
        params.push_back(argFloat(args, i));
    }

    engine::begin(d.engine());
    engine::noteOn(nodeID, noteID, static_cast<int>(params.size()),
                    params.empty() ? nullptr : params.data());
    engine::go(0);
}

static void handleNoteOff(const char*, const char* data, int len, const char*,
                            NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));
    int noteID = argInt(args, 1);

    engine::begin(d.engine());
    engine::noteOff(nodeID, noteID);
    engine::go(0);
}

static void handleAllNotesOff(const char*, const char* data, int len, const char*,
                                NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.empty()) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));

    engine::begin(d.engine());
    engine::allNotesOff(nodeID);
    engine::go(0);
}

// engine.noteSetParams     "node noteID first value..."
// engine.noteSetParamsRamp "node noteID first rampTime value..."
static void noteSetParamsImpl(const char* data, int len, NatsDispatcher& d, bool ramped) {
    auto args = splitArgs(data, len);
    size_t from = ramped ? 4 : 3;
    if (args.size() < from + 1) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));
    int noteID = argInt(args, 1);
    int first = argInt(args, 2);
    engine::f64 ramp = ramped ? argFloat(args, 3) : 0.;
    std::vector<engine::f32> values;
    for (size_t i = from; i < args.size(); ++i) values.push_back(argFloat(args, i));

    engine::begin(d.engine());
    engine::noteSetParamRange(nodeID, noteID, first, (int)values.size(), values.data(), ramp);
    engine::go(0);
}

static void handleNoteSetParams(const char*, const char* data, int len, const char*,
                                  NatsDispatcher& d) {
    noteSetParamsImpl(data, len, d, false);
}

static void handleNoteSetParamsRamp(const char*, const char* data, int len, const char*,
                                      NatsDispatcher& d) {
    noteSetParamsImpl(data, len, d, true);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerEngineHandlers(NatsDispatcher& dispatcher,
                             const std::string& engineName) {
    auto wrap = [&](auto fn) -> NatsHandler {
        return [&dispatcher, fn](const char* subject, const char* data,
                                  int dataLen, const char* reply) {
            fn(subject, data, dataLen, reply, dispatcher);
        };
    };

    // Helper: register a handler under the flat subject and, if engineName is
    // set, also under engines.{name}.{cmd} and engines.all.{cmd}.
    auto reg = [&](const char* cmd, NatsHandler handler) {
        dispatcher.addHandler(cmd, handler);
        if (!engineName.empty()) {
            dispatcher.addHandler("engines." + engineName + "." + cmd, handler);
            dispatcher.addHandler("engines.all." + std::string(cmd), handler);
        }
    };

    // Engine lifecycle
    reg("engine.startAudio",    wrap(handleStartAudio));
    reg("engine.stopAudio",     wrap(handleStopAudio));
    reg("engine.masterGain",    wrap(handleMasterGain));
    reg("engine.safetyLimiter", wrap(handleSafetyLimiter));
    reg("engine.loadDefs",      wrap(handleLoadDefs));
    reg("engine.loadDef",       wrap(handleLoadDef));

    // Queries
    reg("engine.getStreamTime",   wrap(handleGetStreamTime));
    reg("engine.listNodeDefs",    wrap(handleListNodeDefs));
    reg("engine.isAudioRunning",  wrap(handleIsAudioRunning));

    // Graph commands
    reg("engine.newNode",          wrap(handleNewNode));
    reg("engine.freeNode",         wrap(handleFreeNode));
    reg("engine.freeAllNodes",     wrap(handleFreeAllNodes));
    reg("engine.connect",          wrap(handleConnect));
    reg("engine.connectX",         wrap(handleConnectX));
    reg("engine.disconnectInput",  wrap(handleDisconnectInput));
    reg("engine.disconnectOutput", wrap(handleDisconnectOutput));
    reg("engine.disconnectNode",   wrap(handleDisconnectNode));
    reg("engine.replaceNode",      wrap(handleReplaceNode));
    reg("engine.setInput",         wrap(handleSetInput));
    reg("engine.setInputAt",       wrap(handleSetInputAt));
    reg("engine.setInputRamp",     wrap(handleSetInputRamp));
    reg("engine.setInputX",        wrap(handleSetInputX));
    reg("engine.impulseInput",     wrap(handleImpulseInput));
    reg("engine.impulseInputAt",   wrap(handleImpulseInputAt));
    reg("engine.noteOn",           wrap(handleNoteOn));
    reg("engine.noteOff",          wrap(handleNoteOff));
    reg("engine.allNotesOff",      wrap(handleAllNotesOff));
    reg("engine.noteSetParams",    wrap(handleNoteSetParams));
    reg("engine.noteSetParamsRamp", wrap(handleNoteSetParamsRamp));
}

} // namespace nats
