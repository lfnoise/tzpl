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

#include "document.hpp"
#include "content_hash.hpp"

#include "tzpl_app_context.hpp"
#include "tzpl_ui_state.hpp"
#include "tzpl_ui_taps.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_sexpr_bin.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>

namespace doc {

using tzpl::sbin::Value;
using tzpl::sbin::Reader;
using tzpl::sbin::Tag;

// ---------------------------------------------------------------------------
// DocumentStore
// ---------------------------------------------------------------------------

DocumentStore::DocumentStore()
    : interns_(std::make_unique<InternPool>()) {
    snap_ = std::make_shared<DocSnapshot const>();
}

DocumentStore::~DocumentStore() = default;

std::shared_ptr<Cell const> DocumentStore::cellAt(int index) const {
    if (index < 0 || index >= (int)snap_->cells.size()) return nullptr;
    return snap_->cells[index];
}

std::shared_ptr<Cell const> DocumentStore::cell(CellId id) const {
    for (auto const& c : snap_->cells) {
        if (c->id == id) return c;
    }
    return nullptr;
}

int DocumentStore::indexOf(CellId id) const {
    for (int i = 0; i < (int)snap_->cells.size(); ++i) {
        if (snap_->cells[i]->id == id) return i;
    }
    return -1;
}

CellId DocumentStore::insertCell(int index, CellKind kind, std::string name,
                                 std::string text) {
    auto next = std::make_shared<DocSnapshot>(*snap_);
    auto cell = std::make_shared<Cell>();
    cell->id = next->nextCellId++;
    cell->kind = kind;
    cell->name = std::move(name);
    cell->text = std::move(text);
    index = std::clamp(index, 0, (int)next->cells.size());
    next->cells.insert(next->cells.begin() + index, std::move(cell));
    CellId id = next->cells[index]->id;
    snap_ = std::move(next);
    modified_ = true;
    return id;
}

void DocumentStore::removeCell(CellId id) {
    int i = indexOf(id);
    if (i < 0) return;
    auto next = std::make_shared<DocSnapshot>(*snap_);
    next->cells.erase(next->cells.begin() + i);
    snap_ = std::move(next);
    modified_ = true;
}

void DocumentStore::moveCell(CellId id, int delta) {
    int i = indexOf(id);
    if (i < 0) return;
    int j = std::clamp(i + delta, 0, (int)snap_->cells.size() - 1);
    if (i == j) return;
    auto next = std::make_shared<DocSnapshot>(*snap_);
    auto cell = next->cells[i];
    next->cells.erase(next->cells.begin() + i);
    next->cells.insert(next->cells.begin() + j, std::move(cell));
    snap_ = std::move(next);
    modified_ = true;
}

Cell* DocumentStore::mutableCell(CellId id) {
    int i = indexOf(id);
    if (i < 0) return nullptr;
    auto next = std::make_shared<DocSnapshot>(*snap_);
    auto copy = std::make_shared<Cell>(*next->cells[i]);
    Cell* raw = copy.get();
    next->cells[i] = std::move(copy);
    snap_ = std::move(next);
    modified_ = true;
    return raw;
}

void DocumentStore::setCellText(CellId id, std::string const& text) {
    auto existing = cell(id);
    if (!existing || existing->text == text) return;
    if (Cell* c = mutableCell(id)) c->text = text;
}

void DocumentStore::setCellName(CellId id, std::string const& name) {
    auto existing = cell(id);
    if (!existing || existing->name == name) return;
    if (Cell* c = mutableCell(id)) c->name = name;
}

void DocumentStore::setCellRunOnLoad(CellId id, bool runOnLoad) {
    auto existing = cell(id);
    if (!existing || existing->runOnLoad == runOnLoad) return;
    if (Cell* c = mutableCell(id)) c->runOnLoad = runOnLoad;
}

void DocumentStore::setCellPanelHeight(CellId id, float height) {
    auto existing = cell(id);
    if (!existing || existing->panelHeight == height) return;
    if (Cell* c = mutableCell(id)) c->panelHeight = height;
}

void DocumentStore::setCellCollapsed(CellId id, bool collapsed) {
    auto existing = cell(id);
    if (!existing || existing->collapsed == collapsed) return;
    if (Cell* c = mutableCell(id)) c->collapsed = collapsed;
}

void DocumentStore::setCellPresets(
    CellId id, std::vector<std::shared_ptr<Preset const>> presets) {
    // Content-intern each slot: a recreated-equal preset (re-capture,
    // rename back, ...) collapses onto the existing object anywhere in
    // the history instead of duplicating it.
    for (auto& p : presets) p = interns_->presets.intern(std::move(p));
    if (Cell* c = mutableCell(id)) c->presets = std::move(presets);
}

void DocumentStore::reset(SnapshotPtr snap, std::string filePath) {
    snap_ = std::move(snap);
    filePath_ = std::move(filePath);
    modified_ = false;
    rerootHistory("open");
}

// ---------------------------------------------------------------------------
// History tree
// ---------------------------------------------------------------------------

// (sameWidgetLists lives in content_hash.cpp: shared-pointer short-circuit,
// cached-hash inequality pre-check, then deep ==.)

bool DocumentStore::setWidgetSnap(std::shared_ptr<WidgetSnapList const> widgets) {
    // Content-intern the incoming elements so recurring states (toggle
    // A->B->A across non-adjacent commits) collapse onto the existing
    // objects anywhere in the history. captureWidgets' prev-reuse only
    // catches the adjacent-frame case.
    if (widgets) {
        bool changed = false;
        auto interned = std::make_shared<WidgetSnapList>(*widgets);
        for (auto& w : *interned) {
            auto q = interns_->snaps.intern(w);
            if (q != w) { w = std::move(q); changed = true; }
        }
        if (changed) widgets = std::move(interned);
    }
    bool same;
    if (snap_->widgets == widgets) {
        same = true;
    } else if (snap_->widgets && widgets) {
        same = sameWidgetLists(*snap_->widgets, *widgets);
    } else {
        // One side null: equal only if the other is empty.
        auto const* nonNull = (snap_->widgets ? snap_->widgets : widgets).get();
        same = !nonNull || nonNull->empty();
    }
    if (same) return false;
    auto next = std::make_shared<DocSnapshot>(*snap_);
    next->widgets = std::move(widgets);
    snap_ = std::move(next);
    return true;
}

static int countHistNodes(HistNode const* n) {
    int c = 1;
    for (auto const& ch : n->children) c += countHistNodes(ch.get());
    return c;
}

bool DocumentStore::commit(std::string const& label) {
    if (!cursor_) return false;
    if (snap_ == cursor_->snap) return false;

    // Content-intern the working snapshot's cells and the snapshot
    // itself: undo-branch re-edits that recreate an earlier state
    // collapse onto the existing objects instead of duplicating them.
    {
        bool changed = false;
        auto cells = snap_->cells;
        for (auto& c : cells) {
            auto q = interns_->cells.intern(c);
            if (q != c) { c = std::move(q); changed = true; }
        }
        if (changed) {
            auto next = std::make_shared<DocSnapshot>(*snap_);
            next->cells = std::move(cells);
            snap_ = std::move(next);
        }
        snap_ = interns_->snapshots.intern(snap_);
        if (snap_ == cursor_->snap) return false;   // interned to the tip
    }

    auto node = std::make_unique<HistNode>();
    node->snap = snap_;
    node->label = label;
    node->parent = cursor_;
    cursor_->activeChild = (int)cursor_->children.size();
    cursor_->children.push_back(std::move(node));
    cursor_ = cursor_->children.back().get();

    // A new node is unsaved state even when no cell text changed (widget
    // adjustments, arranges, preset recalls): history is saved in the file,
    // so quitting now without saving would lose it.
    modified_ = true;

    enforceHistoryCap();
    return true;
}

void DocumentStore::enforceHistoryCap() {
    // Advance the root toward the cursor, one generation at a time, until
    // the tree fits. The cursor's own ancestry survives; branches off
    // dropped ancestors go with them.
    while (root_.get() != cursor_
           && countHistNodes(root_.get()) > kHistoryCap) {
        HistNode* n = cursor_;
        while (n->parent != root_.get()) n = n->parent;
        std::unique_ptr<HistNode> keep;
        for (auto& ch : root_->children) {
            if (ch.get() == n) {
                keep = std::move(ch);
                break;
            }
        }
        keep->parent = nullptr;
        root_ = std::move(keep);
    }
}

void DocumentStore::rerootHistory(std::string const& label) {
    root_ = std::make_unique<HistNode>();
    root_->snap = snap_;
    root_->label = label;
    cursor_ = root_.get();
}

void DocumentStore::adoptHistory(std::unique_ptr<HistNode> root,
                                 HistNode* cursor) {
    if (!root || !cursor) return;
    root_ = std::move(root);
    cursor_ = cursor;
    // Unify the working snapshot with the cursor's when content-equal, so
    // the first post-load commit's pointer compare doesn't record a
    // phantom node. (They differ legitimately when the file was saved
    // with uncommitted changes.)
    if (snap_ && cursor_->snap
        && contentHashOf(*snap_) == contentHashOf(*cursor_->snap)
        && sameSnapshot(*snap_, *cursor_->snap)) {
        snap_ = cursor_->snap;
    }
    enforceHistoryCap();
}

SnapshotPtr DocumentStore::undo() {
    if (!canUndo()) return nullptr;
    // Remember which child we came from so redo retraces it.
    HistNode* parent = cursor_->parent;
    for (int i = 0; i < (int)parent->children.size(); ++i) {
        if (parent->children[i].get() == cursor_) {
            parent->activeChild = i;
            break;
        }
    }
    cursor_ = parent;
    snap_ = cursor_->snap;
    modified_ = true;
    return snap_;
}

SnapshotPtr DocumentStore::redo() {
    if (!canRedo()) return nullptr;
    cursor_ = cursor_->children[cursor_->activeChild].get();
    snap_ = cursor_->snap;
    modified_ = true;
    return snap_;
}

SnapshotPtr DocumentStore::jumpTo(HistNode* node) {
    if (!node || node == cursor_) return nullptr;
    // Mark the path from the root to `node` as the active branch.
    for (HistNode* n = node; n->parent; n = n->parent) {
        for (int i = 0; i < (int)n->parent->children.size(); ++i) {
            if (n->parent->children[i].get() == n) {
                n->parent->activeChild = i;
                break;
            }
        }
    }
    cursor_ = node;
    snap_ = cursor_->snap;
    modified_ = true;
    return snap_;
}

// ---------------------------------------------------------------------------
// Persistence
//
// Layout (TZB Vec trees, first child a Symbol discriminator; the TZB format
// itself is documented in lang/docs/FFI_Guide.html section 15):
//   (doc 1 nextCellId (cells cell...) (panels panel...))
//   cell   = (cell id kindInt name text runOnLoad)
//   panel  = (panel name widget...)
//   widget = (widget name kindInt spec spec2 values frame)
//   spec   = (lo hi init warpInt warpParam)
//   values = (v...)          frame = (x y w h)   [frame unused until M5]
// ---------------------------------------------------------------------------

// (specValue lives in content_hash.cpp, shared with the content hashes.)

static bridge::UISpec readSpec(Reader r) {
    bridge::UISpec s;
    if (r.tag() != Tag::Vec || r.childCount() < 5) return s;
    s.lo = r.child(0).asFloat();
    s.hi = r.child(1).asFloat();
    s.init = r.child(2).asFloat();
    int warp = (int)r.child(3).asInt();
    if (warp < 0 || warp > (int)bridge::UIWarp::Cubed) warp = 0;
    s.warp = (bridge::UIWarp)warp;
    s.warpParam = r.child(4).asFloat();
    return s;
}

// Parse one (preset name (entry panel widget (v...))...) record.
// Returns false on malformed structure.
static bool readPresetRec(Reader rp2, Preset& p) {
    if (rp2.tag() != Tag::Vec || rp2.childCount() < 2) return false;
    p.name = std::string(rp2.child(1).asStr());
    for (std::uint32_t m = 2; m < rp2.childCount(); ++m) {
        Reader re = rp2.child(m);
        if (re.tag() != Tag::Vec || re.childCount() < 4) continue;
        PresetEntry e;
        e.panel = std::string(re.child(1).asStr());
        e.widget = std::string(re.child(2).asStr());
        Reader rv = re.child(3);
        for (std::uint32_t j = 0; j < rv.childCount(); ++j)
            e.values.push_back(rv.child(j).asFloat());
        p.entries.push_back(std::move(e));
    }
    return true;
}

// Read the M5 widget-record extension (frame, matrix dims, notes, label,
// roll params) if present; older files simply lack the trailing children.
static void readWidgetExtras(Reader rw, bridge::UIWidget& w) {
    if (rw.childCount() > 6) {
        Reader rf = rw.child(6);
        if (rf.tag() == Tag::Vec && rf.childCount() >= 4) {
            w.fx = (float)rf.child(0).asFloat();
            w.fy = (float)rf.child(1).asFloat();
            w.fw = (float)rf.child(2).asFloat();
            w.fh = (float)rf.child(3).asFloat();
        }
    }
    if (rw.childCount() > 13) {
        w.rows = std::max(1, (int)rw.child(7).asInt());
        w.cols = std::max(1, (int)rw.child(8).asInt());
        Reader rn = rw.child(9);
        if (rn.tag() == Tag::Vec) {
            w.noteData.resize(rn.childCount());
            for (std::uint32_t i = 0; i < rn.childCount(); ++i)
                w.noteData[i] = (float)rn.child(i).asFloat();
        }
        w.labelText = std::string(rw.child(10).asStr());
        w.rollBeats = (float)rw.child(11).asFloat();
        w.rollLowPitch = (int)rw.child(12).asInt();
        w.rollRows = std::max(1, (int)rw.child(13).asInt());
    }
    if (rw.childCount() > 14)
        w.rollEdo = std::max(1, (int)rw.child(14).asInt());
    if (rw.childCount() > 15)
        w.keyChord = std::string(rw.child(15).asStr());
    if (rw.childCount() > 16)
        w.momentary = rw.child(16).asBool();
    if (rw.childCount() > 17) {
        Reader rl = rw.child(17);
        if (rl.tag() == Tag::Vec) {
            w.cellLabels.resize(rl.childCount());
            for (std::uint32_t i = 0; i < rl.childCount(); ++i)
                w.cellLabels[i] = std::string(rl.child(i).asStr());
            w.labelsVersion++;
        }
    }
}

// ---------------------------------------------------------------------------
// History serialization (v2): content-addressed tables + flat preorder tree.
//
//   history = (history (ptab preset...) (wtab wsnap...) (ctab cellrec...)
//              (stab snaprec...) (tree noderec...) cursorIdx)
//   cellrec = v1 cell record except child 8 is (prefs pIdx...)
//   snaprec = (snap nextCellId (cIdx...) hasWidgets (wIdx...))
//   noderec = (node sIdx label activeChild parentIdx)   [root parentIdx -1]
//
// Each table entry is written once; records reference entries by index, so
// shared content across the ~500 snapshots costs one copy on disk. Dedup
// keys on pointer identity first (Phase-1 interning makes equal content
// pointer-identical in practice) with a content-hash + deep-equality
// fallback for stragglers.
// ---------------------------------------------------------------------------

namespace {

// One deduplicated table: items are appended on first sight, looked up by
// pointer, then by content hash with a structural verify.
template <class T>
struct DedupTable {
    std::vector<Value> tab;
    std::vector<std::shared_ptr<T const>> items;
    std::unordered_map<T const*, int> byPtr;
    std::unordered_map<std::uint64_t, std::vector<int>> byHash;

