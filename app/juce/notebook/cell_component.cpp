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
//  cell_component.cpp
//  app (JUCE)
//

#include "cell_component.hpp"
#include "../tzpl_fonts.hpp"

#include <cmath>

namespace tzplapp {

using juce::String;

namespace {
char const* kindName(doc::CellKind k) {
    switch (k) {
        case doc::CellKind::Prose:   return "prose";
        case doc::CellKind::Code:    return "code";
        case doc::CellKind::Panel:   return "panel";
        case doc::CellKind::Presets: return "presets";
    }
    return "?";
}
}

CellComponent::~CellComponent() {
    if (codeDoc_) codeDoc_->removeListener(this);
}

CellComponent::CellComponent(doc::CellId id, TzplTokeniser& tokeniser,
                             float fontSize, bridge::UIState* ui,
                             ControlsDispatcher* dispatcher)
    : id_(id), tokeniser_(tokeniser), fontSize_(fontSize),
      ui_(ui), dispatcher_(dispatcher)
{
    kindLabel_.setFont(juce::FontOptions(11.0f));
    kindLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff909090));
    addAndMakeVisible(kindLabel_);

    runButton_.onClick = [this] { if (onRun) onRun(); };
    deleteButton_.onClick = [this] { if (onDelete) onDelete(); };
    upButton_.onClick = [this] { if (onMove) onMove(-1); };
    downButton_.onClick = [this] { if (onMove) onMove(+1); };
    collapseButton_.onClick = [this] {
        collapsed_ = !collapsed_;
        if (onCollapse) onCollapse(collapsed_);
        repaint();
    };

    for (auto* b : { &runButton_, &upButton_, &downButton_, &deleteButton_ }) {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0x30ffffff));
        addAndMakeVisible(b);
    }

    runOnLoadToggle_.onClick = [this] {
        if (onRunOnLoad) onRunOnLoad(runOnLoadToggle_.getToggleState());
    };
    addChildComponent(runOnLoadToggle_);

    // Output resize grip: dragging sets a sticky per-cell height (session
    // only); until dragged the pane auto-fits its content.
    outputGrip_.onDragStart = [this] { dragStartOutputH_ = outputPaneHeight(); };
    outputGrip_.onDrag = [this](int dy) {
        int lineH = juce::roundToInt(fontSize_ * 1.4f);
        outputHeight_ = juce::jmax(lineH + kPad, dragStartOutputH_ + dy);
        if (onLayoutChanged) onLayoutChanged();
    };
    addChildComponent(outputGrip_);

    arrangeButton_.setColour(juce::TextButton::buttonColourId,
                             juce::Colour(0x30ffffff));
    arrangeButton_.onClick = [this] {
        arrangeOn_ = !arrangeOn_;
        arrangeButton_.setColour(juce::TextButton::buttonColourId,
                                 arrangeOn_ ? juce::Colour(0xffe0a020)
                                            : juce::Colour(0x30ffffff));
        if (panelCanvas_) panelCanvas_->setArrange(arrangeOn_);
    };
    addChildComponent(arrangeButton_);
    // The disclosure control is a bare click target; paint() draws the
    // triangle over it (a glyph gets ellipsized in the narrow button).
    collapseButton_.setColour(juce::TextButton::buttonColourId,
                              juce::Colours::transparentBlack);
    collapseButton_.setColour(juce::TextButton::buttonOnColourId,
                              juce::Colours::transparentBlack);
    addAndMakeVisible(collapseButton_);

    output_.setMultiLine(true);
    output_.setReadOnly(true);
    output_.setCaretVisible(false);
    output_.setScrollbarsShown(true);
    output_.setFont(monoFont(fontSize_));

    placeholder_.setJustificationType(juce::Justification::centred);
    placeholder_.setColour(juce::Label::textColourId, juce::Colour(0xff808080));

    // Editable name: Code cells store an optional label, Panel cells the
    // panel name that code targets. Commit on Return / focus-loss.
    nameField_.setFont(juce::FontOptions(12.0f));
    nameField_.setTextToShowWhenEmpty("(name)", juce::Colour(0xff707070));
    nameField_.onReturnKey = [this] { commitCellName(); };
    nameField_.onFocusLost = [this] { commitCellName(); };
    addChildComponent(nameField_);
}

