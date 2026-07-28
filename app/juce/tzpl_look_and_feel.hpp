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
//  tzpl_look_and_feel.hpp
//  app (JUCE)
//
//  Per-theme LookAndFeel. Theme identity (AppTheme enum, kAppThemeNames) is
//  shared with the ImGui app via themes.hpp; the color values here are the
//  JUCE mapping of those ImGui themes' key colors.
//

#ifndef tzpl_look_and_feel_hpp
#define tzpl_look_and_feel_hpp

#include "themes.hpp"
#include <juce_gui_basics/juce_gui_basics.h>

namespace tzplapp {

class TzplLookAndFeel : public juce::LookAndFeel_V4 {
public:
    TzplLookAndFeel() { applyTheme(themeDark); }

    // Swap all colors to theme `t` (an AppTheme index). The caller is
    // responsible for triggering repaints (sendLookAndFeelChange).
    void applyTheme(int t);

    int currentTheme() const { return theme_; }

    // Editor tabs: JUCE's default draws every tab in the (transparent) colour
    // the tab was added with, so the active one is invisible. Fill the front
    // tab with the editor background and mark it with an accent bar.
    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                       bool isMouseOver, bool isMouseDown) override;
    // Tab labels are filenames: monospaced like the editor, and wide enough
    // not to run into the next tab.
    juce::Font getTabButtonFont(juce::TabBarButton&, float height) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;

private:
    int theme_ = themeDark;
    // Kept from the last applyTheme for the drawing overrides above.
    juce::Colour tabBg_, tabOutline_, tabAccent_;
};

}

#endif /* tzpl_look_and_feel_hpp */
