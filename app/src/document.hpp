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
//  document.hpp
//  app
//
//  Notebook document model: an immutable snapshot of cells plus a
//  GUI-thread-only mutator (DocumentStore) that produces new snapshots by
//  path copying -- unchanged cells are shared between snapshots, so a
//  snapshot is one root pointer (the persistent-history substrate for M4).
//
//  Persistence is the .tzd container (TZB binary, tzpl_sexpr_bin.hpp):
//  the cell list plus, for each panel cell, that panel's widgets
//  (name/kind/spec/values/frame) snapshotted from the ui registry.
//  Bindings are never saved: a loaded document's widgets are unbound
//  until code re-runs (ui constructors are idempotent upserts keyed by
//  (panel, name) -- rebinding is identical to first creation).
//

#ifndef document_hpp
#define document_hpp

#include "tzpl_ui_state.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bridge { struct AppContext; }

namespace doc {

using CellId = std::uint64_t;

enum class CellKind : int { Prose = 0, Code = 1, Panel = 2 };

struct Cell {
    CellId id = 0;
    CellKind kind = CellKind::Code;
    std::string name;        // Panel: panel name. Code: optional label.
    std::string text;        // Prose/Code source.
    bool runOnLoad = false;  // Code: run automatically after open.
    float panelHeight = 240.0f;  // Panel: canvas height (arrange layout).
    bool collapsed = false;  // Body hidden; only the header strip shows.
};

// State of one widget in a document-claimed panel, captured into snapshots
// at history commit points so undo/redo can restore (and audibly re-send)
// control values. Bindings are runtime-only and never captured.
struct WidgetSnap {
    std::string panel;
    std::string name;
    bridge::UIWidgetKind kind = bridge::UIWidgetKind::Slider;
    bridge::UISpec spec;
    bridge::UISpec spec2;
    std::vector<double> values;
    // Layout + kind-specific state (M5).
    float fx = -1.0f, fy = -1.0f, fw = 0.0f, fh = 0.0f;
    int rows = 1, cols = 1;
    std::vector<float> noteData;
    std::string labelText;
    float rollBeats = 4.0f;
    int rollLowPitch = 48;
    int rollRows = 24;
    int rollEdo = 12;
    std::string keyChord;

    bool operator==(WidgetSnap const& o) const {
        return panel == o.panel && name == o.name && kind == o.kind
            && spec.lo == o.spec.lo && spec.hi == o.spec.hi
            && spec.init == o.spec.init && spec.warp == o.spec.warp
            && spec.warpParam == o.spec.warpParam
            && spec2.lo == o.spec2.lo && spec2.hi == o.spec2.hi
            && spec2.init == o.spec2.init && spec2.warp == o.spec2.warp
            && spec2.warpParam == o.spec2.warpParam
            && values == o.values
            && fx == o.fx && fy == o.fy && fw == o.fw && fh == o.fh
            && rows == o.rows && cols == o.cols
            && noteData == o.noteData && labelText == o.labelText
            && rollBeats == o.rollBeats && rollLowPitch == o.rollLowPitch
            && rollRows == o.rollRows && rollEdo == o.rollEdo
            && keyChord == o.keyChord;
    }
};

using WidgetSnapList = std::vector<WidgetSnap>;

struct DocSnapshot {
    std::vector<std::shared_ptr<Cell const>> cells;
    CellId nextCellId = 1;
    // Widgets of document-claimed panels at the last commit (shared between
    // snapshots when unchanged).
    std::shared_ptr<WidgetSnapList const> widgets;
};

using SnapshotPtr = std::shared_ptr<DocSnapshot const>;

// One node of the persistent history tree. Undo moves to parent, redo
// descends activeChild; an edit made off-tip appends a new child (branch)
// -- nothing is ever destroyed.
struct HistNode {
    SnapshotPtr snap;
    std::string label;
    HistNode* parent = nullptr;
    std::vector<std::unique_ptr<HistNode>> children;
    int activeChild = -1;
};

// GUI-thread-only mutator. Every edit produces a new snapshot; M3 keeps
// only the current one (M4 hangs the history tree off these).
class DocumentStore {
public:
    DocumentStore();