void CellComponent::commitCellName() {
    if (onRenameCell) onRenameCell(nameField_.getText());
}

void CellComponent::buildForKind(doc::CellKind kind) {
    kind_ = kind;
    kindLabel_.setText(kindName(kind), juce::dontSendNotification);
    runButton_.setVisible(kind == doc::CellKind::Code);
    runOnLoadToggle_.setVisible(kind == doc::CellKind::Code);
    // Code and Panel cells expose an editable name; Prose/Presets don't.
    nameField_.setVisible(kind == doc::CellKind::Code
                          || kind == doc::CellKind::Panel);

    // Code cells get the syntax-highlighting TzplCodeEditor; Prose cells a
    // word-wrapping TextEditor. Tear down the other kind's editor so a
    // kind change (view rebuild) swaps cleanly.
    if (kind == doc::CellKind::Code) {
        proseEd_.reset();
        proseHeight_ = 0;
        if (!editor_) {
            codeDoc_ = std::make_unique<juce::CodeDocument>();
            editor_ = std::make_unique<TzplCodeEditor>(*codeDoc_, &tokeniser_);
            editor_->setFont(monoFont(fontSize_));
            // The cell grows with its content; wheel scrolls the notebook.
            editor_->setFitToContent(true);
            codeDoc_->addListener(this); // text change -> onTextChanged
            addAndMakeVisible(*editor_);
        }
    } else if (kind == doc::CellKind::Prose) {
        if (codeDoc_) codeDoc_->removeListener(this);
        editor_.reset();
        codeDoc_.reset();
        if (!proseEd_) {
            proseEd_ = std::make_unique<juce::TextEditor>();
            proseEd_->setMultiLine(true, /*wordWrap*/ true);
            proseEd_->setReturnKeyStartsNewLine(true);
            proseEd_->setTabKeyUsedAsCharacter(true);
            proseEd_->setScrollbarsShown(false);
            proseEd_->setFont(monoFont(fontSize_));
            proseEd_->setColour(juce::TextEditor::outlineColourId,
                                juce::Colours::transparentBlack);
            proseEd_->setColour(juce::TextEditor::focusedOutlineColourId,
                                juce::Colours::transparentBlack);
            proseEd_->onTextChange = [this] {
                proseHeight_ = 0;   // wrapped height is stale
                if (onTextChanged) onTextChanged();
            };
            addAndMakeVisible(*proseEd_);
        }
    } else {
        if (codeDoc_) codeDoc_->removeListener(this);
        editor_.reset();
        codeDoc_.reset();
        proseEd_.reset();
        proseHeight_ = 0;
    }
    if (kind == doc::CellKind::Code) addAndMakeVisible(output_);
    else                            output_.setVisible(false);

    if (kind == doc::CellKind::Presets) {
        // The preset bank. Slot data is loaded in syncFromModel; the view
        // forwards store/recall/overwrite/rename/delete to the NotebookView.
        presetsView_ = std::make_unique<PresetsView>();
        presetsView_->onStore = [this] { if (onPresetStore) onPresetStore(); };
        presetsView_->onRecall = [this](int i) {
            if (onPresetRecall) onPresetRecall(i);
        };
        presetsView_->onOverwrite = [this](int i) {
            if (onPresetOverwrite) onPresetOverwrite(i);
        };
        presetsView_->onRename = [this](int i, String n) {
            if (onPresetRename) onPresetRename(i, n);
        };
        presetsView_->onDelete = [this](int i) {
            if (onPresetDelete) onPresetDelete(i);
        };
        addAndMakeVisible(*presetsView_);
        placeholder_.setVisible(false);
    } else if (kind == doc::CellKind::Panel && ui_ && dispatcher_) {
        // Panel cells render the panel's live widgets inline. The exact
        // panel name is set in syncFromModel (cell.name); rebuild here with
        // an empty name and let reconcile pick up widgets after the name is
        // known.
        panelCanvas_.reset();
        placeholder_.setVisible(false);
    } else if (kind == doc::CellKind::Panel) {
        placeholder_.setText(String("PANEL cell -- no live widgets"),
                             juce::dontSendNotification);
        addAndMakeVisible(placeholder_);
    } else {
        placeholder_.setVisible(false);
    }
}