    template <class MakeValue>
    int index(std::shared_ptr<T const> const& p, MakeValue&& makeValue) {
        auto it = byPtr.find(p.get());
        if (it != byPtr.end()) return it->second;
        std::uint64_t h = contentHashOf(*p);
        for (int i : byHash[h]) {
            if (sameContent(*items[i], *p)) {
                byPtr.emplace(p.get(), i);
                return i;
            }
        }
        int idx = (int)items.size();
        items.push_back(p);
        byPtr.emplace(p.get(), idx);
        byHash[h].push_back(idx);
        tab.push_back(makeValue(*p));
        return idx;
    }
};

struct HistoryTables {
    DedupTable<Preset> presets;
    DedupTable<WidgetSnap> snaps;
    DedupTable<Cell> cells;
    DedupTable<DocSnapshot> snapshots;

    HistoryTables() {
        presets.tab.push_back(Value::Symbol("ptab"));
        snaps.tab.push_back(Value::Symbol("wtab"));
        cells.tab.push_back(Value::Symbol("ctab"));
        snapshots.tab.push_back(Value::Symbol("stab"));
        // Discriminator symbols occupy slot 0; indices are 1-based into
        // the vec but stored 0-based relative to the first entry, so keep
        // items[] and tab[] aligned by subtracting nothing: entry i lives
        // at tab child (i + 1).
    }

