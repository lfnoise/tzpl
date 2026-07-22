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
//  notebook_view.hpp
//  app (JUCE)
//
//  The notebook: a scrolling vertical strip of cells over a
//  doc::DocumentStore (shared, unchanged, with the ImGui app). Code cells
//  evaluate against the shared REPLSession one at a time on the AsyncEval
//  worker; MainComponent routes results/prints back here by cell id.
//  Panel/Presets cells are placeholders until M4/M5.
//

#ifndef notebook_view_hpp
#define notebook_view_hpp

#include "cell_component.hpp"
#include "document.hpp"
#include "gui_state.hpp"
#include "../tzpl_tokeniser.hpp"
#include <juce_gui_extra/juce_gui_extra.h>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>

namespace bridge { struct AppContext; }
namespace ts { class REPLSession; }

namespace tzplapp {

class ControlsDispatcher;

class NotebookView : public juce::Component,
                     private juce::FocusChangeListener,
                     private juce::Timer {
public:
    NotebookView(bridge::AppContext& appCtx, GuiState& guiState,
                 std::function<ts::REPLSession*()> session,
                 ControlsDispatcher& dispatcher);
    ~NotebookView() override;

    void resized() override;

    // -- Document lifecycle --
    void newDocument();
    bool openFile(juce::File const& file, juce::String& err);
    bool saveToFile(juce::File const& file, juce::String& err);
    juce::File currentFile() const;
    // Drop the file association: the open document becomes an untitled copy
    // and saving prompts for a new location. Used for distribution examples.
    void detachFile();
    bool isModified() const;

    // -- Cell operations --
    void addCell(doc::CellKind kind);      // after the selected cell
    void deleteSelectedCell();

    // -- Eval --
    void runFocusedCell();
    void runAll();
    // MainComponent forwards a finished cell eval (AsyncEval.cellId != 0).
    void onCellEvalDone(doc::CellId cellId,
                        ts::REPLSession::EvalResult const& result,
                        std::string const& code);
    void addCellOutput(doc::CellId cellId, OutputLine const& line);
    // Pump queued runs (Run All): launch the next when no eval is in flight.
    void pumpRunQueue();
    bool hasQueuedRuns() const { return !runQueue_.empty(); }

    // -- History --
    void undoDocument();
    void redoDocument();

    // One row of the history tree, flattened preorder for the History window.
    struct HistoryRow {
        doc::HistNode* node = nullptr;
        int depth = 0;
        juce::String label;
        bool isCursor = false;
    };
    std::vector<HistoryRow> historyRows();
    // Recall a history node (moves the cursor, restores + re-sends widgets).
    void jumpToHistory(doc::HistNode* node);
    // Open/close the floating History window.
    void toggleHistoryWindow();

    // Panels claimed by this document's panel cells (M4 dispatch skips them).
    std::vector<std::string> claimedPanels() const;

    // ControlsDispatcher reports finished widget gestures as {panel, name}
    // pairs; gestures on claimed panels become one "adjust ..." history
    // commit (floating-panel tweaks are performance state, not document).
    void onWidgetGesturesEnded(
        std::vector<std::pair<std::string, std::string>> const& widgets);

    void setFontSize(float px);

    // Test hooks (TZPL_JUCE_SELFTEST).
    void testTypeIntoFocusedCell(juce::String const& text);
    // Replace cell `index`'s editor text as if typed (fires the document
    // listener, like real keystrokes). Returns false if it has no editor.
    bool testTypeIntoCell(int index, juce::String const& text);
    juce::String testFocusedCellOutput() const;
    bool testFocusedCellOutputPaneShown() const {
        auto it = cells_.find(selectedCell_);
        return it != cells_.end() && it->second->outputPaneShown();
    }
    void testCollapseFocusedCell(bool collapsed) {
        store_.setCellCollapsed(selectedCell_, collapsed);
        if (auto* cc = cellFor(selectedCell_))
            cc->syncFromModel(*store_.cell(selectedCell_));
        relayoutContent();
    }
    bool testFocusedCellEditorVisible() {
        auto* cc = cellFor(selectedCell_);
        return cc && cc->editor() && cc->editor()->isVisible();
    }
    int testCellCount() const { return store_.cellCount(); }
    void testAddCell(doc::CellKind kind) { addCell(kind); }
    // The live editor text of cell `index` (not the store's copy).
    juce::String testCellEditorText(int index);
    // Build a Panel cell naming `panel` + a Presets cell, store the live
    // values, perturb widget "a", recall, and report whether it was restored.
    bool testPresetsRoundTrip(std::string const& panel);

private:
    void rebuildCells();          // reconcile CellComponents against the snapshot
    void relayoutContent();
    CellComponent* cellFor(doc::CellId id);
    // Push a cell editor's text into the store; true if the text differed.
    bool syncCellTextToModel(doc::CellId id);
    void syncAllCellText();
    void selectCell(doc::CellId id);
    void launchCell(doc::CellId id);
    void queueRunOnLoad();
    void globalFocusChanged(juce::Component* focused) override;

    // -- History commits --
    // One history node: capture the claimed panels' live widgets into the
    // working snapshot, then commit. Content-interning collapses no-ops.
    void commitHistory(std::string const& label);
    // Typing coalesce: a keystroke marks its cell pending and starts the
    // poll timer; ~1s after the last keystroke the timer syncs the text and
    // commits one "edit" node (mirrors NotebookPanel::update).
    void noteCellEdited(doc::CellId id);
    void timerCallback() override;
    // Sync all pending typing now and commit it as its own "edit" node --
    // undo must step back to the pre-typing state, not past it.
    void flushPendingEdits();

    // -- Presets (mirror of ImGui NotebookPanel; UIState-backed) --
    // Panel roots governed by the Presets cell `id`: the Panel cells after
    // it, up to the next Presets cell.
    std::vector<std::string> presetScope(doc::CellId id) const;
    // Snapshot the in-scope input widgets' values into a new Preset.
    doc::Preset capturePreset(std::vector<std::string> const& scope) const;
    // Write a preset's values back into the live widgets (marks them dirty).
    void applyPreset(doc::Preset const& p);
    // Install a mutated bank, commit history, and refresh the cell view.
    void commitPresets(doc::CellId id,
                       std::vector<std::shared_ptr<doc::Preset const>> presets,
                       char const* label);

    bridge::AppContext& appCtx_;
    GuiState& guiState_;
    std::function<ts::REPLSession*()> session_;
    ControlsDispatcher& dispatcher_;
    TzplTokeniser tokeniser_;

    doc::DocumentStore store_;
    juce::Component toolbar_;
    juce::TextButton addCodeButton_ { "+ Code" };
    juce::TextButton addProseButton_ { "+ Prose" };
    juce::TextButton addPanelButton_ { "+ Panel" };
    juce::TextButton addPresetsButton_ { "+ Presets" };
    juce::TextButton historyButton_ { "History" };
    juce::TextButton runAllButton_ { "Run All" };
    std::unique_ptr<class HistoryWindow> historyWindow_;
    juce::Viewport viewport_;
    juce::Component content_;                 // holds the CellComponents
    std::unordered_map<doc::CellId, std::unique_ptr<CellComponent>> cells_;
    doc::CellId selectedCell_ = 0;
    std::deque<doc::CellId> runQueue_;
    float fontSize_ = 14.0f;
    // Cells with typing the store hasn't seen -> ms timestamp of the last
    // keystroke (juce::Time::getMillisecondCounterHiRes).
    std::unordered_map<doc::CellId, double> pendingEdits_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotebookView)
};

}

#endif /* notebook_view_hpp */
