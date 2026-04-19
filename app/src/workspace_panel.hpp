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

    // Draw the sidebar (directory trees for all workspaces + "Open Files").
    void drawSidebar(float width, float height);

    // Open an individual file. Routes to the most-specific workspace whose
    // root contains the path (or "Open Files" if none). If the file is
    // already open, switches to its existing workspace view and focuses it.
    void openFile(const std::string& path);

    // Shared editor panel (one global tab store across all workspaces).
    EditorPanel& activeEditor() { return editor_; }
    EditorPanel const& activeEditor() const { return editor_; }

    bool hasWorkspaces() const { return !workspaces_.empty(); }

    // Unsaved changes across all workspaces + "Open Files"
    bool hasUnsavedChanges() const { return editor_.hasUnsavedChanges(); }
    std::vector<std::string> unsavedFileNames() const {
        return editor_.unsavedFileNames();
    }
    int saveAll() { return editor_.saveAll(); }

    // Returns the editor panel awaiting save/discard/cancel confirmation for
    // a modified tab, or nullptr. (Only one editor now, but the API stays.)
    EditorPanel* editorWithPendingClose() {
        return editor_.pendingCloseIndex() >= 0 ? &editor_ : nullptr;
    }

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
    };

    std::vector<std::unique_ptr<Workspace>> workspaces_;
    int activeIdx_ = -1;           // -1 = "Open Files", else workspaces_ index
    EditorPanel editor_;
    float sidebarWidth_ = 220.0f;

    void drawTree(DirEntry& entry, int workspaceIdx);
    static void loadChildren(DirEntry& entry);

    // Longest-prefix match of path against workspace roots. Returns the
    // workspace index whose root most specifically contains path, or -1.
    int computeOwner(std::string const& path) const;

    // Switch activeIdx_ and keep the shared editor's active-owner in sync.
    void setActive(int ownerIdx);
};

#endif /* workspace_panel_hpp */
