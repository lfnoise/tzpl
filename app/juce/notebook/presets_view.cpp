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
// route plain clicks to recall and cmd/right-clicks to the edit menu (as the
// ImGui slot matrix does).
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
        selectLastOnStore_ = true;   // select the new slot when the model syncs
        if (onStore) onStore();
    };
    overwriteButton_.onClick = [this] {
        if (selected_ >= 0 && onOverwrite) onOverwrite(selected_);
    };
    deleteButton_.onClick = [this] {
        if (selected_ >= 0 && onDelete) onDelete(selected_);
    };
    for (auto* b : { &storeButton_, &overwriteButton_, &deleteButton_ }) {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0x30ffffff));
        addChildComponent(b);
    }
    addAndMakeVisible(storeButton_);

    nameField_.setTextToShowWhenEmpty("(name)", juce::Colour(0xff707070));
    nameField_.onReturnKey  = [this] { commitRename(); };
    nameField_.onFocusLost  = [this] { commitRename(); };
    addChildComponent(nameField_);

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
    if (selectLastOnStore_) {
        selected_ = (int)names_.size() - 1;   // the just-stored slot
        selectLastOnStore_ = false;
    } else if (selected_ >= (int)names_.size()) {
        selected_ = -1;
    }
    rebuildSlots();
}

void PresetsView::rebuildSlots() {
    slotButtons_.clear();
    for (int i = 0; i < (int)names_.size(); ++i) {
        auto b = std::make_unique<SlotButton>();
        b->setButtonText(names_[(size_t)i].isEmpty() ? String(i + 1)
                                                     : names_[(size_t)i]);
        b->onSlotDown = [this, i](juce::ModifierKeys mods) {
            // Recall / menu BEFORE changing selection; select() only
            // recolours (it must not recreate this very button mid-click).
            if (mods.isPopupMenu() || mods.isCommandDown()) {
                select(i);
                showSlotMenu(i);
            } else {
                select(i);
                if (onRecall) onRecall(i);
            }
        };
        addAndMakeVisible(*b);
        slotButtons_.push_back(std::move(b));
    }
    updateSelectionUI();
}

// Recolour the slots and toggle the top row for the current selection.
// Does NOT recreate buttons, so it is safe to call from a slot's own click.
void PresetsView::updateSelectionUI() {
    for (int i = 0; i < (int)slotButtons_.size(); ++i)
        slotButtons_[(size_t)i]->setColour(
            juce::TextButton::buttonColourId,
            i == selected_ ? juce::Colour(0xff4a70a0) : juce::Colour(0x30ffffff));

    bool hasSel = selected_ >= 0 && selected_ < (int)names_.size();
    overwriteButton_.setVisible(hasSel);
    deleteButton_.setVisible(hasSel);
    nameField_.setVisible(hasSel);
    hintLabel_.setVisible(!hasSel);
    hintLabel_.setText(names_.empty()
        ? String("(stores the controls of the panels below, "
                 "until the next presets cell)")
        : String("(click recalls; cmd- or right-click to "
                 "name / overwrite / delete)"),
        juce::dontSendNotification);
    if (hasSel)
        nameField_.setText(names_[(size_t)selected_], juce::dontSendNotification);
    resized();
    repaint();
}

void PresetsView::select(int idx) {
    if (selected_ == idx) return;
    selected_ = idx;
    updateSelectionUI();
}

void PresetsView::commitRename() {
    if (selected_ < 0 || selected_ >= (int)names_.size()) return;
    String text = nameField_.getText();
    if (text == names_[(size_t)selected_]) return;
    if (onRename) onRename(selected_, text);
}

void PresetsView::showSlotMenu(int idx) {
    juce::PopupMenu m;
    m.addItem(1, "Overwrite with current values");
    m.addItem(2, "Delete");
    m.showMenuAsync(juce::PopupMenu::Options()
                        .withTargetComponent(slotButtons_[(size_t)idx].get()),
        [this, idx](int result) {
            if (result == 1 && onOverwrite) onOverwrite(idx);
            else if (result == 2 && onDelete) onDelete(idx);
        });
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
    if (selected_ >= 0 && selected_ < (int)names_.size()) {
        overwriteButton_.setBounds(top.removeFromLeft(80));
        top.removeFromLeft(kGap);
        nameField_.setBounds(top.removeFromLeft(160));
        top.removeFromLeft(kGap);
        deleteButton_.setBounds(top.removeFromLeft(60));
    } else {
        hintLabel_.setBounds(top);
    }

    r.removeFromTop(kGap);
    layOutSlots();
}

void PresetsView::layOutSlots() {
    int width = getWidth();
    int perRow = juce::jmax(1, (width - kGap) / (kSlotW + kGap));
    int x0 = 0, y0 = kRowH + kGap;
    for (int i = 0; i < (int)slotButtons_.size(); ++i) {
        int col = i % perRow, row = i / perRow;
        slotButtons_[(size_t)i]->setBounds(
            x0 + col * (kSlotW + kGap), y0 + row * (kSlotH + kGap),
            kSlotW, kSlotH);
    }
}

void PresetsView::paint(juce::Graphics&) {}

}
