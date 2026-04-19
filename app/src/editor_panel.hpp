// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef editor_panel_hpp
#define editor_panel_hpp

#include "TextEditor.h"
#include "gui_state.hpp"
#include "find_replace.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <filesystem>
#include <chrono>

class EditorPanel {
public:
    EditorPanel();

    // Draw the editor panel (tab bar + text editor).
    void draw(float width, float height, GuiState& state);

    // Tab management. New tabs inherit the current active owner so they
    // appear in the currently-visible workspace view.
    void newTab(const std::string& name = "untitled.x");
    // Opens a file under the given owner index. If the file is already open
    // under any owner, this is a no-op beyond focusing the existing tab --
    // the caller must call setActiveOwner() to surface it if needed.
    void openFile(const std::string& path, int ownerIdx);
    void closeTab(int index);
    void closeActiveTab();
    int tabCount() const { return (int)tabs_.size(); }
    int activeTabIndex() const { return activeTab_; }

    // Owner-view control: -1 = "Open Files" (no workspace), else workspace
    // index. Only tabs matching the active owner are drawn in the tab bar.
    // Switching owners restores the previously-selected tab for that owner.
    int activeOwner() const { return activeOwner_; }
    void setActiveOwner(int ownerIdx);

    // Recompute each tab's owner using the supplied function (typically
    // longest-prefix match against workspace roots). Call after the set of
    // workspaces changes.
    void reassignOwnership(std::function<int(std::string const&)> computeOwner);

    // Look up the owner of the tab whose filePath == path, across all owners.
    std::optional<int> findTabOwner(const std::string& path) const;

    // File operations (return true on success)
    bool save();                                   // save active tab
    bool saveAs(const std::string& path);          // save active tab to path
    bool saveCopy(const std::string& path);        // save copy without changing tab's path
    bool saveTab(int idx);                         // save tab at idx to its own path
    bool saveTabAs(int idx, const std::string& path);
    bool hasFilePath() const;                      // active tab has a file path
    std::string activeFilePath() const;
    std::string activeTabName() const;

    // Pending close confirmation: closeActiveTab() or a click on the tab
    // X button sets this when the tab is modified, instead of closing.
    // The app layer resolves it by calling saveTab/closeTab/clearPendingClose.
    int pendingCloseIndex() const { return pendingCloseTab_; }
    std::string pendingCloseName() const;
    bool pendingCloseHasPath() const;
    void clearPendingClose() { pendingCloseTab_ = -1; }

    // Switch to an already-open file by path. Returns true if found.
    bool switchToFile(const std::string& path);

    // Text access for evaluation
    std::string getSelectedText() const;
    std::string getCurrentLineText() const;
    std::string getAllText() const;
    TextEditor::Coordinates getCursorPosition() const;

    // Get the text of the current block (contiguous non-empty lines around cursor)
    std::string getCurrentBlockText(int& outStartLine, int& outEndLine) const;

    // Edit operations (delegate to active tab's TextEditor)
    void cut();
    void copy();
    void paste();
    void undo();
    void redo();
    void selectAll();
    void toggleComment();
    void indent();
    void outdent();

    // Cursor movement (for shortcuts handled outside ImGui)
    void moveHome(bool select);
    void moveEnd(bool select);
    void moveTop(bool select);
    void moveBottom(bool select);

    // Error markers
    void setErrorMarkers(const TextEditor::ErrorMarkers& markers);
    void clearErrorMarkers();

    // Unsaved changes
    bool hasUnsavedChanges() const;
    std::vector<std::string> unsavedFileNames() const;
    int saveAll();  // save all modified tabs that have paths; returns count saved

    // External file change detection
    void checkForExternalChanges();

    // Find/Replace
    FindReplaceState& findReplace() { return findReplace_; }
    TextEditor* activeEditor();
    void updateSearchHighlights();

private:
    static TextEditor::LanguageDefinition createTzopilotlDef();

    struct Tab {
        std::string name;
        std::string filePath;
        TextEditor editor;
        bool modified = false;
        unsigned id = 0;           // stable ID for ImGui child window (scroll state)
        std::string editorTitle;   // "##editorN" -- cached to avoid re-alloc
        std::filesystem::file_time_type diskWriteTime{}; // last known mod time on disk
        std::string diskContent;   // content at last save/load, for modified detection
        int ownerIdx = -1;         // -1 = "Open Files", else workspace index
    };

    std::vector<Tab> tabs_;
    int activeTab_ = -1;
    int prevActiveTab_ = -1;
    int activeOwner_ = -1;
    // Per-owner memory: tab id that was active when we last left that owner's
    // view. Used to restore selection when the user switches back.
    std::map<int, unsigned> lastActiveIdByOwner_;
    bool needsFocus_ = false;
    int pendingSelectTab_ = -1;    // tab index to force-select on next draw
    unsigned nextTabId_ = 0;
    bool pendingEdit_ = false;     // set by edit ops that run before Render()
    int pendingCloseTab_ = -1;     // tab awaiting save/discard/cancel confirmation
    TextEditor::LanguageDefinition langDef_;
    FindReplaceState findReplace_;
    std::chrono::steady_clock::time_point lastDiskCheck_{};

    int findTabIdxById(unsigned id) const;
    int firstTabOfOwner(int owner) const;
};

#endif /* editor_panel_hpp */
