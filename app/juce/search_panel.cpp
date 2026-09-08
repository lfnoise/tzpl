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
//  search_panel.cpp
//  app (JUCE)
//

#include "search_panel.hpp"
#include "tzpl_fonts.hpp"

namespace tzplapp {

using juce::String;

// ---------------------------------------------------------------------------
// Tree items
// ---------------------------------------------------------------------------

class SearchPanel::HitItem : public juce::TreeViewItem {
public:
    HitItem(SearchPanel& owner, int fileIndex, int hitIndex)
        : owner_(owner), fileIndex_(fileIndex), hitIndex_(hitIndex) {}

    bool mightContainSubItems() override { return false; }
    int getItemHeight() const override { return owner_.rowHeight(); }
    String getUniqueName() const override {
        return String(fileIndex_) + ":" + String(hitIndex_);
    }

    void paintItem(juce::Graphics& g, int width, int height) override {
        auto const& hit = owner_.results_[(size_t)fileIndex_].hits[(size_t)hitIndex_];
        auto& lf = owner_.getLookAndFeel();
        auto text = lf.findColour(juce::TextEditor::textColourId);
        if (isSelected()) {
            g.setColour(lf.findColour(juce::TextEditor::highlightColourId));
            g.fillRect(0, 0, width, height);
        }
        auto const& font = owner_.rowFont(false);
        g.setFont(font);
        int x = 2;
        // Line number, dimmed, right-aligned in a fixed column.
        String num = String(hit.line + 1);
        int numW = juce::GlyphArrangement::getStringWidthInt(font, "00000");
        g.setColour(text.withMultipliedAlpha(0.55f));
        g.drawText(num, x, 0, numW, height, juce::Justification::centredRight, false);
        x += numW + 8;
        if (hit.tag.isNotEmpty()) {
            g.setColour(text.withMultipliedAlpha(0.7f));
            g.setFont(owner_.rowFont(true));
            int tagW = juce::GlyphArrangement::getStringWidthInt(owner_.rowFont(true), hit.tag);
            g.drawText(hit.tag, x, 0, tagW, height, juce::Justification::centredLeft, false);
            x += tagW + 6;
            g.setFont(font);
        }
        // The matched span gets a highlight band behind it.
        int hs = juce::jlimit(0, hit.lineText.length(), hit.hlStart);
        int he = juce::jlimit(hs, hit.lineText.length(), hit.hlStart + hit.hlLength);
        if (he > hs) {
            int before = juce::GlyphArrangement::getStringWidthInt(
                font, hit.lineText.substring(0, hs));
            int span = juce::GlyphArrangement::getStringWidthInt(
                font, hit.lineText.substring(hs, he));
            g.setColour(lf.findColour(juce::TextEditor::highlightColourId)
                            .withMultipliedAlpha(isSelected() ? 0.9f : 0.6f));
            g.fillRoundedRectangle((float)(x + before), 2.0f, (float)span,
                                   (float)(height - 4), 3.0f);
        }
        g.setColour(text);
        g.drawText(hit.lineText, x, 0, width - x - 2, height,
                   juce::Justification::centredLeft, false);
    }

    void itemClicked(juce::MouseEvent const&) override {
        owner_.openHit(fileIndex_, hitIndex_);
    }
    void itemDoubleClicked(juce::MouseEvent const&) override {
        owner_.openHit(fileIndex_, hitIndex_);
    }

    int fileIndex() const { return fileIndex_; }
    int hitIndex() const { return hitIndex_; }

private:
    SearchPanel& owner_;
    int fileIndex_, hitIndex_;
};

class SearchPanel::FileItem : public juce::TreeViewItem {
public:
    FileItem(SearchPanel& owner, int fileIndex)
        : owner_(owner), fileIndex_(fileIndex) {
        auto const& r = owner_.results_[(size_t)fileIndex_];
        for (int i = 0; i < (int)r.hits.size(); ++i)
            addSubItem(new HitItem(owner_, fileIndex_, i));
    }

