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
//  test_graph_shadow.cpp
//  integration-tests
//
//  Tests for the engine's NRT topology shadow (graph view): the
//  graphGeneration counter, getGraphDesc snapshots, per-op shadow edits,
//  atomic bundle abort, and execution-order commits for beat-scheduled
//  bundles (via NRT rendering).
//

#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include <cmath>
#include <format>
#include <print>
#include <string_view>
#include <vector>

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
// Helpers
// ---------------------------------------------------------------------------

static Engine* makeEngine(int numSilos) {
    AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    EngineConfig config;
    config.numSilos = numSilos;

    Engine* e = newEngine(config, params);
    createSineNode(e);
    createAddOpNode(e);
    return e;
}

static bool hasNode(GraphDesc const& g, i64 nodeID, std::string_view defName = {}) {
    for (auto const& n : g.nodes) {
        if (n.nodeID == nodeID)
            return defName.empty() || n.defName == defName;
    }
    return false;
}

static int countConns(GraphDesc const& g, i64 srcNode, int srcPort,
                      i64 dstNode, int dstPort) {
    int n = 0;
    for (auto const& c : g.conns) {
        if (c.srcNode == srcNode && c.srcPort == srcPort &&
            c.dstNode == dstNode && c.dstPort == dstPort) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Tests (audio stopped: bundles execute inline at submit, commits included)
// ---------------------------------------------------------------------------

static void test_initial_state() {
    std::print("Test: initial shadow state\n");
    Engine* e = makeEngine(2);

    check(numSilos(e) == 2, "numSilos reports configured silo count");
    check(graphGeneration(e) >= 1, "generation starts >= 1");

    GraphDesc g;
    check(getGraphDesc(e, 0, g), "getGraphDesc(0) succeeds");
    check(g.nodes.size() == 2, "fresh silo has exactly nodes 0 and 1");
    check(hasNode(g, 0, "Audio Out"), "node 0 is Audio Out");
    check(hasNode(g, 1, "Audio In"), "node 1 is Audio In");
    check(g.conns.empty(), "fresh silo has no connections");
    check(!getGraphDesc(e, 5, g), "out-of-range silo returns false");

    freeEngine(e);
}

static void test_new_connect_free() {
    std::print("Test: newNode / connect / freeNode shadow effects\n");
    Engine* e = makeEngine(1);

    u64 gen0 = graphGeneration(e);
    begin(e);
    newNode("sinosc", 10);
    connect({10, 0}, {0, 0});
    go(0);

    GraphDesc g;
    getGraphDesc(e, 0, g);
    check(graphGeneration(e) > gen0, "topology bundle bumps generation");
    check(hasNode(g, 10, "sinosc"), "new node appears with def name");
    check(countConns(g, 10, 0, 0, 0) == 1, "connection appears");
    check(g.generation == graphGeneration(e), "snapshot generation is coherent");

    // Non-topological bundle: no generation bump.
    u64 gen1 = graphGeneration(e);
    f32 v = 440.f;
    begin(e);
    setInput({10, 0}, 1, &v);
    go(0);
    check(graphGeneration(e) == gen1, "setInput bundle does not bump generation");

    // Fan-in: second source into the same inlet.
    begin(e);
    newNode("sinosc", 11);
    connect({11, 0}, {0, 0});
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 0, 0) == 1 && countConns(g, 11, 0, 0, 0) == 1,
          "fan-in shows two conns into one inlet");

    // disconnectSource with fan-in removes only the matching conn.
    begin(e);
    disconnectSource({10, 0}, {0, 0});
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 0, 0) == 0, "disconnectSource removed matching conn");
    check(countConns(g, 11, 0, 0, 0) == 1, "disconnectSource kept the other conn");

    // freeNode purges the node and every conn touching it.
    begin(e);
    freeNode(11);
    go(0);
    getGraphDesc(e, 0, g);
    check(!hasNode(g, 11), "freed node is gone");
    check(g.conns.empty(), "freed node's conns are gone");
    check(hasNode(g, 10), "unrelated node survives freeNode");

    freeEngine(e);
}

