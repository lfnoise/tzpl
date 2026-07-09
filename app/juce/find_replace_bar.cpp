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
//  find_replace_bar.cpp
//  app (JUCE)
//

#include "find_replace_bar.hpp"
#include "editor_pane.hpp"

namespace tzplapp {

using juce::CodeDocument;
using juce::String;

namespace {

// Offsets of all matches of `needle` in `haystack`. Empty needle -> none.
std::vector<int> findAllOffsets(String const& haystack, String const& needle,
                                bool caseSensitive) {
    std::vector<int> offsets;
    if (needle.isEmpty()) return offsets;
    String hay = caseSensitive ? haystack : haystack.toLowerCase();
    String nee = caseSensitive ? needle : needle.toLowerCase();
    int from = 0;
    for (;;) {
        int idx = hay.indexOf(from, nee);
        if (idx < 0) break;
        offsets.push_back(idx);
        from = idx + nee.length();
    }
    return offsets;
}

}

FindReplaceBar::FindReplaceBar(std::function<TzplCodeEditor*()> activeEditor)
    : activeEditor_(std::move(activeEditor))
{
    auto setupField = [this](juce::TextEditor& f, String const& placeholder) {
        f.setMultiLine(false);
        f.setReturnKeyStartsNewLine(false);
        f.setTextToShowWhenEmpty(placeholder, juce::Colours::grey);
        addAndMakeVisible(f);
    };
    setupField(findField_, "Find");
    setupField(replaceField_, "Replace");

    // Re-search live as the find term changes.
    findField_.onTextChange = [this] { search(true, false); };
    findField_.onReturnKey = [this] { findNext(); };
    replaceField_.onReturnKey = [this] { replaceCurrent(); };

    prevButton_.onClick = [this] { findPrevious(); };
    nextButton_.onClick = [this] { findNext(); };
    replaceButton_.onClick = [this] { replaceCurrent(); };
    replaceAllButton_.onClick = [this] { replaceAll(); };
    caseButton_.onClick = [this] { search(true, false); };
    closeButton_.onClick = [this] { hide(); };

    caseButton_.setTooltip("Case sensitive");
    matchLabel_.setJustificationType(juce::Justification::centredRight);
    matchLabel_.setMinimumHorizontalScale(1.0f);

    for (auto* b : { &prevButton_, &nextButton_, &replaceButton_,
                     &replaceAllButton_, &closeButton_ })
        addAndMakeVisible(b);
    addAndMakeVisible(caseButton_);
    addAndMakeVisible(matchLabel_);
}

void FindReplaceBar::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::TextEditor::backgroundColourId).contrasting(0.06f));
    g.setColour(findColour(juce::TextEditor::outlineColourId));
    g.drawLine(0.0f, (float)getHeight(), (float)getWidth(), (float)getHeight());
}

void FindReplaceBar::resized() {
    auto r = getLocalBounds().reduced(4, 4);
    auto row = r;
    closeButton_.setBounds(row.removeFromRight(24));
    row.removeFromRight(4);
    caseButton_.setBounds(row.removeFromRight(40));
    matchLabel_.setBounds(row.removeFromRight(90));
    replaceAllButton_.setBounds(row.removeFromRight(40));
    replaceButton_.setBounds(row.removeFromRight(64));
    nextButton_.setBounds(row.removeFromRight(28));
    prevButton_.setBounds(row.removeFromRight(28));
    row.removeFromRight(6);
    // Split the remaining width between find and replace fields.
    int half = row.getWidth() / 2;
    findField_.setBounds(row.removeFromLeft(half - 3));
    row.removeFromLeft(6);
    replaceField_.setBounds(row);
}

bool FindReplaceBar::keyPressed(juce::KeyPress const& key) {
    if (key == juce::KeyPress::escapeKey) { hide(); return true; }
    return false;
}

void FindReplaceBar::show(String const& seed) {
    if (seed.isNotEmpty())
        findField_.setText(seed, juce::dontSendNotification);
    bool wasHidden = !shown_;
    shown_ = true;
    setVisible(true);
    if (wasHidden && onVisibilityChanged) onVisibilityChanged();
    findField_.grabKeyboardFocus();
    findField_.selectAll();
    search(true, false);
}

