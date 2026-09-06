// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "tzpl_process.hpp"

#include <cstring>

namespace tzpl {

// Deliver complete lines from a byte stream; flushLast() emits any tail.
namespace {
struct LineSplitter {
    std::function<void(std::string_view)> const& onLine;
    std::string pending;
    explicit LineSplitter(std::function<void(std::string_view)> const& f) : onLine(f) {}
    void feed(char const* data, size_t n) {
        pending.append(data, n);
        size_t start = 0;
        for (;;) {
            size_t nl = pending.find('\n', start);
            if (nl == std::string::npos) break;
            size_t end = nl;
            if (end > start && pending[end - 1] == '\r') --end;
            onLine(std::string_view(pending).substr(start, end - start));
            start = nl + 1;
        }
        pending.erase(0, start);
    }
    void flushLast() {
        if (!pending.empty()) { onLine(pending); pending.clear(); }
    }
};
} // namespace

std::string commandLineForDisplay(std::vector<std::string> const& argv) {
    std::string s;
    for (auto const& a : argv) {
        if (!s.empty()) s += ' ';
        bool needsQuote = a.empty() || a.find_first_of(" \t\"'") != std::string::npos;
        if (needsQuote) { s += '"'; s += a; s += '"'; } else s += a;
    }
    return s;
}

} // namespace tzpl

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace tzpl {

static std::wstring utf8ToWide(std::string const& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Quote one argument the way CommandLineToArgvW / the CRT expect
// (backslashes only special before a quote).
static void appendQuoted(std::wstring& cmd, std::wstring const& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += L'"';
        } else {
            cmd.append(backslashes, L'\\');
            cmd += c;
        }
        backslashes = 0;
    }
    cmd.append(backslashes * 2, L'\\');
    cmd += L'"';
}

int runProcess(std::vector<std::string> const& argv,
               std::function<void(std::string_view)> const& onLine) {
    if (argv.empty()) return -1;

    std::wstring cmd;
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) cmd += L' ';
        appendQuoted(cmd, utf8ToWide(argv[i]));
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        onLine("runProcess: CreatePipe failed");
        return -1;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    PROCESS_INFORMATION pi{};

    // lpApplicationName stays null so argv[0] is searched on PATH like a
    // shell would; CREATE_NO_WINDOW keeps a console from flashing up when
    // the caller is a GUI app.
    std::wstring mutableCmd = cmd;
    BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writeEnd);
    if (!ok) {
        DWORD code = GetLastError();
        CloseHandle(readEnd);
        onLine("runProcess: cannot start '" + argv[0] + "' (error " + std::to_string(code) + ")");
        return -1;
    }

    LineSplitter lines(onLine);
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(readEnd, buf, sizeof buf, &got, nullptr) && got > 0) {
        lines.feed(buf, got);
    }
    lines.flushLast();
    CloseHandle(readEnd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exitCode;
}

} // namespace tzpl

#else // POSIX

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>

extern char** environ;

namespace tzpl {

int runProcess(std::vector<std::string> const& argv,
               std::function<void(std::string_view)> const& onLine) {
    if (argv.empty()) return -1;

    int fds[2];
    if (pipe(fds) != 0) {
        onLine(std::string("runProcess: pipe failed: ") + std::strerror(errno));
        return -1;
    }

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto const& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, cargv[0], &actions, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        onLine("runProcess: cannot start '" + argv[0] + "': " + std::strerror(rc));
        return -1;
    }

    LineSplitter lines(onLine);
    char buf[4096];
    for (;;) {
        ssize_t n = read(fds[0], buf, sizeof buf);
        if (n > 0) { lines.feed(buf, (size_t)n); continue; }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    lines.flushLast();
    close(fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

} // namespace tzpl

#endif
