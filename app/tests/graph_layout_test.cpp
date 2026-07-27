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
//  graph_layout_test.cpp
//  app
//
//  Headless tests for the graph view-model builder and auto-layout:
//  layering on chains, fan-in, cycles, unreachable nodes, user-position
//  overrides, and defMissing pin synthesis. No engine instance needed.
//

#include "graph_edits.hpp"
#include "graph_layout.hpp"
#include "graph_meters.hpp"
#include "graph_model.hpp"
#include "tzpl_test_plugins.hpp"
#include "tzpl_ui_node_controls.hpp"

#include <cmath>
#include <cstdio>
#include <map>
#include <print>
#include <string>
#include <string_view>

using namespace graph;

static int gPassed = 0, gFailed = 0;

static void check(bool cond, std::string_view what) {
    if (cond) { std::print("  PASS: {}\n", what); ++gPassed; }
    else      { std::print("  FAIL: {}\n", what); ++gFailed; }
}

// ---------------------------------------------------------------------------
// Helpers: build a GraphDesc + def table by hand.
// ---------------------------------------------------------------------------

struct DefTable {
    std::map<std::string, engine::DefDesc> defs;

    void def(std::string const& name, int numIns, int numOuts) {
        engine::DefDesc d;
        d.name = name;
        for (int i = 0; i < numIns; ++i)
            d.ins.push_back({"in" + std::to_string(i),
                             {tzpl_kF32, tzpl_audioRate, 2}});
        for (int i = 0; i < numOuts; ++i)
            d.outs.push_back({"out" + std::to_string(i),
                              {tzpl_kF32, tzpl_audioRate, 2}});
        defs[name] = d;
    }

    bool lookup(std::string const& name, engine::DefDesc& out) const {
        auto it = defs.find(name);
        if (it == defs.end()) return false;
        out = it->second;
        return true;
    }
};

static GraphViewModel build(engine::GraphDesc const& g, DefTable const& t,
                            int silo = 0) {
    GraphViewModel vm;
    buildViewModel(g, silo,
        [&](std::string const& n, engine::DefDesc& d) { return t.lookup(n, d); },
        vm);
    // Give every node a nominal size (the view normally measures these).
    for (auto& n : vm.nodes) { n.w = 100; n.h = 40; }
    return vm;
}

static NodeVM const* node(GraphViewModel const& vm, long long id) {
    int i = vm.indexOfNode(id);
    return i < 0 ? nullptr : &vm.nodes[i];
}

// ---------------------------------------------------------------------------

static void test_view_model() {
    std::print("Test: view-model construction\n");
    DefTable t;
    t.def("Audio Out", 1, 0);
    t.def("Audio In", 0, 1);
    t.def("osc", 2, 1);

    engine::GraphDesc g;
    g.generation = 7;
    g.nodes = {{0, "Audio Out"}, {1, "Audio In"}, {10, "osc"}, {11, "ghost"}};
    g.conns = {{10, 0, 0, 0}, {11, 0, 0, 0}, {99, 0, 0, 0}, {11, 3, 0, 0}};

    auto vm = build(g, t);
    check(vm.generation == 7, "generation carried through");
    check(vm.nodes.size() == 4, "all nodes present");
    check(node(vm, 10) && node(vm, 10)->ins.size() == 2 &&
          node(vm, 10)->outs.size() == 1, "ports resolved from DefDesc");
    check(node(vm, 11) && node(vm, 11)->defMissing, "unknown def flagged missing");
    check(node(vm, 11)->outs.size() == 4,
          "defMissing pins synthesized up to referenced index");
    // conns: 10->0 ok, 11->0 ok, 99->0 dropped (dangling), 11.3->0 ok (synth pin)
    check(vm.edges.size() == 3, "dangling-endpoint conn dropped");
}

