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

#include "tzpl_paths.hpp"

#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace tzpl {

std::vector<std::string> splitPathList(std::string_view list) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start < list.size()) {
        size_t end = list.find(kPathListSep, start);
        if (end == std::string_view::npos) end = list.size();
        if (end > start) result.emplace_back(list.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

static fs::path envPath(char const* name) {
    char const* v = std::getenv(name);
    return (v && *v) ? fs::path(v) : fs::path{};
}

fs::path executablePath() {
    std::error_code ec;
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);  // truncated: grow and retry
    }
    fs::path exe = fs::canonical(fs::path(buf), ec);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));
    fs::path exe = fs::canonical(buf, ec);
#else
    fs::path exe = fs::canonical("/proc/self/exe", ec);
#endif
    return ec ? fs::path{} : exe;
}

fs::path distRoot() {
    fs::path exe = executablePath();
    if (exe.empty()) return {};
    std::error_code ec;
    fs::path dir = exe.parent_path();
    for (int depth = 0;
         depth < 6 && !dir.empty() && dir != dir.root_path();
         ++depth, dir = dir.parent_path()) {
        if (fs::is_directory(dir / "modules", ec)) return dir;
    }
    return {};
}

fs::path homeDir() {
#if defined(_WIN32)
    if (auto p = envPath("USERPROFILE"); !p.empty()) return p;
    auto drive = envPath("HOMEDRIVE"), path = envPath("HOMEPATH");
    if (!drive.empty() && !path.empty()) return drive / path;
    return {};
#else
    return envPath("HOME");
#endif
}

fs::path userConfigDir() {
#if defined(_WIN32)
    if (auto p = envPath("APPDATA"); !p.empty()) return p / "Tzopilotl";
    return {};
#elif defined(__APPLE__)
    if (auto h = homeDir(); !h.empty())
        return h / "Library" / "Application Support" / "Tzopilotl";
    return {};
#else
    if (auto p = envPath("XDG_CONFIG_HOME"); !p.empty()) return p / "tzpl";
    if (auto h = homeDir(); !h.empty()) return h / ".config" / "tzpl";
    return {};
#endif
}

fs::path defaultBuildDir() {
#if defined(_WIN32)
    if (auto p = envPath("LOCALAPPDATA"); !p.empty()) return p / "tzpl-build";
#else
    if (auto h = homeDir(); !h.empty()) return h / "tzpl-build";
#endif
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec || tmp.empty()) tmp = "/tmp";
    return tmp / "tzpl-build";
}

} // namespace tzpl
