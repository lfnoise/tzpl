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
//  synthdef_compile_link.cpp
//  synthdef-compiler
//
//  Compilation and linking functions for synthdef plugins.
//  Extracted from synthdef_compile.cpp so these are available in the library.
//

#include "synthdef_compile_link.hpp"
#include "tzpl_dynlib.hpp"
#include "tzpl_paths.hpp"
#include "tzpl_process.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <print>
#include <unordered_map>

namespace fs = std::filesystem;

namespace synthdef {

using SynthDefLoadFun = tzpl_SynthDef (*)();

// Monotonic revision counter per synth name, used to give each compiled
// dylib a unique path so that dlopen loads fresh code without invalidating
// function pointers held by nodes still using a previous revision.
static std::unordered_map<string, u64>& revisionCounters() {
    static std::unordered_map<string, u64> counters;
    return counters;
}

static string synthNameSuffix = "_synth";

// Plugin extension. Must agree with the engine's plugin scanner
// (kPluginExt in tzpl_client_interface.cpp).
#if defined(__APPLE__)
static constexpr char const kDylibExt[] = ".dylib";
#elif defined(_WIN32)
static constexpr char const kDylibExt[] = ".dll";
#else
static constexpr char const kDylibExt[] = ".so";
#endif

// The compiler used for generated plugins, resolved once per process:
//   1. $TZPL_CC -- explicit override
//   2. Windows: the llvm-mingw toolchain bundled in the distribution folder
//      (toolchain/bin/clang++.exe beside modules/), so plugin compilation
//      works on a machine with no developer tools installed
//   3. macOS: the historical bare "clang" (Xcode command line tools)
//   4. the compiler this binary was configured with (baked in by CMake), if
//      it still exists
//   5. clang++ from PATH
static string toolchainCommand() {
    static string const resolved = [] {
        if (char const* cc = getenv("TZPL_CC"); cc && cc[0]) return string(cc);
#if defined(_WIN32)
        if (fs::path root = tzpl::distRoot(); !root.empty()) {
            fs::path bundled = root / "toolchain" / "bin" / "clang++.exe";
            std::error_code ec;
            if (fs::is_regular_file(bundled, ec)) return bundled.generic_string();
        }
#elif defined(__APPLE__)
        return string("clang");
#endif
#if defined(TZPL_PLUGIN_CXX)
        {
            std::error_code ec;
            if (fs::is_regular_file(TZPL_PLUGIN_CXX, ec)) return string(TZPL_PLUGIN_CXX);
        }
#endif
        return string("clang++");
    }();
    return resolved;
}

// Run one toolchain step, echoing the command and everything it prints.
static int runToolchain(char const* label, std::vector<string> const& argv) {
    printf("%s: %s\n", label, tzpl::commandLineForDisplay(argv).c_str());
    return tzpl::runProcess(argv, [](std::string_view line) {
        printf("%.*s\n", (int)line.size(), line.data());
    });
}

// Every {name}_synth_rN.dylib in {buildDir}/dylib, as (revision, path).
// Unrevisioned {name}_synth.dylib files are ignored: compileAndLink has never
// produced one, so any that exist are foreign artifacts and not ours to touch.
static vector<std::pair<u64, fs::path>>
scanRevisions(string const& buildDir, string const& synthName) {
    vector<std::pair<u64, fs::path>> revs;
    string const prefix = synthName + synthNameSuffix + "_r";
    std::error_code ec;
    // On error the iterator compares equal to end(), so this loop just skips.
    for (auto const& entry : fs::directory_iterator(buildDir + "dylib", ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != kDylibExt)
            continue;
        string stem = entry.path().stem().string();
        if (!stem.starts_with(prefix)) continue;
        string digits = stem.substr(prefix.size());
        if (digits.empty()
            || digits.find_first_not_of("0123456789") != string::npos)
            continue;
        u64 rev = 0;
        try {
            rev = std::stoull(digits);
        } catch (std::exception const&) {  // absurdly long digit run
            continue;
        }
        revs.emplace_back(rev, entry.path());
    }
    return revs;
}

// Seed a name's counter from the dylibs already on disk, so revisions keep
// increasing across process restarts.
//
// The counter is process-lifetime state. Without seeding, every launch starts
// at 0 and the first compile of a name rewrites {name}_synth_r1.dylib -- so a
// higher revision left by an earlier session outlives the build that replaced
// it, and anything that reads the revision as "newest" (the plugin browser)
// resolves to the stale file. Seeding also restores the invariant that a
// compile never overwrites a dylib some node may still be running.
//
// No-ops once the name has a counter, so it costs one directory scan per name
// per process.
static void seedRevisionFromDisk(string const& buildDir, string const& synthName) {
    auto& counters = revisionCounters();
    if (counters.find(synthName) != counters.end()) return;

    u64 maxRev = 0;
    for (auto const& [rev, path] : scanRevisions(buildDir, synthName))
        if (rev > maxRev) maxRev = rev;
    counters[synthName] = maxRev;
}

// How many revisions of a name to keep on disk. $TZPL_KEEP_REVISIONS, else 3.
static u64 keepRevisions() {
    static u64 const keep = [] {
        char const* env = getenv("TZPL_KEEP_REVISIONS");
        if (!env || !env[0]) return u64{3};
        errno = 0;
        char* end = nullptr;
        unsigned long long v = strtoull(env, &end, 10);
        if (errno || end == env || v == 0) return u64{3};  // keep at least one
        return static_cast<u64>(v);
    }();
    return keep;
}

// Drop all but the newest keepRevisions() revisions of a name.
//
// On POSIX, deleting a dylib that is currently dlopen'd is safe: unlink drops
// the directory entry while the mapping holds the inode, so nodes running that
// revision keep valid function pointers. What a deletion can break is opening
// it *again* by path -- which is why callers that cache a dylib path must fall
// back to recompiling when the load fails. Windows refuses to delete a loaded
// DLL instead; the remove simply fails (ignored here) and the file is picked
// up by pruneAllOldRevisions on a later launch, once nothing maps it.
static void pruneOldRevisions(string const& buildDir, string const& synthName) {
    auto revs = scanRevisions(buildDir, synthName);
    u64 keep = keepRevisions();
    if (revs.size() <= keep) return;

    // Newest first; everything past the keep window goes.
    std::sort(revs.begin(), revs.end(),
              [](auto const& a, auto const& b) { return a.first > b.first; });

    usize removed = 0;
    for (usize i = keep; i < revs.size(); ++i) {
        std::error_code ec;
        if (fs::remove(revs[i].second, ec) && !ec) ++removed;
    }
    if (removed)
        std::println("pruned {} old revision(s) of {}", removed, synthName);
}

// Once per process: prune every name found in {buildDir}/dylib. Catches the
// revisions a previous session could not delete because they were still
// loaded (always the case on Windows) and anything left by a crash.
static void pruneAllOldRevisions(string const& buildDir) {
    std::error_code ec;
    std::vector<string> names;
    for (auto const& entry : fs::directory_iterator(buildDir + "dylib", ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != kDylibExt)
            continue;
        string stem = entry.path().stem().string();
        size_t r = stem.rfind("_r");
        if (r == string::npos || r < synthNameSuffix.size()) continue;
        string base = stem.substr(0, r);
        if (!base.ends_with(synthNameSuffix)) continue;
        string name = base.substr(0, base.size() - synthNameSuffix.size());
        if (std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    }
    for (auto const& name : names) pruneOldRevisions(buildDir, name);
}

static int compile(string const& filepath_c, string const& filepath_o, string const& includeDir)
{
    printf("\nbegin C compile plugin =====================================================\n");

    std::vector<string> argv{toolchainCommand(), "-x", "c++", "-std=c++23"};
#if defined(__APPLE__)
    argv.insert(argv.end(), {"-arch", "arm64", "-stdlib=libc++"});
#elif !defined(_WIN32)
    argv.push_back("-fPIC");
#endif
    argv.push_back("-O3");
    // fast-math minus the finite-math assumption: generated event loops can
    // legitimately compute transient Inf (e.g. 1/(freq*decay) with pre-note
    // zeros at control priming), which is UB under -ffinite-math-only -- and
    // x86-64 clang exploits it into a silent render. IEEE Inf handling makes
    // it well-defined (pow(x, inf) = 0, overwritten at noteOn).
    argv.insert(argv.end(), {"-ffast-math", "-fno-finite-math-only"});
    argv.insert(argv.end(), {"-I", includeDir, "-c", filepath_c, "-o", filepath_o});

    int status = runToolchain("COMPILE", argv);
    if (status) {
        printf("error %d compiling '%s'\n", status, filepath_c.c_str());
        return status;
    }
    printf("end C compile plugin =====================================================\n");
    return 0;
}

static int link(string const& filepath_o, string const& filepath_dylib,
                string const& buildDir) {
    printf("\nbegin call linker =====================================================\n");
    std::vector<string> argv{toolchainCommand()};
#if defined(__APPLE__)
    (void)buildDir;
    argv.insert(argv.end(), {"-arch", "arm64", "-dynamiclib",
                             "-undefined", "dynamic_lookup",
                             "-compatibility_version", "1", "-current_version", "1",
                             "-o", filepath_dylib, filepath_o});
#elif defined(_WIN32)
    // PE has no rpath and resolves every symbol at link time, so the C++
    // runtime and Sleef (staged as libsleef.a by ensureBuildDirs) are linked
    // statically: the DLL imports only the system CRT. --exclude-all-symbols
    // keeps the export table to the TZPL_PLUGIN_EXPORT symbols.
    argv.insert(argv.end(), {"-shared", "-static", "-Wl,--exclude-all-symbols",
                             "-o", filepath_dylib, filepath_o,
                             "-L" + buildDir + "lib", "-lsleef"});
#else
    // ELF shared objects leave undefined symbols to be resolved from the
    // host at dlopen time by default, which is the -undefined dynamic_lookup
    // behavior the plugin ABI relies on. Sleef is staged into the build dir
    // by ensureBuildDirs(); the rpath keeps the plugin loadable after the
    // CMake build tree is gone.
    argv.insert(argv.end(), {"-shared", "-fPIC", "-o", filepath_dylib, filepath_o,
                             "-L" + buildDir + "lib", "-lsleef",
                             "-Wl,-rpath," + buildDir + "lib"});
#endif
    int status = runToolchain("LINK", argv);
    if (status) {
        printf("link failed: %d\n", status);
        return status;
    }
    printf("end call linker =====================================================\n");
    return 0;
}

static string ensureTrailingSlash(string const& path) {
    if (path.empty() || path.back() == '/' || path.back() == '\\') return path;
    return path + '/';
}

string getBuildDir() {
    const char* tzpl_build = getenv("TZPL_BUILD");
    if (tzpl_build && tzpl_build[0] != '\0') {
        return ensureTrailingSlash(tzpl_build);
    }
    // ~/tzpl-build (POSIX) or %LOCALAPPDATA%\tzpl-build (Windows). Forward
    // slashes throughout: every consumer (std::filesystem, the compiler
    // command line, Win32) accepts them.
    return ensureTrailingSlash(tzpl::defaultBuildDir().generic_string());
}

// Where the plugin headers (tzpl_plugin_abi.h and friends) come from, in
// order: $TZPL_SHARED_INCLUDE; the distribution folder's include/ (installed
// by the dist component next to modules/); the source tree baked in at
// configure time (dev builds). Empty if none exists.
static fs::path sharedHeaderSource() {
    std::error_code ec;
    if (char const* p = getenv("TZPL_SHARED_INCLUDE"); p && *p && fs::is_directory(p, ec))
        return p;
    if (fs::path root = tzpl::distRoot(); !root.empty() && fs::is_directory(root / "include", ec))
        return root / "include";
#ifdef TZPL_SHARED_DIR
    if (fs::is_directory(TZPL_SHARED_DIR, ec)) return TZPL_SHARED_DIR;
#endif
    return {};
}

#ifndef __APPLE__
// Stage Sleef next to the shared headers: generated code includes <sleef.h>
// via tzpl_simd.hpp, and link() resolves -lsleef against {buildDir}/lib.
// Sources are the CMake build tree (dev builds: TZPL_SLEEF_*, or the
// plugin-toolchain copy TZPL_PLUGIN_SLEEF_LIB on Windows) and the
// distribution folder (lib/, or toolchain/tzpl/lib on Windows). Copying
// every libsleef.so* name (real file, SONAME, linker name) keeps both the
// link step and the recorded rpath working after the build tree is gone.
static void stageSleef(string const& buildDir) {
    fs::create_directories(buildDir + "lib");
    std::error_code ec;
    auto copyInto = [&](fs::path const& src, string const& dstDir) {
        if (fs::is_regular_file(src, ec))
            fs::copy_file(src, dstDir + src.filename().string(),
                          fs::copy_options::update_existing, ec);
    };
    auto copyLibsFrom = [&](fs::path const& dir) {
        for (auto const& entry : fs::directory_iterator(dir, ec)) {
            auto name = entry.path().filename().string();
            if (name.starts_with("libsleef.so") || name == "libsleef.a")
                copyInto(entry.path(), buildDir + "lib/");
        }
    };
#if defined(TZPL_SLEEF_INCLUDE_DIR)
    copyInto(fs::path(TZPL_SLEEF_INCLUDE_DIR) / "sleef.h", buildDir + "include/");
#endif
#if defined(TZPL_PLUGIN_SLEEF_LIB)
    copyLibsFrom(fs::path(TZPL_PLUGIN_SLEEF_LIB).parent_path());
#elif defined(TZPL_SLEEF_LIB)
    copyLibsFrom(fs::path(TZPL_SLEEF_LIB).parent_path());
#endif
    if (fs::path root = tzpl::distRoot(); !root.empty()) {
        copyLibsFrom(root / "lib");
#ifdef _WIN32
        copyLibsFrom(root / "toolchain" / "tzpl" / "lib");
#endif
    }
}
#endif

void ensureBuildDirs(string const& buildDir) {
    fs::create_directories(buildDir + "include");
    fs::create_directories(buildDir + "cpp");
    fs::create_directories(buildDir + "obj");
    fs::create_directories(buildDir + "dylib");

    std::error_code ec;
    if (fs::path srcDir = sharedHeaderSource(); !srcDir.empty()) {
        string dstDir = buildDir + "include/";
        for (auto const& entry : fs::directory_iterator(srcDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            auto ext = entry.path().extension().string();
            if (ext == ".h" || ext == ".hpp") {
                fs::copy_file(entry.path(), dstDir + entry.path().filename().string(),
                              fs::copy_options::update_existing, ec);
            }
        }
    } else {
        static std::once_flag warned;
        std::call_once(warned, [] {
            std::println(stderr,
                "warning: cannot find the plugin headers (tzpl_plugin_abi.h): no "
                "include/ beside modules/ and no source tree; set "
                "TZPL_SHARED_INCLUDE to the directory containing them");
        });
    }

#ifndef __APPLE__
    stageSleef(buildDir);
#endif

    static std::once_flag pruned;
    std::call_once(pruned, [&] { pruneAllOldRevisions(buildDir); });
}

void writeCodeToFile(string const& buildDir, string const& synthName, string const& ccode) {
    string filename = synthName + synthNameSuffix + ".cpp";
    string filepath = buildDir + "cpp/" + filename;
    std::println("writing code to {}", filepath);

    FILE* fp = fopen(filepath.c_str(), "wb");  // "b": no CRLF translation on Windows
    if (!fp) {
        throw std::runtime_error(std::format("couldn't open output file '{}'", filepath));
    }
    auto writeSize = ccode.size();
    auto writtenSize = fwrite(ccode.c_str(), 1, writeSize, fp);
    if (writtenSize != writeSize) {
        fclose(fp);
        throw std::runtime_error("failed to write everything");
    }
    fclose(fp);
}

string dylibPath(string const& buildDir, string const& synthName) {
    auto& counters = revisionCounters();
    // find, not operator[]: inserting a 0 here would make the name look
    // already-seeded to seedRevisionFromDisk.
    auto it = counters.find(synthName);
    u64 rev = it == counters.end() ? 0 : it->second;
    if (rev == 0)
        return buildDir + "dylib/" + synthName + synthNameSuffix + kDylibExt;
    return buildDir + "dylib/" + synthName + synthNameSuffix + "_r" + std::to_string(rev) + kDylibExt;
}

int compileAndLink(string const& buildDir, string const& synthName) {
    // Refresh the build dir's header copies (update_existing) so generated
    // code never compiles against a stale plugin ABI, whichever entry point
    // (CLI, --test, bridge) got here.
    ensureBuildDirs(buildDir);

    // Bump revision so this compilation produces a unique dylib path.
    // Old dylibs stay on disk (and in memory via dlopen) so that
    // nodes still running the previous version keep valid function pointers.
    // Seeding first makes that hold across restarts too, not just within a run.
    seedRevisionFromDisk(buildDir, synthName);
    revisionCounters()[synthName]++;

    string filename = synthName + synthNameSuffix;
    string filepath_c = buildDir + "cpp/" + filename + ".cpp";
    string filepath_o = buildDir + "obj/" + filename + ".o";
    string filepath_dylib = dylibPath(buildDir, synthName);
    string includeDir = buildDir + "include";

    int err = compile(filepath_c, filepath_o, includeDir);
    if (err) return err;

    err = link(filepath_o, filepath_dylib, buildDir);
    if (err) return err;

    // Bound the cache here rather than at shutdown: this runs whatever way the
    // process exits, and it keeps a long live-coding session from growing
    // without limit instead of only cleaning up on the next launch.
    pruneOldRevisions(buildDir, synthName);

    return 0;
}

optional<LoadedDef> loadDef(std::string path) {
    const char* path_c = path.c_str();

    void* handle = tzpl::dynlibOpen(path_c);

    if (!handle) {
        string err = tzpl::dynlibError();
        fprintf(stderr, "*** ERROR: dlopen '%s' err '%s'\n", path_c, err.c_str());
        fprintf(stdout, "*** ERROR: dlopen '%s' err '%s'\n", path_c, err.c_str());
        return {};
    }

    // ABI version stamp. A MISSING symbol means the plugin predates
    // versioning: its tzpl_SynthDef layout is unknowable (see the header), so
    // refuse it rather than reading its structs. Also refuse anything newer
    // than this header.
    i64 abiVersion = 0;
    if (void* verPtr = tzpl::dynlibSym(handle, "tzpl_abi_version")) {
        abiVersion = *(int64_t*)verPtr;
    } else {
        fprintf(stderr, "*** ERROR: plugin '%s' predates ABI versioning "
                "(no tzpl_abi_version symbol) and cannot be loaded safely; "
                "rebuild it\n", path_c);
        tzpl::dynlibClose(handle);
        return {};
    }
    if (abiVersion > TZPL_PLUGIN_ABI_VERSION) {
        fprintf(stderr, "*** ERROR: plugin '%s' ABI version %lld is newer than "
                "this compiler supports (%d)\n",
                path_c, (long long)abiVersion, TZPL_PLUGIN_ABI_VERSION);
        tzpl::dynlibClose(handle);
        return {};
    }

    void *ptr;

    ptr = tzpl::dynlibSym(handle, "load");
    if (!ptr) {
        fprintf(stderr, "*** ERROR: dlsym %s err '%s'\n", "load", tzpl::dynlibError().c_str());
        tzpl::dynlibClose(handle);
        return {};
    }

    SynthDefLoadFun loadFunc = (SynthDefLoadFun)ptr;

    tzpl_SynthDef def = (*loadFunc)();

    LoadedDef loaded{def, handle};

    // Optional symbols: plugins without sample buffers / tags / sample banks
    // (or compiled before the symbols existed) don't export them.
    if (void* bufPtr = tzpl::dynlibSym(handle, "loadBufferDefs")) {
        loaded.bufferDefs = (*(tzpl_LoadBufferDefsFun)bufPtr)();
    }
    if (void* tagPtr = tzpl::dynlibSym(handle, "loadTags")) {
        loaded.tagList = (*(tzpl_LoadTagsFun)tagPtr)();
    }
    if (void* bankPtr = tzpl::dynlibSym(handle, "loadSampleBankDefs")) {
        loaded.bankDefs = (*(tzpl_LoadSampleBankDefsFun)bankPtr)();
    }
    loaded.swapSampleBank = (tzpl_SwapSampleBankFun)tzpl::dynlibSym(handle, "swapSampleBank");

    return loaded;
}

} // namespace synthdef
