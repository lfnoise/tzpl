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
    output_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                      fontSize_, juce::Font::plain));

    placeholder_.setJustificationType(juce::Justification::centred);
    placeholder_.setColour(juce::Label::textColourId, juce::Colour(0xff808080));
}

void CellComponent::buildForKind(doc::CellKind kind) {
    kind_ = kind;
    kindLabel_.setText(kindName(kind), juce::dontSendNotification);
    runButton_.setVisible(kind == doc::CellKind::Code);

    bool wantsEditor = (kind == doc::CellKind::Prose || kind == doc::CellKind::Code);
    if (wantsEditor && !editor_) {
        codeDoc_ = std::make_unique<juce::CodeDocument>();
        editor_ = std::make_unique<TzplCodeEditor>(*codeDoc_, &tokeniser_);
        editor_->setFont(juce::FontOptions(
            juce::Font::getDefaultMonospacedFontName(), fontSize_, juce::Font::plain));
        // Prose cells hide the gutter (the ImGuiColorTextEdit patch's role).
        editor_->setLineNumbersShown(kind == doc::CellKind::Code);
        codeDoc_->addListener(this); // text change -> onTextChanged
        addAndMakeVisible(*editor_);
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

    if (kind_ == doc::CellKind::Panel) {
        kindLabel_.setText(String("panel: ") + cell.name,
                           juce::dontSendNotification);
        // (Re)build the live canvas once the panel name is known.
        if (ui_ && dispatcher_
            && (!panelCanvas_ || panelName_ != String(cell.name))) {
            panelName_ = String(cell.name);
            panelCanvas_ = std::make_unique<PanelCanvas>(*ui_, cell.name,
                                                         *dispatcher_);
            panelCanvas_->onArrangeCommit = [this] {
                if (onArrangeCommit) onArrangeCommit();
            };
            panelCanvas_->setArrange(arrangeOn_);
            arrangeButton_.setVisible(true);
            addAndMakeVisible(*panelCanvas_);
            resized();
        }
    }

    if (kind_ == doc::CellKind::Presets && presetsView_)
        presetsView_->setPresets(cell.presets);

    // Only replace editor text when it actually differs, to avoid stomping
    // in-progress typing / resetting the caret.
    if (editor_ && codeDoc_->getAllContent() != String(cell.text))
        codeDoc_->replaceAllContent(cell.text);
}

String CellComponent::editorText() const {
    return codeDoc_ ? codeDoc_->getAllContent() : String();
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
    if (auto* p = getParentComponent()) p->resized(); // grow to fit output
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
    if (editor_)
        editor_->setFont(juce::FontOptions(
            juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain));
    output_.applyFontToAllText(juce::FontOptions(
        juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain));
}

int CellComponent::preferredHeight(int width) const {
    int h = kHeaderH + kPad;
    if (collapsed_) return h + kPad;

    // Size the editor from its ACTUAL line height, not a font-based estimate
    // (which overshoots CodeEditorComponent's line height and leaves a gap).
    if (editor_) {
        int lh = editor_->getLineHeight();
        if (lh <= 0) lh = juce::roundToInt(fontSize_ * 1.4f);
        int lines = juce::jmax(1, codeDoc_->getNumLines());
        h += juce::jlimit(lh * 2, lh * 40, lines * lh + kPad);
    }
    if (kind_ == doc::CellKind::Code && !outputLines_.empty()) {
        int outLineH = juce::roundToInt(fontSize_ * 1.4f);
        int outLines = juce::jmin(12, (int)outputLines_.size());
        h += kPad + outLineH * (outLines + 1);
    }
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
    if (collapsed_) return;

    if (kind_ == doc::CellKind::Code && !outputLines_.empty()) {
        int lineH = juce::roundToInt(fontSize_ * 1.4f);
        int outH = juce::jmin(12, (int)outputLines_.size() + 1) * lineH + kPad;
        output_.setBounds(r.removeFromBottom(outH));
        r.removeFromBottom(kPad);
    }
    if (editor_)
        editor_->setBounds(r);
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
    deleteButton_.setBounds(header.removeFromRight(24));
    header.removeFromRight(2);
    downButton_.setBounds(header.removeFromRight(24));
    upButton_.setBounds(header.removeFromRight(24));
    header.removeFromRight(4);
    if (runButton_.isVisible()) {
        runButton_.setBounds(header.removeFromRight(48));
        header.removeFromRight(4);
    }
    if (arrangeButton_.isVisible()) {
        arrangeButton_.setBounds(header.removeFromRight(64));
        header.removeFromRight(4);
    }
    kindLabel_.setBounds(header);
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
