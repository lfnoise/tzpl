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
//  history_window.hpp
//  app (JUCE)
//
//  A floating window showing the notebook's persistent undo-history tree.
//  Each node is a row (indented under branch points); the cursor node is
//  highlighted; clicking a row -- or Up/Down while focused -- recalls that
//  state. Mirrors NotebookPanel::drawHistoryWindow. The tree is polled from
//  the NotebookView on a light timer since edits mutate it continuously.
//

#ifndef history_window_hpp
#define history_window_hpp

#include "notebook_view.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace tzplapp {

class HistoryView : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::Timer {
public:
    explicit HistoryView(NotebookView& notebook);

    void resized() override;

    // ListBoxModel
    int getNumRows() override { return (int)rows_.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                          bool rowSelected) override;
    void listBoxItemClicked(int row, juce::MouseEvent const&) override;
    void returnKeyPressed(int row) override;

private:
    void timerCallback() override;   // repoll the tree
    void refresh();
    void recallRow(int row);         // jump the document to that node

    NotebookView& notebook_;
    juce::ListBox list_ { "history", this };
    juce::Label footer_;
    std::vector<NotebookView::HistoryRow> rows_;
    doc::HistNode* lastCursor_ = nullptr;
    std::size_t lastCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryView)
};

class HistoryWindow : public juce::DocumentWindow {
public:
    HistoryWindow(NotebookView& notebook, std::function<void()> onClose);
    void closeButtonPressed() override { if (onClose_) onClose_(); }

private:
    std::function<void()> onClose_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryWindow)
};

}

#endif /* history_window_hpp */
