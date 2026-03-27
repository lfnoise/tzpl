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
//  Single (non-bundled) graph commands are auto-wrapped in begin(0)/go().
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

    engine::begin(d.engine(), 0);
    engine::newNode(defName, nodeID);
    engine::go();
}

static void handleFreeNode(const char*, const char* data, int len, const char*,
                             NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.empty()) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));

    engine::begin(d.engine(), 0);
    engine::freeNode(nodeID);
    engine::go();
}

static void handleFreeAllNodes(const char*, const char*, int, const char*,
                                NatsDispatcher& d) {
    engine::begin(d.engine(), 0);
    engine::freeAllNodes();
    engine::go();
}

static void handleConnect(const char*, const char* data, int len, const char*,
                           NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 4) return;
    engine::PortAddr src{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};
    engine::PortAddr dst{static_cast<engine::i64>(argInt(args, 2)), argInt(args, 3)};

    engine::begin(d.engine(), 0);
    engine::connect(src, dst);
    engine::go();
}

static void handleConnectX(const char*, const char* data, int len, const char*,
                             NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 6) return;
    engine::PortAddr src{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};
    engine::PortAddr dst{static_cast<engine::i64>(argInt(args, 2)), argInt(args, 3)};
    float xfade = argFloat(args, 4);
    auto curve = static_cast<engine::FadeCurve>(argInt(args, 5));

    engine::begin(d.engine(), 0);
    engine::connect(src, dst, xfade, curve);
    engine::go();
}

static void handleDisconnectInput(const char*, const char* data, int len, const char*,
                                    NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    engine::PortAddr dst{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};

    engine::begin(d.engine(), 0);
    engine::disconnectInput(dst);
    engine::go();
}

static void handleDisconnectOutput(const char*, const char* data, int len, const char*,
                                     NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    engine::PortAddr src{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};

    engine::begin(d.engine(), 0);
    engine::disconnectOutput(src);
    engine::go();
}

static void handleDisconnectNode(const char*, const char* data, int len, const char*,
                                   NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.empty()) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));

    engine::begin(d.engine(), 0);
    engine::disconnectNode(nodeID);
    engine::go();
}

static void handleReplaceNode(const char*, const char* data, int len, const char*,
                                NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 4) return;
    auto oldID = static_cast<engine::i64>(argInt(args, 0));
    auto newID = static_cast<engine::i64>(argInt(args, 1));
    float xfade = argFloat(args, 2);
    auto curve = static_cast<engine::FadeCurve>(argInt(args, 3));

    engine::begin(d.engine(), 0);
    engine::replaceNode(oldID, newID, xfade, curve);
    engine::go();
}

static void handleSetInput(const char*, const char* data, int len, const char*,
                             NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 3) return;
    engine::PortAddr port{static_cast<engine::i64>(argInt(args, 0)), argInt(args, 1)};
    engine::f32 val = argFloat(args, 2);

    engine::begin(d.engine(), 0);
    engine::setInput(port, 1, &val);
    engine::go();
}

static void handleSetControl(const char*, const char* data, int len, const char*,
                               NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 3) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));
    auto controlID = static_cast<engine::i64>(argInt(args, 1));
    engine::f32 val = argFloat(args, 2);

    engine::begin(d.engine(), 0);
    engine::setControl(nodeID, controlID, 1, &val);
    engine::go();
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

    engine::begin(d.engine(), 0);
    engine::noteOn(nodeID, noteID, static_cast<int>(params.size()),
                    params.empty() ? nullptr : params.data());
    engine::go();
}

static void handleNoteOff(const char*, const char* data, int len, const char*,
                            NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.size() < 2) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));
    int noteID = argInt(args, 1);

    engine::begin(d.engine(), 0);
    engine::noteOff(nodeID, noteID);
    engine::go();
}

static void handleAllNotesOff(const char*, const char* data, int len, const char*,
                                NatsDispatcher& d) {
    auto args = splitArgs(data, len);
    if (args.empty()) return;
    auto nodeID = static_cast<engine::i64>(argInt(args, 0));

    engine::begin(d.engine(), 0);
    engine::allNotesOff(nodeID);
    engine::go();
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
    reg("engine.setControl",       wrap(handleSetControl));
    reg("engine.noteOn",           wrap(handleNoteOn));
    reg("engine.noteOff",          wrap(handleNoteOff));
    reg("engine.allNotesOff",      wrap(handleAllNotesOff));
}

} // namespace nats