    SnapshotPtr snapshot() const { return snap_; }
    int cellCount() const { return (int)snap_->cells.size(); }
    std::shared_ptr<Cell const> cellAt(int index) const;
    std::shared_ptr<Cell const> cell(CellId id) const;
    int indexOf(CellId id) const;

    CellId insertCell(int index, CellKind kind, std::string name = {},
                      std::string text = {});
    void removeCell(CellId id);
    void moveCell(CellId id, int delta);
    void setCellText(CellId id, std::string const& text);
    void setCellName(CellId id, std::string const& name);
    void setCellRunOnLoad(CellId id, bool runOnLoad);
    void setCellPanelHeight(CellId id, float height);
    void setCellCollapsed(CellId id, bool collapsed);

    bool modified() const { return modified_; }
    void clearModified() { modified_ = false; }
    std::string const& filePath() const { return filePath_; }
    void setFilePath(std::string path) { filePath_ = std::move(path); }

    // Replace the whole document (after load / new). Re-roots the history
    // at the new snapshot.
    void reset(SnapshotPtr snap, std::string filePath);

    // ---- History tree -----------------------------------------------------
    // snap_ is the WORKING snapshot; commit() records it as a HistNode
    // child of the cursor (branching if the cursor already has children).

    // Install a widget capture into the working snapshot. Returns true if
    // it differed from the current one.
    bool setWidgetSnap(std::shared_ptr<WidgetSnapList const> widgets);

    // Commit the working snapshot (no-op if identical to the cursor's).
    // Returns true if a node was created.
    bool commit(std::string const& label);

    // Make the current working snapshot the new history root (discards any
    // previous tree). Used right after new/open.
    void rerootHistory(std::string const& label);

    bool canUndo() const { return cursor_ && cursor_->parent; }
    bool canRedo() const {
        return cursor_ && cursor_->activeChild >= 0
            && cursor_->activeChild < (int)cursor_->children.size();
    }
    // Move the cursor and set the working snapshot; return it (null if
    // no move happened). The caller applies it to editors/widgets.
    SnapshotPtr undo();
    SnapshotPtr redo();
    // Jump directly to any node in the tree (marks the path to it as the
    // active branch so redo follows it).
    SnapshotPtr jumpTo(HistNode* node);

    HistNode* historyRoot() { return root_.get(); }
    HistNode* historyCursor() { return cursor_; }

    // History is bounded: past this many nodes, commit() advances the
    // root along the path to the cursor, discarding the oldest state
    // and any branches hanging off it. Snapshots are COW-shared, so the
    // cap bounds node count, not memory precisely -- but dropped nodes
    // release every cell/widget state unique to them.
    static constexpr int kHistoryCap = 500;

private:
    // Clone-on-write: returns a mutable copy of the cell installed in a
    // fresh snapshot.
    Cell* mutableCell(CellId id);
    SnapshotPtr snap_;
    std::string filePath_;
    bool modified_ = false;

    std::unique_ptr<HistNode> root_;
    HistNode* cursor_ = nullptr;
};

// ---------------------------------------------------------------------------
// Persistence (.tzd)
// ---------------------------------------------------------------------------

// Save the document. Widgets of each panel cell's panel are snapshotted
// from ctx.uiState. Returns false with `err` set on failure.
bool saveDocument(DocSnapshot const& snap, bridge::AppContext& ctx,
                  std::string const& path, std::string& err);

// Load a document. Widgets recorded in the file are (re)created unbound in
// ctx.uiState -- existing widgets in those panels are removed first (their
// engine taps untapped). Returns null with `err` set on failure.
SnapshotPtr loadDocument(bridge::AppContext& ctx, std::string const& path,
                         std::string& err);

// ---------------------------------------------------------------------------
// Widget capture / restore (history commits and navigation)
// ---------------------------------------------------------------------------

// Snapshot the widgets of the given panels from the ui registry.
std::shared_ptr<WidgetSnapList const>
captureWidgets(bridge::UIState* ui, std::vector<std::string> const& panels);

// Restore the given panels' widgets to a captured state: values updated
// (marked dirty so the per-frame dispatch re-sends them -- undo is
// audible), missing widgets recreated unbound, extra widgets removed
// (untapping engine taps). Bindings of surviving widgets are preserved.
void restoreWidgets(bridge::AppContext& ctx, WidgetSnapList const& target,
                    std::vector<std::string> const& panels);

} // namespace doc

#endif /* document_hpp */
