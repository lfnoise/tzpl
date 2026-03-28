// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "gui_state.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

// ---------------------------------------------------------------------------
// OutputBuffer
// ---------------------------------------------------------------------------

void OutputBuffer::append(const std::string& text, LineKind kind) {
    std::lock_guard<std::mutex> lock(mtx_);
    pending_.push_back({text, kind});
    hasNew_ = true;
}

void OutputBuffer::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    lines_.clear();
    pending_.clear();
    hasNew_ = false;
}

std::vector<OutputLine> OutputBuffer::drain() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (pending_.empty()) return {};
    std::vector<OutputLine> result;
    result.swap(pending_);
    for (auto& line : result) {
        lines_.push_back(line);
    }
    hasNew_ = false;
    return result;
}

// ---------------------------------------------------------------------------
// PrintCapture
// ---------------------------------------------------------------------------

PrintCapture::PrintCapture() {
    if (pipe(pipeFds_) == 0) {
        // Make read end non-blocking
        fcntl(pipeFds_[0], F_SETFL, O_NONBLOCK);
        // Create FILE* for the write end (unbuffered for immediate output)
        writeFile_ = fdopen(pipeFds_[1], "w");
        if (writeFile_) {
            setvbuf(writeFile_, nullptr, _IONBF, 0);
        }
    }
}

PrintCapture::~PrintCapture() {
    if (writeFile_) fclose(writeFile_); // also closes pipeFds_[1]
    if (pipeFds_[0] >= 0) close(pipeFds_[0]);
}

void PrintCapture::drain(OutputBuffer& buf) {
    if (pipeFds_[0] < 0) return;

    char tmp[4096];
    std::string accum;

    for (;;) {
        ssize_t n = read(pipeFds_[0], tmp, sizeof(tmp));
        if (n <= 0) break;
        accum.append(tmp, n);
    }

    if (accum.empty()) return;

    // Split by newlines and append each line
    size_t start = 0;
    while (start < accum.size()) {
        size_t nl = accum.find('\n', start);
        if (nl == std::string::npos) {
            buf.append(accum.substr(start), LineKind::Output);
            break;
        }
        buf.append(accum.substr(start, nl - start), LineKind::Output);
        start = nl + 1;
    }
}

// ---------------------------------------------------------------------------
// EvalFlash
// ---------------------------------------------------------------------------

void EvalFlash::trigger(int start, int end) {
    startLine = start;
    endLine = end;
    alpha = 1.0f;
}

void EvalFlash::update(float deltaTime) {
    if (alpha > 0.0f) {
        alpha -= deltaTime * 3.0f; // fade over ~0.33s
        if (alpha < 0.0f) alpha = 0.0f;
    }
}
