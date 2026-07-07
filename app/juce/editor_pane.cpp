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
//  editor_pane.cpp
//  app (JUCE)
//

#include "editor_pane.hpp"

namespace tzplapp {

using juce::CodeDocument;
using juce::String;

// ---------------------------------------------------------------------------
// TzplCodeEditor::Overlay -- eval flash + error markers, painted above the
// text without intercepting mouse events. The flash fades on a timer.
// ---------------------------------------------------------------------------

class TzplCodeEditor::Overlay : public juce::Component, private juce::Timer {
public:
    explicit Overlay(TzplCodeEditor& editor) : editor_(editor) {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(true);
    }

    void flash(int startLine, int endLine) {
        flashStart_ = startLine;
        flashEnd_ = endLine;
        alpha_ = 0.45f;
        startTimerHz(30);
        repaint();
    }

    void setMarkers(std::map<int, String> markers) {
        markers_ = std::move(markers);
        repaint();
    }

    bool hasContent() const { return alpha_ > 0.0f || !markers_.empty(); }

    void paint(juce::Graphics& g) override {
        if (alpha_ > 0.0f && flashStart_ >= 0) {
            g.setColour(juce::Colours::white.withAlpha(alpha_ * 0.35f));
            for (int line = flashStart_; line <= flashEnd_; ++line)
                g.fillRect(lineRect(line));
        }
        for (auto const& [line, msg] : markers_) {
            g.setColour(juce::Colour(0x60ff3030));
            g.fillRect(lineRect(line));
        }
    }

private:
    juce::Rectangle<int> lineRect(int line) const {
        auto charBounds = editor_.getCharacterBounds(
            CodeDocument::Position(editor_.getDocument(), line, 0));
        return { 0, charBounds.getY(), getWidth(), editor_.getLineHeight() };
    }

    void timerCallback() override {
        alpha_ -= 1.0f / 30.0f * 1.35f; // fade over ~0.33s, like EvalFlash
        if (alpha_ <= 0.0f) {
            alpha_ = 0.0f;
            flashStart_ = flashEnd_ = -1;
            stopTimer();
        }
        repaint();
    }

