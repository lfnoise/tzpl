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
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <unordered_map>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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
#ifdef __APPLE__
static constexpr char const kDylibExt[] = ".dylib";
#else
static constexpr char const kDylibExt[] = ".so";
#endif

// The compiler used for generated plugins: $TZPL_CC overrides everything;
// otherwise macOS keeps the historical bare "clang", and elsewhere we use
// the compiler this binary was configured with (baked in by CMake) so the
// runtime shell-out never depends on what PATH happens to hold.
static string toolchainCommand() {
    if (char const* cc = getenv("TZPL_CC"); cc && cc[0]) return cc;
#ifdef __APPLE__
    return "clang";
#elif defined(TZPL_PLUGIN_CXX)
    return TZPL_PLUGIN_CXX;
#else
    return "clang++";
#endif
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
// Deleting a dylib that is currently dlopen'd is safe: unlink drops the
// directory entry while the mapping holds the inode, so nodes running that
// revision keep valid function pointers. What a deletion can break is opening
// it *again* by path -- which is why callers that cache a dylib path must fall
// back to recompiling when the load fails.
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

static int compile(string const& filepath_c, string const& filepath_o, string const& includeDir)
{
    printf("\nbegin C compile plugin =====================================================\n");

    string cmd = toolchainCommand();
#ifdef __APPLE__
    cmd += " -x c++ -arch arm64 -std=c++23 -stdlib=libc++";
#else
    cmd += " -x c++ -std=c++23 -fPIC";
#endif
    cmd += " -o " + filepath_o;
    cmd += " -O3";
    // fast-math minus the finite-math assumption: generated event loops can
    // legitimately compute transient Inf (e.g. 1/(freq*decay) with pre-note
    // zeros at control priming), which is UB under -ffinite-math-only -- and
    // x86-64 clang exploits it into a silent render. IEEE Inf handling makes
    // it well-defined (pow(x, inf) = 0, overwritten at noteOn).
    cmd += " -ffast-math -fno-finite-math-only";
    cmd += " -I " + includeDir;
    cmd += " -c " + filepath_c;

    printf("COMPILE: %s\n", cmd.c_str());
    FILE* pf = popen(cmd.c_str(), "r");

    while(1) {
        char buffer[2048];
        char *line = fgets(buffer, sizeof(buffer), pf);
        if (!line) break;
        printf("%s", line);
    }
    int status = pclose(pf);
    if (status) {
        printf("error %d compiling '%s'\n", status, filepath_c.c_str());
        return WEXITSTATUS(status);
    }
    printf("end C compile plugin =====================================================\n");
    return 0;
}

static int link(string const& filepath_o, string const& filepath_dylib,
                string const& buildDir) {
    printf("\nbegin call linker =====================================================\n");
    string cmd = toolchainCommand();
#ifdef __APPLE__
    (void)buildDir;
    cmd += " -arch arm64";
    cmd += " -dynamiclib";
    cmd += " -undefined dynamic_lookup";
    cmd += " -compatibility_version 1 -current_version 1";
    cmd += " -framework Accelerate";
#else
    // ELF shared objects leave undefined symbols to be resolved from the
    // host at dlopen time by default, which is the -undefined dynamic_lookup
    // behavior the plugin ABI relies on. Sleef is staged into the build dir
    // by ensureBuildDirs(); the rpath keeps the plugin loadable after the
    // CMake build tree is gone.
    cmd += " -shared -fPIC";
    cmd += " -L" + buildDir + "lib -lsleef";
    cmd += " -Wl,-rpath," + buildDir + "lib";
#endif
    cmd += " -o " + filepath_dylib;
    cmd += " " + filepath_o;
    printf("LINK: %s\n", cmd.c_str());
    FILE* pf = popen(cmd.c_str(), "r");

    while(1) {
        char buffer[2048];
        char *line = fgets(buffer, sizeof(buffer), pf);
        if (!line) break;
        printf("%s", line);
    }
    int status = pclose(pf);
    if (status) {
        printf("link failed: %d\n", status);
        return WEXITSTATUS(status);
    }
    printf("end call linker =====================================================\n");
    return 0;
}

static string ensureTrailingSlash(string const& path) {
    if (path.empty() || path.back() == '/') return path;
    return path + '/';
}

string getBuildDir() {
    const char* tzpl_build = getenv("TZPL_BUILD");
    if (tzpl_build && tzpl_build[0] != '\0') {
        return ensureTrailingSlash(tzpl_build);
    }
    const char* homedir = getenv("HOME");
    if (homedir) {
        return string(homedir) + "/tzpl-build/";
    }
    return "/tmp/tzpl-build/";
}

// Absolute path of the running executable with symlinks resolved. Empty on
// failure. (Same walk as ts::executablePath in lang/src/module_paths.cpp;
// duplicated because the compiler library does not depend on lang.)
static fs::path executablePath() {
    std::error_code ec;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));
    fs::path exe = fs::canonical(buf, ec);