    int presetIndex(std::shared_ptr<Preset const> const& p) {
        return presets.index(p, [](Preset const& v) { return presetValue(v); });
    }
    int snapIndex(std::shared_ptr<WidgetSnap const> const& s) {
        return snaps.index(s, [](WidgetSnap const& v) { return widgetSnapValue(v); });
    }
    int cellIndex(std::shared_ptr<Cell const> const& c) {
        return cells.index(c, [this](Cell const& v) {
            // v1 cell record shape, but child 8 references ptab entries.
            std::vector<Value> ps{Value::Symbol("prefs")};
            for (auto const& pp : v.presets)
                ps.push_back(Value::Int(presetIndex(pp)));
            return Value::Vec({
                Value::Symbol("cell"),
                Value::Int((std::int64_t)v.id),
                Value::Int((int)v.kind),
                Value::String(v.name),
                Value::String(v.text),
                Value::Bool(v.runOnLoad),
                Value::Float(v.panelHeight),
                Value::Bool(v.collapsed),
                Value::Vec(std::move(ps)),
            });
        });
    }
    int snapshotIndex(SnapshotPtr const& s) {
        return snapshots.index(s, [this](DocSnapshot const& v) {
            std::vector<Value> cIdx;
            for (auto const& c : v.cells)
                cIdx.push_back(Value::Int(cellIndex(c)));
            std::vector<Value> wIdx;
            if (v.widgets) {
                for (auto const& w : *v.widgets)
                    wIdx.push_back(Value::Int(snapIndex(w)));
            }
            return Value::Vec({
                Value::Symbol("snap"),
                Value::Int((std::int64_t)v.nextCellId),
                Value::Vec(std::move(cIdx)),
                Value::Bool(v.widgets != nullptr),
                Value::Vec(std::move(wIdx)),
            });
        });
    }
};

Value buildHistoryValue(HistNode const* root, HistNode const* cursor) {
    HistoryTables t;
    std::vector<Value> tree{Value::Symbol("tree")};
    std::int64_t cursorIdx = 0;

    // Iterative preorder walk; children pushed in reverse so they pop in
    // order, which makes reconstruction (append to parent) reproduce the
    // children order and keep activeChild indices valid.
    struct Item { HistNode const* node; int parentIdx; };
    std::vector<Item> stack;
    std::unordered_map<HistNode const*, int> nodeIdx;
    if (root) stack.push_back({root, -1});
    while (!stack.empty()) {
        auto [node, parentIdx] = stack.back();
        stack.pop_back();
        int idx = (int)nodeIdx.size();
        nodeIdx.emplace(node, idx);
        if (node == cursor) cursorIdx = idx;
        tree.push_back(Value::Vec({
            Value::Symbol("node"),
            Value::Int(t.snapshotIndex(node->snap)),
            Value::String(node->label),
            Value::Int(node->activeChild),
            Value::Int(parentIdx),
        }));
        for (auto it = node->children.rbegin(); it != node->children.rend();
             ++it) {
            stack.push_back({it->get(), idx});
        }
    }

    return Value::Vec({
        Value::Symbol("history"),
        Value::Vec(std::move(t.presets.tab)),
        Value::Vec(std::move(t.snaps.tab)),
        Value::Vec(std::move(t.cells.tab)),
        Value::Vec(std::move(t.snapshots.tab)),
        Value::Vec(std::move(tree)),
        Value::Int(cursorIdx),
    });
}

} // namespace

bool saveDocument(DocumentStore const& store, bridge::AppContext& ctx,
                  std::string const& path, std::string& err) {
    DocSnapshot const& snap = *store.snapshot();
    std::vector<Value> cells{Value::Symbol("cells")};
    std::set<std::string> panelNames;
    for (auto const& c : snap.cells) {
        // The canonical builder (content_hash.cpp) is also what the
        // content hashes are computed from -- one representation.
        cells.push_back(cellValue(*c));
        if (c->kind == CellKind::Panel) panelNames.insert(c->name);
    }

    std::vector<Value> panels{Value::Symbol("panels")};
    if (ctx.uiState) {
        std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
        // One record per distinct widget panel name claimed by a panel
        // cell -- exact names and "name/sub" sub-panels (cell tabs).
        std::vector<std::string> claimed;
        for (auto const& w : ctx.uiState->widgets) {
            bool owned = false;
            for (auto const& root : panelNames)
                if (bridge::panelUnderRoot(w->panel, root)) { owned = true; break; }
            if (owned && std::find(claimed.begin(), claimed.end(), w->panel)
                             == claimed.end())
                claimed.push_back(w->panel);
        }
        for (auto const& panel : claimed) {
            std::vector<Value> pv{Value::Symbol("panel"), Value::String(panel)};
            for (auto const& w : ctx.uiState->widgets) {
                if (w->panel != panel) continue;
                std::vector<Value> values;
                for (double v : w->values) values.push_back(Value::Float(v));
                std::vector<Value> notes;
                for (float v : w->noteData) notes.push_back(Value::Float(v));
                std::vector<Value> labels;
                for (auto const& l : w->cellLabels)
                    labels.push_back(Value::String(l));
                pv.push_back(Value::Vec({
                    Value::Symbol("widget"),
                    Value::String(w->name),
                    Value::Int((int)w->kind),
                    specValue(w->spec),
                    specValue(w->spec2),
                    Value::Vec(std::move(values)),
                    Value::Vec({Value::Float(w->fx), Value::Float(w->fy),
                                Value::Float(w->fw), Value::Float(w->fh)}),
                    Value::Int(w->rows),
                    Value::Int(w->cols),
                    Value::Vec(std::move(notes)),
                    Value::String(w->labelText),
                    Value::Float(w->rollBeats),
                    Value::Int(w->rollLowPitch),
                    Value::Int(w->rollRows),
                    Value::Int(w->rollEdo),
                    Value::String(w->keyChord),
                    Value::Bool(w->momentary),
                    Value::Vec(std::move(labels)),
                }));
            }
            panels.push_back(Value::Vec(std::move(pv)));
        }
    }

    std::vector<Value> rootKids{
        Value::Symbol("doc"),
        Value::Int(2),  // version (2 = trailing content-addressed history)
        Value::Int((std::int64_t)snap.nextCellId),
        Value::Vec(std::move(cells)),
        Value::Vec(std::move(panels)),
    };
    if (store.historyRoot()) {
        rootKids.push_back(
            buildHistoryValue(store.historyRoot(), store.historyCursor()));
    }
    Value root = Value::Vec(std::move(rootKids));

    auto bytes = tzpl::sbin::encode(root);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot open \"" + path + "\" for writing";
        return false;
    }
    out.write(reinterpret_cast<char const*>(bytes.data()),
              (std::streamsize)bytes.size());
    if (!out) {
        err = "write failed for \"" + path + "\"";
        return false;
    }
    return true;
}

