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

// Cheap change detection for the 1 Hz re-query: names + section counts +
// embedded tags (tags feed row hidden state and are tiny). Avoids rebuilding
// the row model when nothing changed.
bool sameDefs(std::vector<engine::DefDesc> const& a,
              std::vector<engine::DefDesc> const& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].name != b[i].name
            || a[i].ins.size() != b[i].ins.size()
            || a[i].outs.size() != b[i].outs.size()
            || a[i].controls.size() != b[i].controls.size()
            || a[i].buffers.size() != b[i].buffers.size()
            || a[i].tags != b[i].tags)
            return false;
    }
    return true;
}

bool sameFiles(std::vector<engine::PluginFile> const& a,
               std::vector<engine::PluginFile> const& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name || a[i].path != b[i].path)
            return false;
    return true;
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
    if (!tagEdValid_ || tagEdName_ != name) {
        tagEdEffective_ = store_.effectiveTags(name, embedded);
        tagEdLocal_ = store_.localTags(name);
        tagEdName_ = name;
        tagEdValid_ = true;
    }
    auto isLocal = [this](std::string const& t) {
        return std::find(tagEdLocal_.begin(), tagEdLocal_.end(), t)
            != tagEdLocal_.end();
    };

    ImGui::TextDisabled("tags:");
    std::string toRemove;
    for (auto const& t : tagEdEffective_) {
        ImGui::SameLine();
        ImGui::TextUnformatted(t.c_str());
        if (isLocal(t)) {  // only user-local tags are removable
            ImGui::SameLine(0.0f, 2.0f);
            std::string id = "x##tag_" + t;
            if (ImGui::SmallButton(id.c_str()))
                toRemove = t;
        }
    }
    if (tagEdEffective_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(none)");
    }
    if (!toRemove.empty()) {
        store_.removeLocalTag(name, toRemove);
        tagEdValid_ = false;  // re-read next frame
        rebuildRows();
    }

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
        if (!tag.empty()) {
            store_.addLocalTag(name, tag);
            tagEdValid_ = false;
            rebuildRows();
        }
        tagInput_[0] = '\0';
    }
}

std::vector<std::string> const&
PluginBrowserPanel::availEmbedded(engine::PluginFile const& f) const {
    static std::vector<std::string> const kNoTags;
    if (fileDescFetched_ && fileDescOk_ && f.path == selectedPath_)
        return fileDesc_.tags;
    return kNoTags;
}

void PluginBrowserPanel::rebuildRows() {
    loadedRows_.clear();
    availRows_.clear();
    hiddenCount_ = 0;
    for (auto const& d : defs_) {
        if (!containsCaseless(d.name, filter_)) continue;
        bool hidden = store_.shouldHide(d.name, d.tags);
        if (hidden) {
            ++hiddenCount_;
            if (!showHidden_) continue;
        }
        char label[512];
        std::snprintf(label, sizeof(label),
                      "%s  (%zu in, %zu out, %zu ctl, %zu buf)",
                      d.name.c_str(), d.ins.size(), d.outs.size(),
                      d.controls.size(), d.buffers.size());
        loadedRows_.push_back({d.name, label, {}, false, hidden});
    }
    for (auto const& f : available_) {
        if (!containsCaseless(f.name, filter_)) continue;
        bool hidden = store_.shouldHide(f.name, availEmbedded(f));
        if (hidden) {
            ++hiddenCount_;
            if (!showHidden_) continue;
        }
        availRows_.push_back({f.name, f.name, f.path, true, hidden});
    }
}

void PluginBrowserPanel::refresh(bridge::AppContext& ctx) {
    std::vector<engine::DefDesc> defs;
    if (ctx.engine) engine::listDefDescs(ctx.engine, defs);

    // Available = on-disk plugins whose name is not a loaded def. The scan
    // never dlopens, so the 1s cadence is fine.
    std::vector<engine::PluginFile> files;
    engine::listPluginFiles(ctx.pluginSearchPaths, files);
    std::vector<engine::PluginFile> avail;
    for (auto& f : files) {
        bool loaded = std::any_of(defs.begin(), defs.end(),
                                  [&f](engine::DefDesc const& d) {
                                      return d.name == f.name;
                                  });
        if (!loaded) avail.push_back(std::move(f));
    }

    lastRefresh_ = ImGui::GetTime();
    if (sameDefs(defs, defs_) && sameFiles(avail, available_))
        return;  // routine 1 Hz tick, nothing changed: keep cached rows

    defs_ = std::move(defs);
    available_ = std::move(avail);
    tagEdValid_ = false;

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

    rebuildRows();
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
    if (ImGui::InputTextWithHint("##filter", "filter", filter_,
                                 sizeof(filter_)))
        rebuildRows();
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh"))
        refresh(ctx);

    char showLabel[48];
    std::snprintf(showLabel, sizeof(showLabel),
                  "Show hidden (%d)###showhidden", hiddenCount_);
    if (ImGui::Checkbox(showLabel, &showHidden_))
        rebuildRows();

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
            bool tagsChanged = false;
            for (auto const& [tag, count] : tagCounts) {
                TagFilter f = store_.filterFor(tag);
                auto radio = [&](char const* label, TagFilter state) {
                    std::string id = std::string(label) + "##" + tag;
                    if (ImGui::RadioButton(id.c_str(), f == state)
                        && f != state) {
                        store_.setFilter(tag, state);  // persists
                        tagsChanged = true;
                    }
                    ImGui::SameLine();
                };
                radio("S", TagFilter::Show);
                radio("H", TagFilter::Hide);
                radio("-", TagFilter::DontCare);
                ImGui::TextUnformatted(tag.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("(%d)", count);
            }
            if (tagsChanged)
                rebuildRows();
        }
        ImGui::EndPopup();
    }

    // Rows are precomputed (rebuildRows); the clipper keeps per-frame work
    // to the visible Selectables only.
    ImGui::BeginChild("##pluginListScroll");
    ImGui::SeparatorText("Loaded");
    if (!ctx.engine) {
        ImGui::TextDisabled("engine not running");
    } else {
        ImGuiListClipper clipper;
        clipper.Begin((int)loadedRows_.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                Row const& r = loadedRows_[(size_t)i];
                bool sel = !selectedIsFile_ && r.name == selectedName_;
                if (r.hidden)
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                if (ImGui::Selectable(r.label.c_str(), sel) && !sel) {
                    selectedName_ = r.name;
                    selectedIsFile_ = false;
                    selectedPath_.clear();
                    fileDescFetched_ = false;
                    loadError_.clear();
                }
                if (r.hidden)
                    ImGui::PopStyleColor();
            }
        }
    }
    if (!availRows_.empty()) {
        ImGui::SeparatorText("Available");
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGuiListClipper clipper;
        clipper.Begin((int)availRows_.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                Row const& r = availRows_[(size_t)i];
                bool sel = selectedIsFile_ && r.path == selectedPath_;
                if (ImGui::Selectable(r.label.c_str(), sel) && !sel) {
                    selectedName_ = r.name;
                    selectedIsFile_ = true;
                    selectedPath_ = r.path;
                    fileDescFetched_ = false;
                    loadError_.clear();
                }
                ImGui::SetItemTooltip("%s", r.path.c_str());
            }
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
            // Embedded tags just became known; the row's hidden state (and
            // the tags line) may change.
            tagEdValid_ = false;
            rebuildRows();
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