static void test_chain_layout() {
    std::print("Test: chain layout\n");
    DefTable t;
    t.def("Audio Out", 1, 0);
    t.def("Audio In", 0, 1);
    t.def("osc", 2, 1);
    t.def("fx", 1, 1);

    // 10(osc) -> 20(fx) -> 0(out). Audio In unconnected.
    engine::GraphDesc g;
    g.nodes = {{0, "Audio Out"}, {1, "Audio In"}, {10, "osc"}, {20, "fx"}};
    g.conns = {{10, 0, 20, 0}, {20, 0, 0, 0}};

    auto vm = build(g, t);
    LayoutStore store;
    autoLayout(vm, store);

    auto *out = node(vm, 0), *in = node(vm, 1), *osc = node(vm, 10), *fx = node(vm, 20);
    check(osc->x < fx->x && fx->x < out->x, "chain flows left to right into Audio Out");
    check(in->x < out->x, "unconnected Audio In parks left of the sink");
    check(in->x <= osc->x, "unreachable/unconnected nodes park leftmost");
}

static void test_fan_in_and_cycle() {
    std::print("Test: fan-in and cycle layout\n");
    DefTable t;
    t.def("Audio Out", 1, 0);
    t.def("Audio In", 0, 1);
    t.def("osc", 2, 1);
    t.def("mix", 2, 1);

    // Fan-in: 10,11 -> 20(mix) -> 0. Cycle: 20 -> 10 (feedback).
    engine::GraphDesc g;
    g.nodes = {{0, "Audio Out"}, {1, "Audio In"}, {10, "osc"}, {11, "osc"}, {20, "mix"}};
    g.conns = {{10, 0, 20, 0}, {11, 0, 20, 1}, {20, 0, 0, 0}, {20, 0, 10, 0}};

    auto vm = build(g, t);
    LayoutStore store;
    autoLayout(vm, store);

    auto *out = node(vm, 0), *a = node(vm, 10), *b = node(vm, 11), *mix = node(vm, 20);
    check(a->x < mix->x && b->x < mix->x, "fan-in sources left of the mixer");
    check(mix->x < out->x, "mixer left of Audio Out");
    check(a->x == b->x, "parallel sources share a column");
    check(a->y != b->y, "parallel sources get distinct rows");
    // The cycle edge (20 -> 10) must not hang or invert the layering.
    check(true, "cycle did not hang the layering pass");
}

static void test_unreachable_placed_after_sources() {
    std::print("Test: unreachable nodes sit right of their sources\n");
    DefTable t;
    t.def("Audio Out", 1, 0);
    t.def("Audio In", 0, 1);
    t.def("sinosc", 2, 1);
    t.def("+", 2, 1);
    t.def("*", 2, 1);

    // The user-reported case: 101,102 feed both +103 (-> Audio Out) and
    // *203, whose output is unconnected. 203 has no path to Audio Out but
    // must still lay out RIGHT of the sinoscs, not park leftmost with
    // backwards input edges.
    engine::GraphDesc g;
    g.nodes = {{0, "Audio Out"}, {1, "Audio In"},
               {101, "sinosc"}, {102, "sinosc"}, {103, "+"}, {203, "*"}};
    g.conns = {{101, 0, 103, 0}, {102, 0, 103, 1}, {103, 0, 0, 0},
               {101, 0, 203, 0}, {102, 0, 203, 1}};

    auto vm = build(g, t);
    LayoutStore store;
    autoLayout(vm, store);

    auto *osc1 = node(vm, 101), *osc2 = node(vm, 102);
    auto *add = node(vm, 103), *mul = node(vm, 203), *in = node(vm, 1);
    check(mul->x > osc1->x && mul->x > osc2->x,
          "unreachable node placed right of its sources (no backwards edges)");
    check(mul->x == add->x, "shares the column of its reachable sibling");
    check(in->x <= osc1->x, "unconnected Audio In stays leftmost");

    // Chain hanging off an unreachable node keeps flowing right.
    g.nodes.push_back({204, "*"});
    g.conns.push_back({203, 0, 204, 0});
    auto vm2 = build(g, t);
    autoLayout(vm2, store);
    check(node(vm2, 204)->x > node(vm2, 203)->x,
          "unreachable chain continues rightward");
}