#else
    fs::path exe = fs::canonical("/proc/self/exe", ec);
#endif
    return ec ? fs::path{} : exe;
}

// The directory holding the plugin headers generated code includes
// (tzpl_plugin_abi.h, tzpl_random.hpp, ...). Checked in order, mirroring the
// stdlib discovery in ts::defaultModulePaths:
//
//   1. $TZPL_HOME/include -- explicit override
//   2. the include/ folder of the nearest ancestor of the running executable
//      that has one -- the distribution folder (Tzopilotl/include next to
//      Tzopilotl.app and bin/tzpl)
//   3. the shared/ source directory baked in at build time -- dev builds,
//      whose build directory has no include/ ancestor
//
// Only a directory that actually holds tzpl_plugin_abi.h counts, so an
// unrelated include/ up the tree is never mistaken for ours. Empty if none
// is found -- which is what happens when a release binary runs on a machine
// without the source tree and without the distribution's include/ folder.
static fs::path sharedHeaderDir() {
    std::error_code ec;
    auto hasAbiHeader = [&](fs::path const& dir) {
        return fs::is_regular_file(dir / "tzpl_plugin_abi.h", ec);
    };
    if (char const* home = getenv("TZPL_HOME"); home && *home) {
        if (fs::path p = fs::path(home) / "include"; hasAbiHeader(p)) return p;
    }
    if (fs::path exe = executablePath(); !exe.empty()) {
        fs::path dir = exe.parent_path();
        for (int depth = 0;
             depth < 6 && !dir.empty() && dir != dir.root_path();
             ++depth, dir = dir.parent_path()) {
            if (fs::path p = dir / "include"; hasAbiHeader(p)) return p;
        }
    }
#ifdef TZPL_SHARED_DIR
    if (fs::path p = TZPL_SHARED_DIR; hasAbiHeader(p)) return p;
#endif
    return {};
}

std::expected<void, string> ensureBuildDirs(string const& buildDir) {
    // Everything here goes through the error_code overloads: this runs on
    // the async compile worker, where an escaping filesystem_error would
    // take the whole process down instead of failing one synthdef.
    std::error_code ec;
    for (char const* sub : {"include", "cpp", "obj", "dylib"}) {
        fs::create_directories(buildDir + sub, ec);
        if (ec) {
            return std::unexpected(std::format(
                "cannot create build directory '{}{}': {}", buildDir, sub, ec.message()));
        }
    }

    fs::path srcDir = sharedHeaderDir();
    if (srcDir.empty()) {
        return std::unexpected(
            "cannot find the plugin headers (tzpl_plugin_abi.h): keep "
            "Tzopilotl.app and bin/tzpl inside the distribution folder next to "
            "its include/ directory, or set TZPL_HOME to a distribution root");
    }
    string dstDir = buildDir + "include/";
    fs::directory_iterator it(srcDir, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        auto const& entry = *it;
        // Per-entry stat failures (a dangling symlink) skip that entry; only
        // iteration errors are fatal, so they get their own code.
        std::error_code entryEc;
        if (!entry.is_regular_file(entryEc)) continue;
        auto ext = entry.path().extension().string();
        if (ext != ".h" && ext != ".hpp") continue;
        string dst = dstDir + entry.path().filename().string();
        fs::copy_file(entry.path(), dst, fs::copy_options::update_existing, ec);
        if (ec) {
            return std::unexpected(std::format(
                "cannot copy '{}' to '{}': {}", entry.path().string(), dst, ec.message()));
        }
    }
    if (ec) {
        return std::unexpected(std::format(
            "cannot read plugin headers in '{}': {}", srcDir.string(), ec.message()));
    }

#if !defined(__APPLE__) && defined(TZPL_SLEEF_INCLUDE_DIR) && defined(TZPL_SLEEF_LIB)
    // Stage Sleef next to the shared headers: generated code includes
    // <sleef.h> via tzpl_simd.hpp, and link() resolves -lsleef against
    // {buildDir}/lib. Copying every libsleef.so* name (real file, SONAME,
    // linker name) keeps both the link step and the recorded rpath working
    // even after the CMake build tree is deleted.
    fs::create_directories(buildDir + "lib");
    std::error_code ec;
    fs::copy_file(string(TZPL_SLEEF_INCLUDE_DIR) + "/sleef.h",
                  buildDir + "include/sleef.h",
                  fs::copy_options::update_existing, ec);
    fs::path const sleefLib = TZPL_SLEEF_LIB;
    for (auto const& entry : fs::directory_iterator(sleefLib.parent_path(), ec)) {
        auto name = entry.path().filename().string();
        if (name.starts_with("libsleef.so")) {
            fs::copy_file(entry.path(), buildDir + "lib/" + name,
                          fs::copy_options::update_existing, ec);
        }
    }
#endif
    return {};
}

