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
//  cell_component.hpp
//  app (JUCE)
//
//  One notebook cell. Prose/Code cells embed a TzplCodeEditor; Code cells
//  add a per-cell output area below it. Panel/Presets cells are labeled
//  placeholders until the widget set lands (M4/M5). The cell owns view and
//  runtime state; document structure lives in doc::DocumentStore.
//

#ifndef cell_component_hpp
#define cell_component_hpp

#include "document.hpp"
#include "gui_state.hpp"
#include "../editor_pane.hpp"      // TzplCodeEditor
#include "../tzpl_tokeniser.hpp"
#include "../widgets/panel_canvas.hpp"
#include "presets_view.hpp"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include <vector>

namespace bridge { struct UIState; }

namespace tzplapp {

class ControlsDispatcher;

class CellComponent : public juce::Component,
                      private juce::CodeDocument::Listener {
public:
    // ui/dispatcher are non-null only for hosts that render panel cells
    // (the notebook); null elsewhere (panel cells fall back to a label).
    CellComponent(doc::CellId id, TzplTokeniser& tokeniser, float fontSize,
                  bridge::UIState* ui = nullptr,
                  ControlsDispatcher* dispatcher = nullptr);
    ~CellComponent() override;

    doc::CellId cellId() const { return id_; }

    // Sync this cell's model state (kind/text/name/collapsed) into the view.
    // Rebuilds the editor content only when the model text differs from what
    // the editor holds, so it doesn't stomp in-progress typing.
    void syncFromModel(doc::Cell const& cell);

    // Editor text (Prose/Code). Empty for Panel/Presets.
    juce::String editorText() const;
    bool hasEditor() const { return editor_ != nullptr; }
    TzplCodeEditor* editor() { return editor_.get(); }

    bool isSelected() const { return selected_; }
    void setSelected(bool sel);

    // Per-cell eval output.
    void clearOutput();
    void addOutputLine(OutputLine const& line);
    std::vector<OutputLine> const& outputLines() const { return outputLines_; }
    void setErrorMarkers(std::vector<std::pair<int, juce::String>> const& m);

    void setFontSize(float px);

    // The preferred total height for the current content and width.
    int preferredHeight(int width) const;
    void resized() override;
    void paint(juce::Graphics& g) override;

    // Callbacks to the NotebookView. `onTextChanged` fires on every edit so
    // the view can mark the document modified / schedule a history commit.
    std::function<void()> onRun;            // run button / focus + Cmd+Enter
    std::function<void()> onDelete;
    std::function<void(int)> onMove;        // delta -1 / +1
    std::function<void(bool)> onCollapse;   // new collapsed state
    std::function<void()> onFocused;        // editor gained focus
    std::function<void()> onTextChanged;
    std::function<void()> onArrangeCommit;  // panel arrange gesture ended
    std::function<void(juce::String)> onRenameCell;  // name field committed

    // Presets cell callbacks (index into the cell's preset bank).
    std::function<void()> onPresetStore;
    std::function<void(int)> onPresetRecall;
    std::function<void(int)> onPresetOverwrite;
    std::function<void(int, juce::String)> onPresetRename;
    std::function<void(int)> onPresetDelete;

private:
    void buildForKind(doc::CellKind kind);
    void layOutHeader(juce::Rectangle<int>& r);
    void paintDisclosure(juce::Graphics& g) const;
    void codeDocumentTextInserted(juce::String const&, int) override {
        if (onTextChanged) onTextChanged();
    }
    void codeDocumentTextDeleted(int, int) override {
        if (onTextChanged) onTextChanged();
    }

    doc::CellId id_;
    TzplTokeniser& tokeniser_;
    float fontSize_;
    bridge::UIState* ui_ = nullptr;
    ControlsDispatcher* dispatcher_ = nullptr;
    doc::CellKind kind_ = doc::CellKind::Code;
    juce::String panelName_;           // Panel cells: canvas panel name
    bool collapsed_ = false;
    bool selected_ = false;

    // Header
    juce::Label kindLabel_;
    juce::TextEditor nameField_;      // Code label / Panel name (editable)
    void commitCellName();
    juce::TextButton runButton_ { "Run" };
    juce::TextButton arrangeButton_ { "arrange" };
    bool arrangeOn_ = false;
    juce::TextButton collapseButton_ { "-" };
    juce::TextButton upButton_ { "^" };
    juce::TextButton downButton_ { "v" };
    juce::TextButton deleteButton_ { "x" };

    // Body
    std::unique_ptr<juce::CodeDocument> codeDoc_;
    std::unique_ptr<TzplCodeEditor> editor_;
    juce::TextEditor output_;          // Code cells
    juce::Label placeholder_;          // panel w/o registry
    std::unique_ptr<PanelCanvas> panelCanvas_; // Panel cells
    std::unique_ptr<PresetsView> presetsView_; // Presets cells
    std::vector<OutputLine> outputLines_;

    static constexpr int kHeaderH = 22;
    static constexpr int kPad = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CellComponent)
};

}

#endif /* cell_component_hpp */