static void test_unconnected_sink_parks_right() {
    std::print("Test: an unconnected sink parks on the right\n");
    DefTable t;
    t.def("Audio Out", 1, 0);
    t.def("Audio In", 0, 1);
    t.def("osc", 0, 1);
    t.def("fx", 1, 1);

    // Nothing reaches Audio Out: it must still sit right of every node,
    // so the user's first wire runs left-to-right instead of backwards.
    engine::GraphDesc g;
    g.nodes = {{0, "Audio Out"}, {1, "Audio In"}, {10, "osc"}, {20, "fx"}};
    g.conns = {{10, 0, 20, 0}};

    auto vm = build(g, t);
    LayoutStore store;
    autoLayout(vm, store);

    auto *out = node(vm, 0), *in = node(vm, 1);
    auto *osc = node(vm, 10), *fx = node(vm, 20);
    check(out->x > osc->x && out->x > fx->x && out->x > in->x,
          "unconnected Audio Out parks right of everything");
    check(osc->x < fx->x, "the rest of the graph still flows left to right");
    check(in->x <= osc->x, "an unconnected source stays leftmost");

    // A lone Audio Out has nothing to sit right of: no empty gutter.
    engine::GraphDesc solo;
    solo.nodes = {{0, "Audio Out"}};
    auto vm2 = build(solo, t);
    autoLayout(vm2, store);
    check(node(vm2, 0)->x == LayoutMetrics{}.margin,
          "a lone Audio Out sits at the left margin");

    // The launch state with no input channels configured: Audio In has no
    // outlet either, so it looks like a sink. It is still the source side.
    DefTable noIn;
    noIn.def("Audio Out", 1, 0);
    noIn.def("Audio In", 0, 0);
    engine::GraphDesc pair;
    pair.nodes = {{0, "Audio Out"}, {1, "Audio In"}};
    auto vm3 = build(pair, noIn);
    autoLayout(vm3, store);
    check(node(vm3, 1)->x < node(vm3, 0)->x,
          "a pinless Audio In stays left of an idle Audio Out");
}

static void test_store_overrides() {
    std::print("Test: user-position overrides\n");
    DefTable t;
    t.def("Audio Out", 1, 0);
    t.def("osc", 0, 1);

    engine::GraphDesc g;
    g.nodes = {{0, "Audio Out"}, {10, "osc"}};
    g.conns = {{10, 0, 0, 0}};

    auto vm = build(g, t);
    LayoutStore store;
    store.set(0, 10, 500.f, 600.f);
    autoLayout(vm, store);

    check(node(vm, 10)->x == 500.f && node(vm, 10)->y == 600.f,
          "stored position wins over auto-layout");
    check(node(vm, 10)->placedByUser, "override marks placedByUser");
    check(!node(vm, 0)->placedByUser, "unstored node is auto-placed");

    store.clearSilo(0);
    autoLayout(vm, store);
    check(!node(vm, 10)->placedByUser, "clearSilo restores auto placement");

    // Positions in another silo don't apply.
    store.set(3, 10, 1.f, 2.f);
    autoLayout(vm, store);
    check(node(vm, 10)->x != 1.f, "other silo's stored position ignored");
}

