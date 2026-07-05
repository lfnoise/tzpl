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
//  notebook_panel.hpp
//  app
//
//  The notebook view: a vertical list of cells (prose / code / panel).
//  Code cells embed a TextEditor and evaluate against the shared
//  REPLSession (one cell at a time, on the AsyncEval worker); their
//  output, staleness, and error markers are per-cell runtime state.
//  Panel cells render the `ui` registry's widgets for their panel name
//  inline. Document structure lives in doc::DocumentStore; editor text
//  is synced into the snapshot on eval and save.
//

#ifndef notebook_panel_hpp
#define notebook_panel_hpp

#include "document.hpp"
#include "gui_state.hpp"
#include "TextEditor.h"

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bridge { struct AppContext; }
namespace ts { class REPLSession; }
struct ControlsPanel;

class NotebookPanel {
public:
    NotebookPanel();

    bool isOpen() const { return open_; }
    std::string const& filePath() const { return store_.filePath(); }
    bool modified() const { return store_.modified() || anyEditorDirty(); }

    // Document lifecycle. open/save report errors via `err`.
    void newDocument();
    bool open(std::string const& path, bridge::AppContext& ctx, std::string& err);
    bool save(bridge::AppContext& ctx, std::string& err);          // to filePath()
    bool saveAs(std::string const& path, bridge::AppContext& ctx, std::string& err);
    void close();

    // Draw the notebook into the current layout region.
    void draw(float width, float height, GuiState& gui,
              bridge::AppContext& ctx, ts::REPLSession* session,
              ControlsPanel& controls);

    // Panel names claimed by this document's panel cells (floating Controls
    // windows skip these; save snapshots their widgets).
    std::vector<std::string> claimedPanels() const;

    // Run the focused cell (Cmd+Enter route).
    void runFocused(GuiState& gui, bridge::AppContext& ctx,
                    ts::REPLSession* session);
    void runAll();

    // File > Open with a code cell focused reads the file into that
    // cell (one history commit). Returns false when not applicable
    // (no focused code cell, unreadable file) -- the caller then opens
    // the file in the editor as usual.
    bool openFileIntoFocusedCell(std::string const& path);

    // Cmd+Arrow cursor movement (native menu shortcuts), routed to the
    // focused cell's editor. No-ops when no text cell is focused.
    void moveHome(bool select);
    void moveEnd(bool select);
    void moveTop(bool select);
    void moveBottom(bool select);

    // Edit-menu commands applied directly to the focused cell's editor
    // (the selected cell with the accent bar). Direct calls, not key
    // injection -- deterministic regardless of ImGui focus timing.
    // No-ops when no text cell is focused.
    void cutText();
    void copyText();
    void pasteText();
    void selectAllText();
    // Character-level undo/redo in the focused cell's editor; false if
    // no cell editor is focused (caller falls back to doc history).
    bool undoText();
    bool redoText();

    // Route a finished cell eval (AsyncEval cellId != 0) into cell state.
    void onEvalDone(std::uint64_t cellId,
                    ts::REPLSession::EvalResult const& result,
                    std::string const& code);

    // Route a captured print line to the cell whose eval is in flight.
    void addCellOutput(std::uint64_t cellId, std::string const& line,
                       LineKind kind);

    // Pump queued runs (Run All / runOnLoad): launch the next queued cell
    // when no eval is in flight. Call once per frame.
    void pumpRunQueue(GuiState& gui, bridge::AppContext& ctx,
                      ts::REPLSession* session);

    // True while queued runs are waiting -- keeps the frame loop ticking.
    bool hasQueuedRuns() const { return !runQueue_.empty(); }

    // The toolbar Close button was pressed; the app routes this through
    // the same confirm-and-close flow as Cmd+W. One-shot.
    bool takeCloseRequest() {
        bool r = closeRequested_;
        closeRequested_ = false;
        return r;
    }

    // The toolbar Editor button was pressed: show the editor tabs while
    // keeping the notebook open in the background. One-shot.
    bool takeEditorViewRequest() {
        bool r = editorViewRequested_;
        editorViewRequested_ = false;
        return r;
    }

    // ---- History (M4) -----------------------------------------------------
    // One immutable history tree over cells + code text + claimed-panel
    // widget values. Continuous gestures commit once on release; typing
    // coalesces on ~1s idle; each cell eval commits once.

    // Per-frame history upkeep: consume widget gesture-end flags and the
    // typing-coalesce timer. Call after widget event dispatch.
    void update(bridge::AppContext& ctx);