    bool mightContainSubItems() override { return true; }
    int getItemHeight() const override { return owner_.rowHeight(); }
    String getUniqueName() const override {
        return owner_.results_[(size_t)fileIndex_].title + "#" + String(fileIndex_);
    }

    void paintItem(juce::Graphics& g, int width, int height) override {
        auto const& r = owner_.results_[(size_t)fileIndex_];
        auto& lf = owner_.getLookAndFeel();
        auto text = lf.findColour(juce::TextEditor::textColourId);
        if (isSelected()) {
            g.setColour(lf.findColour(juce::TextEditor::highlightColourId));
            g.fillRect(0, 0, width, height);
        }
        g.setFont(owner_.rowFont(true));
        g.setColour(text);
        int titleW = juce::GlyphArrangement::getStringWidthInt(owner_.rowFont(true), r.title);
        g.drawText(r.title, 2, 0, width - 4, height, juce::Justification::centredLeft, true);
        String rest = " (" + String((int)r.hits.size()) + ")";
        if (r.subtitle.isNotEmpty()) rest += "  " + r.subtitle;
        g.setFont(owner_.rowFont(false));
        g.setColour(text.withMultipliedAlpha(0.55f));
        g.drawText(rest, 2 + titleW, 0, width - 4 - titleW, height,
                   juce::Justification::centredLeft, true);
    }

    void itemClicked(juce::MouseEvent const&) override { setOpen(!isOpen()); }

private:
    SearchPanel& owner_;
    int fileIndex_;
};

class SearchPanel::RootItem : public juce::TreeViewItem {
public:
    bool mightContainSubItems() override { return true; }
};

// Return activates the selected hit; Escape hands focus back.
class SearchPanel::ResultsTree : public juce::TreeView {
public:
    explicit ResultsTree(SearchPanel& owner) : owner_(owner) {}
    bool keyPressed(juce::KeyPress const& key) override {
        if (key == juce::KeyPress::returnKey) {
            if (auto* hit = dynamic_cast<HitItem*>(getSelectedItem(0))) {
                owner_.openHit(hit->fileIndex(), hit->hitIndex());
                return true;
            }
        }
        if (key == juce::KeyPress::escapeKey) {
            if (owner_.onDismiss) owner_.onDismiss();
            return true;
        }
        return juce::TreeView::keyPressed(key);
    }
private:
    SearchPanel& owner_;
};

// ---------------------------------------------------------------------------
// SearchPanel
// ---------------------------------------------------------------------------

SearchPanel::SearchPanel() {
    field_.setMultiLine(false);
    field_.setReturnKeyStartsNewLine(false);
    field_.setTextToShowWhenEmpty("Find in files", juce::Colours::grey);
    field_.setEscapeAndReturnKeysConsumed(true);
    field_.onReturnKey = [this] { if (onSearch) onSearch(); };
    field_.onEscapeKey = [this] { if (onDismiss) onDismiss(); };
    addAndMakeVisible(field_);

    for (int i = 0; i < kNumMatchModes; ++i)
        modeBox_.addItem(kMatchModeNames[i], i + 1);
    modeBox_.setSelectedId(1, juce::dontSendNotification);
    modeBox_.setTooltip("How the term is matched");
    modeBox_.onChange = [this] {
        if (term().isNotEmpty() && onSearch) onSearch();
    };
    addAndMakeVisible(modeBox_);

    caseButton_.setTooltip("Case sensitive");
    caseButton_.onClick = [this] {
        if (term().isNotEmpty() && onSearch) onSearch();
    };
    addAndMakeVisible(caseButton_);

    heading_.setJustificationType(juce::Justification::centredLeft);
    heading_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(heading_);

    tree_ = std::make_unique<ResultsTree>(*this);
    root_ = std::make_unique<RootItem>();
    tree_->setRootItem(root_.get());
    tree_->setRootItemVisible(false);
    tree_->setDefaultOpenness(true);
    tree_->setMultiSelectEnabled(false);
    tree_->setOpenCloseButtonsVisible(true);
    addAndMakeVisible(*tree_);
    rebuildFonts();
    lookAndFeelChanged();
}

SearchPanel::~SearchPanel() {
    tree_->setRootItem(nullptr);
}

void SearchPanel::resized() {
    auto r = getLocalBounds().reduced(2);
    auto row = r.removeFromTop(26);
    field_.setBounds(row);
    r.removeFromTop(3);
    row = r.removeFromTop(24);
    caseButton_.setBounds(row.removeFromRight(44));
    modeBox_.setBounds(row);
    r.removeFromTop(3);
    heading_.setBounds(r.removeFromTop(20));
    tree_->setBounds(r);
}

void SearchPanel::paint(juce::Graphics& g) {
    auto& lf = getLookAndFeel();
    g.fillAll(lf.findColour(juce::ResizableWindow::backgroundColourId));
}

void SearchPanel::lookAndFeelChanged() {
    auto& lf = getLookAndFeel();
    tree_->setColour(juce::TreeView::backgroundColourId,
                     lf.findColour(juce::ResizableWindow::backgroundColourId));
    tree_->setColour(juce::TreeView::linesColourId,
                     lf.findColour(juce::TextEditor::textColourId)
                         .withMultipliedAlpha(0.25f));
    heading_.setColour(juce::Label::textColourId,
                       lf.findColour(juce::TextEditor::textColourId)
                           .withMultipliedAlpha(0.7f));
    repaint();
}

bool SearchPanel::keyPressed(juce::KeyPress const& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onDismiss) onDismiss();
        return true;
    }
    return false;
}

