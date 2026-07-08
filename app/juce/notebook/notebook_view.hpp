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

class NotebookView : public juce::Component,
                     private juce::FocusChangeListener {
public:
    NotebookView(bridge::AppContext& appCtx, GuiState& guiState,
                 std::function<ts::REPLSession*()> session);
    ~NotebookView() override;

    void resized() override;

    // -- Document lifecycle --
    void newDocument();
    bool openFile(juce::File const& file, juce::String& err);
    bool saveToFile(juce::File const& file, juce::String& err);
    juce::File currentFile() const;
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

    // Panels claimed by this document's panel cells (M4 dispatch skips them).
    std::vector<std::string> claimedPanels() const;

    void setFontSize(float px);

    // Test hooks (TZPL_JUCE_SELFTEST).
    void testTypeIntoFocusedCell(juce::String const& text);
    juce::String testFocusedCellOutput() const;
    int testCellCount() const { return store_.cellCount(); }

private:
    void rebuildCells();          // reconcile CellComponents against the snapshot
    void relayoutContent();
    CellComponent* cellFor(doc::CellId id);
    void syncCellTextToModel(doc::CellId id);
    void syncAllCellText();
    void selectCell(doc::CellId id);
    void launchCell(doc::CellId id);
    void queueRunOnLoad();
    void globalFocusChanged(juce::Component* focused) override;

    bridge::AppContext& appCtx_;
    GuiState& guiState_;
    std::function<ts::REPLSession*()> session_;
    TzplTokeniser tokeniser_;

    doc::DocumentStore store_;
    juce::Component toolbar_;
    juce::TextButton addCodeButton_ { "+ Code" };
    juce::TextButton addProseButton_ { "+ Prose" };
    juce::TextButton addPanelButton_ { "+ Panel" };
    juce::TextButton runAllButton_ { "Run All" };
    juce::Viewport viewport_;
    juce::Component content_;                 // holds the CellComponents
    std::unordered_map<doc::CellId, std::unique_ptr<CellComponent>> cells_;
    doc::CellId selectedCell_ = 0;
    std::deque<doc::CellId> runQueue_;
    float fontSize_ = 14.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NotebookView)
};

}

#endif /* notebook_view_hpp */
