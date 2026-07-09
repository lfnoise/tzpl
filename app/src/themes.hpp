// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//
//  themes.hpp
//  app
//
//  ImGui color themes, selectable from View > Theme.
//

#ifndef themes_hpp
#define themes_hpp

enum AppTheme {
    themeDark = 0,   // ImGui's stock dark style (the default)
    themeCorpGray,
    themeDark2,
    themeCherry,
    themeDarkGrey,
    themeCount,
};

// Display names, indexed by AppTheme. Defined here (not in themes.cpp) so
// the JUCE app can share theme identity without pulling in ImGui.
inline const char* const kAppThemeNames[themeCount] = {
    "Dark (Default)",
    "Corporate Gray",
    "Dark 2",
    "Cherry",
    "Dark Grey",
};

// Reset the style to defaults, then apply the theme's colors and metrics.
// (ImGui app only; the JUCE app's counterpart is TzplLookAndFeel.)
void applyAppTheme(int theme);

#endif /* themes_hpp */