void CellComponent::syncFromModel(doc::Cell const& cell) {
    // First sync (label still empty) or a kind change rebuilds the body.
    if (kindLabel_.getText().isEmpty() || kind_ != cell.kind)
        buildForKind(cell.kind);

    collapsed_ = cell.collapsed;
    runOnLoadToggle_.setToggleState(cell.runOnLoad,
                                    juce::dontSendNotification);

    // Seed the editable name (unless the user is mid-edit in it).
    if (nameField_.isVisible() && !nameField_.hasKeyboardFocus(true)
        && nameField_.getText() != String(cell.name))
        nameField_.setText(cell.name, juce::dontSendNotification);

    if (kind_ == doc::CellKind::Panel) {
        // (Re)build the live canvas once the panel name is known.
        if (ui_ && dispatcher_
            && (!panelCanvas_ || panelName_ != String(cell.name))) {
            panelName_ = String(cell.name);
            panelCanvas_ = std::make_unique<PanelCanvas>(*ui_, cell.name,
                                                         *dispatcher_);
            panelCanvas_->onArrangeCommit = [this] {
                if (onArrangeCommit) onArrangeCommit();
            };
            // Code created/removed widgets: the cell's preferred height
            // changed, so the notebook must relayout -- the canvas grows
            // to fit the new controls.
            panelCanvas_->onContentChanged = [this] {
                if (onLayoutChanged) onLayoutChanged();
            };
            panelCanvas_->setArrange(arrangeOn_);
            arrangeButton_.setVisible(true);
            addAndMakeVisible(*panelCanvas_);
            resized();
        }
    }

    if (kind_ == doc::CellKind::Presets && presetsView_)
        presetsView_->setPresets(cell.presets);

    // Re-seed from the model only when the text actually differs, so an
    // unchanged cell keeps its caret / undo history. This DOES overwrite
    // typing the store has not seen yet -- callers sync first (see
    // NotebookView::rebuildCells).
    if (editor_ && codeDoc_->getAllContent() != String(cell.text))
        codeDoc_->replaceAllContent(cell.text);
    else if (proseEd_ && proseEd_->getText() != String(cell.text)) {
        proseEd_->setText(cell.text, false);
        proseHeight_ = 0;   // remeasure on next layout
    }
}

String CellComponent::editorText() const {
    if (codeDoc_) return codeDoc_->getAllContent();
    if (proseEd_) return proseEd_->getText();
    return {};
}

void CellComponent::setEditorText(String const& text) {
    if (codeDoc_) codeDoc_->replaceAllContent(text);
    else if (proseEd_) proseEd_->setText(text);   // fires onTextChange
}

void CellComponent::setSelected(bool sel) {
    if (selected_ == sel) return;
    selected_ = sel;
    repaint();
}

void CellComponent::clearOutput() {
    outputLines_.clear();
    output_.clear();
    if (editor_) editor_->clearErrorMarkers();
    if (onLayoutChanged) onLayoutChanged();
}

void CellComponent::addOutputLine(OutputLine const& line) {
    outputLines_.push_back(line);
    juce::Colour c;
    switch (line.kind) {
        case LineKind::Result: c = juce::Colour(0xff7fd07f); break;
        case LineKind::Error:  c = juce::Colour(0xffff6b6b); break;
        case LineKind::Info:   c = juce::Colour(0xff9fb8d0); break;
        default:               c = findColour(juce::TextEditor::textColourId);
    }
    output_.moveCaretToEnd();
    output_.setColour(juce::TextEditor::textColourId, c);
    output_.insertTextAtCaret(String(line.text) + "\n");
    // Grow the cell to fit: the view recomputes preferredHeight and sets
    // new bounds (the parent component's own resized() is a no-op).
    if (onLayoutChanged) onLayoutChanged();
}