void SearchPanel::rebuildFonts() {
    plainFont_ = juce::Font(juce::FontOptions(monoFontName(), fontSize_, juce::Font::plain));
    boldFont_ = juce::Font(juce::FontOptions(monoFontName(), fontSize_, juce::Font::bold));
    heading_.setFont(juce::Font(juce::FontOptions(fontSize_ * 0.9f)));
}

void SearchPanel::setFontSize(float px) {
    fontSize_ = px;
    rebuildFonts();
    root_->treeHasChanged();
    repaint();
}

int SearchPanel::rowHeight() const {
    return juce::roundToInt(fontSize_ * 1.5f);
}

MatchMode SearchPanel::mode() const {
    int id = modeBox_.getSelectedId();
    return (MatchMode)juce::jlimit(0, kNumMatchModes - 1, id - 1);
}

void SearchPanel::setTerm(String const& t) {
    field_.setText(t, juce::dontSendNotification);
}

void SearchPanel::focusField(String const& seed) {
    if (seed.isNotEmpty()) setTerm(seed);
    field_.grabKeyboardFocus();
    field_.selectAll();
}

void SearchPanel::setResults(String const& heading,
                             std::vector<SearchFileResult> results) {
    results_ = std::move(results);
    heading_.setText(heading, juce::dontSendNotification);
    rebuildTree();
}

void SearchPanel::clearResults() {
    results_.clear();
    heading_.setText("", juce::dontSendNotification);
    rebuildTree();
}

int SearchPanel::hitCount() const {
    int n = 0;
    for (auto const& r : results_) n += (int)r.hits.size();
    return n;
}

void SearchPanel::rebuildTree() {
    root_->clearSubItems();
    for (int i = 0; i < (int)results_.size(); ++i) {
        auto* item = new FileItem(*this, i);
        root_->addSubItem(item);
        item->setOpen(true);
    }
    repaint();
}

void SearchPanel::openHit(int fileIndex, int hitIndex) {
    if (fileIndex < 0 || fileIndex >= (int)results_.size()) return;
    auto const& r = results_[(size_t)fileIndex];
    if (hitIndex < 0 || hitIndex >= (int)r.hits.size()) return;
    if (onOpenHit) onOpenHit(r, r.hits[(size_t)hitIndex]);
}

bool SearchPanel::testClickFirstHit() {
    for (int i = 0; i < (int)results_.size(); ++i)
        if (!results_[(size_t)i].hits.empty()) { openHit(i, 0); return true; }
    return false;
}

}
