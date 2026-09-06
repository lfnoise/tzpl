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

#include "tzpl_dynlib.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>

namespace tzpl {

static thread_local std::string tlsLastError;

static std::wstring toWide(char const* utf8) {
    if (!utf8 || !*utf8) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(n > 0 ? (size_t)n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
    return w;
}

static std::string lastErrorText() {
    DWORD code = GetLastError();
    LPWSTR buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                 | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, (LPWSTR)&buf, 0, nullptr);
    std::string out = "error " + std::to_string(code);
    if (n && buf) {
        int len = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
        std::string msg((size_t)len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, msg.data(), len, nullptr, nullptr);
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' '))
            msg.pop_back();
        out += ": " + msg;
        LocalFree(buf);
    }
    return out;
}

static DynLib openImpl(char const* path) {
    std::wstring wpath = std::filesystem::absolute(toWide(path)).wstring();
    HMODULE h = LoadLibraryExW(wpath.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                                   | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) tlsLastError = lastErrorText();
    return (DynLib)h;
}

DynLib dynlibOpen(char const* path)      { return openImpl(path); }
DynLib dynlibOpenLocal(char const* path) { return openImpl(path); }  // DLL symbols are always local

void* dynlibSym(DynLib lib, char const* name) {
    if (!lib) return nullptr;
    void* p = (void*)GetProcAddress((HMODULE)lib, name);
    if (!p) tlsLastError = lastErrorText();
    return p;
}

void dynlibClose(DynLib lib) {
    if (lib) FreeLibrary((HMODULE)lib);
}

std::string dynlibError() { return tlsLastError; }

DynLib dynlibRetainFromAddress(void const* addr) {
    HMODULE h = nullptr;
    // Without GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT this increments the
    // module's reference count, exactly the retain we want.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (LPCWSTR)addr, &h)) {
        tlsLastError = lastErrorText();
        return nullptr;
    }
    return (DynLib)h;
}

} // namespace tzpl

#else // POSIX

#include <dlfcn.h>

namespace tzpl {

DynLib dynlibOpen(char const* path)      { return dlopen(path, RTLD_NOW); }
DynLib dynlibOpenLocal(char const* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }

void* dynlibSym(DynLib lib, char const* name) {
    return lib ? dlsym(lib, name) : nullptr;
}

void dynlibClose(DynLib lib) {
    if (lib) dlclose(lib);
}

std::string dynlibError() {
    char const* e = dlerror();
    return e ? e : "";
}

DynLib dynlibRetainFromAddress(void const* addr) {
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_fname) {
        return dlopen(info.dli_fname, RTLD_NOW);
    }
    return nullptr;
}

} // namespace tzpl

#endif
