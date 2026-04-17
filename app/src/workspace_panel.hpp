// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef workspace_panel_hpp
#define workspace_panel_hpp

#include "editor_panel.hpp"
#include <string>
#include <vector>
#include <memory>

class WorkspacePanel {
public:
    WorkspacePanel();

    // Add a workspace rooted at dirPath. If already open, switches to it.
    void addWorkspace(const std::string& dirPath);

    // Draw the sidebar (directory trees for all workspaces).
    void drawSidebar(float width, float height);

    // Active editor panel (for the currently selected workspace).
    // Returns the default editor if no workspace is selected.
    EditorPanel& activeEditor();

    bool hasWorkspaces() const { return !workspaces_.empty(); }

    // Unsaved changes across all workspaces + default editor
    bool hasUnsavedChanges() const;
    std::vector<std::string> unsavedFileNames() const;
    int saveAll();

    // Returns the editor panel (across all workspaces) that is awaiting
    // save/discard/cancel confirmation for a modified tab, or nullptr.
    EditorPanel* editorWithPendingClose();

    float sidebarWidth() const { return sidebarWidth_; }
    void setSidebarWidth(float w) { sidebarWidth_ = w; }

private:
    struct DirEntry {
        std::string name;
        std::string fullPath;
        bool isDirectory = false;
        bool childrenLoaded = false;
        std::vector<DirEntry> children;
    };

    struct Workspace {
        std::string rootPath;
        std::string name;
        DirEntry root;
        EditorPanel editor;
    };

    std::vector<std::unique_ptr<Workspace>> workspaces_;
    int activeIdx_ = -1;
    EditorPanel defaultEditor_;
    float sidebarWidth_ = 220.0f;

    void drawTree(DirEntry& entry, int workspaceIdx);
    static void loadChildren(DirEntry& entry);
};

#endif /* workspace_panel_hpp */
