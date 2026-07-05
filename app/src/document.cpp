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

#include "tzpl_app_context.hpp"
#include "tzpl_ui_state.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_sexpr_bin.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>

namespace doc {

using tzpl::sbin::Value;
using tzpl::sbin::Reader;
using tzpl::sbin::Tag;

// ---------------------------------------------------------------------------
// DocumentStore
// ---------------------------------------------------------------------------

DocumentStore::DocumentStore() {
    snap_ = std::make_shared<DocSnapshot const>();
}

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

void DocumentStore::reset(SnapshotPtr snap, std::string filePath) {
    snap_ = std::move(snap);
    filePath_ = std::move(filePath);
    modified_ = false;
    rerootHistory("open");
}

// ---------------------------------------------------------------------------
// History tree
// ---------------------------------------------------------------------------

bool DocumentStore::setWidgetSnap(std::shared_ptr<WidgetSnapList const> widgets) {
    bool same;
    if (snap_->widgets == widgets) {
        same = true;
    } else if (snap_->widgets && widgets) {
        same = *snap_->widgets == *widgets;
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

bool DocumentStore::commit(std::string const& label) {
    if (!cursor_) return false;
    if (snap_ == cursor_->snap) return false;
    auto node = std::make_unique<HistNode>();
    node->snap = snap_;
    node->label = label;
    node->parent = cursor_;
    cursor_->activeChild = (int)cursor_->children.size();
    cursor_->children.push_back(std::move(node));
    cursor_ = cursor_->children.back().get();
    return true;
}

void DocumentStore::rerootHistory(std::string const& label) {
    root_ = std::make_unique<HistNode>();
    root_->snap = snap_;
    root_->label = label;
    cursor_ = root_.get();
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
// Layout (TZB Vec trees, first child a Symbol discriminator):
//   (doc 1 nextCellId (cells cell...) (panels panel...))
//   cell   = (cell id kindInt name text runOnLoad)
//   panel  = (panel name widget...)
//   widget = (widget name kindInt spec spec2 values frame)
//   spec   = (lo hi init warpInt warpParam)
//   values = (v...)          frame = (x y w h)   [frame unused until M5]
// ---------------------------------------------------------------------------

static Value specValue(bridge::UISpec const& s) {
    return Value::Vec({Value::Float(s.lo), Value::Float(s.hi),
                       Value::Float(s.init), Value::Int((int)s.warp),
                       Value::Float(s.warpParam)});
}

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
}

bool saveDocument(DocSnapshot const& snap, bridge::AppContext& ctx,
                  std::string const& path, std::string& err) {
    std::vector<Value> cells{Value::Symbol("cells")};
    std::set<std::string> panelNames;
    for (auto const& c : snap.cells) {
        cells.push_back(Value::Vec({
            Value::Symbol("cell"),
            Value::Int((std::int64_t)c->id),
            Value::Int((int)c->kind),
            Value::String(c->name),
            Value::String(c->text),
            Value::Bool(c->runOnLoad),
            Value::Float(c->panelHeight),
        }));
        if (c->kind == CellKind::Panel) panelNames.insert(c->name);
    }

    std::vector<Value> panels{Value::Symbol("panels")};
    if (ctx.uiState) {
        std::lock_guard<std::mutex> lock(ctx.uiState->mtx);
        for (auto const& panel : panelNames) {
            std::vector<Value> pv{Value::Symbol("panel"), Value::String(panel)};
            for (auto const& w : ctx.uiState->widgets) {
                if (w->panel != panel) continue;
                std::vector<Value> values;
                for (double v : w->values) values.push_back(Value::Float(v));
                std::vector<Value> notes;
                for (float v : w->noteData) notes.push_back(Value::Float(v));
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
                }));
            }
            panels.push_back(Value::Vec(std::move(pv)));
        }
    }

    Value root = Value::Vec({
        Value::Symbol("doc"),
        Value::Int(1),  // version
        Value::Int((std::int64_t)snap.nextCellId),
        Value::Vec(std::move(cells)),
        Value::Vec(std::move(panels)),
    });

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
    if (!ctx.engine || tapID == 0) return;
    if (engine::begin(ctx.engine) == tzpl_errNone) {
        engine::untap(tapID);
        engine::go(silo);
    }
}

SnapshotPtr loadDocument(bridge::AppContext& ctx, std::string const& path,
                         std::string& err) {
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
        cell->kind = (kind >= 0 && kind <= 2) ? (CellKind)kind : CellKind::Code;
        cell->name = std::string(rc.child(3).asStr());
        cell->text = std::string(rc.child(4).asStr());
        cell->runOnLoad = rc.child(5).asBool();
        if (rc.childCount() > 6) {
            cell->panelHeight = (float)rc.child(6).asFloat();
        }
        if (cell->id >= snap->nextCellId) snap->nextCellId = cell->id + 1;
        snap->cells.push_back(std::move(cell));
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
                if (kindInt < 0 || kindInt > (int)bridge::UIWidgetKind::Label)
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
                        || kind == bridge::UIWidgetKind::Matrix) {
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

    return snap;
}

// ---------------------------------------------------------------------------
// Widget capture / restore
// ---------------------------------------------------------------------------

static bool inPanels(std::vector<std::string> const& panels,
                     std::string const& p) {
    return std::find(panels.begin(), panels.end(), p) != panels.end();
}

std::shared_ptr<WidgetSnapList const>
captureWidgets(bridge::UIState* ui, std::vector<std::string> const& panels) {
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
        s.fx = w->fx; s.fy = w->fy; s.fw = w->fw; s.fh = w->fh;
        s.rows = w->rows; s.cols = w->cols;
        s.noteData = w->noteData;
        s.labelText = w->labelText;
        s.rollBeats = w->rollBeats;
        s.rollLowPitch = w->rollLowPitch;
        s.rollRows = w->rollRows;
        s.rollEdo = w->rollEdo;
        out->push_back(std::move(s));
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
            for (auto const& s : target) {
                if (s.panel == w.panel && s.name == w.name) return true;
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
        for (auto const& s : target) {
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
                if (w->noteData != s.noteData) {
                    w->noteData = s.noteData;
                    w->dirtyCallback = true;
                }
                if (w->values != s.values) {
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
                    || s.kind == bridge::UIWidgetKind::Matrix) {
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
                nw->dirtyEngine = true;
                nw->dirtyCallback = true;
            }
        }
    }
    for (auto [tapID, silo] : taps) untapWidget(ctx, tapID, silo);
}

} // namespace doc
