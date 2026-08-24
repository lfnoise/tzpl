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
//  test_stopped_engine.cpp
//  integration-tests
//
//  Stopped-engine (synchronous command mode) invariants. With audio not
//  running, sendCmds executes both command stages inline on the calling
//  thread, so any cleanup normally deferred to the processing loop must
//  happen eagerly instead.
//
//  Regression: Silo::removeNode must unlink the node from
//  rt_sortedNodeList_ eagerly. It used to rely on the next sortNodes(),
//  which never runs while stopped -- the node was deleted with the list
//  still pointing at it, and the panic AllNotesOffAllCmd (or the clearing
//  walk in sortNodes() on restart) then read freed memory. Seen in the
//  2026-08-23 StatusBar panic-button crash.
//

#include "tzpl_client_interface.hpp"
#include "tzpl_engine.hpp"
#include "tzpl_silo.hpp"
#include "tzpl_node.hpp"
#include "tzpl_audio_backend.hpp"
#include <cstring>
#include <memory>
#include <print>
#include <string_view>

using namespace engine;

static int gTestsPassed = 0;
static int gTestsFailed = 0;

static void check(bool condition, std::string_view description) {
    if (condition) {
        std::print("  PASS: {}\n", description);
        ++gTestsPassed;
    } else {
        std::print("  FAIL: {}\n", description);
        ++gTestsFailed;
    }
}

// ---------------------------------------------------------------------------
// A backend that never opens a device: the engine stays in the stopped
// (initted) state, so every bundle executes synchronously at go()/sched().
// ---------------------------------------------------------------------------

struct StubBackend : AudioBackend {
    void init(Engine*) override {}
    void uninit() override {}
    void start() override {}
    void stop() override {}
    f64 streamTime() override { return 0.; }
    void printDevices() override {}
};

// ---------------------------------------------------------------------------
// A minimal note-capable synth whose allNotesOff counts its calls, so the
// test can tell exactly which nodes a panic sweep reached.
// ---------------------------------------------------------------------------

static int gAllNotesOffCalls = 0;

struct CountingSynth : tzpl_SynthData {};
static tzpl_SynthData* cs_alloc() { return (tzpl_SynthData*)new CountingSynth(); }
static tzpl_SErr cs_free(tzpl_SynthData* s) {
    delete (CountingSynth*)s;
    return tzpl_errNone;
}
static void cs_process(tzpl_SynthData*) {}
static tzpl_SErr cs_allNotesOff(tzpl_SynthData*, int64_t) {
    ++gAllNotesOffCalls;
    return tzpl_errNone;
}

static void createCountingNode(Engine* e) {
    static PortInfo outPort{"out", {tzpl_kF32, tzpl_audioRate, 2}};
    NodeDefInfo info;
    memset(&info, 0, sizeof info);
    info.name = "CountingSynth";
    info.num_outs = 1;
    info.outs = &outPort;
    info.funs.alloc = cs_alloc;
    info.funs.free = cs_free;
    info.funs.processAudio = cs_process;
    info.funs.allNotesOff = cs_allNotesOff;
    addNodeDef(e, info);
}

int main() {
    AudioStreamParameters params{};
    params.deviceName = "stub";
    params.channels = 2;
    params.bufferFrames = 64;
    params.sampleRate = 48000.;

    EngineConfig config;
    config.numSilos = 1;

    Engine* e = newEngine(config, params, std::make_unique<StubBackend>());
    check(!isAudioRunning(e), "engine starts stopped (synchronous command mode)");
    createCountingNode(e);

    // Build node 100 -> Audio Out while stopped (runs inline).
    begin(e);
    newNode("CountingSynth", 100);
    connect({100, 0}, {0, 0});
    check(go(0) == tzpl_errNone, "build bundle runs synchronously");

    Silo* s = &e->silos_[0];
    Node* n100 = s->rt_getNode(100);
    check(n100 != nullptr, "node is in the RT table");

    // Build the sorted list, as a running engine would have.
    s->sortNodes();
    bool inList = false;
    for (Node* n = s->rt_sortedNodeList_; n; n = n->sorted_next)
        if (n == n100) inList = true;
    check(inList, "node is in the RT sorted list after sortNodes");

    // Panic while the node is alive reaches it through the sorted list.
    begin(e);
    allNotesOffAll();
    check(go(0) == tzpl_errNone, "panic bundle runs synchronously");
    check(gAllNotesOffCalls == 1, "panic reached the live node");

    // Free the node while stopped: both stages run inline and the Node is
    // deleted immediately. It must have left the sorted list.
    Node* dangling = n100; // address only, never dereferenced
    begin(e);
    freeNode(100);
    check(go(0) == tzpl_errNone, "free bundle runs synchronously");

    bool stillLinked = false;
    for (Node* n = s->rt_sortedNodeList_; n; n = n->sorted_next) {
        if (n == dangling) { stillLinked = true; break; } // stop before dereferencing
    }
    check(!stillLinked, "freed node was unlinked from the RT sorted list");

    // The original crash: panic after the free walks the sorted list.
    if (!stillLinked) {
        begin(e);
        allNotesOffAll();
        check(go(0) == tzpl_errNone, "panic after free runs synchronously");
        check(gAllNotesOffCalls == 1, "panic no longer reaches the freed node");
    }

    freeEngine(e);

    std::print("\n{} passed, {} failed\n", gTestsPassed, gTestsFailed);
    return gTestsFailed == 0 ? 0 : 1;
}
