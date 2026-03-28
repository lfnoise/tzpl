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
    int tabCount() const { return (int)tabs_.size(); }

    // Text access for evaluation
    std::string getSelectedText() const;
    std::string getCurrentLineText() const;
    std::string getAllText() const;
    TextEditor::Coordinates getCursorPosition() const;

    // Get the text of the current block (contiguous non-empty lines around cursor)
    std::string getCurrentBlockText(int& outStartLine, int& outEndLine) const;

    // Error markers
    void setErrorMarkers(const TextEditor::ErrorMarkers& markers);
    void clearErrorMarkers();

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
    TextEditor::LanguageDefinition langDef_;
};

#endif /* editor_panel_hpp */
