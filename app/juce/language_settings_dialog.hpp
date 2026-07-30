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
//  language_settings_dialog.hpp
//  app (JUCE)
//
//  Language Settings (advanced): a form over the `lang*` tzpl-config keys
//  (app_config.hpp) -- VM heap sizes, execution-structure limits, GC tuning,
//  and graph-traversal limits. Like Engine Settings, everything is consumed
//  when a VM is constructed, so the dialog saves and offers to relaunch.
//  Deliberately has no keyboard shortcut: changing these should take intent.
//

#ifndef language_settings_dialog_hpp
#define language_settings_dialog_hpp

#include <functional>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

namespace tzplapp {

// Show the (non-modal) Language Settings window, or bring the open one to
// front. Same contract as showEngineSettings: `projectDir` may be "" (user
// file becomes the only save target), `log` gets a console line after a
// save, `relaunch` runs after "Save & Relaunch" has written the file.
void showLanguageSettings(std::string const& projectDir,
                          std::function<void(juce::String const&)> log,
                          std::function<void()> relaunch);

// Close the settings window if it is open (app shutdown).
void closeLanguageSettings();

}  // namespace tzplapp

#endif /* language_settings_dialog_hpp */
