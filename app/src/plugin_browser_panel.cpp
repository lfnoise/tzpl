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

#include "plugin_browser_panel.hpp"

#include "tzpl_app_context.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace {

char const* elemName(tzpl_ElemType e) {
    switch (e) {
    case tzpl_kI32: return "i32";
    case tzpl_kF32: return "f32";
    case tzpl_kI64: return "i64";
    case tzpl_kF64: return "f64";
    }
    return "?";
}

char const* rateName(tzpl_Rate r) {
    switch (r) {
    case tzpl_constRate: return "const";
    case tzpl_initRate:  return "init";
    case tzpl_resetRate: return "reset";
    case tzpl_eventRate: return "event";
    case tzpl_audioRate: return "audio";
    }
    return "?";
}

bool containsCaseless(std::string const& hay, char const* needle) {
    if (!needle[0]) return true;
    auto it = std::search(hay.begin(), hay.end(), needle,
                          needle + std::strlen(needle),
                          [](char a, char b) {
                              return std::tolower((unsigned char)a)
                                  == std::tolower((unsigned char)b);
                          });
    return it != hay.end();
}

// Three shared columns: name | chans | type | rate.
void signalTypeCells(std::string const& name, tzpl_SignalType const& t) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::TextUnformatted(name.c_str());
    ImGui::TableNextColumn(); ImGui::Text("%d", t.chans);
    ImGui::TableNextColumn(); ImGui::TextUnformatted(elemName(t.elem));
    ImGui::TableNextColumn(); ImGui::TextUnformatted(rateName(t.rate));
}

constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
    | ImGuiTableFlags_SizingStretchProp;

void drawPortSection(char const* title, char const* tableID,
                     std::vector<engine::PortDesc> const& ports) {
    if (ports.empty()) return;
    ImGui::SeparatorText(title);
    if (ImGui::BeginTable(tableID, 4, kTableFlags)) {
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("chans");
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("rate");
        ImGui::TableHeadersRow();
        for (auto const& p : ports)
            signalTypeCells(p.name, p.type);
        ImGui::EndTable();
    }
}

void drawControlSection(std::vector<engine::ControlDesc> const& controls) {
    if (controls.empty()) return;
    ImGui::SeparatorText("Controls");
    if (ImGui::BeginTable("##controls", 7, kTableFlags)) {
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("chans");
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("rate");
        ImGui::TableSetupColumn("lo");
        ImGui::TableSetupColumn("hi");
        ImGui::TableSetupColumn("init");
        ImGui::TableHeadersRow();
        for (auto const& c : controls) {
            signalTypeCells(c.name, c.type);
            ImGui::TableNextColumn(); ImGui::Text("%g", c.spec.lo);
            ImGui::TableNextColumn(); ImGui::Text("%g", c.spec.hi);
            ImGui::TableNextColumn(); ImGui::Text("%g", c.spec.init);
        }
        ImGui::EndTable();
    }
}

void drawBufferSection(std::vector<engine::BufferDesc> const& buffers) {
    if (buffers.empty()) return;
    ImGui::SeparatorText("Buffers");
    if (ImGui::BeginTable("##buffers", 5, kTableFlags)) {
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("chans");
        ImGui::TableSetupColumn("type");
        ImGui::TableSetupColumn("rate");
        ImGui::TableSetupColumn("id");
        ImGui::TableHeadersRow();
        for (auto const& b : buffers) {
            signalTypeCells(b.name, b.type);
            ImGui::TableNextColumn(); ImGui::Text("%lld", (long long)b.bufID);
        }
        ImGui::EndTable();
    }
}

} // namespace

void PluginBrowserPanel::setOpen(bool open) {
    if (open && !open_) lastRefresh_ = -1.0; // re-query on open
    open_ = open;
}

void PluginBrowserPanel::drawTagsEditor(std::string const& name,
                                        std::vector<std::string> const& embedded) {
    auto effective = store_.effectiveTags(name, embedded);
    auto local = store_.localTags(name);
    auto isLocal = [&local](std::string const& t) {
        return std::find(local.begin(), local.end(), t) != local.end();
    };

    ImGui::TextDisabled("tags:");
    std::string toRemove;
    for (auto const& t : effective) {
        ImGui::SameLine();
        ImGui::TextUnformatted(t.c_str());
        if (isLocal(t)) {  // only user-local tags are removable
            ImGui::SameLine(0.0f, 2.0f);
            std::string id = "x##tag_" + t;
            if (ImGui::SmallButton(id.c_str()))
                toRemove = t;
        }
    }
    if (effective.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(none)");
    }
    if (!toRemove.empty())
        store_.removeLocalTag(name, toRemove);  // effectiveTags re-read next frame

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    bool submit = ImGui::InputTextWithHint("##addtag", "add tag...",
                                           tagInput_, sizeof(tagInput_),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::SmallButton("+"))
        submit = true;
    if (submit) {
        std::string tag = tagInput_;
        auto b = tag.find_first_not_of(" \t");
        auto e = tag.find_last_not_of(" \t");
        tag = b == std::string::npos ? std::string{} : tag.substr(b, e - b + 1);
        if (!tag.empty())
            store_.addLocalTag(name, tag);
        tagInput_[0] = '\0';
    }
}

