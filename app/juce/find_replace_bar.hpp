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
//  find_replace_bar.hpp
//  app (JUCE)
//
//  Find/Replace bar shown at the top of the editor pane. The JUCE port of
//  find_replace.{hpp,cpp} -- the search logic runs against the active
//  editor's CodeDocument (via a supplied accessor) instead of the vendored
//  ImGuiColorTextEdit.
//

#ifndef find_replace_bar_hpp
#define find_replace_bar_hpp

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

namespace tzplapp {

class TzplCodeEditor;

class FindReplaceBar : public juce::Component {
public:
    // `activeEditor` returns the editor to search (may return nullptr).
    explicit FindReplaceBar(std::function<TzplCodeEditor*()> activeEditor);

    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(juce::KeyPress const& key) override;

    // Reveal the bar and focus the find field. If `seed` is non-empty it
    // becomes the search term (used by "Use Selection for Find").
    void show(juce::String const& seed = {});
    void hide();
    bool isShown() const { return shown_; }

    void seedReplace(juce::String const& text);

    void findNext();
    void findPrevious();

    // Called when the bar's visibility changes so the pane can re-lay out.
    std::function<void()> onVisibilityChanged;

    static constexpr int kBarHeight = 34;

private:
    void search(bool forward, bool fromSelectionEnd);
    void replaceCurrent();
    void replaceAll();
    void updateMatchLabel(int matchIndex, int total);

    std::function<TzplCodeEditor*()> activeEditor_;
    juce::TextEditor findField_;
    juce::TextEditor replaceField_;
    juce::TextButton prevButton_ { "<" };
    juce::TextButton nextButton_ { ">" };
    juce::TextButton replaceButton_ { "Replace" };
    juce::TextButton replaceAllButton_ { "All" };
    juce::ToggleButton caseButton_ { "Aa" };
    juce::Label matchLabel_;
    juce::TextButton closeButton_ { "x" };
    bool shown_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FindReplaceBar)
};

}

#endif /* find_replace_bar_hpp */
