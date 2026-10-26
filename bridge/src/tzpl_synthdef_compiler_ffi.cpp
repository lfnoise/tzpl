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
//  tzpl_synthdef_compiler_ffi.cpp
//  bridge
//
//  FFI bridge for synthc, the Tzopilotl-hosted synth compiler: write the
//  generated C++, compile and link it (tzpl_synth_build), load the plugin
//  and register it with the engine, sync or async. The frozen C++
//  synthdef-compiler is no longer reachable from here (block-engine plan D9).
//

#include "tzpl_synthdef_compiler_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_nrt_render.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_synth_build.hpp"
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>
#include <vector>
#include <cmath>
#include <cstdio>

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static engine::Engine* getEngine(ts::VM& vm) {
    // During an NRT render the script's defSynth/loadSynthDylib calls must
    // register the def into the per-render engine -- the same engine the audio
    // bundling FFI (newNode/connect/setInput) targets via its own
    // currentRenderContext() check. Without this, a def compiled inside a
    // render lands in the live placeholder engine while newNode looks in the
    // render engine, so the node is never created and the render is silent.
    if (auto* r = bridge::currentRenderContext()) {
        return r->engine;
    }
    auto* ctx = static_cast<AppContext*>(vm.userData());
    return ctx ? ctx->engine : nullptr;
}

static const char* regString(ts::VM& vm, u16 reg) {
    return ts::stringData(vm.reg(reg).o);
}

// Return a Tzopilotl String to the caller.
static void returnString(ts::VM& vm, u16 dst, std::string const& str) {
    auto* result = new ts::StringObj(str);
    vm.reg(dst).o = result;
}

// Strip the "ffi_" prefix compilers add to __func__ so the log message
// matches the name callers see in Tzopilotl.
static char const* stripFfiPrefix(char const* name) {
    if (name && name[0] == 'f' && name[1] == 'f' && name[2] == 'i' && name[3] == '_') {
        return name + 4;
    }
    return name;
}

// Return an error string to the caller. If non-empty, also log it to stderr
// so the failure is not silent even when the caller discards the return value.
static void returnErrString(ts::VM& vm, u16 dst, std::string const& err,
                            char const* fnName) {
    if (!err.empty()) {
        std::fprintf(vm.errorOutput(), "synthdef.%s: %s\n", stripFfiPrefix(fnName), err.c_str());
    }
    returnString(vm, dst, err);
}

// ---------------------------------------------------------------------------
// Core compilation pipeline
// ---------------------------------------------------------------------------

// Serializes the build dir's revision scan and compiles between the VM
// thread's synchronous FFIs and the async compile worker.
static std::mutex& compileMtx() {
    static std::mutex m;
    return m;
}

// Load a compiled plugin dylib and register its def with the VM's engine.
// Returns "" on success, error message on failure.
static std::string loadAndRegister(ts::VM& vm, std::string const& path) {
    auto optDef = synthbuild::loadDef(path);
    if (!optDef.has_value()) {
        return std::string("failed to load plugin: ") + path;
    }

    engine::Engine* eng = getEngine(vm);
    if (!eng) {
        return "no engine attached to VM";
    }

    engine::addSynthDef(eng, optDef->def, optDef->dlHandle, &optDef->bufferDefs,
                        &optDef->tagList, &optDef->bankDefs, optDef->swapSampleBank);
    return "";
}

// ---------------------------------------------------------------------------
// FFI functions
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Low-level FFI for the Tzopilotl-hosted synthdef compiler (synthc modules)
// ---------------------------------------------------------------------------

