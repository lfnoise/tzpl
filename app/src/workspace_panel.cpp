// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "workspace_panel.hpp"
#include "imgui.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

WorkspacePanel::WorkspacePanel() = default;

void WorkspacePanel::addWorkspace(const std::string& dirPath) {
    // Avoid duplicates -- if already open, just switch to it
    for (int i = 0; i < (int)workspaces_.size(); ++i) {
        if (workspaces_[i]->rootPath == dirPath) {
            activeIdx_ = i;
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
    activeIdx_ = (int)workspaces_.size() - 1;
}

bool WorkspacePanel::hasUnsavedChanges() const {
    if (defaultEditor_.hasUnsavedChanges()) return true;
    for (auto& ws : workspaces_)
        if (ws->editor.hasUnsavedChanges()) return true;
    return false;
}

std::vector<std::string> WorkspacePanel::unsavedFileNames() const {
    std::vector<std::string> names;
    auto add = [&](EditorPanel const& ep) {
        auto n = ep.unsavedFileNames();
        names.insert(names.end(), n.begin(), n.end());
    };
    add(defaultEditor_);
    for (auto& ws : workspaces_)
        add(ws->editor);
    return names;
}

int WorkspacePanel::saveAll() {
    int total = defaultEditor_.saveAll();
    for (auto& ws : workspaces_)
        total += ws->editor.saveAll();
    return total;
}

EditorPanel* WorkspacePanel::editorWithPendingClose() {
    if (defaultEditor_.pendingCloseIndex() >= 0) return &defaultEditor_;
    for (auto& ws : workspaces_)
        if (ws->editor.pendingCloseIndex() >= 0) return &ws->editor;
    return nullptr;
}

EditorPanel& WorkspacePanel::activeEditor() {
    if (activeIdx_ >= 0 && activeIdx_ < (int)workspaces_.size())
        return workspaces_[activeIdx_]->editor;
    return defaultEditor_;
}

// ---------------------------------------------------------------------------
// Sidebar drawing
// ---------------------------------------------------------------------------

void WorkspacePanel::drawSidebar(float width, float height) {
    ImGui::BeginChild("##Sidebar", ImVec2(width, height), ImGuiChildFlags_Border,
                      ImGuiWindowFlags_HorizontalScrollbar);

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
            activeIdx_ = i;

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
            activeIdx_ = workspaceIdx;
            auto& editor = workspaces_[workspaceIdx]->editor;
            if (!editor.switchToFile(entry.fullPath))
                editor.openFile(entry.fullPath);
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
