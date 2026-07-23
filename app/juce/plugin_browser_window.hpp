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
//  plugin_browser_window.hpp
//  app (JUCE)
//
//  A floating window listing every synthdef plugin (node def) registered
//  with the audio engine, plus plugins found on disk in the search paths
//  but not yet loaded: a filterable two-section name list on the left
//  (Loaded / Available), and the selected def's inlets/outlets/controls/
//  buffers on the right -- with a Load button for available plugins.
//  Plugins carry tags (embedded + name patterns + user-local, via
//  PluginTagStore); per-tag tri-state filters (show / hide / don't-care)
//  decide row visibility unless "show hidden" is on, and local tags can be
//  added/removed in the details pane.
//  Refreshes from the engine on show and about once a second while
//  visible. Closing hides the window (the View menu toggles it back).
//

#ifndef plugin_browser_window_hpp
#define plugin_browser_window_hpp

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace bridge { struct AppContext; }

namespace tzplapp {

class PluginBrowserWindow : public juce::DocumentWindow {
public:
    explicit PluginBrowserWindow(bridge::AppContext& appCtx);

    void closeButtonPressed() override;

    // Re-fetch the def list from the engine now (the content also polls at
    // ~1 Hz while the window is showing).
    void refresh();

    // Invoked when the user closes (hides) the window, so the owner can
    // update the menu tick.
    std::function<void()> onClose;

private:
    class Content;
    Content* content_ = nullptr;  // owned by the window via setContentOwned

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserWindow)
};

}

#endif /* plugin_browser_window_hpp */
