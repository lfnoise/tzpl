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

namespace ts { class REPLSession; struct NRTVM; }

class OutputPanel {
public:
    OutputPanel();

    // Draw the output panel (scrolling output + REPL input line).
    // session and nrtvm are used for evaluating REPL input.
    void draw(float width, float height, OutputBuffer& output,
              ts::REPLSession* session, ts::NRTVM* nrtvm);

private:
    static int inputCallback(struct ImGuiInputTextCallbackData* data);

    char inputBuf_[4096] = {};
    std::vector<std::string> history_;
    int historyIdx_ = -1;
    bool scrollToBottom_ = false;
    bool reclaimFocus_ = false;
};

#endif /* output_panel_hpp */