static void test_edit_helpers() {
    std::print("Test: edit helpers (canConnect, nextFreeNodeID)\n");

    PortVM f32a2{"out", 2, tzpl_kF32, tzpl_audioRate};
    PortVM f32a2in{"in", 2, tzpl_kF32, tzpl_audioRate};
    PortVM f32a1in{"in", 1, tzpl_kF32, tzpl_audioRate};
    PortVM i64a2in{"in", 2, tzpl_kI64, tzpl_audioRate};
    PortVM f32e2in{"in", 2, tzpl_kF32, tzpl_eventRate};

    check(canConnect(f32a2, f32a2in, 5), "matching type/rate/chans connects");
    check(canConnect(f32a2, f32a1in, 5), "channel mismatch is adapted, not rejected");
    check(canConnect(f32a2, f32a1in, 0), "Audio Out takes any channel count");
    check(!canConnect(f32a2, i64a2in, 5), "element type mismatch rejected");
    check(!canConnect(f32a2, i64a2in, 0), "Audio Out still checks element type");
    check(!canConnect(f32a2, f32e2in, 5), "rate mismatch rejected");

    GraphViewModel vm;
    vm.nodes.push_back({.nodeID = 0});
    vm.nodes.push_back({.nodeID = 1});
    check(nextFreeNodeID(vm) == 2, "first free ID is 2");
    vm.nodes.push_back({.nodeID = 2});
    vm.nodes.push_back({.nodeID = 3});
    vm.nodes.push_back({.nodeID = 5});
    check(nextFreeNodeID(vm) == 4, "gaps are reused");
}

// End-to-end: the edit submitters against a real (audio-stopped) engine,
// observed through GraphPoller -- the exact pipeline the GraphView uses.
static void test_edit_submitters_live() {
    std::print("Test: edit submitters against a live engine\n");

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    engine::EngineConfig config;
    config.numSilos = 1;
    engine::Engine* e = engine::newEngine(config, params);
    engine::createSineNode(e);

    GraphPoller poller(e);
    GraphViewModel vm;
    check(poller.poll(0, vm), "first poll snapshots");
    check(!poller.poll(0, vm), "second poll is a no-op");

    long long id = nextFreeNodeID(vm);
    check(createNode(e, 0, "sinosc", id) == 0, "createNode succeeds");
    check(poller.poll(0, vm), "poll sees the new node");
    check(vm.indexOfNode(id) >= 0, "new node in the view-model");

    check(connectNodes(e, 0, id, 0, 0, 0) == 0, "connectNodes succeeds");
    check(poller.poll(0, vm) && vm.edges.size() == 1, "poll sees the wire");

    check(disconnectWire(e, 0, id, 0, 0, 0) == 0, "disconnectWire succeeds");
    check(poller.poll(0, vm) && vm.edges.empty(), "wire gone");

    check(connectNodes(e, 0, id, 0, 0, 0) == 0, "reconnect for disconnectNode");
    poller.poll(0, vm);
    check(disconnectGraphNode(e, 0, id) == 0, "disconnectGraphNode succeeds");
    check(poller.poll(0, vm) && vm.edges.empty(), "node's wires gone");

    // The fading variant used by the node context menu: one bundle of
    // per-wire disconnectSource commands built from the view-model.
    check(connectNodes(e, 0, id, 0, 0, 0) == 0, "reconnect for disconnectAllWires");
    poller.poll(0, vm);
    check(disconnectAllWires(e, 0, vm, id) == 0, "disconnectAllWires succeeds");
    check(poller.poll(0, vm) && vm.edges.empty(), "faded wires gone from shadow");
    check(disconnectAllWires(e, 0, vm, 999) == tzpl_errNodeNotFound,
          "disconnectAllWires rejects unknown node");

    check(freeGraphNode(e, 0, id) == 0, "freeGraphNode succeeds");
    check(poller.poll(0, vm) && vm.indexOfNode(id) < 0, "node gone");

    // Failed edits report the engine error and leave the graph untouched.
    check(createNode(e, 0, "no_such_def", 7) != 0, "bad def reports error");
    check(connectNodes(e, 0, 99, 0, 0, 0) != 0, "bad connect reports error");
    check(!poller.poll(0, vm), "failed edits do not bump the generation");
    check(errText(tzpl_errChanMismatch) == "channel count mismatch",
          "errText maps codes");

    engine::freeEngine(e);
}

