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
//      tzpl_doc_tests filtercells IN OUT CMD
//                                     pipe each prose/code cell's text
//                                     through shell command CMD (text on
//                                     stdin, result on stdout) and save the
//                                     result. For batch source edits across
//                                     notebooks (API migrations etc.).
//      tzpl_doc_tests dumpcells IN    print code cells separated by %% lines
//                                     (the TZPL_EVAL_CELLS cell-runner format)
//

#include "content_hash.hpp"
#include "document.hpp"
#include "module_compiler.hpp"   // complete type for AppContext's SiloVMState
#include "tzpl_app_context.hpp"
#include "tzpl_sexpr_bin.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

// CHECK() vanishes under NDEBUG (Release builds); tests must always check.
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,   \
                         __LINE__, #cond);                                   \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

static int resave(char const* inPath, char const* outPath) {
    bridge::AppContext ctx;   // all subsystem pointers null: headless
    std::string err;
    doc::DocumentStore store;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx, inPath, err, &store.interns(), &hist);
    if (!snap) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    store.reset(std::move(snap), inPath);
    if (hist.root) store.adoptHistory(std::move(hist.root), hist.cursor);
    if (!doc::saveDocument(store, ctx, outPath, err)) {
        std::fprintf(stderr, "save failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("resaved %s -> %s\n", inPath, outPath);
    return 0;
}

// Run one cell's text through the filter command via temp files. Returns
// false on a filter failure.
static bool runCellFilter(std::string const& cmd, std::string const& text,
                          std::string& out) {
    namespace fs = std::filesystem;
    fs::path tmpIn  = fs::temp_directory_path() / "tzpl_doc_cell_in.txt";
    fs::path tmpOut = fs::temp_directory_path() / "tzpl_doc_cell_out.txt";
    {
        std::ofstream f(tmpIn, std::ios::binary);
        f << text;
    }
    std::string sh = cmd + " < \"" + tmpIn.string() + "\" > \""
                   + tmpOut.string() + "\"";
    if (std::system(sh.c_str()) != 0) return false;
    std::ifstream f(tmpOut, std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    // Line filters (sed & co) append a final newline the cell may not have
    // had; don't let that count as (or pollute) a real edit.
    if (!out.empty() && out.back() == '\n'
        && (text.empty() || text.back() != '\n')) {
        out.pop_back();
    }
    return true;
}

static int filterCells(char const* inPath, char const* outPath,
                       char const* cmd) {
    bridge::AppContext ctx;   // all subsystem pointers null: headless
    std::string err;
    doc::DocumentStore store;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx, inPath, err, &store.interns(), &hist);
    if (!snap) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    store.reset(std::move(snap), inPath);
    if (hist.root) store.adoptHistory(std::move(hist.root), hist.cursor);

    int changed = 0;
    for (int i = 0; i < store.cellCount(); ++i) {
        auto cell = store.cellAt(i);
        if (cell->kind != doc::CellKind::Code
            && cell->kind != doc::CellKind::Prose) continue;
        std::string out;
        if (!runCellFilter(cmd, cell->text, out)) {
            std::fprintf(stderr, "filter failed on cell %d of %s\n", i, inPath);
            return 1;
        }
        if (out != cell->text) {
            store.setCellText(cell->id, out);
            ++changed;
        }
    }

    if (!doc::saveDocument(store, ctx, outPath, err)) {
        std::fprintf(stderr, "save failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("filtered %s -> %s: %d cell(s) changed\n", inPath, outPath,
                changed);
    return 0;
}

// Print a document's code cells in the TZPL_EVAL_CELLS cell-runner format
// (cells separated by lines containing only "%%"), so notebooks can be
// evaluated headlessly:  tzpl_doc_tests dumpcells nb.tzd | TZPL_EVAL_CELLS=1 tzpl_app ...
static int dumpCells(char const* inPath) {
    bridge::AppContext ctx;
    std::string err;
    doc::InternPool pool;
    auto snap = doc::loadDocument(ctx, inPath, err, &pool, nullptr);
    if (!snap) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 1;
    }
    for (auto const& cell : snap->cells) {
        if (cell->kind != doc::CellKind::Code) continue;
        std::printf("%s\n%%%%\n", cell->text.c_str());
    }
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
    CHECK(doc::contentHashOf(p1) == doc::contentHashOf(p2));
    CHECK(p1 == p2);
    auto p3 = makePreset("a", 440.0, 0.6);
    auto p4 = makePreset("b", 440.0, 0.5);
    CHECK(doc::contentHashOf(p1) != doc::contentHashOf(p3));
    CHECK(doc::contentHashOf(p1) != doc::contentHashOf(p4));

    auto s1 = makeSnap("freq", 440.0);
    auto s2 = makeSnap("freq", 440.0);
    CHECK(doc::contentHashOf(s1) == doc::contentHashOf(s2));
    CHECK(s1 == s2);
    s2.rollEdo = 19;
    doc::WidgetSnap s2b = s2;             // copy resets the cache
    CHECK(doc::contentHashOf(s2b) != doc::contentHashOf(s1));

    // The cache resets across copy-then-mutate (the mutableCell pattern).
    doc::Cell c1;
    c1.id = 7;
    c1.text = "x = 1;";
    std::uint64_t h1 = doc::contentHashOf(c1);
    doc::Cell c2 = c1;
    c2.text = "x = 2;";
    CHECK(doc::contentHashOf(c2) != h1);
    std::printf("content hash: ok\n");
}

static void testInterner() {
    doc::Interner<doc::Preset> pool;
    auto a = std::make_shared<doc::Preset const>(makePreset("a", 1.0, 2.0));
    auto b = std::make_shared<doc::Preset const>(makePreset("a", 1.0, 2.0));
    auto c = std::make_shared<doc::Preset const>(makePreset("c", 1.0, 2.0));
    CHECK(pool.intern(a) == a);
    CHECK(pool.intern(b) == a);          // equal content collapses
    CHECK(pool.intern(c) == c);          // distinct content stays distinct
    CHECK(pool.liveCount() == 2);
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
    CHECK(store.cell(id)->presets[0] == first);

    // Widget toggle A -> B -> A across commits: the third state's snap is
    // pointer-identical to the first (content interning, not prev-reuse).
    store.setWidgetSnap(makeSnapList({makeSnap("freq", 1.0)}));
    store.commit("A");
    auto snapA = (*store.snapshot()->widgets)[0];
    store.setWidgetSnap(makeSnapList({makeSnap("freq", 2.0)}));
    store.commit("B");
    store.setWidgetSnap(makeSnapList({makeSnap("freq", 1.0)}));
    bool committed = store.commit("A again");
    CHECK((*store.snapshot()->widgets)[0] == snapA);

    // Snapshot-level interning: returning exactly to the previous state
    // must still record a node (undo history), but the SNAPSHOT object is
    // shared with the earlier node.
    CHECK(committed);
    auto cursorSnap = store.historyCursor()->snap;
    auto nodeA = store.historyCursor()->parent->parent;   // "A"
    CHECK(doc::sameSnapshot(*cursorSnap, *nodeA->snap));
    CHECK(cursorSnap == nodeA->snap);   // interned to the same object

    // Working snapshot pointer-equals the tip: immediate commit no-ops.
    CHECK(!store.commit("noop"));
    std::printf("store dedup: ok\n");
}

static std::string tempPath(char const* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// Walk two history trees in parallel, asserting identical shape, labels,
// activeChild, and content-equal snapshots.
static void assertSameTree(doc::HistNode const* a, doc::HistNode const* b) {
    CHECK(a && b);
    CHECK(a->label == b->label);
    CHECK(a->activeChild == b->activeChild);
    CHECK(doc::sameSnapshot(*a->snap, *b->snap));
    CHECK(a->children.size() == b->children.size());
    for (std::size_t i = 0; i < a->children.size(); ++i)
        assertSameTree(a->children[i].get(), b->children[i].get());
}

static void testHistoryRoundTrip() {
    bridge::AppContext ctx;
    std::string err;
    std::string path = tempPath("tzpl_doc_roundtrip.tzd");

    // Build a store with edits, a widget capture, presets, and an
    // undo-branch.
    doc::DocumentStore store;
    store.rerootHistory("start");
    auto id = store.insertCell(0, doc::CellKind::Code, "", "a();");
    store.commit("add cell");
    store.setCellText(id, "b();");
    store.commit("edit b");
    CHECK(store.undo());
    store.setCellText(id, "c();");
    store.commit("edit c");                     // branch off "add cell"
    store.setWidgetSnap(makeSnapList({makeSnap("freq", 1.0)}));
    store.commit("widget");
    auto pid = store.insertCell(1, doc::CellKind::Presets, "bank");
    store.setCellPresets(pid, {std::make_shared<doc::Preset const>(
                                   makePreset("a", 1.0, 2.0))});
    store.commit("preset");

    CHECK(doc::saveDocument(store, ctx, path, err));

    // Reload into a fresh store, emulating NotebookPanel::open: reset,
    // re-capture widgets (headless: install the equivalent list by hand),
    // adopt the history.
    doc::DocumentStore store2;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx, path, err, &store2.interns(), &hist);
    CHECK(snap);
    store2.reset(std::move(snap), path);
    CHECK(hist.root && hist.cursor);
    store2.setWidgetSnap(makeSnapList({makeSnap("freq", 1.0)}));
    store2.adoptHistory(std::move(hist.root), hist.cursor);

    // The whole tree round-trips: shape, labels, redo path, content.
    assertSameTree(store.historyRoot(), store2.historyRoot());

    // Cursor restored; working snapshot unified with it (no phantom node
    // on the next commit).
    CHECK(store2.historyCursor()->label == "preset");
    CHECK(store2.snapshot() == store2.historyCursor()->snap);
    CHECK(!store2.commit("noop"));

    // Branch structure: "add cell" has both edits, redo path follows the
    // branch that was taken.
    auto const* root2 = store2.historyRoot();
    CHECK(root2->label == "start" && root2->children.size() == 1);
    auto const* add = root2->children[0].get();
    CHECK(add->children.size() == 2);
    CHECK(add->children[0]->label == "edit b");
    CHECK(add->children[1]->label == "edit c");
    CHECK(add->activeChild == 1);

    // Content addressing on disk + intern-on-load: the same WidgetSnap
    // object serves the "widget" and "preset" nodes, and the working
    // snapshot's capture collapsed onto it too.
    auto const* widgetNode = add->children[1]->children[0].get();
    auto const* presetNode = widgetNode->children[0].get();
    CHECK((*widgetNode->snap->widgets)[0] == (*presetNode->snap->widgets)[0]);
    CHECK((*store2.snapshot()->widgets)[0]
           == (*widgetNode->snap->widgets)[0]);

    // Undo walks the restored tree and restores content.
    CHECK(store2.canUndo());
    auto prev = store2.undo();
    CHECK(prev && doc::sameSnapshot(*prev, *widgetNode->snap));

    std::filesystem::remove(path);
    std::printf("history round trip: ok\n");
}

// The dirty flag tracks the state the FILE holds, not "something
// happened": navigating history (or typing an edit back out) returns the
// document to its saved state and must clear it again.
static void testDirtyFlag() {
    bridge::AppContext ctx;
    std::string err;
    std::string path = tempPath("tzpl_doc_dirty.tzd");

    doc::DocumentStore store;
    store.rerootHistory("start");
    store.clearModified();
    CHECK(!store.modified());
    auto id = store.insertCell(0, doc::CellKind::Code, "", "a();");
    CHECK(store.modified());
    store.commit("add cell");
    store.setCellText(id, "b();");
    store.commit("edit b");
    CHECK(store.modified());
    CHECK(doc::saveDocument(store, ctx, path, err));
    store.clearModified();
    CHECK(!store.modified());

    // Reload, emulating NotebookView::openFile.
    doc::DocumentStore store2;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx, path, err, &store2.interns(), &hist);
    CHECK(snap);
    store2.reset(std::move(snap), path);
    CHECK(hist.root && hist.cursor);
    store2.adoptHistory(std::move(hist.root), hist.cursor);
    store2.clearModified();
    CHECK(!store2.modified());

    // History window: jump to an earlier state, then back to the one the
    // file was saved at.
    auto* root = store2.historyRoot();
    auto* tip = root->children[0]->children[0].get();
    CHECK(store2.historyCursor() == tip);
    CHECK(store2.jumpTo(root));
    CHECK(store2.modified());
    CHECK(store2.jumpTo(tip));
    CHECK(!store2.modified());

    // Same for undo/redo.
    CHECK(store2.undo());
    CHECK(store2.modified());
    CHECK(store2.redo());
    CHECK(!store2.modified());

    // An edit typed back out is not a change either.
    std::string const original = store2.cell(id)->text;
    store2.setCellText(id, original + " -- typo");
    CHECK(store2.modified());
    store2.setCellText(id, original);
    CHECK(!store2.modified());

    // But a committed node IS unsaved state even back at the saved cursor
    // with the saved content: the node itself lives in the file.
    store2.setCellText(id, "c();");
    store2.commit("edit c");
    CHECK(store2.modified());
    CHECK(store2.undo());
    CHECK(store2.historyCursor() == tip);
    CHECK(store2.snapshot() == tip->snap);
    CHECK(store2.modified());

    std::filesystem::remove(path);
    std::printf("dirty flag: ok\n");
}

