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
#include <vector>

class OutputPanel {
public:
    OutputPanel();

    // Draw the output panel (scrolling output + REPL input line).
    void draw(float width, float height, OutputBuffer& output);

    // Pending REPL input (set by draw, consumed by main loop)
    bool hasPendingInput() const { return !pendingInput_.empty(); }
    std::string takePendingInput() { std::string s; s.swap(pendingInput_); return s; }

    // Copy selected output text to clipboard. Returns true if output was focused.
    bool tryCopy();
    // Select all output text. Returns true if output was focused.
    bool trySelectAll();
    // Whether the output text area currently has focus.
    bool hasFocus() const { return outputActive_; }

private:
    static int inputCallback(struct ImGuiInputTextCallbackData* data);
    static int outputScrollCallback(struct ImGuiInputTextCallbackData* data);

    char inputBuf_[4096] = {};
    std::vector<std::string> history_;
    int historyIdx_ = -1;
    bool scrollToBottom_ = false;
    bool reclaimFocus_ = false;
    std::string outputText_;
    size_t lastLineCount_ = 0;
    bool outputActive_ = false;
    bool pendingSelectAll_ = false;
    int selStart_ = 0;
    int selEnd_ = 0;
    std::string pendingInput_;
};

#endif /* output_panel_hpp */
