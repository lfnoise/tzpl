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
//  plugin_browser_panel.hpp
//  app
//
//  Floating "Plugins" window: lists the synthdef plugins (node defs)
//  registered with the audio engine plus plugins found on disk in the
//  search paths but not yet loaded, with a filterable list on the left
//  and inlet/outlet/control/buffer details for the selected def on the
//  right. Available (on-disk) plugins can be introspected and loaded
//  into the engine. Def metadata is cached and re-queried at most once
//  per second while the window is open.
//

#ifndef plugin_browser_panel_hpp
#define plugin_browser_panel_hpp

#include "plugin_tags.hpp"
#include "tzpl_client_interface.hpp"

#include <string>
#include <vector>

namespace bridge { struct AppContext; }

class PluginBrowserPanel {
public:
    bool isOpen() const { return open_; }
    void setOpen(bool open);
    void toggle() { setOpen(!open_); }

    // Draw the window. No-op when closed.
    void draw(bridge::AppContext& ctx);

private:
    void refresh(bridge::AppContext& ctx);
    // Effective-tags line (local tags removable) + add-tag input for `name`.
    void drawTagsEditor(std::string const& name,
                        std::vector<std::string> const& embedded);

    bool open_ = false;
    tzplapp::PluginTagStore store_;
    std::vector<engine::DefDesc> defs_;        // loaded into the engine
    std::vector<engine::PluginFile> available_; // on disk, not loaded
    std::string selectedName_;       // selection keyed by name, stable across refreshes
    bool selectedIsFile_ = false;    // selection is in the Available section
    std::string selectedPath_;       // path of the selected available plugin
    // Introspection of the selected available plugin, fetched once per
    // selection (engine caches by path+mtime, but don't call per frame).
    engine::DefDesc fileDesc_;
    bool fileDescOk_ = false;
    bool fileDescFetched_ = false;
    std::string loadError_;          // last Load failure; cleared on selection change
    bool showHidden_ = false;        // show rows with a hidden tag (dimmed)
    char tagInput_[64] = {};
    char filter_[128] = {};
    double lastRefresh_ = -1.0;      // ImGui::GetTime() of last query; -1 = never
};

#endif /* plugin_browser_panel_hpp */