void CellComponent::setErrorMarkers(
    std::vector<std::pair<int, String>> const& m) {
    if (!editor_) return;
    std::map<int, String> markers;
    for (auto const& [line, msg] : m) markers[line] = msg;
    editor_->setErrorMarkers(std::move(markers));
}

void CellComponent::setFontSize(float px) {
    fontSize_ = px;
    if (editor_) editor_->setFont(monoFont(px));
    if (proseEd_) {
        proseEd_->applyFontToAllText(monoFont(px));
        proseHeight_ = 0;   // remeasure on next layout
    }
    output_.applyFontToAllText(monoFont(px));
}

// Height of the prose editor's wrapped text at `edWidth`, measured with an
// independent TextLayout over the same text/font/wrap-width. Deliberately
// NOT read back from the TextEditor: deriving the cell height from the
// editor's own layout while the editor's size derives from the cell height
// is a feedback loop (it ratcheted one line per layout pass). Cached per
// (text state, width); onTextChange and setFontSize reset the cache.
int CellComponent::proseTextHeight(int edWidth) const {
    if (proseHeight_ > 0 && proseHeightWidth_ == edWidth)
        return proseHeight_;
    // The editor wraps at: width - border(l+r) - leftIndent - rightEdgeSpace.
    // Defaults: border 1+3, leftIndent 4, rightEdgeSpace 2. Measure a touch
    // narrower so rounding can only make us taller, never shorter (shorter
    // would scroll inside the editor).
    float wrapW = (float)juce::jmax(40, edWidth - 12);
    juce::String text = proseEd_->getText();
    if (text.isEmpty()) text = " ";
    if (text.endsWithChar('\n')) text += " ";   // count the trailing line
    juce::AttributedString as;
    as.append(text, monoFont(fontSize_));
    juce::TextLayout tl;
    tl.createLayout(as, wrapW);
    int lineH = juce::roundToInt(fontSize_ * 1.4f);
    int h = (int)std::ceil(tl.getHeight()) + 2 * proseEd_->getTopIndent();
    proseHeight_ = juce::jmax(lineH * 2, h + kPad);
    proseHeightWidth_ = edWidth;
    return proseHeight_;
}

int CellComponent::outputPaneHeight() const {
    if (outputHeight_ > 0) return outputHeight_;   // user-dragged height
    int lineH = juce::roundToInt(fontSize_ * 1.4f);
    return juce::jmin(12, (int)outputLines_.size() + 1) * lineH + kPad;
}

int CellComponent::preferredHeight(int width) const {
    int h = kHeaderH + kPad;
    if (collapsed_) return h + kPad;

    // Size the editor from its ACTUAL line height, not a font-based estimate
    // (which overshoots CodeEditorComponent's line height and leaves a gap).
    // The editor always fits its full content -- the notebook viewport is
    // the scrolling surface -- including the horizontal-scrollbar strip at
    // the bottom (without it the last line sits under the scrollbar and the
    // editor becomes internally scrollable).
    if (editor_) {
        int lh = editor_->getLineHeight();
        if (lh <= 0) lh = juce::roundToInt(fontSize_ * 1.4f);
        int lines = juce::jmax(1, codeDoc_->getNumLines());
        h += juce::jmax(lh * 2,
                        lines * lh + editor_->getScrollbarThickness() + kPad);
    } else if (proseEd_) {
        h += proseTextHeight(width - 2 * kPad);
    }
    if (kind_ == doc::CellKind::Code && !outputLines_.empty())
        h += kPad + outputPaneHeight() + kGripH;
    if (kind_ == doc::CellKind::Panel && panelCanvas_)
        h += panelCanvas_->preferredHeight(width);
    else if (kind_ == doc::CellKind::Presets && presetsView_)
        h += presetsView_->preferredHeight(width - 2 * kPad);
    else if (kind_ == doc::CellKind::Panel)
        h += 80;
    return h + kPad;
}

