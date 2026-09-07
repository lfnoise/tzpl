// Tzopilotl
// Copyright (C) 2026 James McCartney

#include "gui_state.hpp"
#include "tzpl_app_context.hpp"
#include "nrt_vm.hpp"
#include "diagnostic.hpp"
#include <algorithm>
#include <cstring>
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <io.h>
  #include <fcntl.h>
  #include <process.h>
#else
  #include <unistd.h>
  #include <fcntl.h>
  #include <poll.h>
  #include <pthread.h>
#endif

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

// The VM prints through a FILE* on the write end of a pipe; the GUI thread
// drains the read end without blocking. POSIX: O_NONBLOCK + poll. Windows:
// an anonymous pipe cannot be polled or made non-blocking, so the read side
// asks PeekNamedPipe how much is waiting and reads exactly that.
#ifdef _WIN32
static DWORD pendingBytes(int fd) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    DWORD avail = 0;
    if (h == INVALID_HANDLE_VALUE || !PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr))
        return 0;
    return avail;
}
#endif

PrintCapture::PrintCapture() {
#ifdef _WIN32
    if (_pipe(pipeFds_, 1 << 16, _O_BINARY | _O_NOINHERIT) == 0) {
        writeFile_ = _fdopen(pipeFds_[1], "wb");
#else
    if (pipe(pipeFds_) == 0) {
        // Make read end non-blocking
        fcntl(pipeFds_[0], F_SETFL, O_NONBLOCK);
        // Create FILE* for the write end (unbuffered for immediate output)
        writeFile_ = fdopen(pipeFds_[1], "w");
#endif
        if (writeFile_) {
            setvbuf(writeFile_, nullptr, _IONBF, 0);
        }
    }
}

PrintCapture::~PrintCapture() {
    if (writeFile_) fclose(writeFile_); // also closes pipeFds_[1]
#ifdef _WIN32
    if (pipeFds_[0] >= 0) _close(pipeFds_[0]);
#else
    if (pipeFds_[0] >= 0) close(pipeFds_[0]);
#endif
}

std::vector<std::string> PrintCapture::drainLines() {
    std::vector<std::string> lines;
    if (pipeFds_[0] < 0) return lines;

    char tmp[4096];
    std::string accum;

    for (;;) {
#ifdef _WIN32
        DWORD avail = pendingBytes(pipeFds_[0]);
        if (avail == 0) break;
        int n = _read(pipeFds_[0], tmp, (unsigned)std::min<DWORD>(avail, sizeof(tmp)));
#else
        ssize_t n = read(pipeFds_[0], tmp, sizeof(tmp));
#endif
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
#ifdef _WIN32
    return pendingBytes(pipeFds_[0]) > 0;
#else
    struct pollfd pfd = {pipeFds_[0], POLLIN, 0};
    return poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN);
#endif
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

// The evaluation thread gets an 8 MB stack: the compiler and VM recurse
// deeply, and std::thread cannot size a stack, hence pthread_create /
// _beginthreadex directly.
static constexpr size_t kEvalStackBytes = 8 * 1024 * 1024;

#ifdef _WIN32
static void joinThread(std::uintptr_t& h) {
    WaitForSingleObject((HANDLE)h, INFINITE);
    CloseHandle((HANDLE)h);
    h = 0;
}
#else
static void joinThread(pthread_t& t) { pthread_join(t, nullptr); }
#endif

AsyncEval::~AsyncEval() {
    if (threadActive_) joinThread(thread_);
}

// Trampoline for the thread entry point
struct EvalArgs {
    AsyncEval* self;
    bridge::AppContext* ctx;
    ts::REPLSession* session;
};

static void evalThreadBody(EvalArgs* ea) {
    {
        std::lock_guard<std::mutex> lock(ea->ctx->nrtvm->mtx);
        ea->ctx->nrtvm->vm.makeCurrent();
        ea->self->result = ea->session->eval(ea->self->code);
        ea->ctx->nrtvm->vm.gcHeartbeat();
    }
    ea->self->running.store(false);
    if (ea->self->onFinished) ea->self->onFinished(); // wake the GUI loop
    delete ea;
}

#ifdef _WIN32
static unsigned __stdcall evalThreadFunc(void* arg) {
    evalThreadBody(static_cast<EvalArgs*>(arg));
    return 0;
}
#else
static void* evalThreadFunc(void* arg) {
    evalThreadBody(static_cast<EvalArgs*>(arg));
    return nullptr;
}
#endif

void AsyncEval::launch(const std::string& src, bridge::AppContext& ctx,
                       ts::REPLSession& session, int fs, int fe,
                       std::uint64_t cell) {
    if (running.load()) return;
    if (threadActive_) { joinThread(thread_); threadActive_ = false; }

    code = src;
    flashStart = fs;
    flashEnd = fe;
    cellId = cell;
    running.store(true);

    auto* args = new EvalArgs{this, &ctx, &session};
#ifdef _WIN32
    thread_ = _beginthreadex(nullptr, (unsigned)kEvalStackBytes, evalThreadFunc, args, 0, nullptr);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kEvalStackBytes);
    pthread_create(&thread_, &attr, evalThreadFunc, args);
    pthread_attr_destroy(&attr);
#endif
    threadActive_ = true;
}

void AsyncEval::join() {
    if (running.load() || !threadActive_) return;
    joinThread(thread_);
    threadActive_ = false;
}

bool AsyncEval::collect(GuiState& state) {
    if (running.load() || !threadActive_) return false;
    joinThread(thread_);
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
        state.output.append("\xe2\x86\x92 " + result.prettyValue
                            + " : " + result.typeName, LineKind::Result);
    }

    if (flashStart >= 0 && flashEnd >= 0) {
        state.flash.trigger(flashStart, flashEnd);
    }

    return true;
}