// fn writeAndCompileCpp(name String, cppSource String) String
// Writes generated C++ to the build dir, compiles and links it.
// Returns the dylib path on success, "error: ..." on failure.
static void ffi_writeAndCompileCpp(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string name = regString(vm, argBase);
    std::string cppCode = regString(vm, argBase + 1);

    std::lock_guard<std::mutex> lock(compileMtx());

    std::string dir = synthbuild::getBuildDir();
    if (auto ok = synthbuild::ensureBuildDirs(dir); !ok) {
        returnErrString(vm, dst, "error: build dir: " + ok.error(), __func__);
        return;
    }
    try {
        synthbuild::writeCodeToFile(dir, name, cppCode);
    } catch (std::exception const& e) {
        returnErrString(vm, dst, std::string("error: write failed: ") + e.what(),
                        __func__);
        return;
    }

    int err = synthbuild::compileAndLink(dir, name);
    if (err) {
        returnErrString(vm, dst,
                        "error: compile/link failed with exit code " + std::to_string(err),
                        __func__);
        return;
    }

    returnString(vm, dst, synthbuild::dylibPath(dir, name));
}

// fn loadSynthDylib(path String) String
// Loads a compiled plugin dylib and registers its def with the engine.
// Returns "" on success, error message on failure.
static void ffi_loadSynthDylib(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string path = regString(vm, argBase);
    returnErrString(vm, dst, loadAndRegister(vm, path), __func__);
}

// ---------------------------------------------------------------------------
// Async compile FFIs
//
// The clang subprocess behind defSynthX takes on the order of a
// second and used to run on the VM thread, stalling the NRT scheduler -- an
// audible pause in any playing sequence. These variants run the compile on
// the host's async I/O worker (async_io.hpp); the completion step loads the
// dylib, registers the def, and resolves a Future<String> ("" on success)
// under the host mutex with the VM current -- the same cross-thread
// discipline as readFileAsync / siloLoad / renderDone.
// ---------------------------------------------------------------------------

// State shared between an async compile job's work and complete steps.
// System-allocated on purpose: the worker thread must never touch the VM heap.
struct AsyncCompileState {
    std::string name;       // synth name
    std::string cpp;        // generated C++ source (writeCompileAndLoadAsync)
    std::string dylibPath;
    std::string error;
};

// Create a Pending Future<String>, root it while in flight, return it.
static ts::Future* makePendingStringFuture(ts::VM& vm) {
    ts::Type* strT = vm.stringType();
    auto* fut = ts::Future::create(vm.typeUniverse().futureType(strT), strT, 1);
    vm.registerExternalFuture(fut);
    return fut;
}

// Def name -> the Future of its compile currently in flight, so `play` (via
// synthDefReady) can wait for a def whose defSynth/defSynthX was not awaited
// instead of failing with errNodeDefNotFound. Only touched with the host
// mutex held (FFI entry and the executor's complete step), so no lock of
// its own. A future in here is rooted by the VM's external-future list for
// as long as it is pending; the entry is erased before it resolves, so the
// map never holds a pointer to a collectable object.
static std::unordered_map<std::string, ts::Future*>& pendingDefCompiles() {
    static std::unordered_map<std::string, ts::Future*> m;
    return m;
}

static void notePendingCompile(std::string const& name, ts::Future* fut) {
    if (!name.empty()) pendingDefCompiles()[name] = fut;
}

// Drop the entry only if it still refers to this future: a newer compile of
// the same name may have replaced it, and that one is what `play` should wait
// for.
static void forgetPendingCompile(std::string const& name, ts::Future* fut) {
    auto& m = pendingDefCompiles();
    auto it = m.find(name);
    if (it != m.end() && it->second == fut) m.erase(it);
}

// Submit to the host executor. Runs the job inline instead when (a) an NRT
// render is in progress -- currentRenderContext() is thread-local and the def
// must land in the per-render engine before the script continues -- or (b)
// the host has no executor (submitAsyncIO then leaves the job untouched).
static void submitOrRunInline(ts::VM& vm, ts::AsyncIOJob&& job) {
    if (currentRenderContext() != nullptr || !vm.submitAsyncIO(std::move(job))) {
        job.work();
        job.complete(vm);
    }
}

