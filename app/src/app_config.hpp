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
//  app_config.hpp
//  app
//
//  The `tzpl-config` engine settings file (key = value, `--` comments),
//  shared by the startup path in main.cpp and the app's settings dialog.
//  Toolkit-free: no JUCE, no engine.
//
//  Two files are read at launch, later ones overriding earlier ones, and
//  the command line overrides both:
//
//      userConfigFile()                  -- per-user defaults, all projects
//      <project>/tzpl-config             -- the project's own settings
//
//  Every setting here is consumed when the engine is created, so changes
//  take effect on the next launch -- which is why the settings dialog
//  offers to relaunch.
//

#ifndef app_config_hpp
#define app_config_hpp

#include <string>
#include <vector>

namespace tzplapp {

// Field defaults ARE the app defaults: an absent key means "leave as is".
struct AppConfig {
    std::string deviceName = "default";
    std::string inputDeviceName;  // empty = same device as the output
    int channels = 2;
    int firstChannel = 0;
    int inputChannels = 0;  // 0 = no audio input (Audio In has no outlet)
    int firstInputChannel = 0;
    int bufferFrames = 512;
    double sampleRate = 48000.0;
    int numSilos = 4;
    int numTempoClocks = 1;
    int oscPort = 0;      // 0 = disabled
    std::string natsUrl;  // empty = disabled
    std::string engineName;  // empty = single-instance mode
};

// Overwrite the keys `path` mentions, leaving the rest of `cfg` alone.
// False when the file could not be opened (a missing file is not an error
// worth a diagnostic -- callers treat it as "nothing configured"). Bad
// lines are reported as "path:line: ..." in `problems` and skipped.
bool loadConfigFile(std::string const& path, AppConfig& cfg,
                    std::vector<std::string>* problems = nullptr);

// Write `cfg` back to `path`, in place: comments, key order, and unknown
// keys survive. A key is written when the file already mentions it (an
// existing `-- key = value` template line is uncommented and reused) or
// when its value differs from the built-in default, so an untouched file
// stays as short as the user left it. Creates the file (and its parent
// directory) when missing. False on write failure, with `err` set.
bool saveConfigFile(std::string const& path, AppConfig const& cfg,
                    std::string* err = nullptr);

// Per-user config path, read before any project's: on macOS
// ~/Library/Application Support/Tzopilotl/tzpl-config, elsewhere
// $XDG_CONFIG_HOME/tzpl/tzpl-config. The file need not exist.
std::string userConfigFile();

}  // namespace tzplapp

#endif /* app_config_hpp */