// Remove an engine tap (GUI/caller thread; bundles are thread-local).
static void untapWidget(bridge::AppContext& ctx, long tapID, int silo) {
    bridge::untapWidget(ctx.engine, tapID, silo);
}

// ---------------------------------------------------------------------------
// History deserialization (v2). Any malformed record aborts the WHOLE
// history (returns false; caller drops it) -- the document itself always
// loads from the stable current-state sections.
// ---------------------------------------------------------------------------

static bool parseHistory(Reader h, InternPool& pool, LoadedHistory& out) {
    if (h.tag() != Tag::Vec || h.childCount() < 7
        || h.child(0).asStr() != "history") {
        return false;
    }

    // ptab: unique presets.
    Reader ptab = h.child(1);
    if (ptab.tag() != Tag::Vec || ptab.childCount() < 1
        || ptab.child(0).asStr() != "ptab") {
        return false;
    }
    std::vector<std::shared_ptr<Preset const>> presets;
    for (std::uint32_t i = 1; i < ptab.childCount(); ++i) {
        Preset p;
        if (!readPresetRec(ptab.child(i), p)) return false;
        presets.push_back(
            pool.presets.intern(std::make_shared<Preset const>(std::move(p))));
    }

    // wtab: unique widget snapshots. Record shape mirrors widgetSnapValue.
    Reader wtab = h.child(2);
    if (wtab.tag() != Tag::Vec || wtab.childCount() < 1
        || wtab.child(0).asStr() != "wtab") {
        return false;
    }
    std::vector<std::shared_ptr<WidgetSnap const>> snaps;
    for (std::uint32_t i = 1; i < wtab.childCount(); ++i) {
        Reader rw = wtab.child(i);
        if (rw.tag() != Tag::Vec || rw.childCount() < 17
            || rw.child(0).asStr() != "wsnap") {
            return false;
        }
        WidgetSnap s;
        s.panel = std::string(rw.child(1).asStr());
        s.name = std::string(rw.child(2).asStr());
        int kindInt = (int)rw.child(3).asInt();
        if (kindInt < 0 || kindInt > (int)bridge::UIWidgetKind::ButtonMatrix)
            kindInt = 0;
        s.kind = (bridge::UIWidgetKind)kindInt;
        s.spec = readSpec(rw.child(4));
        s.spec2 = readSpec(rw.child(5));
        Reader rv = rw.child(6);
        for (std::uint32_t j = 0; j < rv.childCount(); ++j)
            s.values.push_back(rv.child(j).asFloat());
        Reader rf = rw.child(7);
        if (rf.tag() == Tag::Vec && rf.childCount() >= 4) {
            s.fx = (float)rf.child(0).asFloat();
            s.fy = (float)rf.child(1).asFloat();
            s.fw = (float)rf.child(2).asFloat();
            s.fh = (float)rf.child(3).asFloat();
        }
        s.rows = std::max(1, (int)rw.child(8).asInt());
        s.cols = std::max(1, (int)rw.child(9).asInt());
        Reader rn = rw.child(10);
        for (std::uint32_t j = 0; j < rn.childCount(); ++j)
            s.noteData.push_back((float)rn.child(j).asFloat());
        s.labelText = std::string(rw.child(11).asStr());
        s.rollBeats = (float)rw.child(12).asFloat();
        s.rollLowPitch = (int)rw.child(13).asInt();
        s.rollRows = std::max(1, (int)rw.child(14).asInt());
        s.rollEdo = std::max(1, (int)rw.child(15).asInt());
        s.keyChord = std::string(rw.child(16).asStr());
        // Trailing children are append-only (older files lack them).
        if (rw.childCount() > 17) s.momentary = rw.child(17).asBool();
        if (rw.childCount() > 18) {
            Reader rl = rw.child(18);
            for (std::uint32_t j = 0; j < rl.childCount(); ++j)
                s.cellLabels.push_back(std::string(rl.child(j).asStr()));
        }
        snaps.push_back(
            pool.snaps.intern(std::make_shared<WidgetSnap const>(std::move(s))));
    }

    // ctab: unique cells (preset children are ptab indices).
    Reader ctab = h.child(3);
    if (ctab.tag() != Tag::Vec || ctab.childCount() < 1
        || ctab.child(0).asStr() != "ctab") {
        return false;
    }
    std::vector<std::shared_ptr<Cell const>> cells;
    for (std::uint32_t i = 1; i < ctab.childCount(); ++i) {
        Reader rc = ctab.child(i);
        if (rc.tag() != Tag::Vec || rc.childCount() < 9
            || rc.child(0).asStr() != "cell") {
            return false;
        }
        Cell c;
        c.id = (CellId)rc.child(1).asInt();
        int kind = (int)rc.child(2).asInt();
        c.kind = (kind >= 0 && kind <= 3) ? (CellKind)kind : CellKind::Code;
        c.name = std::string(rc.child(3).asStr());
        c.text = std::string(rc.child(4).asStr());
        c.runOnLoad = rc.child(5).asBool();
        c.panelHeight = (float)rc.child(6).asFloat();
        c.collapsed = rc.child(7).asBool();
        Reader ps = rc.child(8);
        if (ps.tag() != Tag::Vec || ps.childCount() < 1
            || ps.child(0).asStr() != "prefs") {
            return false;
        }
        for (std::uint32_t k = 1; k < ps.childCount(); ++k) {
            std::int64_t idx = ps.child(k).asInt();
            if (idx < 0 || (std::size_t)idx >= presets.size()) return false;
            c.presets.push_back(presets[(std::size_t)idx]);
        }
        cells.push_back(
            pool.cells.intern(std::make_shared<Cell const>(std::move(c))));
    }

    // stab: unique snapshots (cell/widget children are table indices).
    Reader stab = h.child(4);
    if (stab.tag() != Tag::Vec || stab.childCount() < 1
        || stab.child(0).asStr() != "stab") {
        return false;
    }
    std::vector<SnapshotPtr> snapshots;
    // Equal widget-index sequences share one WidgetSnapList allocation.
    std::map<std::vector<std::int64_t>,
             std::shared_ptr<WidgetSnapList const>> listMemo;
    for (std::uint32_t i = 1; i < stab.childCount(); ++i) {
        Reader rs = stab.child(i);
        if (rs.tag() != Tag::Vec || rs.childCount() < 5
            || rs.child(0).asStr() != "snap") {
            return false;
        }
        auto snap = std::make_shared<DocSnapshot>();
        snap->nextCellId = (CellId)rs.child(1).asInt();
        Reader cIdx = rs.child(2);
        for (std::uint32_t k = 0; k < cIdx.childCount(); ++k) {
            std::int64_t idx = cIdx.child(k).asInt();
            if (idx < 0 || (std::size_t)idx >= cells.size()) return false;
            snap->cells.push_back(cells[(std::size_t)idx]);
        }
        if (rs.child(3).asBool()) {
            Reader wIdx = rs.child(4);
            std::vector<std::int64_t> key;
            for (std::uint32_t k = 0; k < wIdx.childCount(); ++k) {
                std::int64_t idx = wIdx.child(k).asInt();
                if (idx < 0 || (std::size_t)idx >= snaps.size()) return false;
                key.push_back(idx);
            }
            auto& memo = listMemo[key];
            if (!memo) {
                auto list = std::make_shared<WidgetSnapList>();
                for (std::int64_t idx : key)
                    list->push_back(snaps[(std::size_t)idx]);
                memo = std::move(list);
            }
            snap->widgets = memo;
        }
        snapshots.push_back(
            pool.snapshots.intern(SnapshotPtr(std::move(snap))));
    }

    // tree: flat preorder node records.
    Reader tree = h.child(5);
    if (tree.tag() != Tag::Vec || tree.childCount() < 2
        || tree.child(0).asStr() != "tree") {
        return false;   // an empty tree is malformed: there is always a root
    }
    std::unique_ptr<HistNode> root;
    std::vector<HistNode*> byIndex;
    for (std::uint32_t i = 1; i < tree.childCount(); ++i) {
        Reader rn = tree.child(i);
        if (rn.tag() != Tag::Vec || rn.childCount() < 5
            || rn.child(0).asStr() != "node") {
            return false;
        }
        std::int64_t sIdx = rn.child(1).asInt();
        if (sIdx < 0 || (std::size_t)sIdx >= snapshots.size()) return false;
        std::int64_t parentIdx = rn.child(4).asInt();
        auto node = std::make_unique<HistNode>();
        node->snap = snapshots[(std::size_t)sIdx];
        node->label = std::string(rn.child(2).asStr());
        node->activeChild = (int)rn.child(3).asInt();
        HistNode* raw = node.get();
        if (byIndex.empty()) {
            if (parentIdx != -1) return false;
            root = std::move(node);
        } else {
            if (parentIdx < 0 || (std::size_t)parentIdx >= byIndex.size())
                return false;
            HistNode* parent = byIndex[(std::size_t)parentIdx];
            node->parent = parent;
            parent->children.push_back(std::move(node));
        }
        byIndex.push_back(raw);
    }
    // Clamp activeChild now that children counts are final.
    for (HistNode* n : byIndex) {
        if (n->activeChild < -1
            || n->activeChild >= (int)n->children.size()) {
            n->activeChild = -1;
        }
    }

    std::int64_t cursorIdx = h.child(6).asInt();
    if (cursorIdx < 0 || (std::size_t)cursorIdx >= byIndex.size())
        cursorIdx = 0;

    out.root = std::move(root);
    out.cursor = byIndex[(std::size_t)cursorIdx];
    return true;
}

