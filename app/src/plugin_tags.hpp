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
//  plugin_tags.hpp
//  app
//
//  PluginTagStore: the user-local layer of the plugin tagging system, shared
//  by both GUI apps. Three tag sources merge into a plugin's effective tag
//  set: tags embedded in the plugin itself (the compiled-in loadTags symbol,
//  reported via engine::DefDesc::tags), name-pattern rules (globs implying a
//  tag, e.g. "test_*" => test, covering legacy untagged plugins), and
//  user-local tags added in the browser. Local tags and patterns are keyed by
//  SYNTHDEF NAME, never by file path -- names are the identity the engine,
//  compile cache, and browser already use, so tags survive moving/renaming
//  plugin folders and recompiles.
//
//  Filtering is per-tag tri-state (show / hide / don't-care). Hide wins: a
//  plugin with any hide tag is hidden. Otherwise, if any tag is marked show,
//  the show set is a whitelist -- only plugins carrying a show tag are
//  visible ("show all fx except distortion" = fx:show, distortion:hide).
//  With no show tags set, everything not hidden is visible.
//
//  Persisted in one per-user, human-editable text file shared by both apps
//  (macOS: ~/Library/Application Support/Tzopilotl/plugin_tags.txt; else
//  $XDG_CONFIG_HOME/tzpl/plugin_tags.txt). Line format:
//      tag <defName> <tag>...     user-added local tags for a synthdef
//      pattern <glob> <tag>       def-name glob implying a tag (fnmatch)
//      hide <tag>...              tags marked hide
//      show <tag>...              tags marked show (whitelist when non-empty)
//  '#' starts a comment. Missing file = defaults: pattern test_* test,
//  hide test. Single-threaded use (GUI/message thread); concurrent apps
//  overwrite whole-file, last save wins.
//

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tzplapp {

class PluginTagStore {
public:
    PluginTagStore();                          // loads from the default path
    explicit PluginTagStore(std::string path); // custom path (tests)

    // User-added local tags for a def name (this layer only).
    std::vector<std::string> localTags(std::string const& name) const;

    // Add/remove a local tag; both persist immediately.
    void addLocalTag(std::string const& name, std::string const& tag);
    void removeLocalTag(std::string const& name, std::string const& tag);

    // Union of embedded + pattern-implied + local tags, deduped in that order.
    std::vector<std::string> effectiveTags(
        std::string const& name, std::vector<std::string> const& embedded) const;

    // Per-tag filter state. Show and hide are independent axes on distinct
    // tags; one tag holds exactly one state.
    enum class TagFilter { DontCare, Show, Hide };

    TagFilter filterFor(std::string const& tag) const;
    void setFilter(std::string const& tag, TagFilter f);  // persists

    // The browser's default filter decision for this plugin: hidden if any
    // effective tag is marked Hide; otherwise, when any tag anywhere is
    // marked Show, hidden unless some effective tag is marked Show.
    bool shouldHide(std::string const& name,
                    std::vector<std::string> const& embedded) const;

    std::vector<std::string> shownTags() const;   // sorted
    std::vector<std::string> hiddenTags() const;  // sorted

    std::string const& path() const { return path_; }

private:
    void load();
    void save() const;

    struct PatternRule {
        std::string glob;
        std::string tag;
    };

    std::string path_;
    std::unordered_map<std::string, std::vector<std::string>> local_;
    std::vector<PatternRule> patterns_;
    std::unordered_set<std::string> shown_;
    std::unordered_set<std::string> hidden_;
};

} // namespace tzplapp
