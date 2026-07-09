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
//  history_window.cpp
//  app (JUCE)
//

#include "history_window.hpp"

namespace tzplapp {

HistoryView::HistoryView(NotebookView& notebook) : notebook_(notebook) {
    list_.setRowHeight(20);
    list_.setColour(juce::ListBox::backgroundColourId,
                    juce::Colour(0xff1e1e1e));
    addAndMakeVisible(list_);

    footer_.setJustificationType(juce::Justification::centredLeft);
    footer_.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    footer_.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(footer_);

    refresh();
    startTimerHz(4);
}

void HistoryView::resized() {
    auto r = getLocalBounds();
    footer_.setBounds(r.removeFromBottom(20).reduced(4, 0));
    list_.setBounds(r);
}

void HistoryView::timerCallback() {
    refresh();  // refresh() no-ops when the tree/cursor are unchanged
}

void HistoryView::refresh() {
    auto rows = notebook_.historyRows();
    doc::HistNode* cursor = nullptr;
    for (auto const& r : rows) if (r.isCursor) cursor = r.node;
    // Skip the churn if nothing changed since the last poll.
    if (rows.size() == lastCount_ && cursor == lastCursor_ && !rows_.empty()) {
        // Still refresh the highlighted row's paint (cheap) and bail.
        return;
    }
    lastCount_ = rows.size();
    lastCursor_ = cursor;
    rows_ = std::move(rows);
    list_.updateContent();
    // Keep the cursor row visible.
    for (int i = 0; i < (int)rows_.size(); ++i)
        if (rows_[(size_t)i].isCursor) {
            list_.selectRow(i, /*dontScroll=*/false, /*deselectOthers=*/true);
            break;
        }
    footer_.setText(juce::String((int)rows_.size()) + " nodes",
                    juce::dontSendNotification);
    repaint();
}

void HistoryView::paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                                   bool rowSelected) {
    if (row < 0 || row >= (int)rows_.size()) return;
    auto const& r = rows_[(size_t)row];
    if (r.isCursor)
        g.fillAll(juce::Colour(0xff3a5a80));
    else if (rowSelected)
        g.fillAll(juce::Colour(0x30ffffff));
    g.setColour(juce::Colours::white.withAlpha(r.isCursor ? 1.0f : 0.8f));
    g.setFont(juce::FontOptions(13.0f));
    int indent = 6 + r.depth * 14;
    g.drawText(r.label, indent, 0, w - indent - 4, h,
               juce::Justification::centredLeft);
}

void HistoryView::recallRow(int row) {
    if (row < 0 || row >= (int)rows_.size()) return;
    notebook_.jumpToHistory(rows_[(size_t)row].node);
    refresh();
}

void HistoryView::listBoxItemClicked(int row, juce::MouseEvent const&) {
    recallRow(row);
}

void HistoryView::returnKeyPressed(int row) {
    recallRow(row);
}

// ---------------------------------------------------------------------------

HistoryWindow::HistoryWindow(NotebookView& notebook,
                             std::function<void()> onClose)
    : juce::DocumentWindow("History",
                           juce::Colour(0xff2a2a2a),
                           juce::DocumentWindow::closeButton),
      onClose_(std::move(onClose)) {
    setUsingNativeTitleBar(true);
    setContentOwned(new HistoryView(notebook), false);
    setResizable(true, false);
    centreWithSize(300, 400);
    setVisible(true);
}

}