static void test_disconnect_variants() {
    std::print("Test: disconnectInput / disconnectOutput / disconnectNode / reconnectOutput\n");
    Engine* e = makeEngine(1);

    // sinosc 10 -> +20.a ; sinosc 11 -> +20.b ; +20 -> Audio Out
    begin(e);
    newNode("sinosc", 10);
    newNode("sinosc", 11);
    newNode("+", 20);
    connect({10, 0}, {20, 0});
    connect({11, 0}, {20, 1});
    connect({20, 0}, {0, 0});
    go(0);

    GraphDesc g;
    getGraphDesc(e, 0, g);
    check(g.conns.size() == 3, "three connections built");

    // disconnectInput clears one inlet.
    begin(e);
    disconnectInput({20, 0});
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 20, 0) == 0, "disconnectInput cleared the inlet");
    check(countConns(g, 11, 0, 20, 1) == 1, "other inlet untouched");

    // disconnectSource on a single direct connection disconnects the inlet
    // (RT does this regardless of which source is named).
    begin(e);
    disconnectSource({11, 0}, {20, 1});
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 11, 0, 20, 1) == 0, "disconnectSource cleared direct conn");

    // Rebuild and test disconnectOutput.
    begin(e);
    connect({10, 0}, {20, 0});
    connect({10, 0}, {20, 1});
    go(0);
    begin(e);
    disconnectOutput({10, 0});
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 20, 0) == 0 && countConns(g, 10, 0, 20, 1) == 0,
          "disconnectOutput cleared all conns from the outlet");
    check(countConns(g, 20, 0, 0, 0) == 1, "downstream conn untouched");

    // disconnectNode: all conns touching, node stays.
    begin(e);
    connect({10, 0}, {20, 0});
    go(0);
    begin(e);
    disconnectNode(20);
    go(0);
    getGraphDesc(e, 0, g);
    check(hasNode(g, 20), "disconnectNode keeps the node");
    check(g.conns.empty(), "disconnectNode purged all its conns");

    // reconnectOutput rewrites the source of existing conns.
    begin(e);
    connect({10, 0}, {20, 0});
    go(0);
    begin(e);
    reconnectOutput({10, 0}, {11, 0});
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 20, 0) == 0 && countConns(g, 11, 0, 20, 0) == 1,
          "reconnectOutput rewrote the conn source");

    freeEngine(e);
}

static void test_atomic_abort() {
    std::print("Test: failed bundle leaves shadow and generation untouched\n");
    Engine* e = makeEngine(1);

    begin(e);
    newNode("sinosc", 10);
    go(0);

    GraphDesc before;
    getGraphDesc(e, 0, before);
    u64 gen = graphGeneration(e);

    // Last op fails (node 10 already exists): whole bundle aborts.
    begin(e);
    newNode("sinosc", 30);
    connect({30, 0}, {0, 0});
    newNode("sinosc", 10);
    tzpl_SErr err = go(0);

    check(err != tzpl_errNone, "bundle with failing op reports error");
    check(graphGeneration(e) == gen, "generation unchanged after abort");
    GraphDesc after;
    getGraphDesc(e, 0, after);
    check(after.nodes.size() == before.nodes.size(), "no nodes leaked by abort");
    check(!hasNode(after, 30), "aborted node not in shadow");
    check(after.conns.size() == before.conns.size(), "no conns leaked by abort");

    freeEngine(e);
}

static void test_free_all_and_replace() {
    std::print("Test: freeAllNodes and replaceNode\n");
    Engine* e = makeEngine(1);

    begin(e);
    newNode("sinosc", 10);
    newNode("sinosc", 11);
    connect({10, 0}, {0, 0});
    go(0);

    // replaceNode: 11 takes over 10's output connection.
    begin(e);
    replaceNode(10, 11);
    go(0);
    GraphDesc g;
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 0, 0) == 0 && countConns(g, 11, 0, 0, 0) == 1,
          "replaceNode moved the output conn to the new node");
    check(hasNode(g, 10), "replaceNode keeps the old node");

    begin(e);
    freeAllNodes();
    go(0);
    getGraphDesc(e, 0, g);
    check(g.nodes.size() == 2 && hasNode(g, 0) && hasNode(g, 1),
          "freeAllNodes keeps only Audio Out / Audio In");
    check(g.conns.empty(), "freeAllNodes cleared conns");

    freeEngine(e);
}

