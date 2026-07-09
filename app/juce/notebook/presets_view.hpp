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
//  presets_view.hpp
//  app (JUCE)
//
//  The body of a Presets cell: a bank of stored control snapshots. A slot
//  grid where a plain click recalls, and cmd/right-click opens a transient
//  menu (Rename / Overwrite / Delete) that a click-away dismisses -- so no
//  destructive control lingers to be hit by a stray click. Rename edits in
//  place. The Preset data and capture/recall logic live in NotebookView.
//

#ifndef presets_view_hpp
#define presets_view_hpp

#include "document.hpp"
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include <vector>

namespace tzplapp {

class PresetsView : public juce::Component {
public:
    PresetsView();

    // Reload the slot bank from the model.
    void setPresets(
        std::vector<std::shared_ptr<doc::Preset const>> const& presets);

    int preferredHeight(int width) const;
    void resized() override;
    void paint(juce::Graphics& g) override;

    // Callbacks to NotebookView. Indices are into the current slot order.
    std::function<void()> onStore;             // + store: capture a new slot
    std::function<void(int)> onRecall;         // click: apply slot to widgets
    std::function<void(int)> onOverwrite;      // recapture into slot
    std::function<void(int, juce::String)> onRename;
    std::function<void(int)> onDelete;

private:
    void rebuildSlots();
    void layOutSlots();
    void showSlotMenu(int idx);
    void startRename(int idx);     // inline editor over the slot
    void commitRename();

    std::vector<juce::String> names_;          // one per slot (may be empty)
    std::vector<std::unique_ptr<juce::TextButton>> slotButtons_;
    bool nameNextStored_ = false;              // "+ store" -> rename new slot

    juce::TextButton storeButton_ { "+ store" };
    juce::Label hintLabel_;
    std::unique_ptr<juce::TextEditor> renameEditor_;
    int renameIndex_ = -1;

    static constexpr int kRowH = 24;
    static constexpr int kSlotW = 84;
    static constexpr int kSlotH = 24;
    static constexpr int kGap = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetsView)
};

}

#endif /* presets_view_hpp */
