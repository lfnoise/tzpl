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
//  doc_roundtrip_test.cpp
//  app
//
//  Headless tests for the notebook document layer: save/load round trips,
//  content hashing, interning, and history persistence. Runs with a
//  default (all-null) bridge::AppContext -- document.cpp guards every ctx
//  use, so no engine or UI registry is needed.
//
//  Usage:
//      tzpl_doc_tests                 run all tests (asserts; exit 0 = pass)
//      tzpl_doc_tests resave IN OUT   load IN headlessly, save to OUT
//                                     (byte-identity instrumentation)
//

#include "content_hash.hpp"
#include "document.hpp"
#include "module_compiler.hpp"   // complete type for AppContext's SiloVMState
#include "tzpl_app_context.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static int resave(char const* inPath, char const* outPath) {
    bridge::AppContext ctx;   // all subsystem pointers null: headless
    std::string err;
    auto snap = doc::loadDocument(ctx, inPath, err);
    if (!snap) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    if (!doc::saveDocument(*snap, ctx, outPath, err)) {
        std::fprintf(stderr, "save failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("resaved %s -> %s\n", inPath, outPath);
    return 0;
}

// --- test helpers ---

static doc::Preset makePreset(std::string name, double v0, double v1) {
    doc::Preset p;
    p.name = std::move(name);
    p.entries.push_back({"panel", "freq", {v0}});
    p.entries.push_back({"panel", "amp", {v1}});
    return p;
}

static doc::WidgetSnap makeSnap(std::string name, double value) {
    doc::WidgetSnap s;
    s.panel = "panel";
    s.name = std::move(name);
    s.values = {value};
    return s;
}

static std::shared_ptr<doc::WidgetSnapList const>
makeSnapList(std::initializer_list<doc::WidgetSnap> snaps) {
    auto out = std::make_shared<doc::WidgetSnapList>();
    for (auto const& s : snaps)
        out->push_back(std::make_shared<doc::WidgetSnap const>(s));
    return out;
}

static void testContentHash() {
    // Equal content, independently built -> equal hash; any field
    // perturbation -> different hash (FNV collisions aside).
    auto p1 = makePreset("a", 440.0, 0.5);
    auto p2 = makePreset("a", 440.0, 0.5);
    assert(doc::contentHashOf(p1) == doc::contentHashOf(p2));
    assert(p1 == p2);
    auto p3 = makePreset("a", 440.0, 0.6);
    auto p4 = makePreset("b", 440.0, 0.5);
    assert(doc::contentHashOf(p1) != doc::contentHashOf(p3));
    assert(doc::contentHashOf(p1) != doc::contentHashOf(p4));

    auto s1 = makeSnap("freq", 440.0);
    auto s2 = makeSnap("freq", 440.0);
    assert(doc::contentHashOf(s1) == doc::contentHashOf(s2));
    assert(s1 == s2);
    s2.rollEdo = 19;
    doc::WidgetSnap s2b = s2;             // copy resets the cache
    assert(doc::contentHashOf(s2b) != doc::contentHashOf(s1));

    // The cache resets across copy-then-mutate (the mutableCell pattern).
    doc::Cell c1;
    c1.id = 7;
    c1.text = "x = 1;";
    std::uint64_t h1 = doc::contentHashOf(c1);
    doc::Cell c2 = c1;
    c2.text = "x = 2;";
    assert(doc::contentHashOf(c2) != h1);
    std::printf("content hash: ok\n");
}

static void testInterner() {
    doc::Interner<doc::Preset> pool;
    auto a = std::make_shared<doc::Preset const>(makePreset("a", 1.0, 2.0));
    auto b = std::make_shared<doc::Preset const>(makePreset("a", 1.0, 2.0));
    auto c = std::make_shared<doc::Preset const>(makePreset("c", 1.0, 2.0));
    assert(pool.intern(a) == a);
    assert(pool.intern(b) == a);          // equal content collapses
    assert(pool.intern(c) == c);          // distinct content stays distinct
    assert(pool.liveCount() == 2);
    std::printf("interner: ok\n");
}

static void testStoreDedup() {
    doc::DocumentStore store;
    store.rerootHistory("start");

    // Equal presets installed twice collapse to one object.
    doc::CellId id = store.insertCell(0, doc::CellKind::Presets, "bank");
    store.setCellPresets(id, {std::make_shared<doc::Preset const>(
                                  makePreset("a", 1.0, 2.0))});
    auto first = store.cell(id)->presets[0];
    store.setCellPresets(id, {std::make_shared<doc::Preset const>(
                                  makePreset("a", 1.0, 2.0))});
    assert(store.cell(id)->presets[0] == first);

    // Widget toggle A -> B -> A across commits: the third state's snap is
    // pointer-identical to the first (content interning, not prev-reuse).
    store.setWidgetSnap(makeSnapList({makeSnap("freq", 1.0)}));
    store.commit("A");
    auto snapA = (*store.snapshot()->widgets)[0];
    store.setWidgetSnap(makeSnapList({makeSnap("freq", 2.0)}));
    store.commit("B");
    store.setWidgetSnap(makeSnapList({makeSnap("freq", 1.0)}));
    bool committed = store.commit("A again");
    assert((*store.snapshot()->widgets)[0] == snapA);

    // Snapshot-level interning: returning exactly to the previous state
    // must still record a node (undo history), but the SNAPSHOT object is
    // shared with the earlier node.
    assert(committed);
    auto cursorSnap = store.historyCursor()->snap;
    auto nodeA = store.historyCursor()->parent->parent;   // "A"
    assert(doc::sameSnapshot(*cursorSnap, *nodeA->snap));
    assert(cursorSnap == nodeA->snap);   // interned to the same object

    // Working snapshot pointer-equals the tip: immediate commit no-ops.
    assert(!store.commit("noop"));
    std::printf("store dedup: ok\n");
}

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "resave") == 0) {
        return resave(argv[2], argv[3]);
    }
    testContentHash();
    testInterner();
    testStoreDedup();
    std::printf("all doc tests passed\n");
    return 0;
}