// 13.3: materialize a node's control interface as engine-bound widgets.
static void test_node_controls_materialize() {
    std::print("Test: materializeNodeControls\n");

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    engine::EngineConfig config;
    config.numSilos = 1;
    engine::Engine* e = engine::newEngine(config, params);

    // Register a def with a control of each kind, single- and multi-
    // channel (funs never called: no node is instantiated, only the
    // def's ControlSpecs are read).
    tzpl_SignalType ctlType{tzpl_kF32, tzpl_eventRate, 1};
    tzpl_SignalType ctlType4{tzpl_kF32, tzpl_eventRate, 4};
    tzpl_SignalType ctlType3{tzpl_kF32, tzpl_eventRate, 3};
    tzpl_ControlDef ctls[6] = {
        {"freq", ctlType, 1,
         {20., 20000., 440., 0., tzpl_cwExponential, tzpl_ckContinuous}},
        {"gate", ctlType, 2, {0., 1., 0., 0., tzpl_cwLinear, tzpl_ckBoolean}},
        {"trig", ctlType, 3, {0., 1., 0., 0., tzpl_cwLinear, tzpl_ckTrigger}},
        {"levels", ctlType4, 4,
         {0., 1., 0.5, 0., tzpl_cwLinear, tzpl_ckContinuous}},
        {"mutes", ctlType3, 5, {0., 1., 0., 0., tzpl_cwLinear, tzpl_ckBoolean}},
        {"trigs", ctlType3, 6, {0., 1., 0., 0., tzpl_cwLinear, tzpl_ckTrigger}},
    };
    tzpl_SynthDef def{};
    def.name = "ctl_test";
    def.num_controls = 6;
    def.controls = ctls;
    engine::addSynthDef(e, def);

    bridge::UIState ui;
    int n = bridge::materializeNodeControls(&ui, e, "ctl_test #5", 5, 0,
                                            "ctl_test");
    check(n == 6, "all controls materialized");

    {
        std::lock_guard<std::mutex> lock(ui.mtx);
        auto* freq = ui.findByName("ctl_test #5", "freq");
        auto* gate = ui.findByName("ctl_test #5", "gate");
        auto* trig = ui.findByName("ctl_test #5", "trig");
        check(freq && freq->kind == bridge::UIWidgetKind::Slider,
              "continuous control becomes a slider");
        check(gate && gate->kind == bridge::UIWidgetKind::Toggle,
              "boolean control becomes a toggle");
        check(trig && trig->kind == bridge::UIWidgetKind::Button,
              "trigger control becomes a button");
        check(freq && freq->spec.lo == 20. && freq->spec.hi == 20000.
                   && freq->spec.init == 440.,
              "spec lo/hi/init carried through");
        check(freq && freq->spec.warp == bridge::UIWarp::Exponential,
              "ABI warp ordinal mapped to UIWarp");
        check(freq && freq->target
                   && freq->target->nodeID == 5 && freq->target->controlID == 1
                   && freq->target->silo == 0,
              "widget bound to (node, controlID, silo)");
        check(freq && freq->dirtyEngine,
              "fresh binding marked for engine push");

        // Multichannel: one widget carrying all channels (the dispatcher
        // sends the full value vector as one setControl).
        auto* levels = ui.findByName("ctl_test #5", "levels");
        check(levels && levels->kind == bridge::UIWidgetKind::MultiSlider
                     && levels->values.size() == 4,
              "multichannel continuous becomes a 4-bar multislider");
        check(levels && levels->values[0] == 0.5 && levels->values[3] == 0.5,
              "multislider bars start at spec init");
        auto* mutes = ui.findByName("ctl_test #5", "mutes");
        check(mutes && mutes->kind == bridge::UIWidgetKind::ButtonMatrix
                    && mutes->rows == 1 && mutes->cols == 3
                    && mutes->values.size() == 3 && !mutes->momentary,
              "multichannel boolean becomes a 1x3 toggle row");
        auto* trigs = ui.findByName("ctl_test #5", "trigs");
        check(trigs && trigs->kind == bridge::UIWidgetKind::ButtonMatrix
                    && trigs->rows == 1 && trigs->cols == 3
                    && trigs->momentary,
              "multichannel trigger becomes a 1x3 momentary row");
        check(levels && levels->target && levels->target->controlID == 4
                     && levels->dirtyEngine,
              "multichannel widget bound and marked for push");
    }

    // Idempotent: reopening rebinds without duplicating widgets.
    size_t before;
    { std::lock_guard<std::mutex> lock(ui.mtx); before = ui.widgets.size(); }
    check(bridge::materializeNodeControls(&ui, e, "ctl_test #5", 5, 0,
                                          "ctl_test") == 6,
          "second materialize rebinds");
    { std::lock_guard<std::mutex> lock(ui.mtx);
      check(ui.widgets.size() == before, "no duplicate widgets"); }

    check(bridge::materializeNodeControls(&ui, e, "x", 1, 0, "no_such") == -1,
          "unknown def reports -1");

    engine::freeEngine(e);
}

