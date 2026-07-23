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
//  plugin_tags.cpp
//  app
//

#include "plugin_tags.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <sstream>

namespace tzplapp {

namespace fs = std::filesystem;

static std::string defaultStorePath() {
    char const* home = std::getenv("HOME");
    std::string base;
#ifdef __APPLE__
    base = std::string(home ? home : "") + "/Library/Application Support/Tzopilotl";
#else
    if (char const* xdg = std::getenv("XDG_CONFIG_HOME")) {
        base = std::string(xdg) + "/tzpl";
    } else {
        base = std::string(home ? home : "") + "/.config/tzpl";
    }
#endif
    return base + "/plugin_tags.txt";
}

PluginTagStore::PluginTagStore() : PluginTagStore(defaultStorePath()) {}

PluginTagStore::PluginTagStore(std::string path) : path_(std::move(path)) {
    // Defaults apply when no file exists; a loaded file replaces them
    // entirely so users can remove the default rules by editing/deleting
    // the lines.
    patterns_.push_back({"test_*", "test"});
    hidden_.insert("test");
    load();
}

std::vector<std::string> PluginTagStore::localTags(std::string const& name) const {
    auto it = local_.find(name);
    return it == local_.end() ? std::vector<std::string>{} : it->second;
}

void PluginTagStore::addLocalTag(std::string const& name, std::string const& tag) {
    if (name.empty() || tag.empty()) return;
    auto& tags = local_[name];
    if (std::find(tags.begin(), tags.end(), tag) != tags.end()) return;
    tags.push_back(tag);
    save();
}

void PluginTagStore::removeLocalTag(std::string const& name, std::string const& tag) {
    auto it = local_.find(name);
    if (it == local_.end()) return;
    auto& tags = it->second;
    auto pos = std::find(tags.begin(), tags.end(), tag);
    if (pos == tags.end()) return;
    tags.erase(pos);
    if (tags.empty()) local_.erase(it);
    save();
}

std::vector<std::string> PluginTagStore::effectiveTags(
    std::string const& name, std::vector<std::string> const& embedded) const {
    std::vector<std::string> out;
    auto add = [&out](std::string const& t) {
        if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) {
            out.push_back(t);
        }
    };
    for (auto const& t : embedded) add(t);
    for (auto const& rule : patterns_) {
        if (fnmatch(rule.glob.c_str(), name.c_str(), 0) == 0) add(rule.tag);
    }
    if (auto it = local_.find(name); it != local_.end()) {
        for (auto const& t : it->second) add(t);
    }
    return out;
}

PluginTagStore::TagFilter PluginTagStore::filterFor(std::string const& tag) const {
    if (hidden_.count(tag)) return TagFilter::Hide;
    if (shown_.count(tag)) return TagFilter::Show;
    return TagFilter::DontCare;
}

void PluginTagStore::setFilter(std::string const& tag, TagFilter f) {
    if (tag.empty() || filterFor(tag) == f) return;
    shown_.erase(tag);
    hidden_.erase(tag);
    if (f == TagFilter::Show) shown_.insert(tag);
    else if (f == TagFilter::Hide) hidden_.insert(tag);
    save();
}

bool PluginTagStore::shouldHide(std::string const& name,
                                std::vector<std::string> const& embedded) const {
    auto eff = effectiveTags(name, embedded);
    // Hide wins.
    for (auto const& t : eff) {
        if (hidden_.count(t)) return true;
    }
    // A non-empty show set is a whitelist.
    if (shown_.empty()) return false;
    for (auto const& t : eff) {
        if (shown_.count(t)) return false;
    }
    return true;
}

static std::vector<std::string> sortedVec(std::unordered_set<std::string> const& s) {
    std::vector<std::string> out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> PluginTagStore::shownTags() const { return sortedVec(shown_); }
std::vector<std::string> PluginTagStore::hiddenTags() const { return sortedVec(hidden_); }

void PluginTagStore::load() {
    std::ifstream in(path_);
    if (!in) return;

    // The file is authoritative: discard the built-in defaults.
    local_.clear();
    patterns_.clear();
    shown_.clear();
    hidden_.clear();

    std::string line;
    while (std::getline(in, line)) {
        if (auto hash = line.find('#'); hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream ss(line);
        std::string kind;
        if (!(ss >> kind)) continue;
        if (kind == "tag") {
            std::string name, tag;
            if (!(ss >> name)) continue;
            auto& tags = local_[name];
            while (ss >> tag) {
                if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
                    tags.push_back(tag);
                }
            }
            if (tags.empty()) local_.erase(name);
        } else if (kind == "pattern") {
            std::string glob, tag;
            if (ss >> glob >> tag) patterns_.push_back({glob, tag});
        } else if (kind == "hide") {
            std::string tag;
            while (ss >> tag) {
                shown_.erase(tag);
                hidden_.insert(tag);
            }
        } else if (kind == "show") {
            std::string tag;
            while (ss >> tag) {
                if (!hidden_.count(tag)) shown_.insert(tag);
            }
        }
    }
}

void PluginTagStore::save() const {
    std::error_code ec;
    fs::create_directories(fs::path(path_).parent_path(), ec);
    std::ofstream out(path_, std::ios::trunc);
    if (!out) return;

    out << "# Tzopilotl plugin tags. Lines:\n"
           "#   tag <defName> <tag>...   user-added tags for a synthdef name\n"
           "#   pattern <glob> <tag>     def-name glob implying a tag\n"
           "#   hide <tag>               plugins with this tag are hidden\n"
           "#   show <tag>               whitelist: when any show tags exist,\n"
           "#                            only plugins carrying one are shown\n";
    for (auto const& rule : patterns_) {
        out << "pattern " << rule.glob << " " << rule.tag << "\n";
    }
    for (auto const& tag : hiddenTags()) {
        out << "hide " << tag << "\n";
    }
    for (auto const& tag : shownTags()) {
        out << "show " << tag << "\n";
    }
    std::vector<std::string> names;
    names.reserve(local_.size());
    for (auto const& [name, tags] : local_) names.push_back(name);
    std::sort(names.begin(), names.end());
    for (auto const& name : names) {
        out << "tag " << name;
        for (auto const& t : local_.at(name)) out << " " << t;
        out << "\n";
    }
}

} // namespace tzplapp
