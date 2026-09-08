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
#include <algorithm>

namespace tzplapp {

using juce::CodeDocument;
using juce::String;

namespace {

std::wstring toWide(String const& s) {
    return std::wstring(s.toWideCharPointer());
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

    // Match mode popup (Xcode's magnifier menu): Contains, Matches Word,
    // Starts With, Ends With, Regular Expression.
    for (int i = 0; i < kNumMatchModes; ++i)
        modeBox_.addItem(kMatchModeNames[i], i + 1);
    modeBox_.setSelectedId(1, juce::dontSendNotification);
    modeBox_.setTooltip("How the term is matched");
    modeBox_.onChange = [this] { search(true, false); };
    addAndMakeVisible(modeBox_);

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
    // The mode popup shrinks first on a narrow pane.
    int modeW = juce::jlimit(70, 150, row.getWidth() / 5);
    modeBox_.setBounds(row.removeFromLeft(modeW));
    row.removeFromLeft(4);
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

MatchMode FindReplaceBar::mode() const {
    return (MatchMode)juce::jlimit(0, kNumMatchModes - 1,
                                   modeBox_.getSelectedId() - 1);
}

void FindReplaceBar::setMode(MatchMode m) {
    modeBox_.setSelectedId((int)m + 1, juce::dontSendNotification);
}

TextMatcher FindReplaceBar::matcher() const {
    return TextMatcher(toWide(findField_.getText()), mode(),
                       caseButton_.getToggleState());
}

std::vector<TextMatch> FindReplaceBar::matchesIn(String const& text,
                                                 TextMatcher const& m) const {
    return m.findAll(toWide(text));
}

void FindReplaceBar::search(bool forward, bool fromSelectionEnd) {
    auto* ed = activeEditor_();
    if (ed == nullptr) return;
    TextMatcher m = matcher();
    if (m.empty()) { updateMatchLabel(-1, 0); return; }
    if (!m.valid()) {
        matchLabel_.setText("bad regex", juce::dontSendNotification);
        matchLabel_.setTooltip(m.error());
        return;
    }
    matchLabel_.setTooltip({});

    auto& doc = ed->getDocument();
    auto matches = matchesIn(doc.getAllContent(), m);
    if (matches.empty()) { updateMatchLabel(-1, 0); return; }

    // Search relative to the current selection/caret.
    auto sel = ed->getHighlightedRegion();
    int anchor = fromSelectionEnd ? sel.getEnd() : sel.getStart();

    int chosen = -1;
    if (forward) {
        for (int i = 0; i < (int)matches.size(); ++i)
            if (matches[i].start >= anchor) { chosen = i; break; }
        if (chosen < 0) chosen = 0; // wrap to first
    } else {
        for (int i = (int)matches.size() - 1; i >= 0; --i)
            if (matches[i].start < anchor) { chosen = i; break; }
        if (chosen < 0) chosen = (int)matches.size() - 1; // wrap to last
    }

    auto const& hit = matches[(size_t)chosen];
    ed->selectAndReveal(hit.start, hit.end());
    updateMatchLabel(chosen, (int)matches.size());
}

void FindReplaceBar::replaceCurrent() {
    auto* ed = activeEditor_();
    if (ed == nullptr) return;
    TextMatcher m = matcher();
    if (m.empty() || !m.valid()) return;

    auto sel = ed->getHighlightedRegion();
    auto& doc = ed->getDocument();
    String content = doc.getAllContent();
    std::wstring wide = toWide(content);
    auto matches = m.findAll(wide);

    // Replace only when the selection is exactly one of the matches.
    auto it = std::find_if(matches.begin(), matches.end(), [&](TextMatch const& t) {
        return t.start == sel.getStart() && t.end() == sel.getEnd();
    });
    if (it != matches.end()) {
        String replacement(m.expandReplacement(wide, *it,
                                               toWide(replaceField_.getText())).c_str());
        doc.newTransaction();
        doc.replaceSection(sel.getStart(), sel.getEnd(), replacement);
        // Place the caret after the inserted replacement, then advance.
        int newEnd = sel.getStart() + replacement.length();
        ed->setHighlightedRegion({ newEnd, newEnd });
    }
    search(true, true);
}

void FindReplaceBar::replaceAll() {
    auto* ed = activeEditor_();
    if (ed == nullptr) return;
    TextMatcher m = matcher();
    if (m.empty() || !m.valid()) return;

    auto& doc = ed->getDocument();
    std::wstring wide = toWide(doc.getAllContent());
    auto matches = m.findAll(wide);
    if (matches.empty()) { updateMatchLabel(-1, 0); return; }

    std::wstring replacement = toWide(replaceField_.getText());
    doc.newTransaction();
    // Replace from the end so earlier offsets stay valid.
    for (int i = (int)matches.size() - 1; i >= 0; --i) {
        auto const& t = matches[(size_t)i];
        doc.replaceSection(t.start, t.end(),
                           String(m.expandReplacement(wide, t, replacement).c_str()));
    }

    matchLabel_.setText(String((int)matches.size()) + " replaced",
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
