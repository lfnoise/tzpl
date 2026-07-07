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
#include <cstdint>
#include <cstdio>
#include <functional>
#include <pthread.h>

#include "repl_session.hpp"

namespace bridge { struct AppContext; }

// ---------------------------------------------------------------------------
// Output line kinds for color coding
// ---------------------------------------------------------------------------

enum class LineKind { Output, Result, Error, Info, Separator };

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

    // Non-blocking read of pending print output as raw lines, so the
    // caller can attribute them (e.g. to the notebook cell whose eval is
    // in flight) instead of the global console.
    std::vector<std::string> drainLines();

    // True while captured output is waiting in the pipe. The frame loop
    // checks this before its idle wait: lines printed late in a frame
    // (e.g. by a widget callback in dispatch) are drained at the top of
    // the NEXT frame, which must not be half a second away.
    bool hasPending() const;

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
    pthread_t thread_{};
    bool threadActive_ = false;
    std::atomic<bool> running{false};

    // Result (written by worker thread, read by main thread after running==false)
    ts::REPLSession::EvalResult result;
    std::string code;
    int flashStart = -1;
    int flashEnd = -1;

    // Notebook cell this eval belongs to (0 = plain editor eval). Only one
    // eval is in flight at a time, so prints drained while busy() can be
    // attributed to this cell unambiguously.
    std::uint64_t cellId = 0;

    // Called from the WORKER thread right after `running` flips false, so
    // the GUI can wake up and collect the result. The ImGui app posts an
    // empty GLFW event; the JUCE app posts a MessageManager callback.
    std::function<void()> onFinished;

    bool busy() const { return running.load(); }

    // Launch eval on a background thread. Ignored if already running.
    void launch(const std::string& code, bridge::AppContext& ctx,
                ts::REPLSession& session, int flashStart, int flashEnd,
                std::uint64_t cellId = 0);

    // True once a launched eval has finished and its result is ready to take.
    bool finished() const { return threadActive_ && !running.load(); }

    // Join the worker thread after finished(); result/code/cellId are then
    // valid until the next launch.
    void join();

    // Editor-eval path: join and format result/errors into the console
    // output + trigger the eval flash. Returns true if collected.
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
    // Notebook view: the console is a right-hand column instead of a
    // bottom strip (the cell strip needs the vertical space).
    float notebookSplitRatio = 0.72f; // notebook takes 72% of the width

    // Eval request flags (set by GLFW key callback, consumed by main loop)
    bool evalSelection = false; // Cmd+Enter
    bool evalLine = false;      // Shift+Enter
    bool evalFile = false;      // Cmd+Shift+Enter
};

#endif /* gui_state_hpp */
