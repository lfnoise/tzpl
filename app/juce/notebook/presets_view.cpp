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
//  presets_view.cpp
//  app (JUCE)
//

#include "presets_view.hpp"

namespace tzplapp {

using juce::String;

namespace {
// A slot button that reports the modifier keys of its press so the view can
// route plain clicks to recall and cmd/right-clicks to the edit menu.
class SlotButton : public juce::TextButton {
public:
    std::function<void(juce::ModifierKeys)> onSlotDown;
    void mouseDown(juce::MouseEvent const& e) override {
        if (onSlotDown) onSlotDown(e.mods);
    }
};
}

PresetsView::PresetsView() {
    storeButton_.onClick = [this] {
        nameNextStored_ = true;   // pop the rename editor on the new slot
        if (onStore) onStore();
    };
    storeButton_.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(0x30ffffff));
    addAndMakeVisible(storeButton_);

    hintLabel_.setJustificationType(juce::Justification::centredLeft);
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff808080));
    hintLabel_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(hintLabel_);
}

void PresetsView::setPresets(
    std::vector<std::shared_ptr<doc::Preset const>> const& presets) {
    names_.clear();
    for (auto const& p : presets)
        names_.push_back(String(p ? p->name : std::string()));
    rebuildSlots();
    if (nameNextStored_) {
        nameNextStored_ = false;
        if (!names_.empty()) startRename((int)names_.size() - 1);
    }
}

void PresetsView::rebuildSlots() {
    slotButtons_.clear();
    for (int i = 0; i < (int)names_.size(); ++i) {
        auto b = std::make_unique<SlotButton>();
        b->setButtonText(names_[(size_t)i].isEmpty() ? String(i + 1)
                                                     : names_[(size_t)i]);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0x30ffffff));
        b->onSlotDown = [this, i](juce::ModifierKeys mods) {
            if (mods.isPopupMenu() || mods.isCommandDown()) showSlotMenu(i);
            else if (onRecall) onRecall(i);
        };
        addAndMakeVisible(*b);
        slotButtons_.push_back(std::move(b));
    }
    hintLabel_.setText(names_.empty()
        ? String("(stores the controls of the panels below, "
                 "until the next presets cell)")
        : String("(click recalls; cmd- or right-click to "
                 "rename / overwrite / delete)"),
        juce::dontSendNotification);
    resized();
    repaint();
}

void PresetsView::showSlotMenu(int idx) {
    juce::PopupMenu m;
    m.addItem(1, "Rename...");
    m.addItem(2, "Overwrite with current values");
    m.addItem(3, "Delete");
    // Dismissing the menu (click away / Esc) does nothing -- the user's
    // way to decline. Each action fires once and the menu closes.
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(slotButtons_[(size_t)idx].get()),
        [this, idx](int result) {
            if (result == 1) startRename(idx);
            else if (result == 2 && onOverwrite) onOverwrite(idx);
            else if (result == 3 && onDelete) onDelete(idx);
        });
}

void PresetsView::startRename(int idx) {
    if (idx < 0 || idx >= (int)slotButtons_.size()) return;
    renameIndex_ = idx;
    renameEditor_ = std::make_unique<juce::TextEditor>();
    renameEditor_->setBounds(slotButtons_[(size_t)idx]->getBounds());
    renameEditor_->setText(names_[(size_t)idx], false);
    renameEditor_->selectAll();
    renameEditor_->onReturnKey = [this] { commitRename(); };
    renameEditor_->onEscapeKey = [this] {   // decline
        renameIndex_ = -1;
        renameEditor_.reset();
    };
    renameEditor_->onFocusLost = [this] { commitRename(); };
    addAndMakeVisible(*renameEditor_);
    renameEditor_->grabKeyboardFocus();
}

void PresetsView::commitRename() {
    if (!renameEditor_) return;
    auto editor = std::move(renameEditor_);
    int idx = renameIndex_;
    renameIndex_ = -1;
    if (idx >= 0 && idx < (int)names_.size() && onRename)
        onRename(idx, editor->getText());
}

int PresetsView::preferredHeight(int width) const {
    int perRow = juce::jmax(1, (width - kGap) / (kSlotW + kGap));
    int rows = ((int)names_.size() + perRow - 1) / perRow;
    return kRowH + kGap + juce::jmax(0, rows) * (kSlotH + kGap) + kGap;
}

void PresetsView::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(kRowH);
    storeButton_.setBounds(top.removeFromLeft(70));
    top.removeFromLeft(kGap);
    hintLabel_.setBounds(top);
    r.removeFromTop(kGap);
    layOutSlots();
    if (renameEditor_ && renameIndex_ >= 0
        && renameIndex_ < (int)slotButtons_.size())
        renameEditor_->setBounds(slotButtons_[(size_t)renameIndex_]->getBounds());
}

void PresetsView::layOutSlots() {
    int width = getWidth();
    int perRow = juce::jmax(1, (width - kGap) / (kSlotW + kGap));
    int y0 = kRowH + kGap;
    for (int i = 0; i < (int)slotButtons_.size(); ++i) {
        int col = i % perRow, row = i / perRow;
        slotButtons_[(size_t)i]->setBounds(
            col * (kSlotW + kGap), y0 + row * (kSlotH + kGap), kSlotW, kSlotH);
    }
}

void PresetsView::paint(juce::Graphics&) {}

}
