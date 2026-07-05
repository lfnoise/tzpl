// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "gui_state.hpp"
#include "tzpl_app_context.hpp"
#include "nrt_vm.hpp"
#include "diagnostic.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <pthread.h>
#include <GLFW/glfw3.h>

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

std::vector<std::string> PrintCapture::drainLines() {
    std::vector<std::string> lines;
    if (pipeFds_[0] < 0) return lines;

    char tmp[4096];
    std::string accum;

    for (;;) {
        ssize_t n = read(pipeFds_[0], tmp, sizeof(tmp));
        if (n <= 0) break;
        accum.append(tmp, n);
    }

    if (accum.empty()) return lines;

    size_t start = 0;
    while (start < accum.size()) {
        size_t nl = accum.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(accum.substr(start));
            break;
        }
        lines.push_back(accum.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

bool PrintCapture::hasPending() const {
    if (pipeFds_[0] < 0) return false;
    struct pollfd pfd = {pipeFds_[0], POLLIN, 0};
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
}

void PrintCapture::drain(OutputBuffer& buf) {
    for (auto& line : drainLines()) {
        buf.append(line, LineKind::Output);
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

// ---------------------------------------------------------------------------
// AsyncEval
// ---------------------------------------------------------------------------

AsyncEval::~AsyncEval() {
    if (threadActive_) pthread_join(thread_, nullptr);
}

// Trampoline for pthread_create
struct EvalArgs {
    AsyncEval* self;
    bridge::AppContext* ctx;
    ts::REPLSession* session;
};

static void* evalThreadFunc(void* arg) {
    auto* ea = static_cast<EvalArgs*>(arg);
    {
        std::lock_guard<std::mutex> lock(ea->ctx->nrtvm->mtx);
        ea->ctx->nrtvm->vm.makeCurrent();
        ea->self->result = ea->session->eval(ea->self->code);
        ea->ctx->nrtvm->vm.gcHeartbeat();
    }
    ea->self->running.store(false);
    glfwPostEmptyEvent();  // wake main loop
    delete ea;
    return nullptr;
}

void AsyncEval::launch(const std::string& src, bridge::AppContext& ctx,
                       ts::REPLSession& session, int fs, int fe,
                       std::uint64_t cell) {
    if (running.load()) return;
    if (threadActive_) { pthread_join(thread_, nullptr); threadActive_ = false; }

    code = src;
    flashStart = fs;
    flashEnd = fe;
    cellId = cell;
    running.store(true);

    auto* args = new EvalArgs{this, &ctx, &session};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);  // 8MB stack
    pthread_create(&thread_, &attr, evalThreadFunc, args);
    pthread_attr_destroy(&attr);
    threadActive_ = true;
}

void AsyncEval::join() {
    if (running.load() || !threadActive_) return;
    pthread_join(thread_, nullptr);
    threadActive_ = false;
}

bool AsyncEval::collect(GuiState& state) {
    if (running.load() || !threadActive_) return false;
    pthread_join(thread_, nullptr);
    threadActive_ = false;

    // Separator between evaluations
    if (!state.output.lines().empty())
        state.output.append("", LineKind::Separator);

    if (!result.errors.empty()) {
        auto formatted = ts::formatErrorsPlain(result.errors, code, "<editor>");
        for (auto& line : formatted) {
            state.output.append(line, LineKind::Error);
        }
    } else if (result.hasValue) {
        state.output.append("\xe2\x86\x92 " + result.formattedValue
                            + " : " + result.typeName, LineKind::Result);
    }

    if (flashStart >= 0 && flashEnd >= 0) {
        state.flash.trigger(flashStart, flashEnd);
    }

    return true;
}
