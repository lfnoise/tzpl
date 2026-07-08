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
//  app_commands.hpp
//  app (JUCE)
//
//  Application command IDs -- the JUCE port of the AppCmd enum in
//  app_gui.mm. Shortcuts are registered in MainComponent::getCommandInfo;
//  ModifierKeys::commandModifier maps to Cmd on macOS and Ctrl elsewhere.
//
//  The ImGui app's MoveHome/MoveEnd/MoveTop/MoveBottom commands have no
//  counterparts here: JUCE text components handle Cmd+Arrow natively.
//

#ifndef app_commands_hpp
#define app_commands_hpp

#include <juce_gui_basics/juce_gui_basics.h>

namespace tzplapp::cmd {

enum : juce::CommandID {
    // File
    fileNew = 1,
    fileNewNotebook,
    fileOpen,
    fileSave,
    fileSaveAs,
    fileSaveCopy,
    fileClose,
    quit, // File > Quit on Windows/Linux; the macOS app menu provides it

    // Edit (clipboard commands route to the focused text target)
    editUndo,
    editRedo,
    editCut,
    editCopy,
    editPaste,
    editSelectAll,
    editClearOutput,
    editToggleComment,
    editIndent,
    editOutdent,

    // Find
    findShow,
    findNext,
    findPrevious,
    findUseSelection,
    findUseSelectionReplace,

    // View
    fontIncrease,
    fontDecrease,
    toggleNotebookView,
    togglePerform,

    // Eval (keyboard only, like the ImGui app)
    evalSelection,
    evalLine,
    evalFile,

    // Ranges: fontSetBase + i selects font size i, themeSetBase + i selects
    // theme i (see kEditorFontSizes / AppTheme).
    fontSetBase = 100,
    themeSetBase = 200,
};

// Editor font sizes offered in View > Font Size. JUCE renders vector fonts
// (no pre-baked bitmap atlas like the ImGui app), so any size is cheap --
// offer a full range. kDefaultFontSizeIndex is the fallback when nothing is
// saved.
inline constexpr float kEditorFontSizes[] = {
    9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f,
    18.0f, 20.0f, 24.0f, 28.0f, 32.0f, 40.0f };
inline constexpr int kNumEditorFontSizes =
    (int)(sizeof(kEditorFontSizes) / sizeof(kEditorFontSizes[0]));
inline constexpr int kDefaultFontSizeIndex = 5;  // 14 pt

}

#endif /* app_commands_hpp */