void PluginBrowserPanel::refresh(bridge::AppContext& ctx) {
    defs_.clear();
    if (ctx.engine) engine::listDefDescs(ctx.engine, defs_);

    // Available = on-disk plugins whose name is not a loaded def. The scan
    // never dlopens, so the 1s cadence is fine.
    std::vector<engine::PluginFile> files;
    engine::listPluginFiles(ctx.pluginSearchPaths, files);
    available_.clear();
    for (auto& f : files) {
        bool loaded = std::any_of(defs_.begin(), defs_.end(),
                                  [&f](engine::DefDesc const& d) {
                                      return d.name == f.name;
                                  });
        if (!loaded) available_.push_back(std::move(f));
    }

    // A selected available plugin that vanished from the list was either
    // loaded (selection follows it by name into Loaded) or removed from
    // disk; either way drop the file selection and its cached desc.
    if (selectedIsFile_) {
        bool still = std::any_of(available_.begin(), available_.end(),
                                 [this](engine::PluginFile const& f) {
                                     return f.path == selectedPath_;
                                 });
        if (!still) {
            selectedIsFile_ = false;
            selectedPath_.clear();
            fileDescFetched_ = false;
        }
    }

    lastRefresh_ = ImGui::GetTime();
}

void PluginBrowserPanel::draw(bridge::AppContext& ctx) {
    if (!open_) return;

    ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugins", &open_)) {
        ImGui::End();
        return;
    }

    double now = ImGui::GetTime();
    if (lastRefresh_ < 0.0 || now - lastRefresh_ >= 1.0)
        refresh(ctx);

    // -- Left: filter + refresh + def list ----------------------------------
    float listW = ImGui::GetContentRegionAvail().x * 0.4f;
    ImGui::BeginChild("##pluginList", ImVec2(listW, 0),
                      ImGuiChildFlags_Border);

    float refreshW = ImGui::CalcTextSize("Refresh").x
                   + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - refreshW
                            - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##filter", "filter", filter_, sizeof(filter_));
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh"))
        refresh(ctx);

    // Embedded tags for an available plugin: known only for the selected one
    // after introspection; name-pattern rules still apply to the rest.
    auto availEmbedded = [this](engine::PluginFile const& f)
        -> std::vector<std::string> const& {
        static std::vector<std::string> const kNoTags;
        if (fileDescFetched_ && fileDescOk_ && f.path == selectedPath_)
            return fileDesc_.tags;
        return kNoTags;
    };

    // Hidden-tag count under the current text filter (checkbox label).
    int hiddenN = 0;
    for (auto const& d : defs_)
        if (containsCaseless(d.name, filter_)
            && store_.shouldHide(d.name, d.tags)) ++hiddenN;
    for (auto const& f : available_)
        if (containsCaseless(f.name, filter_)
            && store_.shouldHide(f.name, availEmbedded(f))) ++hiddenN;
    char showLabel[48];
    std::snprintf(showLabel, sizeof(showLabel),
                  "Show hidden (%d)###showhidden", hiddenN);
    ImGui::Checkbox(showLabel, &showHidden_);

    ImGui::SameLine();
    if (ImGui::SmallButton("Tags..."))
        ImGui::OpenPopup("##tagfilter");
    if (ImGui::BeginPopup("##tagfilter")) {
        ImGui::TextDisabled("S = show (whitelist), H = hide; hide wins");
        // Tag universe: every effective tag in the session, plus tags with a
        // persisted Show/Hide filter (so a filter on a tag no current plugin
        // carries can still be cleared). Counts computed only while open.
        std::map<std::string, int> tagCounts;  // sorted
        for (auto const& d : defs_)
            for (auto const& t : store_.effectiveTags(d.name, d.tags))
                ++tagCounts[t];
        for (auto const& f : available_)
            for (auto const& t : store_.effectiveTags(f.name, availEmbedded(f)))
                ++tagCounts[t];
        for (auto const& t : store_.shownTags())
            tagCounts.emplace(t, 0);
        for (auto const& t : store_.hiddenTags())
            tagCounts.emplace(t, 0);
        if (tagCounts.empty()) {
            ImGui::TextDisabled("no tags");
        } else {
            using TagFilter = tzplapp::PluginTagStore::TagFilter;
            for (auto const& [tag, count] : tagCounts) {
                TagFilter f = store_.filterFor(tag);
                auto radio = [&](char const* label, TagFilter state) {
                    std::string id = std::string(label) + "##" + tag;
                    if (ImGui::RadioButton(id.c_str(), f == state)
                        && f != state)
                        store_.setFilter(tag, state);  // persists; re-filters next frame
                    ImGui::SameLine();
                };
                radio("S", TagFilter::Show);
                radio("H", TagFilter::Hide);
                radio("-", TagFilter::DontCare);
                ImGui::TextUnformatted(tag.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%d)", count);
            }
        }
        ImGui::EndPopup();
    }

    ImGui::BeginChild("##pluginListScroll");
    ImGui::SeparatorText("Loaded");
    if (!ctx.engine) {
        ImGui::TextDisabled("engine not running");
    } else {
        for (auto const& d : defs_) {
            if (!containsCaseless(d.name, filter_)) continue;
            bool hidden = store_.shouldHide(d.name, d.tags);
            if (hidden && !showHidden_) continue;
            char label[512];
            std::snprintf(label, sizeof(label),
                          "%s  (%zu in, %zu out, %zu ctl, %zu buf)",
                          d.name.c_str(), d.ins.size(), d.outs.size(),
                          d.controls.size(), d.buffers.size());
            bool sel = !selectedIsFile_ && d.name == selectedName_;
            if (hidden)
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (ImGui::Selectable(label, sel) && !sel) {
                selectedName_ = d.name;
                selectedIsFile_ = false;
                selectedPath_.clear();
                fileDescFetched_ = false;
                loadError_.clear();
            }
            if (hidden)
                ImGui::PopStyleColor();
        }
    }
    if (!available_.empty()) {
        ImGui::SeparatorText("Available");
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        for (auto const& f : available_) {
            if (!containsCaseless(f.name, filter_)) continue;
            if (!showHidden_ && store_.shouldHide(f.name, availEmbedded(f)))
                continue;
            bool sel = selectedIsFile_ && f.path == selectedPath_;
            if (ImGui::Selectable(f.name.c_str(), sel) && !sel) {
                selectedName_ = f.name;
                selectedIsFile_ = true;
                selectedPath_ = f.path;
                fileDescFetched_ = false;
                loadError_.clear();
            }
            ImGui::SetItemTooltip("%s", f.path.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();

    // -- Right: details for the selected def --------------------------------
    ImGui::BeginChild("##pluginDetail", ImVec2(0, 0),
                      ImGuiChildFlags_Border);
    engine::DefDesc const* sel = nullptr;
    if (selectedIsFile_) {
        // Introspect once per selection; the engine caches by path+mtime.
        if (!fileDescFetched_) {
            fileDesc_ = engine::DefDesc{};
            fileDescOk_ = engine::getPluginFileDesc(selectedPath_.c_str(),
                                                    fileDesc_);
            fileDescFetched_ = true;
        }
        if (fileDescOk_) sel = &fileDesc_;
    } else {
        auto it = std::find_if(defs_.begin(), defs_.end(),
                               [this](engine::DefDesc const& d) {
                                   return d.name == selectedName_;
                               });
        if (it != defs_.end()) sel = &*it;
    }
    if (selectedIsFile_ && !fileDescOk_ && fileDescFetched_) {
        ImGui::TextUnformatted(selectedName_.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", selectedPath_.c_str());
        ImGui::PopStyleColor();
        drawTagsEditor(selectedName_, {});
        ImGui::TextDisabled("not a loadable plugin");
    } else if (!sel) {
        ImGui::TextDisabled("select a plugin");
    } else {
        ImGui::TextUnformatted(sel->name.c_str());
        if (selectedIsFile_ && ctx.engine) {
            ImGui::SameLine();
            if (ImGui::Button("Load")) {
                if (engine::loadOneDef(ctx.engine, selectedPath_.c_str())) {
                    loadError_.clear();
                    refresh(ctx);  // moves it to Loaded; selection follows by name
                } else {
                    loadError_ = "load failed: " + selectedPath_;
                }
            }
        }
        if (selectedIsFile_) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", selectedPath_.c_str());
            ImGui::PopStyleColor();
        }
        drawTagsEditor(sel->name, sel->tags);
        if (!loadError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s",
                               loadError_.c_str());
        // After a successful Load, refresh() rebuilt defs_/available_, but
        // sel points at the fileDesc_ member here, so it stays valid for
        // the rest of this frame.
        drawPortSection("Inlets", "##inlets", sel->ins);
        drawPortSection("Outlets", "##outlets", sel->outs);
        drawControlSection(sel->controls);
        drawBufferSection(sel->buffers);
        if (sel->ins.empty() && sel->outs.empty()
            && sel->controls.empty() && sel->buffers.empty())
            ImGui::TextDisabled("no ports, controls, or buffers");
    }
    ImGui::EndChild();

    ImGui::End();
}
