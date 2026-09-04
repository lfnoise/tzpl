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

//
//  builtins_io.cpp
//  lang
//
//  Non-real-time file and OS builtins: whole-file read/write, directory
//  listing, environment access. Every builtin here performs blocking
//  syscalls and registers with rtSafe=false, so the type checker rejects
//  calls from --rt / RT-restricted contexts. Fallible operations return
//  Option<T> (None on any OS error); mutating operations return Bool.
//
//  Whole-file granularity only -- no streaming file handles. A handle type
//  would need GC-finalizer lifetime rules; readFileBytes + the Bytes
//  accessors (u8At / f64At / utf8At) cover structured binary reads.
//

#include "builtins_internal.hpp"
#include "async_io.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ts {

// Script arguments (everything after the script filename on the CLI),
// stashed by the host before the VM runs. System-allocated: set once at
// startup, read-only afterwards.
static std::vector<std::string> g_programArgs;

void setProgramArgs(std::vector<std::string> args) {
    g_programArgs = std::move(args);
}

// ---------------------------------------------------------------------------
// Result helpers
// ---------------------------------------------------------------------------

static StringObj* makeString(const char* data, size_t n) {
    auto* s = new StringObj();
    s->s.assign(data, n);
    registerNewObj(s);
    return s;
}

static const VMString& argString(VM& vm, u16 r) {
    return static_cast<StringObj*>(vm.reg(r).o)->s;
}

// ---------------------------------------------------------------------------
// Whole-file reads
// ---------------------------------------------------------------------------

// Read the entire file at path into out. False on any error.
// Takes a plain C string so the async worker thread can call it on a
// system-allocated copy of the path (never a VM-heap VMString).
static bool readWholeFile(const char* path, std::string& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    bool ok = false;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        long size = std::ftell(f);
        if (size >= 0 && std::fseek(f, 0, SEEK_SET) == 0) {
            out.resize((size_t)size);
            ok = size == 0 || std::fread(out.data(), 1, (size_t)size, f) == (size_t)size;
        }
    }
    std::fclose(f);
    return ok;
}

// readFile(path String) Option<String>
static void builtin_read_file(VM& vm, u16 dst, u16, u16 ab) {
    std::string data;
    vm.reg(dst).o = readWholeFile(argString(vm, ab).c_str(), data)
                  ? makeString(data.data(), data.size()) : nullptr;
}

