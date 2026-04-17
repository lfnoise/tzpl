// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef output_panel_hpp
#define output_panel_hpp

#include "gui_state.hpp"
#include <string>

class OutputPanel {
public:
    // Draw the output panel (scrolling output).
    void draw(float width, float height, OutputBuffer& output);

    // Copy selected output text to clipboard. Returns true if output was focused.
    bool tryCopy();
    // Select all output text. Returns true if output was focused.
    bool trySelectAll();
    // Whether the output text area currently has focus.
    bool hasFocus() const { return outputActive_; }

    // Cursor movement (Cmd+Arrow, applied via pending flags in callback)
    void moveHome(bool shift)   { pendingMove_ = Move::Home;   pendingMoveShift_ = shift; }
    void moveEnd(bool shift)    { pendingMove_ = Move::End;    pendingMoveShift_ = shift; }
    void moveTop(bool shift)    { pendingMove_ = Move::Top;    pendingMoveShift_ = shift; }
    void moveBottom(bool shift) { pendingMove_ = Move::Bottom; pendingMoveShift_ = shift; }

    // Clear the output panel.
    void clear(OutputBuffer& output);

private:
    enum class Move : uint8_t { None, Home, End, Top, Bottom };

    static int outputScrollCallback(struct ImGuiInputTextCallbackData* data);

    bool autoScroll_ = true;
    std::string outputText_;
    size_t lastLineCount_ = 0;
    bool outputActive_ = false;
    bool pendingSelectAll_ = false;
    Move pendingMove_ = Move::None;
    bool pendingMoveShift_ = false;
    int selStart_ = 0;
    int selEnd_ = 0;
};

#endif /* output_panel_hpp */