// ---------------------------------------------------------------------------
// Graph-view per-node meters (MeterStore): tap ownership and lifetime.
// ---------------------------------------------------------------------------

static void test_graph_meters() {
    std::print("Test: graph-view node meters\n");

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    engine::EngineConfig config;
    config.numSilos = 1;
    engine::Engine* e = engine::newEngine(config, params);
    engine::createSineNode(e);

    GraphPoller poller(e);
    GraphViewModel vm;
    poller.poll(0, vm);
    long long id = nextFreeNodeID(vm);
    createNode(e, 0, "sinosc", id);
    poller.poll(0, vm);

    MeterStore meters;
    MeterKey k{0, id, 0};

    check(meters.enable(e, k) == 0, "enable installs a tap");
    check(meters.enabled(k), "the meter is registered");
    check(meters.count() == 1, "one meter");
    check(engine::tapExists(e, meters.find(k)->tapID), "the engine tap exists");

    check(meters.enable(e, k) == 0 && meters.count() == 1,
          "enabling twice is idempotent");

    long long tapID = meters.find(k)->tapID;
    meters.disable(e, k);
    check(!meters.enabled(k) && meters.count() == 0, "disable removes the meter");
    check(!engine::tapExists(e, tapID), "and untaps the engine");

    // Whole node: one tap per f32 outlet.
    check(meters.enableNode(e, 0, vm, id) == 0, "enableNode succeeds");
    int outs = (int)vm.nodes[(size_t)vm.indexOfNode(id)].outs.size();
    check(meters.count() == outs,
          std::format("one meter per outlet (got {}, want {})",
                      meters.count(), outs));
    check(meters.nodeEnabled(0, id), "nodeEnabled reports the node");
    check(!meters.nodeEnabled(0, 0), "an unmetered node reports false");

    // prune() must clean up after a node disappears: the engine drops the
    // dead node's RT tap entry but leaves the registry slot alive, so nothing
    // else would ever free it.
    long long orphan = meters.find({0, id, 0})->tapID;
    check(freeGraphNode(e, 0, id) == 0, "free the metered node");
    poller.poll(0, vm);
    meters.prune(e, vm);
    check(meters.count() == 0, "prune drops meters on the freed node");
    check(!engine::tapExists(e, orphan), "and untaps them");

    // clear() releases everything (what GraphView's destructor relies on).
    long long id2 = nextFreeNodeID(vm);
    createNode(e, 0, "sinosc", id2);
    poller.poll(0, vm);
    meters.enableNode(e, 0, vm, id2);
    check(meters.count() > 0, "meters re-enabled on a new node");
    meters.clear(e);
    check(meters.count() == 0, "clear releases every meter");

    check(meters.enableNode(e, 0, vm, 4242) == tzpl_errNodeNotFound,
          "enableNode on an unknown node reports not-found");

    engine::freeEngine(e);
}