static void test_replace_node_swap_back() {
    std::print("Test: replaceNode swap-back does not duplicate input conns\n");
    Engine* e = makeEngine(1);

    // 10 -> 101.a, 101 -> out. 102 is the replacement.
    begin(e);
    newNode("sinosc", 10);
    newNode("+", 101);
    newNode("+", 102);
    connect({10, 0}, {101, 0});
    connect({101, 0}, {0, 0});
    go(0);

    // Replace forward: 102 inherits the input and the output.
    begin(e);
    replaceNode(101, 102);
    go(0);
    GraphDesc g;
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 102, 0) == 1, "replacement got the input once");
    check(countConns(g, 10, 0, 101, 0) == 1, "old node keeps its input (until freed)");
    check(countConns(g, 102, 0, 0, 0) == 1, "output moved to replacement");

    // Swap back: 101 still has its input connected, so the input copy must
    // be skipped -- previously this added a second mixer slot for the same
    // source, double-summing the signal (and doubling the drawn wire).
    begin(e);
    replaceNode(102, 101);
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 101, 0) == 1, "swap back does not duplicate 10->101");
    check(countConns(g, 101, 0, 0, 0) == 1, "output moved back");

    // Keep swapping: still no accumulation anywhere.
    begin(e);
    replaceNode(101, 102);
    go(0);
    begin(e);
    replaceNode(102, 101);
    go(0);
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 101, 0) == 1 && countConns(g, 10, 0, 102, 0) == 1,
          "repeated swaps stay at one conn per inlet");

    freeEngine(e);
}

static void test_replace_node_fan_in_swap_back() {
    std::print("Test: replaceNode swap-back with fan-in (mixer) inputs\n");
    Engine* e = makeEngine(1);

    // Two sources into one inlet (hidden mixer), then swap out and back.
    begin(e);
    newNode("sinosc", 10);
    newNode("sinosc", 11);
    newNode("+", 101);
    newNode("+", 102);
    connect({10, 0}, {101, 0});
    connect({11, 0}, {101, 0});
    connect({101, 0}, {0, 0});
    go(0);

    begin(e);
    replaceNode(101, 102);
    go(0);
    begin(e);
    replaceNode(102, 101);
    go(0);

    GraphDesc g;
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 101, 0) == 1 && countConns(g, 11, 0, 101, 0) == 1,
          "mixer-fed inlet not duplicated by swap back");
    check(countConns(g, 10, 0, 102, 0) == 1 && countConns(g, 11, 0, 102, 0) == 1,
          "replacement's mixer-fed inlet not duplicated either");

    freeEngine(e);
}

static void test_per_silo_isolation() {
    std::print("Test: per-silo shadows are independent\n");
    Engine* e = makeEngine(2);

    begin(e);
    newNode("sinosc", 10);
    connect({10, 0}, {0, 0});
    go(1); // submit to silo 1

    GraphDesc g0, g1;
    getGraphDesc(e, 0, g0);
    getGraphDesc(e, 1, g1);
    check(!hasNode(g0, 10), "silo 0 unaffected");
    check(hasNode(g1, 10), "silo 1 has the node");
    check(g0.conns.empty() && g1.conns.size() == 1, "conns are silo-local");

    freeEngine(e);
}

// ---------------------------------------------------------------------------
// Execution-order test: beat-scheduled bundles that execute in the opposite
// order of their submission must leave the shadow in the RT-final state.
// Uses an NRT engine so rendering drives the tempo clocks deterministically.
// ---------------------------------------------------------------------------

static void test_scheduled_out_of_order() {
    std::print("Test: out-of-submission-order scheduled bundles\n");

    AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;

    EngineConfig config;
    config.numSilos = 1;

    Engine* e = newEngineNRT(config, params);
    createSineNode(e);

    setTempo(e, 0, 120.); // 2 beats per second

    // Immediate: build the node.
    begin(e);
    newNode("sinosc", 10);
    go(0);

    // Submitted FIRST, scheduled LATER (beat 2): connect 10 -> out.
    begin(e);
    connect({10, 0}, {0, 0});
    sched(0, 0, 2.0);

    // Submitted SECOND, scheduled EARLIER (beat 1): disconnect the inlet.
    begin(e);
    disconnectInput({0, 0});
    sched(0, 0, 1.0);

    // A submit-ordered shadow would end disconnected. Execution order is
    // disconnect(beat 1) then connect(beat 2) -> final state CONNECTED.
    std::vector<f32> buf(params.bufferFrames * params.channels);
    int blocksFor2s = (int)(2.5 * params.sampleRate / params.bufferFrames) + 1;
    for (int i = 0; i < blocksFor2s; ++i) renderNRTBlock(e, buf.data());

    GraphDesc g;
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 0, 0) == 1,
          "shadow follows execution order, not submission order");

    // Late schedOnTimeOnly bundle: dropped entirely, shadow untouched.
    u64 gen = graphGeneration(e);
    begin(e);
    disconnectInput({0, 0});
    sched(0, 0, 1.0, schedOnTimeOnly); // beat 1 is long past
    for (int i = 0; i < 4; ++i) renderNRTBlock(e, buf.data());
    getGraphDesc(e, 0, g);
    check(countConns(g, 10, 0, 0, 0) == 1, "late onTimeOnly bundle did not commit");
    check(graphGeneration(e) == gen, "late onTimeOnly bundle did not bump generation");

    freeEngine(e);
}

