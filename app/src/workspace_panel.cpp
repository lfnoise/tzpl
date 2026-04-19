// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "workspace_panel.hpp"
#include "imgui.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

WorkspacePanel::WorkspacePanel() = default;

int WorkspacePanel::computeOwner(std::string const& path) const {
    if (path.empty()) return -1;
    int best = -1;
    size_t bestLen = 0;
    for (int i = 0; i < (int)workspaces_.size(); ++i) {
        std::string const& root = workspaces_[i]->rootPath;
        // Root directory must be followed by '/' to count as a prefix; we
        // don't treat the root itself (with no trailing slash) as a match
        // since workspace roots are directories, not files.
        if (path.size() > root.size()
            && path.compare(0, root.size(), root) == 0
            && path[root.size()] == '/') {
            if (root.size() > bestLen) {
                best = i;
                bestLen = root.size();
            }
        }
    }
    return best;
}

void WorkspacePanel::setActive(int ownerIdx) {
    activeIdx_ = ownerIdx;
    editor_.setActiveOwner(ownerIdx);
}

void WorkspacePanel::addWorkspace(const std::string& dirPath) {
    // Avoid duplicates -- if already open, just switch to it
    for (int i = 0; i < (int)workspaces_.size(); ++i) {
        if (workspaces_[i]->rootPath == dirPath) {
            setActive(i);
            return;
        }
    }

    auto ws = std::make_unique<Workspace>();
    ws->rootPath = dirPath;

    // Extract directory name for the header
    auto slash = dirPath.find_last_of('/');
    ws->name = (slash != std::string::npos) ? dirPath.substr(slash + 1) : dirPath;

    ws->root.name = ws->name;
    ws->root.fullPath = dirPath;
    ws->root.isDirectory = true;

    workspaces_.push_back(std::move(ws));

    // The new workspace may subsume files currently visible under "Open
    // Files" or under a less-specific workspace: recompute ownership for
    // every tab so each lands under the most-specific containing workspace.
    editor_.reassignOwnership([this](std::string const& p) {
        return computeOwner(p);
    });

    setActive((int)workspaces_.size() - 1);
}

void WorkspacePanel::openFile(const std::string& path) {
    // If already open anywhere, switch to that tab's workspace view.
    if (auto existing = editor_.findTabOwner(path)) {
        setActive(*existing);
        editor_.switchToFile(path);
        return;
    }

    // Otherwise route to the most-specific workspace containing the path.
    int owner = computeOwner(path);
    setActive(owner);
    editor_.openFile(path, owner);
}

// ---------------------------------------------------------------------------
// Sidebar drawing
// ---------------------------------------------------------------------------

void WorkspacePanel::drawSidebar(float width, float height) {
    ImGui::BeginChild("##Sidebar", ImVec2(width, height), ImGuiChildFlags_Border,
                      ImGuiWindowFlags_HorizontalScrollbar);

    // "Open Files" entry for tabs not associated with any folder workspace
    // (initial scratch tab, or files opened individually outside any root).
    {
        bool isActive = (activeIdx_ == -1);
        if (isActive) {
            ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
            activeColor.w = 0.8f;
            ImGui::PushStyleColor(ImGuiCol_Header, activeColor);
        }
        ImGui::CollapsingHeader("Open Files",
                                ImGuiTreeNodeFlags_Leaf
                              | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        if (isActive)
            ImGui::PopStyleColor();
        if (ImGui::IsItemClicked())
            setActive(-1);
    }

    for (int i = 0; i < (int)workspaces_.size(); ++i) {
        auto& ws = *workspaces_[i];
        bool isActive = (i == activeIdx_);

        ImGui::PushID(i);

        // Highlight active workspace header
        if (isActive) {
            ImVec4 activeColor = ImGui::GetStyleColorVec4(ImGuiCol_Header);
            activeColor.w = 0.8f;
            ImGui::PushStyleColor(ImGuiCol_Header, activeColor);
        }

        bool open = ImGui::CollapsingHeader(ws.name.c_str(),
                                            ImGuiTreeNodeFlags_DefaultOpen);

        if (isActive)
            ImGui::PopStyleColor();

        // Click header to make this workspace active
        if (ImGui::IsItemClicked())
            setActive(i);

        if (open) {
            loadChildren(ws.root);
            for (auto& child : ws.root.children)
                drawTree(child, i);
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Tree drawing (recursive)
// ---------------------------------------------------------------------------

void WorkspacePanel::drawTree(DirEntry& entry, int workspaceIdx) {
    if (entry.isDirectory) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_OpenOnDoubleClick
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;

        bool nodeOpen = ImGui::TreeNodeEx(entry.name.c_str(), flags);

        if (nodeOpen) {
            loadChildren(entry);
            for (auto& child : entry.children)
                drawTree(child, workspaceIdx);
            ImGui::TreePop();
        }
    } else {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                 | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;

        ImGui::TreeNodeEx(entry.name.c_str(), flags);

        if (ImGui::IsItemClicked()) {
            // Route through the shared opener so duplicate-path detection
            // and owner switching stay centralized.
            openFile(entry.fullPath);
        }
    }
}

// ---------------------------------------------------------------------------
// Lazy directory scanning
// ---------------------------------------------------------------------------

void WorkspacePanel::loadChildren(DirEntry& entry) {
    if (entry.childrenLoaded) return;
    entry.childrenLoaded = true;

    std::error_code ec;
    std::vector<DirEntry> dirs, files;

    for (auto& p : fs::directory_iterator(entry.fullPath, ec)) {
        if (ec) break;

        DirEntry child;
        child.name = p.path().filename().string();
        child.fullPath = p.path().string();
        child.isDirectory = p.is_directory(ec);

        // Skip hidden entries
        if (child.name.empty() || child.name[0] == '.') continue;

        if (child.isDirectory)
            dirs.push_back(std::move(child));
        else
            files.push_back(std::move(child));
    }

    auto cmp = [](DirEntry const& a, DirEntry const& b) {
        return a.name < b.name;
    };
    std::sort(dirs.begin(), dirs.end(), cmp);
    std::sort(files.begin(), files.end(), cmp);

    // Directories first, then files
    entry.children = std::move(dirs);
    entry.children.insert(entry.children.end(),
        std::make_move_iterator(files.begin()),
        std::make_move_iterator(files.end()));
}