void writeCodeToFile(string const& buildDir, string const& synthName, string const& ccode) {
    string filename = synthName + synthNameSuffix + ".cpp";
    string filepath = buildDir + "cpp/" + filename;
    std::println("writing code to {}", filepath);

    FILE* fp = fopen(filepath.c_str(), "w");
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
    if (auto ok = ensureBuildDirs(buildDir); !ok) {
        std::println("{}", ok.error());
        return 1;
    }

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

    void* handle = dlopen(path_c, RTLD_NOW);

    if (!handle) {
        fprintf(stderr, "*** ERROR: dlopen '%s' err '%s'\n", path_c, dlerror());
        fprintf(stdout, "*** ERROR: dlopen '%s' err '%s'\n", path_c, dlerror());
        return {};
    }

    // ABI version stamp. A MISSING symbol means the plugin predates
    // versioning: its tzpl_SynthDef layout is unknowable (see the header), so
    // refuse it rather than reading its structs. Also refuse anything newer
    // than this header.
    i64 abiVersion = 0;
    if (void* verPtr = dlsym(handle, "tzpl_abi_version")) {
        abiVersion = *(int64_t*)verPtr;
    } else {
        fprintf(stderr, "*** ERROR: plugin '%s' predates ABI versioning "
                "(no tzpl_abi_version symbol) and cannot be loaded safely; "
                "rebuild it\n", path_c);
        dlclose(handle);
        return {};
    }
    if (abiVersion > TZPL_PLUGIN_ABI_VERSION) {
        fprintf(stderr, "*** ERROR: plugin '%s' ABI version %lld is newer than "
                "this compiler supports (%d)\n",
                path_c, (long long)abiVersion, TZPL_PLUGIN_ABI_VERSION);
        dlclose(handle);
        return {};
    }

    void *ptr;

    ptr = dlsym(handle, "load");
    if (!ptr) {
        fprintf(stderr, "*** ERROR: dlsym %s err '%s'\n", "load", dlerror());
        dlclose(handle);
        return {};
    }

    SynthDefLoadFun loadFunc = (SynthDefLoadFun)ptr;

    tzpl_SynthDef def = (*loadFunc)();

    LoadedDef loaded{def, handle};

    // Optional symbols: plugins without sample buffers / tags / sample banks
    // (or compiled before the symbols existed) don't export them.
    if (void* bufPtr = dlsym(handle, "loadBufferDefs")) {
        loaded.bufferDefs = (*(tzpl_LoadBufferDefsFun)bufPtr)();
    }
    if (void* tagPtr = dlsym(handle, "loadTags")) {
        loaded.tagList = (*(tzpl_LoadTagsFun)tagPtr)();
    }
    if (void* bankPtr = dlsym(handle, "loadSampleBankDefs")) {
        loaded.bankDefs = (*(tzpl_LoadSampleBankDefsFun)bankPtr)();
    }
    loaded.swapSampleBank = (tzpl_SwapSampleBankFun)dlsym(handle, "swapSampleBank");

    return loaded;
}

} // namespace synthdef