// ---------------------------------------------------------------------------
// Fan-in past the hidden mixer's slot count: every source must be audible,
// and every source must still be removable. Measured on rendered audio, not
// just on the shadow -- the shadow records the wire either way.
// ---------------------------------------------------------------------------

// A DC source: an unconnected "+" node whose two inlets hold constants, so
// its output is an exact, phase-free value we can sum and decode.
static i64 addDCSource(Engine* e, i64 id, f32 value) {
    f32 v[2] = {value, value};
    f32 zero[2] = {0.f, 0.f};
    begin(e);
    newNode("+", id);
    setInput({id, 0}, 2, v);
    setInput({id, 1}, 2, zero);
    connect({id, 0}, {0, 0});
    go(0);
    return id;
}

// Render a few blocks and return the (constant) output sample value.
static f32 renderDC(Engine* e, int numSamples) {
    std::vector<f32> buf(numSamples);
    for (int i = 0; i < 4; ++i) renderNRTBlock(e, buf.data());
    return buf[0];
}

static Engine* newDCEngine(AudioStreamParameters& params) {
    params.channels = 2;
    params.bufferFrames = 64;
    params.sampleRate = 44100.0;
    EngineConfig config;
    config.numSilos = 1;
    Engine* e = newEngineNRT(config, params);
    createAddOpNode(e);
    safetyLimiter(e, kOff); // measure the raw sum
    masterGain(e, 1.f);
    return e;
}

static void test_fan_in_beyond_mixer_capacity() {
    std::print("Test: fan-in beyond the hidden mixer's slot count\n");

    AudioStreamParameters params{};
    Engine* e = newDCEngine(params);

    int const numSources = 12;
    int const numSamples = params.bufferFrames * params.channels;

    // Source i contributes 2^i, so the summed output names exactly which
    // sources reached the outlet.
    f32 want = 0.f;
    for (int i = 0; i < numSources; ++i) {
        want += f32(1 << i);
        addDCSource(e, 10 + i, f32(1 << i));
        f32 got = renderDC(e, numSamples);
        check(got == want,
              std::format("{} sources sum at the output (got {}, want {})",
                          i + 1, got, want));
    }

    GraphDesc g;
    getGraphDesc(e, 0, g);
    check((int)g.conns.size() == numSources, "shadow has one wire per source");

    // Remove them one at a time, front to back: each disconnect must remove
    // exactly that source's contribution, wherever it sits.
    for (int i = 0; i < numSources; ++i) {
        want -= f32(1 << i);
        begin(e);
        disconnectSource({10 + i, 0}, {0, 0});
        go(0);
        f32 got = renderDC(e, numSamples);
        check(got == want,
              std::format("{} sources left after disconnect (got {}, want {})",
                          numSources - i - 1, got, want));
    }

    freeEngine(e);
}

// Same fan-in, torn down with disconnectInput (which frees the whole hidden
// mixer structure) and then rebuilt -- the inlet must come back cleanly.
static void test_fan_in_disconnect_input_teardown() {
    std::print("Test: disconnectInput tears down a full mixer chain\n");

    AudioStreamParameters params{};
    Engine* e = newDCEngine(params);

    int const numSources = 10;
    int const numSamples = params.bufferFrames * params.channels;

    for (int pass = 0; pass < 2; ++pass) {
        f32 want = 0.f;
        for (int i = 0; i < numSources; ++i) {
            want += f32(1 << i);
            addDCSource(e, 10 + i, f32(1 << i));
        }
        check(renderDC(e, numSamples) == want,
              std::format("pass {}: all {} sources audible", pass, numSources));

        begin(e);
        disconnectInput({0, 0});
        go(0);
        check(renderDC(e, numSamples) == 0.f,
              std::format("pass {}: inlet silent after disconnectInput", pass));

        begin(e);
        for (int i = 0; i < numSources; ++i) freeNode(10 + i);
        go(0);
        renderDC(e, numSamples); // let the dead-node queue drain
    }

    freeEngine(e);
}

