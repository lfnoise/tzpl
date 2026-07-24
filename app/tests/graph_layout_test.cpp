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

#include "graph_layout.hpp"
#include "graph_model.hpp"

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
    test_store_overrides();
    test_determinism();
    std::print("\n=== {} passed, {} failed ===\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
