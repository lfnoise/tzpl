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

class EditorPanel {
public:
    EditorPanel();

    // Draw the editor panel (tab bar + text editor).
    void draw(float width, float height, GuiState& state);

    // Tab management
    void newTab(const std::string& name = "untitled.x");
    void openFile(const std::string& path);
    void closeTab(int index);
    void closeActiveTab();
    int tabCount() const { return (int)tabs_.size(); }
    int activeTabIndex() const { return activeTab_; }

    // File operations (return true on success)
    bool save();                                   // save active tab
    bool saveAs(const std::string& path);          // save active tab to path
    bool saveCopy(const std::string& path);        // save copy without changing tab's path
    bool hasFilePath() const;                      // active tab has a file path
    std::string activeFilePath() const;
    std::string activeTabName() const;

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

    // Cursor movement (for shortcuts handled outside ImGui)
    void moveHome(bool select);
    void moveEnd(bool select);
    void moveTop(bool select);
    void moveBottom(bool select);

    // Error markers
    void setErrorMarkers(const TextEditor::ErrorMarkers& markers);
    void clearErrorMarkers();

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
    };

    std::vector<Tab> tabs_;
    int activeTab_ = 0;
    int prevActiveTab_ = -1;
    bool needsFocus_ = false;
    TextEditor::LanguageDefinition langDef_;
    FindReplaceState findReplace_;
};

#endif /* editor_panel_hpp */