    // Document-level undo/redo (the Cmd+Z route when no text field is
    // focused). Applies the snapshot to editors and widgets.
    void undoDocument(bridge::AppContext& ctx);
    void redoDocument(bridge::AppContext& ctx);

private:
    struct CellRuntime {
        std::unique_ptr<TextEditor> editor;   // Prose/Code cells
        std::vector<OutputLine> output;       // per-cell eval output
        std::size_t ranTextHash = 0;          // text hash at last clean eval
        bool everRan = false;
        bool lastErrored = false;
        // Typing coalesce: time of the last observed edit; 0 = clean.
        double lastEditTime = 0.0;
        // Panel cells: arrange mode (drag/resize widgets on the canvas).
        bool arrange = false;
        // Panel cells: content height measured last frame; the canvas
        // grows to at least this, so widgets are never clipped.
        float panelContentH = 0.0f;
        // Code cells: pane heights dragged by the user; 0 = fit the
        // content (up to a cap). editorHeight is the text area,
        // outputHeight the output pane below it.
        float editorHeight = 0.0f;
        float outputHeight = 0.0f;
    };

    // Perform mode: locked layout, panel cells only, widgets drawn at
    // kPerformScale for stage-sized targets. Esc or Exit Perform leaves.
    static constexpr float kPerformScale = 1.5f;
    void drawPerform(float width, float height, bridge::AppContext& ctx,
                     ControlsPanel& controls);
    // The panel cell's widget canvas, shared between the normal cell
    // body (arrange supported, scale 1) and perform mode (locked,
    // scaled). Draws the BeginChild canvas at canvasH (unscaled) and
    // returns the measured content height, which the caller caches so
    // the canvas grows to fit its widgets next frame. The caller adds
    // any splitter/chrome.
    float drawPanelCanvas(std::shared_ptr<doc::Cell const> const& cell,
                          float width, bridge::AppContext& ctx,
                          ControlsPanel& controls, bool arrange,
                          float scale, float canvasH);

    CellRuntime& runtime(doc::CellId id, doc::Cell const& cell);
    TextEditor* focusedEditor();              // focused cell's editor, if any
    void syncCellText(doc::CellId id);        // editor text -> snapshot
    void syncAllCellText();
    bool anyEditorDirty() const;              // editor text differs from snapshot
    void launchCell(doc::CellId id, GuiState& gui, bridge::AppContext& ctx,
                    ts::REPLSession* session);
    void drawCell(std::shared_ptr<doc::Cell const> const& cell, float width,
                  GuiState& gui, bridge::AppContext& ctx,
                  ts::REPLSession* session, ControlsPanel& controls);
    void drawCellOutput(CellRuntime& rt, float width);
    void queueRunOnLoad();

    // Capture claimed-panel widgets into the working snapshot and commit
    // one history node (no-op if nothing changed).
    void commitHistory(std::string const& label, bridge::AppContext& ctx);
    // Make current state the history root (after new/open).
    void rerootHistory(std::string const& label, bridge::AppContext& ctx);
    // Apply a history snapshot: resync cell editors + restore widgets.
    // panelsBefore = claimedPanels() captured before the history jump:
    // widgets are reconciled over the union of panels claimed before
    // and after, so undoing past a panel cell's creation removes its
    // widgets instead of orphaning them into floating windows.
    void applySnapshot(doc::SnapshotPtr snap, bridge::AppContext& ctx,
                       std::vector<std::string> const& panelsBefore);
    void drawHistoryWindow(bridge::AppContext& ctx);

    doc::DocumentStore store_;
    std::unordered_map<doc::CellId, CellRuntime> runtime_;
    TextEditor::LanguageDefinition langDef_;
    bool open_ = false;
    doc::CellId focusedCell_ = 0;
    std::deque<doc::CellId> runQueue_;

    // Deferred structural edits requested during draw (applied after the
    // cell loop so iteration stays valid).
    doc::CellId pendingDelete_ = 0;
    doc::CellId pendingMove_ = 0;
    int pendingMoveDelta_ = 0;

    bool showHistory_ = false;
    bool closeRequested_ = false;
    bool editorViewRequested_ = false;
    bool performMode_ = false;

    // Set by onEvalDone; update() turns it into one history commit.
    std::string evalCommitLabel_;
    // Set by draw() when a structural edit happened; committed in update().
    std::string structureCommitLabel_;
};

#endif /* notebook_panel_hpp */