// Fan-in built with crossfaded connects (what the graph editor UI submits):
// the fades must all complete and leave every source connected.
static void test_fan_in_with_xfades() {
    std::print("Test: crossfaded fan-in past the mixer's slot count\n");

    AudioStreamParameters params{};
    Engine* e = newDCEngine(params);

    int const numSources = 10;
    int const numSamples = params.bufferFrames * params.channels;
    f64 const xfade = 0.1; // graph::kUIXFadeTime
    int const blocksPerFade =
        int(xfade * params.sampleRate / params.bufferFrames) + 4;

    std::vector<f32> buf(numSamples);
    f32 want = 0.f;
    for (int i = 0; i < numSources; ++i) {
        want += f32(1 << i);
        f32 v[2] = {f32(1 << i), f32(1 << i)};
        f32 zero[2] = {0.f, 0.f};
        begin(e);
        newNode("+", 10 + i);
        setInput({10 + i, 0}, 2, v);
        setInput({10 + i, 1}, 2, zero);
        connect({10 + i, 0}, {0, 0}, xfade);
        go(0);
        for (int b = 0; b < blocksPerFade; ++b) renderNRTBlock(e, buf.data());
    }
    check(renderDC(e, numSamples) == want,
          std::format("all {} crossfaded sources reached the outlet", numSources));

    // And fade them all back out.
    for (int i = 0; i < numSources; ++i) {
        begin(e);
        disconnectSource({10 + i, 0}, {0, 0}, xfade);
        go(0);
        for (int b = 0; b < blocksPerFade; ++b) renderNRTBlock(e, buf.data());
    }
    check(renderDC(e, numSamples) == 0.f, "all crossfaded sources faded out");

    freeEngine(e);
}

// Freeing and replacing nodes that feed a wide fan-in: the engine has to
// find the owning inlet from a node buried in the middle of the chain.
static void test_fan_in_free_and_replace() {
    std::print("Test: freeNode / replaceNode within a wide fan-in\n");

    AudioStreamParameters params{};
    Engine* e = newDCEngine(params);

    int const numSources = 9;
    int const numSamples = params.bufferFrames * params.channels;

    f32 want = 0.f;
    for (int i = 0; i < numSources; ++i) {
        want += f32(1 << i);
        addDCSource(e, 10 + i, f32(1 << i));
    }
    check(renderDC(e, numSamples) == want, "all sources audible before free");

    // Free a source in the middle of the chain.
    want -= f32(1 << 5);
    begin(e);
    freeNode(15);
    go(0);
    check(renderDC(e, numSamples) == want, "freeNode drops just that source");

    // Replace another one with a node carrying a different value: the
    // replacement inherits the connection to the shared inlet.
    {
        f32 v[2] = {4096.f, 4096.f};
        f32 zero[2] = {0.f, 0.f};
        begin(e);
        newNode("+", 100);
        setInput({100, 0}, 2, v);
        setInput({100, 1}, 2, zero);
        replaceNode(13, 100);
        go(0);
    }
    want += 4096.f - f32(1 << 3);
    check(renderDC(e, numSamples) == want, "replaceNode swaps a mid-chain source");

    // Free everything: the inlet must end up silent and mixer-free.
    // (15 is already gone, and 13 was replaced by 100.)
    begin(e);
    for (int i = 0; i < numSources; ++i) {
        if (i != 5) freeNode(10 + i);
    }
    freeNode(100);
    go(0);
    check(renderDC(e, numSamples) == 0.f, "inlet silent once every source is gone");

    // And it still accepts a fresh connection afterwards.
    addDCSource(e, 200, 7.f);
    check(renderDC(e, numSamples) == 7.f, "inlet reconnects after the chain is gone");

    freeEngine(e);
}

int main() {
    std::print("=== Graph shadow tests ===\n\n");

    test_initial_state();
    test_new_connect_free();
    test_disconnect_variants();
    test_atomic_abort();
    test_free_all_and_replace();
    test_replace_node_swap_back();
    test_replace_node_fan_in_swap_back();
    test_per_silo_isolation();
    test_scheduled_out_of_order();
    test_fan_in_beyond_mixer_capacity();
    test_fan_in_disconnect_input_teardown();
    test_fan_in_with_xfades();
    test_fan_in_free_and_replace();

    std::print("\n=== {} passed, {} failed ===\n", gTestsPassed, gTestsFailed);
    return gTestsFailed == 0 ? 0 : 1;
}