// Complete-step tail shared by both async FFIs: load + register the freshly
// compiled dylib (unless compilation already failed), then resolve the
// Future. Runs with the host mutex held and the VM current.
static void finishAsyncCompile(ts::VM& vm,
                               std::shared_ptr<AsyncCompileState> const& st,
                               ts::Future* fut, char const* fnName) {
    std::string err = st->error;
    if (err.empty()) {
        err = loadAndRegister(vm, st->dylibPath);
    }
    if (!err.empty()) {
        std::fprintf(vm.errorOutput(), "synthdef.%s: %s\n", fnName, err.c_str());
    }
    forgetPendingCompile(st->name, fut);
    ts::Word w;
    w.o = new ts::StringObj(err);
    vm.resolveExternalFuture(fut, &w, 1);
}

// fn writeCompileAndLoadAsync(name String, cppSource String) Future<String>
// Async backend of defSynthX: writes the synthc-generated C++, compiles and
// links it on the worker thread. Resolves to "" on success.
static void ffi_writeCompileAndLoadAsync(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto st = std::make_shared<AsyncCompileState>();
    st->name = regString(vm, argBase);
    st->cpp = regString(vm, argBase + 1);

    ts::Future* fut = makePendingStringFuture(vm);
    vm.reg(dst).o = fut;
    notePendingCompile(st->name, fut);

    ts::AsyncIOJob job;
    job.work = [st] {
        std::lock_guard<std::mutex> lock(compileMtx());
        std::string dir = synthbuild::getBuildDir();
        // One try around the whole step: an exception escaping `work`
        // terminates the process (nothing above the executor catches).
        try {
            if (auto ok = synthbuild::ensureBuildDirs(dir); !ok) {
                st->error = "build dir error: " + ok.error();
                return;
            }
            synthbuild::writeCodeToFile(dir, st->name, st->cpp);
        } catch (std::exception const& e) {
            st->error = std::string("write failed: ") + e.what();
            return;
        }
        int err = synthbuild::compileAndLink(dir, st->name);
        if (err) {
            st->error = "compile/link failed with exit code " + std::to_string(err);
            return;
        }
        st->dylibPath = synthbuild::dylibPath(dir, st->name);
    };
    job.complete = [st, fut](ts::VM& v) {
        finishAsyncCompile(v, st, fut, "writeCompileAndLoadAsync");
    };
    submitOrRunInline(vm, std::move(job));
}

// fn synthDefReady(name String) Future<String>
// The future of the compile in flight for `name`, or an already-resolved
// future ("") when none is. `play` awaits this before newNode, so a def
// whose defSynth/defSynthX was not awaited is waited for at its first play
// instead of the node silently failing with errNodeDefNotFound. Says nothing
// about defs that were never compiled: those still fail at the bundle.
static void ffi_synthDefReady(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string name = regString(vm, argBase);
    auto& m = pendingDefCompiles();
    if (auto it = m.find(name); it != m.end()) {
        vm.reg(dst).o = it->second;
        return;
    }
    ts::Type* strT = vm.stringType();
    auto* fut = ts::Future::create(vm.typeUniverse().futureType(strT), strT, 1);
    fut->value_[0].o = new ts::StringObj("");
    fut->state_ = ts::Future::Resolved;
    vm.reg(dst).o = fut;
}

// fn compareAudioFiles(pathA String, pathB String) Float
// Reads two WAV files (44-byte IEEE-float header + interleaved f32 samples; the
// format wavOpen/wavWrite produce) and returns the maximum absolute difference
// between corresponding samples. Returns a large sentinel (1e30) if a file is
// missing/short or the two differ in sample count -- so a strict "< epsilon"
// check fails loudly on any structural mismatch.
static void ffi_compareAudioFiles(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string pathA = regString(vm, argBase);
    std::string pathB = regString(vm, argBase + 1);

    auto readSamples = [](std::string const& path, std::vector<float>& out) -> bool {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
        long size = ftell(f);
        if (size < 44) { fclose(f); return false; }
        fseek(f, 44, SEEK_SET);  // skip the WAV header
        size_t nbytes = (size_t)(size - 44);
        size_t n = nbytes / sizeof(float);
        out.resize(n);
        size_t got = fread(out.data(), sizeof(float), n, f);
        fclose(f);
        return got == n;
    };

    std::vector<float> a, b;
    if (!readSamples(pathA, a) || !readSamples(pathB, b) || a.size() != b.size()) {
        vm.reg(dst).f = 1e30;
        return;
    }
    double maxDiff = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double d = std::fabs((double)a[i] - (double)b[i]);
        if (d > maxDiff) maxDiff = d;
    }
    vm.reg(dst).f = maxDiff;
}