static void testV1Compat() {
    bridge::AppContext ctx;
    std::string err;

    // Synthesize a v1 document (5 children, version 1, no history child)
    // and check it loads with empty history. Self-contained: the example
    // .tzd files in the repo get re-saved as v2 over time.
    {
        using tzpl::sbin::Value;
        Value root = Value::Vec({
            Value::Symbol("doc"),
            Value::Int(1),
            Value::Int(2),
            Value::Vec({Value::Symbol("cells"),
                        Value::Vec({Value::Symbol("cell"), Value::Int(1),
                                    Value::Int(1), Value::String(""),
                                    Value::String("x();"),
                                    Value::Bool(false)})}),
            Value::Vec({Value::Symbol("panels")}),
        });
        auto bytes = tzpl::sbin::encode(root);
        std::string path = tempPath("tzpl_doc_v1.tzd");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<char const*>(bytes.data()),
                      (std::streamsize)bytes.size());
        }
        doc::DocumentStore store;
        doc::LoadedHistory hist;
        auto snap = doc::loadDocument(ctx, path, err, &store.interns(), &hist);
        CHECK(snap && "v1 document must load");
        CHECK(snap->cells.size() == 1 && snap->cells[0]->text == "x();");
        CHECK(!hist.root && "v1 files carry no history");
        std::filesystem::remove(path);
    }