// readFileBytes(path String) Option<Bytes>
static void builtin_read_file_bytes(VM& vm, u16 dst, u16, u16 ab) {
    std::string data;
    if (readWholeFile(argString(vm, ab).c_str(), data)) {
        vm.reg(dst).o = new BytesObj(reinterpret_cast<const u8*>(data.data()),
                                     data.size());
    } else {
        vm.reg(dst).o = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Whole-file writes
// ---------------------------------------------------------------------------

static bool writeWholeFile(const char* path, const char* data, size_t n,
                           const char* mode) {
    FILE* f = std::fopen(path, mode);
    if (!f) return false;
    bool ok = n == 0 || std::fwrite(data, 1, n, f) == n;
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

// writeFile(path String, s String) Bool
static void builtin_write_file_string(VM& vm, u16 dst, u16, u16 ab) {
    const VMString& s = argString(vm, (u16)(ab + 1));
    vm.reg(dst).i = writeWholeFile(argString(vm, ab).c_str(), s.data(), s.size(), "wb");
}

// writeFile(path String, b Bytes) Bool
static void builtin_write_file_bytes(VM& vm, u16 dst, u16, u16 ab) {
    auto* b = static_cast<BytesObj*>(vm.reg((u16)(ab + 1)).o);
    vm.reg(dst).i = writeWholeFile(argString(vm, ab).c_str(),
        reinterpret_cast<const char*>(b->data.data()), b->data.size(), "wb");
}

// appendFile(path String, s String) Bool
static void builtin_append_file_string(VM& vm, u16 dst, u16, u16 ab) {
    const VMString& s = argString(vm, (u16)(ab + 1));
    vm.reg(dst).i = writeWholeFile(argString(vm, ab).c_str(), s.data(), s.size(), "ab");
}

// appendFile(path String, b Bytes) Bool
static void builtin_append_file_bytes(VM& vm, u16 dst, u16, u16 ab) {
    auto* b = static_cast<BytesObj*>(vm.reg((u16)(ab + 1)).o);
    vm.reg(dst).i = writeWholeFile(argString(vm, ab).c_str(),
        reinterpret_cast<const char*>(b->data.data()), b->data.size(), "ab");
}

// ---------------------------------------------------------------------------
// Async variants
//
// Each builtin copies its arguments out of the VM heap into system-allocated
// strings, creates a Pending Future (rooted via registerExternalFuture), and
// submits a job to the host's I/O executor (async_io.hpp). The job's `work`
// step does the blocking syscall on the worker thread; its `complete` step
// runs under the host mutex with the VM current, builds the result value in
// the VM heap, and resolves the Future -- the same cross-thread discipline
// as renderDone / siloLoad. Hosts with no executor (bare VM) run the job
// inline: identical semantics, just synchronous.
// ---------------------------------------------------------------------------

// State shared between an async job's work and complete steps.
// System-allocated on purpose: the worker thread must never touch the VM heap.
struct AsyncFileIOState {
    std::string path;
    std::string data;   // read result, or the contents to write
    bool ok = false;
};

// Create a Pending Future<valueT>, root it while in flight, return it.
static Future* makePendingFuture(VM& vm, Type* valueT) {
    FutureType* futT = vm.typeUniverse().futureType(valueT);
    u16 vw = (u16)((valueT && valueT->sizeWords_ > 0) ? valueT->sizeWords_ : 1);
    Future* fut = Future::create(futT, valueT, vw);
    vm.registerExternalFuture(fut);
    return fut;
}

// Submit to the host executor; with no executor installed, run both steps
// inline (submitAsyncIO leaves the job untouched when it returns false).
static void submitOrRunInline(VM& vm, AsyncIOJob&& job) {
    if (!vm.submitAsyncIO(std::move(job))) {
        job.work();
        job.complete(vm);
    }
}

// Shared body of readFileAsync / readFileBytesAsync; `asBytes` picks the
// result representation. Reads args BEFORE writing dst (dst may alias ab).
static void asyncReadCommon(VM& vm, u16 dst, u16 ab, bool asBytes) {
    auto st = std::make_shared<AsyncFileIOState>();
    const VMString& p = argString(vm, ab);
    st->path.assign(p.data(), p.size());

    Type* elemT = asBytes ? vm.typeUniverse().types().bytesType : vm.stringType();
    Future* fut = makePendingFuture(vm, vm.typeUniverse().optionType(elemT));
    vm.reg(dst).o = fut;

    AsyncIOJob job;
    job.work = [st] { st->ok = readWholeFile(st->path.c_str(), st->data); };
    job.complete = [st, fut, asBytes](VM& v) {
        Word w;
        if (!st->ok) {
            w.o = nullptr;
        } else if (asBytes) {
            w.o = new BytesObj(reinterpret_cast<const u8*>(st->data.data()),
                               st->data.size());
        } else {
            w.o = makeString(st->data.data(), st->data.size());
        }
        v.resolveExternalFuture(fut, &w, 1);
    };
    submitOrRunInline(vm, std::move(job));
}

// readFileAsync(path String) Future<Option<String>>
static void builtin_read_file_async(VM& vm, u16 dst, u16, u16 ab) {
    asyncReadCommon(vm, dst, ab, /*asBytes=*/false);
}

// readFileBytesAsync(path String) Future<Option<Bytes>>
static void builtin_read_file_bytes_async(VM& vm, u16 dst, u16, u16 ab) {
    asyncReadCommon(vm, dst, ab, /*asBytes=*/true);
}

// Shared body of the four async write/append builtins. `mode` must be a
// string literal (captured by pointer into the worker closure).
static void asyncWriteCommon(VM& vm, u16 dst, u16 ab, bool bytesArg,
                             const char* mode) {
    auto st = std::make_shared<AsyncFileIOState>();
    const VMString& p = argString(vm, ab);
    st->path.assign(p.data(), p.size());
    if (bytesArg) {
        auto* b = static_cast<BytesObj*>(vm.reg((u16)(ab + 1)).o);
        st->data.assign(reinterpret_cast<const char*>(b->data.data()),
                        b->data.size());
    } else {
        const VMString& s = argString(vm, (u16)(ab + 1));
        st->data.assign(s.data(), s.size());
    }

    Future* fut = makePendingFuture(vm, vm.typeUniverse().types().boolType);
    vm.reg(dst).o = fut;

    AsyncIOJob job;
    job.work = [st, mode] {
        st->ok = writeWholeFile(st->path.c_str(), st->data.data(),
                                st->data.size(), mode);
    };
    job.complete = [st, fut](VM& v) {
        Word w;
        w.i = st->ok ? 1 : 0;
        v.resolveExternalFuture(fut, &w, 1);
    };
    submitOrRunInline(vm, std::move(job));
}

// writeFileAsync(path String, s String) Future<Bool>
static void builtin_write_file_string_async(VM& vm, u16 dst, u16, u16 ab) {
    asyncWriteCommon(vm, dst, ab, /*bytesArg=*/false, "wb");
}

// writeFileAsync(path String, b Bytes) Future<Bool>
static void builtin_write_file_bytes_async(VM& vm, u16 dst, u16, u16 ab) {
    asyncWriteCommon(vm, dst, ab, /*bytesArg=*/true, "wb");
}

// appendFileAsync(path String, s String) Future<Bool>
static void builtin_append_file_string_async(VM& vm, u16 dst, u16, u16 ab) {
    asyncWriteCommon(vm, dst, ab, /*bytesArg=*/false, "ab");
}

// appendFileAsync(path String, b Bytes) Future<Bool>
static void builtin_append_file_bytes_async(VM& vm, u16 dst, u16, u16 ab) {
    asyncWriteCommon(vm, dst, ab, /*bytesArg=*/true, "ab");
}

// ---------------------------------------------------------------------------
// File metadata
// ---------------------------------------------------------------------------

// fileExists(path String) Bool -- true for any existing entry (file or dir).
static void builtin_file_exists(VM& vm, u16 dst, u16, u16 ab) {
    struct stat st;
    vm.reg(dst).i = ::stat(argString(vm, ab).c_str(), &st) == 0;
}

// isDirectory(path String) Bool
static void builtin_is_directory(VM& vm, u16 dst, u16, u16 ab) {
    struct stat st;
    vm.reg(dst).i = ::stat(argString(vm, ab).c_str(), &st) == 0
                 && S_ISDIR(st.st_mode);
}

// fileSize(path String) Option<Int> -- size in bytes of a regular file.
static void builtin_file_size(VM& vm, u16 dst, u16, u16 ab) {
    struct stat st;
    bool ok = ::stat(argString(vm, ab).c_str(), &st) == 0 && S_ISREG(st.st_mode);
    writeOptionIntResult(vm, dst, ok, ok ? (i64)st.st_size : 0);
}

// fileModTime(path String) Option<Float> -- last-modified time, Unix seconds.
static void builtin_file_mod_time(VM& vm, u16 dst, u16, u16 ab) {
    struct stat st;
    bool ok = ::stat(argString(vm, ab).c_str(), &st) == 0;
    f64 t = 0.0;
    if (ok) {
#ifdef __APPLE__
        t = (f64)st.st_mtimespec.tv_sec + (f64)st.st_mtimespec.tv_nsec * 1e-9;
#else
        t = (f64)st.st_mtim.tv_sec + (f64)st.st_mtim.tv_nsec * 1e-9;
#endif
    }
    writeOptionFloatResult(vm, dst, ok, t);
}

// ---------------------------------------------------------------------------
// Directory operations
// ---------------------------------------------------------------------------

// listDir(path String) Option<[String]> -- entry names (no "." / ".."),
// sorted for determinism.
static void builtin_list_dir(VM& vm, u16 dst, u16, u16 ab) {
    DIR* d = ::opendir(argString(vm, ab).c_str());
    if (!d) { vm.reg(dst).o = nullptr; return; }
    std::vector<std::string> names;
    while (struct dirent* ent = ::readdir(d)) {
        std::string name = ent->d_name;
        if (name != "." && name != "..") names.push_back(std::move(name));
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    auto* arr = new ObjArray(vm.arrayType(vm.stringType()));
    GCKeepAliveScope keep(vm, arr);
    for (auto const& n : names) arr->push(makeString(n.data(), n.size()));
    vm.reg(dst).o = arr;
}

// makeDir(path String) Bool -- create the directory (and missing parents);
// true if the directory exists afterwards.
static void builtin_make_dir(VM& vm, u16 dst, u16, u16 ab) {
    const VMString& path = argString(vm, ab);
    std::string p(path.data(), path.size());
    // Create each parent in turn; EEXIST is fine at every step.
    for (size_t i = 1; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') {
            std::string prefix = p.substr(0, i);
            if (::mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST) break;
        }
    }
    struct stat st;
    vm.reg(dst).i = ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// removeFile(path String) Bool -- delete a file (not a directory).
static void builtin_remove_file(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = ::unlink(argString(vm, ab).c_str()) == 0;
}

// renameFile(from String, to String) Bool
static void builtin_rename_file(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = ::rename(argString(vm, ab).c_str(),
                             argString(vm, (u16)(ab + 1)).c_str()) == 0;
}

// ---------------------------------------------------------------------------
// Environment / process
// ---------------------------------------------------------------------------

// getEnv(name String) Option<String>
static void builtin_get_env(VM& vm, u16 dst, u16, u16 ab) {
    const char* v = ::getenv(argString(vm, ab).c_str());
    vm.reg(dst).o = v ? makeString(v, std::strlen(v)) : nullptr;
}

// programArgs() [String] -- CLI arguments after the script filename.
static void builtin_program_args(VM& vm, u16 dst, u16, u16) {
    auto* arr = new ObjArray(vm.arrayType(vm.stringType()));
    GCKeepAliveScope keep(vm, arr);
    for (auto const& a : g_programArgs) arr->push(makeString(a.data(), a.size()));
    vm.reg(dst).o = arr;
}

// currentDir() Option<String>
static void builtin_current_dir(VM& vm, u16 dst, u16, u16) {
    char buf[4096];
    vm.reg(dst).o = ::getcwd(buf, sizeof buf)
                  ? makeString(buf, std::strlen(buf)) : nullptr;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerIoBuiltins(Compiler& compiler, FuncMap& functions) {
    Type* Str      = compiler.stringType();
    Type* Bytes    = compiler.bytesType();
    Type* Int      = compiler.intType();
    Type* Bool     = compiler.boolType();
    Type* ArrayStr = compiler.arrayType(Str);
    Type* OptStr   = compiler.optionType(Str);
    Type* OptBytes = compiler.optionType(Bytes);
    Type* OptInt   = compiler.optionType(Int);
    Type* OptFloat = compiler.optionType(compiler.floatType());
    Type* OptArrayStr = compiler.optionType(ArrayStr);

    // All impure (they observe / mutate the filesystem) and all NRT
    // (blocking syscalls): pure=false, rtSafe=false.
    registerOne(compiler, functions, "readFile",      OptStr,   {Str},        builtin_read_file,          /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "readFileBytes", OptBytes, {Str},        builtin_read_file_bytes,    /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "writeFile",     Bool,     {Str, Str},   builtin_write_file_string,  /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "writeFile",     Bool,     {Str, Bytes}, builtin_write_file_bytes,   /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "appendFile",    Bool,     {Str, Str},   builtin_append_file_string, /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "appendFile",    Bool,     {Str, Bytes}, builtin_append_file_bytes,  /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "fileExists",    Bool,     {Str},        builtin_file_exists,        /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "isDirectory",   Bool,     {Str},        builtin_is_directory,       /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "fileSize",      OptInt,   {Str},        builtin_file_size,          /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "fileModTime",   OptFloat, {Str},        builtin_file_mod_time,      /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "listDir",       OptArrayStr, {Str},     builtin_list_dir,           /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "makeDir",       Bool,     {Str},        builtin_make_dir,           /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "removeFile",    Bool,     {Str},        builtin_remove_file,        /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "renameFile",    Bool,     {Str, Str},   builtin_rename_file,        /*pure=*/false, /*rtSafe=*/false);

    // Async variants: same operations, run on the host's I/O worker; the
    // returned Future resolves when the I/O lands. Still rtSafe=false --
    // NRT-only until the RT payload-delivery path is designed (a Pending
    // Future is RT-compatible, but resolving one with file contents on the
    // audio thread is not; see devplans/LANG_IMPLEMENTATION_PLAN.md).
    Type* FutOptStr   = compiler.futureType(OptStr);
    Type* FutOptBytes = compiler.futureType(OptBytes);
    Type* FutBool     = compiler.futureType(Bool);
    registerOne(compiler, functions, "readFileAsync",      FutOptStr,   {Str},        builtin_read_file_async,         /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "readFileBytesAsync", FutOptBytes, {Str},        builtin_read_file_bytes_async,   /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "writeFileAsync",     FutBool,     {Str, Str},   builtin_write_file_string_async, /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "writeFileAsync",     FutBool,     {Str, Bytes}, builtin_write_file_bytes_async,  /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "appendFileAsync",    FutBool,     {Str, Str},   builtin_append_file_string_async,/*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "appendFileAsync",    FutBool,     {Str, Bytes}, builtin_append_file_bytes_async, /*pure=*/false, /*rtSafe=*/false);

    registerOne(compiler, functions, "getEnv",        OptStr,   {Str},        builtin_get_env,            /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "programArgs",   ArrayStr, {},           builtin_program_args,       /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "currentDir",    OptStr,   {},           builtin_current_dir,        /*pure=*/false, /*rtSafe=*/false);
}

} // namespace ts
