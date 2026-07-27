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
//  settings_dialog.hpp
//  app (JUCE)
//
//  Engine Settings: a form over the `tzpl-config` keys (app_config.hpp),
//  writing either the open project's file or the per-user one. Every
//  setting is consumed when the engine is created, so the dialog cannot
//  apply anything live -- it saves, and offers to relaunch.
//

#ifndef settings_dialog_hpp
#define settings_dialog_hpp

#include <functional>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

namespace tzplapp {

// Show the (non-modal) Engine Settings window, or bring the open one to
// front. `projectDir` is the app's current project ("" when none, which
// leaves the per-user file as the only save target). `log` receives a line
// for the console after a save; `relaunch` is invoked when the user picks
// "Save & Relaunch", after the file has been written.
void showEngineSettings(std::string const& projectDir,
                        std::function<void(juce::String const&)> log,
                        std::function<void()> relaunch);

// Close the settings window if it is open (app shutdown).
void closeEngineSettings();

}  // namespace tzplapp

#endif /* settings_dialog_hpp */