#ifdef TZPL_EXAMPLES_DIR
    // The shipped examples (whatever version they were last saved as)
    // must load cleanly.
    for (char const* name : {"piano_roll.tzd", "ui_controls_layout.tzd"}) {
        std::string path = std::string(TZPL_EXAMPLES_DIR) + "/" + name;
        if (!std::filesystem::exists(path)) continue;
        doc::DocumentStore store;
        doc::LoadedHistory hist;
        auto snap = doc::loadDocument(ctx, path, err, &store.interns(), &hist);
        CHECK(snap && "example must load");
    }
#endif
    std::printf("v1 compat: ok\n");
}

static int countNodes(doc::HistNode const* n) {
    int c = 1;
    for (auto const& ch : n->children) c += countNodes(ch.get());
    return c;
}

static void testHistorySize() {
    // 120 commits toggling one widget between two states: the file must
    // grow with UNIQUE content (2 widget states, ~2 snapshots, 1 chunky
    // cell), not node count x document size. Naive duplication would be
    // ~120 x 2KB; content addressing keeps it to the tree records.
    bridge::AppContext ctx;
    std::string err;
    std::string path = tempPath("tzpl_doc_size.tzd");
    doc::DocumentStore store;
    store.rerootHistory("start");
    store.insertCell(0, doc::CellKind::Code, "", std::string(2000, 'x'));
    store.commit("big cell");
    for (int i = 0; i < 120; ++i) {
        store.setWidgetSnap(
            makeSnapList({makeSnap("freq", (i % 2) ? 1.0 : 2.0)}));
        store.commit("toggle");
    }
    CHECK(doc::saveDocument(store, ctx, path, err));
    auto size = std::filesystem::file_size(path);
    CHECK(size < 32768);   // naive would exceed 240KB

    // The full 122-node tree survives the round trip.
    doc::DocumentStore store2;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx, path, err, &store2.interns(), &hist);
    CHECK(snap && hist.root);
    CHECK(countNodes(hist.root.get()) == 122);
    std::filesystem::remove(path);
    std::printf("history size: ok (%lld bytes for 122 nodes)\n",
                (long long)size);
}