void CellComponent::resized() {
    auto r = getLocalBounds().reduced(kPad, kPad / 2);
    layOutHeader(r);

    // Collapsed: hide the body outright -- the children keep their old
    // bounds, so a sliver of the editor's top rows would otherwise stay
    // visible under the header.
    bool body = !collapsed_;
    if (editor_) editor_->setVisible(body);
    if (proseEd_) proseEd_->setVisible(body);
    output_.setVisible(body && kind_ == doc::CellKind::Code);
    if (panelCanvas_) panelCanvas_->setVisible(body);
    if (presetsView_) presetsView_->setVisible(body);
    placeholder_.setVisible(body && kind_ == doc::CellKind::Panel
                            && !(ui_ && dispatcher_));
    if (collapsed_) { outputGrip_.setVisible(false); return; }

    bool hasOutput = kind_ == doc::CellKind::Code && !outputLines_.empty();
    outputGrip_.setVisible(hasOutput);
    if (hasOutput) {
        outputGrip_.setBounds(r.removeFromBottom(kGripH));
        output_.setBounds(r.removeFromBottom(outputPaneHeight()));
        r.removeFromBottom(kPad);
    }
    if (editor_)
        editor_->setBounds(r);
    else if (proseEd_)
        proseEd_->setBounds(r);
    else if (panelCanvas_)
        panelCanvas_->setBounds(r);
    else if (presetsView_)
        presetsView_->setBounds(r);
    else if (placeholder_.isVisible())
        placeholder_.setBounds(r);
}

void CellComponent::layOutHeader(juce::Rectangle<int>& r) {
    auto header = r.removeFromTop(kHeaderH);
    r.removeFromTop(kPad / 2);
    // Disclosure triangle on the far left.
    collapseButton_.setBounds(header.removeFromLeft(20));
    header.removeFromLeft(2);
    // Right cluster: delete / down / up (+ arrange for panels).
    deleteButton_.setBounds(header.removeFromRight(24));
    header.removeFromRight(2);
    downButton_.setBounds(header.removeFromRight(24));
    upButton_.setBounds(header.removeFromRight(24));
    header.removeFromRight(4);
    if (arrangeButton_.isVisible()) {
        arrangeButton_.setBounds(header.removeFromRight(64));
        header.removeFromRight(4);
    }
    // Left cluster: kind label, then the editable name, then Run to its right.
    kindLabel_.setBounds(header.removeFromLeft(46));
    header.removeFromLeft(2);
    if (nameField_.isVisible()) {
        int reserve = runButton_.isVisible() ? 52 : 0;
        int nw = juce::jlimit(60, 200, header.getWidth() - reserve);
        nameField_.setBounds(header.removeFromLeft(nw).withSizeKeepingCentre(
            nw, kHeaderH - 2));
        header.removeFromLeft(4);
    }
    if (runButton_.isVisible()) {
        runButton_.setBounds(header.removeFromLeft(48));
        header.removeFromLeft(6);
    }
    if (runOnLoadToggle_.isVisible())
        runOnLoadToggle_.setBounds(
            header.removeFromLeft(juce::jmin(110, header.getWidth())));
}

void CellComponent::paintDisclosure(juce::Graphics& g) const {
    auto b = collapseButton_.getBounds().toFloat();
    float cx = b.getCentreX(), cy = b.getCentreY(), s = 5.0f;
    juce::Path tri;
    if (collapsed_)  // right-pointing (closed)
        tri.addTriangle(cx - s * 0.7f, cy - s, cx - s * 0.7f, cy + s,
                        cx + s * 0.8f, cy);
    else             // down-pointing (open)
        tri.addTriangle(cx - s, cy - s * 0.7f, cx + s, cy - s * 0.7f,
                        cx, cy + s * 0.8f);
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.fillPath(tri);
}

void CellComponent::paint(juce::Graphics& g) {
    auto bg = findColour(juce::ResizableWindow::backgroundColourId);
    g.fillAll(bg.brighter(0.03f));
    // Selected-cell accent bar on the left edge.
    if (selected_) {
        g.setColour(juce::Colour(0xff4a90d0));
        g.fillRect(0, 0, 3, getHeight());
    }
    g.setColour(bg.darker(0.3f));
    g.drawLine(0.0f, (float)getHeight() - 0.5f, (float)getWidth(),
               (float)getHeight() - 0.5f);
    paintDisclosure(g);
}

}