SnapshotPtr loadDocument(bridge::AppContext& ctx, std::string const& path,
                         std::string& err, InternPool* pool,
                         LoadedHistory* history) {
    if (history) {
        history->root.reset();
        history->cursor = nullptr;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open \"" + path + "\"";
        return nullptr;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (!Reader::valid(bytes.data(), bytes.size())) {
        err = "\"" + path + "\" is not a TZB document";
        return nullptr;
    }
    Reader root = Reader::root(bytes.data(), bytes.size());
    if (root.tag() != Tag::Vec || root.childCount() < 5
        || root.child(0).asStr() != "doc") {
        err = "\"" + path + "\" is not a notebook document";
        return nullptr;
    }

    auto snap = std::make_shared<DocSnapshot>();
    snap->nextCellId = (CellId)root.child(2).asInt();

    Reader cells = root.child(3);
    for (std::uint32_t i = 1; i < cells.childCount(); ++i) {
        Reader rc = cells.child(i);
        if (rc.tag() != Tag::Vec || rc.childCount() < 6) continue;
        auto cell = std::make_shared<Cell>();
        cell->id = (CellId)rc.child(1).asInt();
        int kind = (int)rc.child(2).asInt();
        cell->kind = (kind >= 0 && kind <= 3) ? (CellKind)kind : CellKind::Code;
        cell->name = std::string(rc.child(3).asStr());
        cell->text = std::string(rc.child(4).asStr());
        cell->runOnLoad = rc.child(5).asBool();
        if (rc.childCount() > 6) {
            cell->panelHeight = (float)rc.child(6).asFloat();
        }
        if (rc.childCount() > 7) cell->collapsed = rc.child(7).asBool();
        if (rc.childCount() > 8) {
            Reader ps = rc.child(8);
            for (std::uint32_t k = 1; k < ps.childCount(); ++k) {
                Preset p;
                if (!readPresetRec(ps.child(k), p)) continue;
                auto sp = std::make_shared<Preset const>(std::move(p));
                if (pool) sp = pool->presets.intern(std::move(sp));
                cell->presets.push_back(std::move(sp));
            }
        }
        if (cell->id >= snap->nextCellId) snap->nextCellId = cell->id + 1;
        std::shared_ptr<Cell const> cc = std::move(cell);
        if (pool) cc = pool->cells.intern(std::move(cc));
        snap->cells.push_back(std::move(cc));
    }

    // Restore panel widgets into the ui registry, unbound. Existing widgets
    // in the restored panels are removed first (untapping any engine taps).
    if (ctx.uiState) {
        Reader panels = root.child(4);
        for (std::uint32_t i = 1; i < panels.childCount(); ++i) {
            Reader rp = panels.child(i);
            if (rp.tag() != Tag::Vec || rp.childCount() < 2) continue;
            std::string panel{rp.child(1).asStr()};

            std::vector<std::pair<long, int>> taps;
            {
                std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
                auto& ws = ctx.uiState->widgets;
                for (auto& w : ws) {
                    if (w->panel == panel && w->tapID)
                        taps.push_back({w->tapID, w->tapSilo});
                }
                std::erase_if(ws, [&](auto const& w) { return w->panel == panel; });
            }
            for (auto [tapID, silo] : taps) untapWidget(ctx, tapID, silo);

            std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
            for (std::uint32_t k = 2; k < rp.childCount(); ++k) {
                Reader rw = rp.child(k);
                if (rw.tag() != Tag::Vec || rw.childCount() < 6) continue;
                std::string name{rw.child(1).asStr()};
                int kindInt = (int)rw.child(2).asInt();
                // Kinds are persisted as raw ordinals, so a document written
                // by a NEWER build can name a kind this one doesn't have.
                // Degrade to Slider rather than reinterpreting garbage.
                if (kindInt < 0
                    || kindInt > (int)bridge::UIWidgetKind::Spectrum)
                    kindInt = 0;
                auto kind = (bridge::UIWidgetKind)kindInt;
                bridge::UISpec spec = readSpec(rw.child(3));
                bridge::UISpec spec2 = readSpec(rw.child(4));
                bridge::UIWidget* w =
                    ctx.uiState->upsert(panel, name, kind, spec, spec2);
                readWidgetExtras(rw, *w);
                Reader rv = rw.child(5);
                if (rv.tag() == Tag::Vec) {
                    if (kind == bridge::UIWidgetKind::MultiSlider
                        || kind == bridge::UIWidgetKind::Matrix
                        || kind == bridge::UIWidgetKind::ButtonMatrix) {
                        w->values.assign(rv.childCount(), 0.0);
                    }
                    for (std::uint32_t v = 0;
                         v < rv.childCount() && v < w->values.size(); ++v) {
                        w->values[v] = rv.child(v).asFloat();
                    }
                }
            }
        }
    }

    // History (v2+): the version field is now actually read. v1 files (or
    // files without the trailing history child) simply carry no history;
    // files from a FUTURE version load their stable current-state sections
    // and drop the history with a warning; malformed history is dropped
    // silently rather than failing the load.
    std::int64_t version = root.child(1).asInt();
    if (history && version >= 2 && root.childCount() > 5) {
        InternPool localPool;
        LoadedHistory parsed;
        if (parseHistory(root.child(5), pool ? *pool : localPool, parsed)) {
            *history = std::move(parsed);
        }
    }
    if (version > 2) {
        std::fprintf(stderr,
                     "tzpl: \"%s\" is a newer document version (%lld); "
                     "loaded without history\n",
                     path.c_str(), (long long)version);
    }

    return snap;
}

// ---------------------------------------------------------------------------
// Widget capture / restore
// ---------------------------------------------------------------------------

static bool inPanels(std::vector<std::string> const& panels,
                     std::string const& p) {
    for (auto const& root : panels)
        if (bridge::panelUnderRoot(p, root)) return true;
    return false;
}

std::shared_ptr<WidgetSnapList const>
captureWidgets(bridge::UIState* ui, std::vector<std::string> const& panels,
               WidgetSnapList const* prev) {
    auto out = std::make_shared<WidgetSnapList>();
    if (!ui) return out;
    std::lock_guard<std::mutex> lock(ui->mtx);
    for (auto const& w : ui->widgets) {
        if (!inPanels(panels, w->panel)) continue;
        WidgetSnap s;
        s.panel = w->panel;
        s.name = w->name;
        s.kind = w->kind;
        s.spec = w->spec;
        s.spec2 = w->spec2;
        s.values = w->values;
        // Meter/Scope values are live tap readouts, not document state:
        // capture stable zeros so a run that only changes AUDIO (stop a
        // node, stop a coroutine) doesn't read as a document change and
        // pollute history.
        if (w->kind == bridge::UIWidgetKind::Meter
            || w->kind == bridge::UIWidgetKind::Scope
            || w->kind == bridge::UIWidgetKind::Spectrum)
            std::fill(s.values.begin(), s.values.end(), 0.0);
        s.fx = w->fx; s.fy = w->fy; s.fw = w->fw; s.fh = w->fh;
        s.rows = w->rows; s.cols = w->cols;
        s.noteData = w->noteData;
        s.labelText = w->labelText;
        s.rollBeats = w->rollBeats;
        s.rollLowPitch = w->rollLowPitch;
        s.rollRows = w->rollRows;
        s.rollEdo = w->rollEdo;
        s.keyChord = w->keyChord;
        s.momentary = w->momentary;
        s.cellLabels = w->cellLabels;
        // Share the previous capture's element when unchanged, so
        // history nodes duplicate only the widgets that moved.
        std::shared_ptr<WidgetSnap const> reused;
        if (prev) {
            for (auto const& q : *prev) {
                if (q && q->panel == s.panel && q->name == s.name) {
                    if (*q == s) reused = q;
                    break;
                }
            }
        }
        out->push_back(reused ? reused
                              : std::make_shared<WidgetSnap const>(
                                    std::move(s)));
    }
    return out;
}

void restoreWidgets(bridge::AppContext& ctx, WidgetSnapList const& target,
                    std::vector<std::string> const& panels) {
    if (!ctx.uiState) return;
    std::vector<std::pair<long, int>> taps;
    {
        std::lock_guard<std::mutex> lock(ctx.uiState->mtx);

        // Remove live widgets (in the restored panels) that the target
        // state doesn't contain.
        auto inTarget = [&](bridge::UIWidget const& w) {
            for (auto const& sp : target) {
                if (sp->panel == w.panel && sp->name == w.name) return true;
            }
            return false;
        };
        auto& ws = ctx.uiState->widgets;
        for (auto& w : ws) {
            if (inPanels(panels, w->panel) && !inTarget(*w) && w->tapID)
                taps.push_back({w->tapID, w->tapSilo});
        }
        std::erase_if(ws, [&](auto const& w) {
            return inPanels(panels, w->panel) && !inTarget(*w);
        });

        // Update surviving widgets (bindings preserved; changed values are
        // marked dirty so the per-frame dispatch re-sends them) and
        // recreate missing ones unbound.
        for (auto const& sp : target) {
            WidgetSnap const& s = *sp;
            if (!inPanels(panels, s.panel)) continue;
            bridge::UIWidget* w = ctx.uiState->findByName(s.panel, s.name);
            if (w && w->kind == s.kind) {
                w->spec = s.spec;
                w->spec2 = s.spec2;
                w->fx = s.fx; w->fy = s.fy; w->fw = s.fw; w->fh = s.fh;
                w->rows = s.rows; w->cols = s.cols;
                w->labelText = s.labelText;
                w->rollBeats = s.rollBeats;
                w->rollLowPitch = s.rollLowPitch;
                w->rollRows = s.rollRows;
                w->rollEdo = s.rollEdo;
                w->keyChord = s.keyChord;
                w->momentary = s.momentary;
                if (w->cellLabels != s.cellLabels) {
                    w->cellLabels = s.cellLabels;
                    w->labelsVersion++;
                }
                if (w->noteData != s.noteData) {
                    w->noteData = s.noteData;
                    w->dirtyCallback = true;
                }
                // Tap-backed displays keep their live values (captured
                // as zeros; see captureWidgets).
                bool tapKind = s.kind == bridge::UIWidgetKind::Meter
                            || s.kind == bridge::UIWidgetKind::Scope
                            || s.kind == bridge::UIWidgetKind::Spectrum;
                if (!tapKind && w->values != s.values) {
                    w->values = s.values;
                    w->dirtyEngine = true;
                    w->dirtyCallback = true;
                }
            } else {
                if (w && w->tapID) taps.push_back({w->tapID, w->tapSilo});
                if (w) {
                    // Kind changed across history: replace wholesale.
                    ctx.uiState->remove(w->id);
                }
                bridge::UIWidget* nw =
                    ctx.uiState->upsert(s.panel, s.name, s.kind, s.spec, s.spec2);
                if (s.kind == bridge::UIWidgetKind::MultiSlider
                    || s.kind == bridge::UIWidgetKind::Matrix
                    || s.kind == bridge::UIWidgetKind::ButtonMatrix) {
                    nw->values.assign(s.values.size(), 0.0);
                }
                for (size_t i = 0; i < nw->values.size() && i < s.values.size(); ++i)
                    nw->values[i] = s.values[i];
                nw->fx = s.fx; nw->fy = s.fy; nw->fw = s.fw; nw->fh = s.fh;
                nw->rows = s.rows; nw->cols = s.cols;
                nw->noteData = s.noteData;
                nw->labelText = s.labelText;
                nw->rollBeats = s.rollBeats;
                nw->rollLowPitch = s.rollLowPitch;
                nw->rollRows = s.rollRows;
                nw->rollEdo = s.rollEdo;
                nw->keyChord = s.keyChord;
                nw->momentary = s.momentary;
                nw->cellLabels = s.cellLabels;
                nw->labelsVersion++;
                nw->dirtyEngine = true;
                nw->dirtyCallback = true;
            }
        }
    }
    for (auto [tapID, silo] : taps) untapWidget(ctx, tapID, silo);
}

} // namespace doc