// Write a minimal 16-bit mono PCM WAV so the waveform/buffer paths can be
// tested without repo audio assets.
static std::string writeTestWav(int frames) {
    std::string path = "/tmp/tzpl_graph_test.wav";
    std::vector<std::int16_t> samples(frames);
    for (int i = 0; i < frames; ++i)
        samples[i] = (std::int16_t)(30000 * std::sin(i * 0.05));

    std::uint32_t dataBytes = (std::uint32_t)(samples.size() * 2);
    std::uint32_t riffBytes = 36 + dataBytes;
    std::uint32_t rate = 44100, byteRate = rate * 2;
    std::uint16_t fmt = 1, chans = 1, align = 2, bits = 16;
    std::uint32_t fmtSize = 16;

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return {};
    fwrite("RIFF", 1, 4, f); fwrite(&riffBytes, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&chans, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(samples.data(), 2, samples.size(), f);
    fclose(f);
    return path;
}

static void test_buffer_helpers() {
    std::print("Test: waveform widget + buffer file loading\n");

    std::string wav = writeTestWav(5000);
    check(!wav.empty(), "test wav written");

    bridge::UIState ui;
    auto id = bridge::bindWaveformWidget(&ui, "p", "sample", wav.c_str());
    check(id != 0, "waveform widget built from file");
    {
        std::lock_guard<std::mutex> lock(ui.mtx);
        auto* w = ui.findByName("p", "sample");
        check(w && w->kind == bridge::UIWidgetKind::Waveform
                && w->waveFrames == 5000
                && !w->waveMin.empty()
                && w->waveMin.size() == w->waveMax.size(),
              "waveform overview populated");
    }
    check(bridge::bindWaveformWidget(&ui, "p", "x", "/no/such/file.wav") == 0,
          "unreadable file reports 0");

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    engine::EngineConfig config;
    config.numSilos = 1;
    engine::Engine* e = engine::newEngine(config, params);

    long long frames = 0;
    check(loadBufferFile(e, 0, 10, 0, "/no/such/file.wav", &frames) != 0,
          "loadBufferFile reports unreadable file synchronously");
    check(loadBufferFile(e, 0, 10, 0, wav, &frames) == 0 && frames == 5000,
          "loadBufferFile submits and reports frame count");

    engine::freeEngine(e);
    remove(wav.c_str());
}

static void test_determinism() {
    std::print("Test: layout determinism\n");
    DefTable t;
    t.def("Audio Out", 1, 0);
    t.def("osc", 0, 1);
    t.def("mix", 4, 1);

    engine::GraphDesc g;
    g.nodes = {{0, "Audio Out"}, {10, "osc"}, {11, "osc"}, {12, "osc"}, {20, "mix"}};
    g.conns = {{10, 0, 20, 0}, {11, 0, 20, 1}, {12, 0, 20, 2}, {20, 0, 0, 0}};

    auto vm1 = build(g, t);
    auto vm2 = build(g, t);
    LayoutStore store;
    autoLayout(vm1, store);
    autoLayout(vm2, store);

    bool same = true;
    for (size_t i = 0; i < vm1.nodes.size(); ++i)
        same = same && vm1.nodes[i].x == vm2.nodes[i].x
                    && vm1.nodes[i].y == vm2.nodes[i].y;
    check(same, "same graph lays out identically");
}

int main() {
    std::print("=== Graph layout tests ===\n\n");
    test_view_model();
    test_chain_layout();
    test_fan_in_and_cycle();
    test_unreachable_placed_after_sources();
    test_unconnected_sink_parks_right();
    test_store_overrides();
    test_edit_helpers();
    test_edit_submitters_live();
    test_node_controls_materialize();
    test_graph_meters();
    test_buffer_helpers();
    test_determinism();
    std::print("\n=== {} passed, {} failed ===\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