    TzplCodeEditor& editor_;
    std::map<int, String> markers_;
    int flashStart_ = -1, flashEnd_ = -1;
    float alpha_ = 0.0f;
};

// ---------------------------------------------------------------------------
// TzplCodeEditor
// ---------------------------------------------------------------------------

TzplCodeEditor::TzplCodeEditor(CodeDocument& doc, juce::CodeTokeniser* tokeniser)
    : juce::CodeEditorComponent(doc, tokeniser)
{
    setTabSize(4, /*insertSpaces=*/false);
    setLineNumbersShown(true);
    overlay_ = std::make_unique<Overlay>(*this);
    addAndMakeVisible(*overlay_);
}

TzplCodeEditor::~TzplCodeEditor() = default;

bool TzplCodeEditor::keyPressed(juce::KeyPress const& key) {
    // Undo chunking: a pause in typing, or Enter, starts a new undo
    // transaction so undo steps back a burst at a time, not one char.
    auto now = juce::Time::getMillisecondCounter();
    if (now - lastEditMs_ > 800
        || key == juce::KeyPress(juce::KeyPress::returnKey)) {
        getDocument().newTransaction();
    }
    lastEditMs_ = now;
    // Modified Return (Cmd/Shift) falls through to the command manager's
    // key mappings -- the eval shortcuts.
    return juce::CodeEditorComponent::keyPressed(key);
}

void TzplCodeEditor::resized() {
    juce::CodeEditorComponent::resized();
    if (overlay_) overlay_->setBounds(getLocalBounds());
}

void TzplCodeEditor::editorViewportPositionChanged() {
    juce::CodeEditorComponent::editorViewportPositionChanged();
    if (overlay_ && overlay_->hasContent()) overlay_->repaint();
}

void TzplCodeEditor::triggerFlash(int startLine, int endLine) {
    overlay_->flash(startLine, endLine);
}

void TzplCodeEditor::setErrorMarkers(std::map<int, String> markers) {
    overlay_->setMarkers(std::move(markers));
}

void TzplCodeEditor::clearErrorMarkers() {
    overlay_->setMarkers({});
}

// ---------------------------------------------------------------------------
// EditorPane
// ---------------------------------------------------------------------------

EditorPane::EditorPane() {
    tabsUI_.setOutline(0);
    addAndMakeVisible(tabsUI_);
    findBar_.setVisible(false);
    addChildComponent(findBar_);
    findBar_.onVisibilityChanged = [this] { resized(); };
    newTab("scratch.x");
}

EditorPane::~EditorPane() {
    // Editors are owned by tabs_; detach them from the TabbedComponent
    // before destruction order becomes an issue.
    tabsUI_.clearTabs();
    for (auto& tab : tabs_)
        tab->doc->removeListener(this);
}

void EditorPane::resized() {
    auto r = getLocalBounds();
    if (findBar_.isShown())
        findBar_.setBounds(r.removeFromTop(FindReplaceBar::kBarHeight));
    tabsUI_.setBounds(r);
}

EditorPane::Tab* EditorPane::activeTab() const {
    return tabAt(activeTabIndex());
}

EditorPane::Tab* EditorPane::tabAt(int index) const {
    if (index < 0 || index >= (int)tabs_.size()) return nullptr;
    return tabs_[index].get();
}

int EditorPane::activeTabIndex() const {
    return tabsUI_.getCurrentTabIndex();
}

int EditorPane::indexOfDocument(CodeDocument const* doc) const {
    for (int i = 0; i < (int)tabs_.size(); ++i)
        if (tabs_[i]->doc.get() == doc) return i;
    return -1;
}

void EditorPane::addTabInternal(std::unique_ptr<Tab> tab) {
    tab->doc->addListener(this);
    tab->editor->setFont(juce::FontOptions(
        juce::Font::getDefaultMonospacedFontName(), fontSize_, juce::Font::plain));
    tabsUI_.addTab(tab->name, juce::Colours::transparentBlack,
                   tab->editor.get(), /*deleteComponentWhenNotNeeded=*/false);
    tabs_.push_back(std::move(tab));
    tabsUI_.setCurrentTabIndex((int)tabs_.size() - 1);
    tabs_.back()->editor->grabKeyboardFocus();
}

void EditorPane::newTab(String const& name) {
    auto tab = std::make_unique<Tab>();
    tab->name = name;
    tab->doc = std::make_unique<CodeDocument>();
    tab->doc->setSavePoint();
    tab->editor = std::make_unique<TzplCodeEditor>(*tab->doc, &tokeniser_);
    addTabInternal(std::move(tab));
}

bool EditorPane::openFile(juce::File const& file) {
    for (int i = 0; i < (int)tabs_.size(); ++i) {
        if (tabs_[i]->file == file) {
            tabsUI_.setCurrentTabIndex(i);
            return true;
        }
    }
    if (!file.existsAsFile()) return false;

    auto tab = std::make_unique<Tab>();
    tab->name = file.getFileName();
    tab->file = file;
    tab->doc = std::make_unique<CodeDocument>();
    tab->doc->replaceAllContent(file.loadFileAsString());
    tab->doc->setSavePoint();
    tab->doc->clearUndoHistory();
    tab->editor = std::make_unique<TzplCodeEditor>(*tab->doc, &tokeniser_);
    addTabInternal(std::move(tab));
    return true;
}

void EditorPane::closeTab(int index) {
    if (auto* tab = tabAt(index)) {
        tab->doc->removeListener(this);
        tabsUI_.removeTab(index);
        tabs_.erase(tabs_.begin() + index);
        if (tabs_.empty())
            newTab();
        else if (tabsUI_.getCurrentTabIndex() < 0)
            tabsUI_.setCurrentTabIndex(juce::jmin(index, (int)tabs_.size() - 1));
    }
}

String EditorPane::tabName(int index) const {
    auto* tab = tabAt(index);
    return tab ? tab->name : String();
}

bool EditorPane::tabModified(int index) const {
    auto* tab = tabAt(index);
    return tab && tab->doc->hasChangedSinceSavePoint();
}

bool EditorPane::tabHasFilePath(int index) const {
    auto* tab = tabAt(index);
    return tab && tab->file != juce::File();
}

bool EditorPane::saveTab(int index) {
    auto* tab = tabAt(index);
    if (!tab || tab->file == juce::File()) return false;
    if (!tab->file.replaceWithText(tab->doc->getAllContent())) return false;
    tab->doc->setSavePoint();
    refreshTabTitle(index);
    return true;
}

bool EditorPane::saveTabAs(int index, juce::File const& f) {
    auto* tab = tabAt(index);
    if (!tab) return false;
    if (!f.replaceWithText(tab->doc->getAllContent())) return false;
    tab->file = f;
    tab->name = f.getFileName();
    tab->doc->setSavePoint();
    refreshTabTitle(index);
    return true;
}

bool EditorPane::saveCopy(juce::File const& f) const {
    auto* tab = activeTab();
    return tab && f.replaceWithText(tab->doc->getAllContent());
}

int EditorPane::saveAll() {
    int saved = 0;
    for (int i = 0; i < (int)tabs_.size(); ++i)
        if (tabModified(i) && tabHasFilePath(i) && saveTab(i)) ++saved;
    return saved;
}

bool EditorPane::hasUnsavedChanges() const {
    for (int i = 0; i < (int)tabs_.size(); ++i)
        if (tabModified(i)) return true;
    return false;
}

std::vector<String> EditorPane::unsavedFileNames() const {
    std::vector<String> names;
    for (int i = 0; i < (int)tabs_.size(); ++i)
        if (tabModified(i)) names.push_back(tabName(i));
    return names;
}

void EditorPane::refreshTabTitle(int index) {
    if (auto* tab = tabAt(index))
        tabsUI_.setTabName(index, tab->name + (tabModified(index) ? "*" : ""));
}

void EditorPane::codeDocumentTextInserted(String const&, int) {
    for (int i = 0; i < (int)tabs_.size(); ++i) refreshTabTitle(i);
}

void EditorPane::codeDocumentTextDeleted(int, int) {
    for (int i = 0; i < (int)tabs_.size(); ++i) refreshTabTitle(i);
}

// ---------------------------------------------------------------------------
// Text access
// ---------------------------------------------------------------------------

String EditorPane::getSelectedText() const {
    auto* tab = activeTab();
    if (!tab) return {};
    auto range = tab->editor->getHighlightedRegion();
    return tab->doc->getTextBetween(
        CodeDocument::Position(*tab->doc, range.getStart()),
        CodeDocument::Position(*tab->doc, range.getEnd()));
}

String EditorPane::getCurrentLineText() const {
    auto* tab = activeTab();
    if (!tab) return {};
    String line = tab->doc->getLine(tab->editor->getCaretPos().getLineNumber());
    return line.trimCharactersAtEnd("\r\n");
}

String EditorPane::getAllText() const {
    auto* tab = activeTab();
    return tab ? tab->doc->getAllContent() : String();
}

int EditorPane::cursorLine() const {
    auto* tab = activeTab();
    return tab ? tab->editor->getCaretPos().getLineNumber() : 0;
}

String EditorPane::getCurrentBlockText(int& outStartLine, int& outEndLine) const {
    auto* tab = activeTab();
    if (!tab) { outStartLine = outEndLine = 0; return {}; }

    auto& doc = *tab->doc;
    int numLines = doc.getNumLines();
    int curLine = juce::jmin(tab->editor->getCaretPos().getLineNumber(),
                             numLines - 1);

    auto isEmpty = [&doc](int line) {
        return doc.getLine(line).trim().isEmpty();
    };

    outStartLine = curLine;
    while (outStartLine > 0 && !isEmpty(outStartLine - 1)) --outStartLine;
    outEndLine = curLine;
    while (outEndLine < numLines - 1 && !isEmpty(outEndLine + 1)) ++outEndLine;

    String result;
    for (int i = outStartLine; i <= outEndLine; ++i) {
        if (i > outStartLine) result << '\n';
        result << doc.getLine(i).trimCharactersAtEnd("\r\n");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Edit operations
// ---------------------------------------------------------------------------

void EditorPane::undo()  { if (auto* t = activeTab()) t->editor->undo(); }
void EditorPane::redo()  { if (auto* t = activeTab()) t->editor->redo(); }
void EditorPane::cutToClipboard()  { if (auto* t = activeTab()) t->editor->cutToClipboard(); }
void EditorPane::copyToClipboard() { if (auto* t = activeTab()) t->editor->copyToClipboard(); }
void EditorPane::pasteFromClipboard() { if (auto* t = activeTab()) t->editor->pasteFromClipboard(); }
void EditorPane::selectAll() { if (auto* t = activeTab()) t->editor->selectAll(); }

bool EditorPane::selectedLineRange(int& first, int& last) const {
    auto* tab = activeTab();
    if (!tab) return false;
    auto range = tab->editor->getHighlightedRegion();
    first = CodeDocument::Position(*tab->doc, range.getStart()).getLineNumber();
    last = CodeDocument::Position(*tab->doc, range.getEnd()).getLineNumber();
    // A selection ending at column 0 doesn't include that line.
    if (last > first
        && CodeDocument::Position(*tab->doc, range.getEnd()).getIndexInLine() == 0)
        --last;
    return true;
}

void EditorPane::toggleComment() {
    int first, last;
    if (!selectedLineRange(first, last)) return;
    auto* tab = activeTab();
    auto& doc = *tab->doc;
    doc.newTransaction();

    // Comment unless every non-empty line already starts with "--".
    bool allCommented = true;
    for (int i = first; i <= last; ++i) {
        String line = doc.getLine(i);
        if (line.trim().isEmpty()) continue;
        if (!line.trimStart().startsWith("--")) { allCommented = false; break; }
    }

    for (int i = first; i <= last; ++i) {
        String line = doc.getLine(i);
        if (line.trim().isEmpty()) continue;
        if (allCommented) {
            int col = (int)(line.length() - line.trimStart().length());
            int len = line.substring(col).startsWith("-- ") ? 3 : 2;
            CodeDocument::Position from(doc, i, col), to(doc, i, col + len);
            doc.deleteSection(from, to);
        } else {
            CodeDocument::Position at(doc, i, 0);
            doc.insertText(at, "-- ");
        }
    }
}

void EditorPane::indentSelection() {
    int first, last;
    if (!selectedLineRange(first, last)) return;
    auto& doc = *activeTab()->doc;
    doc.newTransaction();
    for (int i = first; i <= last; ++i) {
        if (doc.getLine(i).trimCharactersAtEnd("\r\n").isEmpty()) continue;
        doc.insertText(CodeDocument::Position(doc, i, 0), "\t");
    }
}

void EditorPane::outdentSelection() {
    int first, last;
    if (!selectedLineRange(first, last)) return;
    auto& doc = *activeTab()->doc;
    doc.newTransaction();
    for (int i = first; i <= last; ++i) {
        String line = doc.getLine(i);
        int remove = 0;
        if (line.startsWith("\t")) remove = 1;
        else {
            while (remove < 4 && remove < line.length() && line[remove] == ' ')
                ++remove;
        }
        if (remove > 0)
            doc.deleteSection(CodeDocument::Position(doc, i, 0),
                              CodeDocument::Position(doc, i, remove));
    }
}

// ---------------------------------------------------------------------------
// Eval feedback + appearance
// ---------------------------------------------------------------------------

void EditorPane::triggerFlash(int startLine, int endLine) {
    if (auto* t = activeTab()) t->editor->triggerFlash(startLine, endLine);
}

void EditorPane::setErrorMarkersFromEval(
    std::vector<std::pair<int, String>> const& lineMessages) {
    if (auto* t = activeTab()) {
        std::map<int, String> markers;
        for (auto const& [line, msg] : lineMessages) markers[line] = msg;
        t->editor->setErrorMarkers(std::move(markers));
    }
}

void EditorPane::clearErrorMarkers() {
    if (auto* t = activeTab()) t->editor->clearErrorMarkers();
}

TzplCodeEditor* EditorPane::activeEditor() const {
    auto* tab = activeTab();
    return tab ? tab->editor.get() : nullptr;
}

void EditorPane::setFontSize(float px) {
    fontSize_ = px;
    for (auto& tab : tabs_)
        tab->editor->setFont(juce::FontOptions(
            juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain));
}

// ---------------------------------------------------------------------------
// Find/Replace
// ---------------------------------------------------------------------------

void EditorPane::showFind(String const& seed) { findBar_.show(seed); }
void EditorPane::findNext()     { findBar_.findNext(); }
void EditorPane::findPrevious() { findBar_.findPrevious(); }
void EditorPane::seedReplace(String const& text) {
    findBar_.seedReplace(text);
    findBar_.show();
}

}
