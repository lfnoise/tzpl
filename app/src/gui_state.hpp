// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef gui_state_hpp
#define gui_state_hpp

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdio>

#include "repl_session.hpp"

namespace bridge { struct AppContext; }

// ---------------------------------------------------------------------------
// Output line kinds for color coding
// ---------------------------------------------------------------------------

enum class LineKind { Output, Result, Error, Info };

struct OutputLine {
    std::string text;
    LineKind kind;
};

// ---------------------------------------------------------------------------
// Thread-safe output buffer for captured VM print output and REPL results
// ---------------------------------------------------------------------------

struct OutputBuffer {
    void append(const std::string& text, LineKind kind);
    void clear();

    // Returns a snapshot of new lines since last call, clearing the pending flag.
    // Call from the GUI thread each frame.
    std::vector<OutputLine> drain();

    // Access all lines (call from GUI thread only after drain).
    const std::vector<OutputLine>& lines() const { return lines_; }
    bool hasNew() const { return hasNew_; }

private:
    std::mutex mtx_;
    std::vector<OutputLine> lines_;
    std::vector<OutputLine> pending_;
    bool hasNew_ = false;
};

// ---------------------------------------------------------------------------
// Print output capture via pipe
// ---------------------------------------------------------------------------

struct PrintCapture {
    PrintCapture();
    ~PrintCapture();

    // Redirect a VM's print output to the capture pipe.
    // Call before entering the main loop.
    FILE* captureFile() const { return writeFile_; }

    // Non-blocking read of any pending print output into the buffer.
    // Call once per frame from the GUI thread.
    void drain(OutputBuffer& buf);

private:
    int pipeFds_[2] = {-1, -1};
    FILE* writeFile_ = nullptr;
};

// ---------------------------------------------------------------------------
// Eval flash state (brief highlight on evaluated lines)
// ---------------------------------------------------------------------------

struct EvalFlash {
    int startLine = -1;
    int endLine = -1;
    float alpha = 0.0f;

    void trigger(int start, int end);
    void update(float deltaTime);
    bool active() const { return alpha > 0.01f; }
};

// ---------------------------------------------------------------------------
// Async evaluation (runs on a background thread)
// ---------------------------------------------------------------------------

struct GuiState;  // forward

struct AsyncEval {
    std::thread thread;
    std::atomic<bool> running{false};

    // Result (written by worker thread, read by main thread after running==false)
    ts::REPLSession::EvalResult result;
    std::string code;
    int flashStart = -1;
    int flashEnd = -1;

    bool busy() const { return running.load(); }

    // Launch eval on a background thread. Ignored if already running.
    void launch(const std::string& code, bridge::AppContext& ctx,
                ts::REPLSession& session, int flashStart, int flashEnd);

    // If eval finished, join thread and process result into output. Returns true if collected.
    bool collect(GuiState& state);

    ~AsyncEval();
};

// ---------------------------------------------------------------------------
// Shared GUI state
// ---------------------------------------------------------------------------

struct GuiState {
    OutputBuffer output;
    PrintCapture printCapture;
    EvalFlash flash;
    AsyncEval asyncEval;
    float splitRatio = 0.65f; // editor takes 65% of window height

    // Eval request flags (set by GLFW key callback, consumed by main loop)
    bool evalSelection = false; // Cmd+Enter
    bool evalLine = false;      // Shift+Enter
    bool evalFile = false;      // Cmd+Shift+Enter
};

#endif /* gui_state_hpp */