void FindReplaceBar::hide() {
    if (!shown_) return;
    shown_ = false;
    setVisible(false);
    if (onVisibilityChanged) onVisibilityChanged();
    if (auto* ed = activeEditor_()) ed->grabKeyboardFocus();
}

void FindReplaceBar::seedReplace(String const& text) {
    replaceField_.setText(text, juce::dontSendNotification);
}

void FindReplaceBar::findNext()     { search(true, true); }
void FindReplaceBar::findPrevious() { search(false, false); }

void FindReplaceBar::search(bool forward, bool fromSelectionEnd) {
    auto* ed = activeEditor_();
    if (ed == nullptr) return;
    String term = findField_.getText();
    if (term.isEmpty()) { updateMatchLabel(-1, 0); return; }

    auto& doc = ed->getDocument();
    String content = doc.getAllContent();
    auto offsets = findAllOffsets(content, term, caseButton_.getToggleState());
    if (offsets.empty()) { updateMatchLabel(-1, 0); return; }

    // Search relative to the current selection/caret.
    auto sel = ed->getHighlightedRegion();
    int anchor = fromSelectionEnd ? sel.getEnd() : sel.getStart();

    int chosen = -1;
    if (forward) {
        for (int i = 0; i < (int)offsets.size(); ++i)
            if (offsets[i] >= anchor) { chosen = i; break; }
        if (chosen < 0) chosen = 0; // wrap to first
    } else {
        for (int i = (int)offsets.size() - 1; i >= 0; --i)
            if (offsets[i] < anchor) { chosen = i; break; }
        if (chosen < 0) chosen = (int)offsets.size() - 1; // wrap to last
    }

    int start = offsets[chosen];
    ed->setHighlightedRegion({ start, start + term.length() });
    updateMatchLabel(chosen, (int)offsets.size());
}

void FindReplaceBar::replaceCurrent() {
    auto* ed = activeEditor_();
    if (ed == nullptr) return;
    String term = findField_.getText();
    if (term.isEmpty()) return;

    auto sel = ed->getHighlightedRegion();
    auto& doc = ed->getDocument();
    String selText = doc.getTextBetween(
        CodeDocument::Position(doc, sel.getStart()),
        CodeDocument::Position(doc, sel.getEnd()));

    bool selectionIsMatch = caseButton_.getToggleState()
        ? selText == term
        : selText.equalsIgnoreCase(term);

    if (selectionIsMatch) {
        doc.newTransaction();
        doc.replaceSection(sel.getStart(), sel.getEnd(), replaceField_.getText());
        // Place the caret after the inserted replacement, then advance.
        int newEnd = sel.getStart() + replaceField_.getText().length();
        ed->setHighlightedRegion({ newEnd, newEnd });
    }
    search(true, true);
}

void FindReplaceBar::replaceAll() {
    auto* ed = activeEditor_();
    if (ed == nullptr) return;
    String term = findField_.getText();
    if (term.isEmpty()) return;

    auto& doc = ed->getDocument();
    String content = doc.getAllContent();
    auto offsets = findAllOffsets(content, term, caseButton_.getToggleState());
    if (offsets.empty()) { updateMatchLabel(-1, 0); return; }

    String replacement = replaceField_.getText();
    doc.newTransaction();
    // Replace from the end so earlier offsets stay valid.
    for (int i = (int)offsets.size() - 1; i >= 0; --i)
        doc.replaceSection(offsets[i], offsets[i] + term.length(), replacement);

    matchLabel_.setText(String((int)offsets.size()) + " replaced",
                        juce::dontSendNotification);
}

void FindReplaceBar::updateMatchLabel(int matchIndex, int total) {
    if (total == 0) {
        matchLabel_.setText(findField_.getText().isEmpty() ? "" : "no matches",
                            juce::dontSendNotification);
    } else {
        matchLabel_.setText(String(matchIndex + 1) + " / " + String(total),
                            juce::dontSendNotification);
    }
}

}