static void testMalformedHistory() {
    // A version-2 document whose history child is garbage must load its
    // current state and silently drop the history.
    using tzpl::sbin::Value;
    Value root = Value::Vec({
        Value::Symbol("doc"),
        Value::Int(2),
        Value::Int(1),
        Value::Vec({Value::Symbol("cells")}),
        Value::Vec({Value::Symbol("panels")}),
        Value::Int(12345),   // not a history vec
    });
    auto bytes = tzpl::sbin::encode(root);
    std::string path = tempPath("tzpl_doc_badhist.tzd");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<char const*>(bytes.data()),
                  (std::streamsize)bytes.size());
    }
    bridge::AppContext ctx;
    std::string err;
    doc::DocumentStore store;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx, path, err, &store.interns(), &hist);
    CHECK(snap && "document must load despite bad history");
    CHECK(!hist.root);
    std::filesystem::remove(path);
    std::printf("malformed history: ok\n");
}

static void testButtonMatrix() {
    // ButtonMatrix document state (momentary flag + per-cell labels)
    // hashes, saves, and loads through both the live panels section and
    // the history tables.
    auto bm = makeSnap("pads", 0.0);
    bm.kind = bridge::UIWidgetKind::ButtonMatrix;
    bm.rows = 2;
    bm.cols = 2;
    bm.values = {0.0, 1.0, 0.0, 1.0};
    bm.cellLabels = {"a", "b", "c", "d"};
    auto bmSame = bm;
    CHECK(doc::contentHashOf(bm) == doc::contentHashOf(bmSame));
    CHECK(bm == bmSame);
    auto bmLabel = bm;
    bmLabel.cellLabels[3] = "D";
    CHECK(doc::contentHashOf(bmLabel) != doc::contentHashOf(bm));
    CHECK(!(bmLabel == bm));
    auto bmMode = bm;
    bmMode.momentary = true;
    CHECK(doc::contentHashOf(bmMode) != doc::contentHashOf(bm));
    CHECK(!(bmMode == bm));

    // Live widget -> save -> load -> live widget, through a real UIState.
    bridge::UIState ui;
    bridge::AppContext ctx;
    ctx.uiState = &ui;
    std::vector<double> const vals{1, 0, 0, 0, 1, 0};
    std::vector<std::string> const labels{"kick", "snare", "hat",
                                          "tom", "clap", "ride"};
    {
        std::lock_guard<std::mutex> lock(ui.mtx);
        auto* w = ui.upsert("pads", "grid", bridge::UIWidgetKind::ButtonMatrix,
                            bridge::UISpec{}, bridge::UISpec{});
        w->rows = 2;
        w->cols = 3;
        w->values = vals;
        w->momentary = true;
        w->cellLabels = labels;
    }
    doc::DocumentStore store;
    store.rerootHistory("start");
    store.insertCell(0, doc::CellKind::Panel, "pads");
    store.setWidgetSnap(doc::captureWidgets(&ui, {"pads"}));
    store.commit("grid");
    std::string err;
    std::string path = tempPath("tzpl_doc_buttonmatrix.tzd");
    CHECK(doc::saveDocument(store, ctx, path, err));

    bridge::UIState ui2;
    bridge::AppContext ctx2;
    ctx2.uiState = &ui2;
    doc::DocumentStore store2;
    doc::LoadedHistory hist;
    auto snap = doc::loadDocument(ctx2, path, err, &store2.interns(), &hist);
    CHECK(snap);
    {
        std::lock_guard<std::mutex> lock(ui2.mtx);
        auto* w = ui2.findByName("pads", "grid");
        CHECK(w && w->kind == bridge::UIWidgetKind::ButtonMatrix);
        CHECK(w->rows == 2 && w->cols == 3);
        CHECK(w->momentary);
        CHECK(w->values == vals);
        CHECK(w->cellLabels == labels);
    }
    // The history tables carry the new fields too.
    CHECK(hist.root && !hist.root->children.empty());
    auto const* node = hist.root->children[0].get();
    CHECK(node->snap->widgets && node->snap->widgets->size() == 1);
    auto const& s = *(*node->snap->widgets)[0];
    CHECK(s.kind == bridge::UIWidgetKind::ButtonMatrix);
    CHECK(s.momentary);
    CHECK(s.values == vals && s.cellLabels == labels);
    std::filesystem::remove(path);
    std::printf("button matrix: ok\n");
}

int main(int argc, char** argv) {
    if (argc == 5 && std::strcmp(argv[1], "filtercells") == 0) {
        return filterCells(argv[2], argv[3], argv[4]);
    }
    if (argc == 3 && std::strcmp(argv[1], "dumpcells") == 0) {
        return dumpCells(argv[2]);
    }
    if (argc == 4 && std::strcmp(argv[1], "resave") == 0) {
        return resave(argv[2], argv[3]);
    }
    testContentHash();
    testInterner();
    testStoreDedup();
    testHistoryRoundTrip();
    testDirtyFlag();
    testV1Compat();
    testHistorySize();
    testMalformedHistory();
    testButtonMatrix();
    std::printf("all doc tests passed\n");
    return 0;
}
