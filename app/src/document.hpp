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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bridge { struct AppContext; struct UIState; }

namespace doc {

using CellId = std::uint64_t;

enum class CellKind : int { Prose = 0, Code = 1, Panel = 2 };

struct Cell {
    CellId id = 0;
    CellKind kind = CellKind::Code;
    std::string name;        // Panel: panel name. Code: optional label.
    std::string text;        // Prose/Code source.
    bool runOnLoad = false;  // Code: run automatically after open.
};

struct DocSnapshot {
    std::vector<std::shared_ptr<Cell const>> cells;
    CellId nextCellId = 1;
};

using SnapshotPtr = std::shared_ptr<DocSnapshot const>;

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

    bool modified() const { return modified_; }
    void clearModified() { modified_ = false; }
    std::string const& filePath() const { return filePath_; }
    void setFilePath(std::string path) { filePath_ = std::move(path); }

    // Replace the whole document (after load / new).
    void reset(SnapshotPtr snap, std::string filePath);

private:
    // Clone-on-write: returns a mutable copy of the cell installed in a
    // fresh snapshot.
    Cell* mutableCell(CellId id);
    SnapshotPtr snap_;
    std::string filePath_;
    bool modified_ = false;
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

} // namespace doc

#endif /* document_hpp */