// fn ftosF32Cpp(x Float) String
// Shortest round-trip decimal for x rounded to f32, with a trailing 'f' --
// exactly the generator's f32 constant formatting (synthbuild::ftos(f32)). synthc
// uses this so its emitted f32 constants byte-match the C++ generator (a value's
// shortest-f32 form differs from its shortest-f64 form, e.g. 0.70794576f).
static void ffi_ftosF32Cpp(ts::VM& vm, u16 dst, u16, u16 argBase) {
    double x = vm.reg(argBase).f;
    returnString(vm, dst, synthbuild::ftosF32(x));
}

// fn audioFileMaxAbs(path String) Float
// Returns the maximum absolute sample value in a WAV file (same 44-byte-header
// f32 format as compareAudioFiles), or -1.0 if the file is missing/short. Used
// by the render A/B test to assert the rendered audio is non-silent, so a
// silence-vs-silence comparison can't pass vacuously.
static void ffi_audioFileMaxAbs(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string path = regString(vm, argBase);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { vm.reg(dst).f = -1.0; return; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); vm.reg(dst).f = -1.0; return; }
    long size = ftell(f);
    if (size < 44) { fclose(f); vm.reg(dst).f = -1.0; return; }
    fseek(f, 44, SEEK_SET);
    size_t n = (size_t)(size - 44) / sizeof(float);
    std::vector<float> s(n);
    size_t got = fread(s.data(), sizeof(float), n, f);
    fclose(f);
    if (got != n) { vm.reg(dst).f = -1.0; return; }
    double maxAbs = 0.0;
    for (float v : s) { double a = std::fabs((double)v); if (a > maxAbs) maxAbs = a; }
    vm.reg(dst).f = maxAbs;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerSynthdefCompilerFFI(ts::Compiler& compiler) {
    auto* String = compiler.stringType();
    auto* Float  = compiler.floatType();
    ts::Type* FutureString = reinterpret_cast<ts::Type*>(compiler.futureType(String));

    using R = void (*)(ts::VM&, u16, u16, u16);

    // All functions go into the "synthdef_ffi" module namespace.
    // The script wrapper `lang/modules/synthdef.x` re-exports these as
    // `synthdef.*` so users can write `import synthdef.*;`.
    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignModuleFunction("synthdef_ffi", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, /*rtSafe=*/false);
    };

    // Async backend of defSynthX: the compile runs on the host's async I/O
    // worker so the NRT scheduler keeps dispatching while clang runs.
    reg("writeCompileAndLoadAsync",    FutureString, {String, String},
        ffi_writeCompileAndLoadAsync);
    // The in-flight compile of a def by name (or a ready future); play awaits it.
    reg("synthDefReady",               FutureString, {String}, ffi_synthDefReady);

    // Low-level functions for the Tzopilotl-hosted compiler (synthc modules)
    // and render tests.
    reg("writeAndCompileCpp",      String, {String, String}, ffi_writeAndCompileCpp);
    reg("loadSynthDylib",          String, {String},         ffi_loadSynthDylib);
    reg("compareAudioFiles",       Float,  {String, String}, ffi_compareAudioFiles);
    reg("audioFileMaxAbs",         Float,  {String},         ffi_audioFileMaxAbs);
    reg("ftosF32Cpp",              String, {Float},          ffi_ftosF32Cpp);
}

} // namespace bridge
