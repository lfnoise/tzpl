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
//  FFI bridge: wraps synthdef-compiler functions into the CFun signature
//  expected by the Tzopilotl VM, and registers them with the compiler.
//

#include "tzpl_synthdef_compiler_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_nrt_render.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "tzpl_client_interface.hpp"
#include "synthdef_compile_link.hpp"
#include "synthdef_from_sexpr.hpp"
#include "synthdef_cpp_codegen.hpp"
#include "synthdef_synth.hpp"
#include "synthdef_str_util.hpp"
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
    // bundling FFI (newNode/connect/setControl) targets via its own
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
        std::fprintf(stderr, "synthdef.%s: %s\n", stripFfiPrefix(fnName), err.c_str());
    }
    returnString(vm, dst, err);
}

// ---------------------------------------------------------------------------
// Compilation cache
// ---------------------------------------------------------------------------

struct CacheEntry {
    std::string dylibPath;
};

static std::unordered_map<std::string, CacheEntry>& compilationCache() {
    static std::unordered_map<std::string, CacheEntry> cache;
    return cache;
}

static std::string cacheKey(std::string const& name, std::string const& sexpr) {
    size_t h = std::hash<std::string>{}(sexpr);
    return name + ":" + std::to_string(h);
}

// ---------------------------------------------------------------------------
// Core compilation pipeline
// ---------------------------------------------------------------------------

// Serializes users of the C++ compiler's process-wide state (PushSynth's
// current-synth stack, gApplyRewrites, the build dir's revision scan) between
// the VM thread's synchronous FFIs and the async compile worker.
static std::mutex& compileMtx() {
    static std::mutex m;
    return m;
}

// Runs the full synthdef compilation pipeline.
// The sexpr should be in (Synth <name> (Graph ...)) format.
// Returns "" on success, error message on failure.
// On success, sets dylibPath and synthName.
static std::string compileSynthDefPipeline(std::string const& sexpr,
                                            std::string& synthName,
                                            std::string& dylibPath) {
    std::lock_guard<std::mutex> lock(compileMtx());

    // Parse s-expression and build synth graph (name extracted from Synth wrapper)
    auto result = synthdef::synthFromSExprText(sexpr);
    if (!result) {
        return std::string("parse error: ") + result.error();
    }

    // Take ownership so the Synth (and its Arena, holding every Expr/Graph/
    // ExprTree/GenLoop allocated during parse + analysis + codegen) is freed
    // when this function returns. Without this every call leaks an arena.
    std::unique_ptr<synthdef::Synth> synth(result.value());
    synthName = synth->name;

    // Graph analysis and code generation (need PushSynth scope)
    std::string cppCode;
    try {
        synthdef::PushSynth ps(synth.get());
        synth->graphAnalysis();
        cppCode = synthdef::cppCodeGen(synth.get());
    } catch (std::exception const& e) {
        return std::string("codegen error: ") + e.what();
    }

    // Write generated code to file
    std::string dir = synthdef::getBuildDir();
    synthdef::ensureBuildDirs(dir);
    try {
        synthdef::writeCodeToFile(dir, synthName, cppCode);
    } catch (std::exception const& e) {
        return std::string("write error: ") + e.what();
    }

    // Compile and link to .dylib
    int err = synthdef::compileAndLink(dir, synthName);
    if (err) {
        return std::string("compile/link failed with exit code ") + std::to_string(err);
    }

    dylibPath = synthdef::dylibPath(dir, synthName);
    return "";
}

// Load a compiled plugin dylib and register its def with the VM's engine.
// Returns "" on success, error message on failure.
static std::string loadAndRegister(ts::VM& vm, std::string const& path) {
    auto optDef = synthdef::loadDef(path);
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

// fn compileSynthDef(sexpr String) String
// Returns "" on success, error message on failure.
static void ffi_compileSynthDef(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string sexpr = regString(vm, argBase);

    std::string synthName, dylibPath;
    std::string error = compileSynthDefPipeline(sexpr, synthName, dylibPath);

    if (error.empty()) {
        // Cache the result
        compilationCache()[cacheKey(synthName, sexpr)] = CacheEntry{dylibPath};
    }

    returnErrString(vm, dst, error, __func__);
}

// fn compileSynthDefAndLoad(sexpr String) String
// Compiles, loads the .dylib, and registers the def with the engine.
// Returns "" on success, error message on failure.
static void ffi_compileSynthDefAndLoad(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string sexpr = regString(vm, argBase);

    std::string synthName, dylibPath;

    // Check cache (use sexpr as key since name is embedded in it)
    std::string key = cacheKey("", sexpr);
    auto it = compilationCache().find(key);
    bool fromCache = it != compilationCache().end();
    if (fromCache) {
        dylibPath = it->second.dylibPath;
    } else {
        // Compile
        std::string error = compileSynthDefPipeline(sexpr, synthName, dylibPath);
        if (!error.empty()) {
            returnErrString(vm, dst, error, __func__);
            return;
        }
        compilationCache()[key] = CacheEntry{dylibPath};
    }

    // Load the .dylib
    auto optDef = synthdef::loadDef(dylibPath);
    if (!optDef.has_value() && fromCache) {
        // The cached revision is gone -- pruned after enough later builds of
        // the same name. The cache records a path, not the code, so recompile
        // and re-point it. Only worth retrying on a cache hit: a load failure
        // on a dylib we just built is a real failure, not a stale path.
        compilationCache().erase(key);
        std::string error = compileSynthDefPipeline(sexpr, synthName, dylibPath);
        if (!error.empty()) {
            returnErrString(vm, dst, error, __func__);
            return;
        }
        compilationCache()[key] = CacheEntry{dylibPath};
        optDef = synthdef::loadDef(dylibPath);
    }
    if (!optDef.has_value()) {
        returnErrString(vm, dst, std::string("failed to load plugin: ") + dylibPath,
                        __func__);
        return;
    }

    // Register with the engine
    engine::Engine* eng = getEngine(vm);
    if (!eng) {
        returnErrString(vm, dst, "no engine attached to VM", __func__);
        return;
    }

    engine::addSynthDef(eng, optDef->def, optDef->dlHandle, &optDef->bufferDefs,
                        &optDef->tagList, &optDef->bankDefs, optDef->swapSampleBank);

    returnString(vm, dst, "");
}

// ---------------------------------------------------------------------------
// Low-level FFI for the Tzopilotl-hosted synthdef compiler (synthc modules)
// ---------------------------------------------------------------------------

// Saves/restores the rewrite flag so a test setting can't leak.
struct RewriteGuard {
    bool prev;
    explicit RewriteGuard(bool apply) : prev(synthdef::gApplyRewrites) {
        synthdef::gApplyRewrites = apply;
    }
    ~RewriteGuard() { synthdef::gApplyRewrites = prev; }
};

// fn writeAndCompileCpp(name String, cppSource String) String
// Writes generated C++ to the build dir, compiles and links it.
// Returns the dylib path on success, "error: ..." on failure.
static void ffi_writeAndCompileCpp(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string name = regString(vm, argBase);
    std::string cppCode = regString(vm, argBase + 1);

    std::lock_guard<std::mutex> lock(compileMtx());

    std::string dir = synthdef::getBuildDir();
    synthdef::ensureBuildDirs(dir);
    try {
        synthdef::writeCodeToFile(dir, name, cppCode);
    } catch (std::exception const& e) {
        returnErrString(vm, dst, std::string("error: write failed: ") + e.what(),
                        __func__);
        return;
    }

    int err = synthdef::compileAndLink(dir, name);
    if (err) {
        returnErrString(vm, dst,
                        "error: compile/link failed with exit code " + std::to_string(err),
                        __func__);
        return;
    }

    returnString(vm, dst, synthdef::dylibPath(dir, name));
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
// The clang subprocess behind defSynth/defSynthX takes on the order of a
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
    std::string sexpr;      // pipeline input (compileSynthDefAndLoadAsync)
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
        std::fprintf(stderr, "synthdef.%s: %s\n", fnName, err.c_str());
    }
    ts::Word w;
    w.o = new ts::StringObj(err);
    vm.resolveExternalFuture(fut, &w, 1);
}

// fn compileSynthDefAndLoadAsync(sexpr String) Future<String>
// Async backend of defSynth: the full C++ pipeline (parse, analysis, codegen,
// clang) runs on the worker thread. Resolves to "" on success.
static void ffi_compileSynthDefAndLoadAsync(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto st = std::make_shared<AsyncCompileState>();
    st->sexpr = regString(vm, argBase);

    ts::Future* fut = makePendingStringFuture(vm);
    vm.reg(dst).o = fut;

    // Cache hit: nothing to compile -- load and resolve right away. A stale
    // path (revision pruned since it was cached) falls through to a fresh
    // compile, mirroring the synchronous version's retry.
    std::string key = cacheKey("", st->sexpr);
    auto it = compilationCache().find(key);
    if (it != compilationCache().end()) {
        std::string err = loadAndRegister(vm, it->second.dylibPath);
        if (err.empty()) {
            ts::Word w;
            w.o = new ts::StringObj("");
            vm.resolveExternalFuture(fut, &w, 1);
            return;
        }
        compilationCache().erase(it);
    }

    ts::AsyncIOJob job;
    job.work = [st] {
        st->error = compileSynthDefPipeline(st->sexpr, st->name, st->dylibPath);
    };
    job.complete = [st, fut](ts::VM& v) {
        if (st->error.empty()) {
            // Host-mutex-held, so the cache access can't race the VM thread.
            compilationCache()[cacheKey("", st->sexpr)] = CacheEntry{st->dylibPath};
        }
        finishAsyncCompile(v, st, fut, "compileSynthDefAndLoadAsync");
    };
    submitOrRunInline(vm, std::move(job));
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

    ts::AsyncIOJob job;
    job.work = [st] {
        std::lock_guard<std::mutex> lock(compileMtx());
        std::string dir = synthdef::getBuildDir();
        synthdef::ensureBuildDirs(dir);
        try {
            synthdef::writeCodeToFile(dir, st->name, st->cpp);
        } catch (std::exception const& e) {
            st->error = std::string("write failed: ") + e.what();
            return;
        }
        int err = synthdef::compileAndLink(dir, st->name);
        if (err) {
            st->error = "compile/link failed with exit code " + std::to_string(err);
            return;
        }
        st->dylibPath = synthdef::dylibPath(dir, st->name);
    };
    job.complete = [st, fut](ts::VM& v) {
        finishAsyncCompile(v, st, fut, "writeCompileAndLoadAsync");
    };
    submitOrRunInline(vm, std::move(job));
}

// fn synthdefGenCppFromSexpr(sexpr String, maxSimdWidth Int, applyRewrites Bool) String
// Runs the C++ compiler pipeline up to code generation and returns the
// generated C++ source (no compile). For differential testing against the
// Tzopilotl-hosted compiler. With applyRewrites=false the algebraic rewriter is
// bypassed so the output is comparable against a compiler that does not yet
// implement rewriting. Returns "error: ..." on failure.
static void ffi_synthdefGenCppFromSexpr(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string sexpr = regString(vm, argBase);
    int simdWidth = static_cast<int>(vm.reg(argBase + 1).i);
    bool applyRewrites = vm.reg(argBase + 2).i != 0;

    std::lock_guard<std::mutex> lock(compileMtx());
    RewriteGuard guard(applyRewrites);

    auto result = synthdef::synthFromSExprText(sexpr);
    if (!result) {
        returnErrString(vm, dst, std::string("error: parse error: ") + result.error(),
                        __func__);
        return;
    }

    std::unique_ptr<synthdef::Synth> synth(result.value());
    try {
        synthdef::PushSynth ps(synth.get());
        synth->graphAnalysis();
        returnString(vm, dst, synthdef::cppCodeGen(synth.get(), simdWidth, simdWidth));
    } catch (std::exception const& e) {
        returnErrString(vm, dst, std::string("error: codegen error: ") + e.what(),
                        __func__);
    }
}

// fn synthdefAnalysisDump(sexpr String, applyRewrites Bool) String
// Runs the C++ compiler's graph analysis and returns Synth::dumpToString().
// With applyRewrites=false the algebraic rewriter is bypassed so the dump is
// comparable against compilers that don't implement rewriting.
// Returns "error: ..." on failure.
static void ffi_synthdefAnalysisDump(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string sexpr = regString(vm, argBase);
    bool applyRewrites = vm.reg(argBase + 1).i != 0;

    std::lock_guard<std::mutex> lock(compileMtx());
    RewriteGuard guard(applyRewrites);

    auto result = synthdef::synthFromSExprText(sexpr);
    if (!result) {
        returnErrString(vm, dst, std::string("error: parse error: ") + result.error(),
                        __func__);
        return;
    }

    std::unique_ptr<synthdef::Synth> synth(result.value());
    try {
        synthdef::PushSynth ps(synth.get());
        synth->graphAnalysis();
        returnString(vm, dst, synth->dumpToString());
    } catch (std::exception const& e) {
        returnErrString(vm, dst, std::string("error: ") + e.what(), __func__);
    }
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
// exactly the generator's f32 constant formatting (synthdef::ftos(f32)). synthc
// uses this so its emitted f32 constants byte-match the C++ generator (a value's
// shortest-f32 form differs from its shortest-f64 form, e.g. 0.70794576f).
static void ffi_ftosF32Cpp(ts::VM& vm, u16 dst, u16, u16 argBase) {
    double x = vm.reg(argBase).f;
    returnString(vm, dst, synthdef::ftos((float)x));
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
    auto* Int    = compiler.intType();
    auto* Bool   = compiler.boolType();
    auto* Float  = compiler.floatType();
    ts::Type* StringArray = reinterpret_cast<ts::Type*>(compiler.arrayType(String));
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

    reg("compileSynthDef",        String,      {String}, ffi_compileSynthDef);
    reg("compileSynthDefAndLoad", String,      {String}, ffi_compileSynthDefAndLoad);

    // Async backends of defSynth / defSynthX: the compile runs on the host's
    // async I/O worker so the NRT scheduler keeps dispatching while clang runs.
    reg("compileSynthDefAndLoadAsync", FutureString, {String},
        ffi_compileSynthDefAndLoadAsync);
    reg("writeCompileAndLoadAsync",    FutureString, {String, String},
        ffi_writeCompileAndLoadAsync);

    // Low-level functions for the Tzopilotl-hosted compiler (synthc modules)
    // and its differential test harness.
    reg("writeAndCompileCpp",      String, {String, String}, ffi_writeAndCompileCpp);
    reg("loadSynthDylib",          String, {String},         ffi_loadSynthDylib);
    reg("compareAudioFiles",       Float,  {String, String}, ffi_compareAudioFiles);
    reg("audioFileMaxAbs",         Float,  {String},         ffi_audioFileMaxAbs);
    reg("ftosF32Cpp",              String, {Float},          ffi_ftosF32Cpp);
    reg("synthdefGenCppFromSexpr", String, {String, Int, Bool}, ffi_synthdefGenCppFromSexpr);
    reg("synthdefAnalysisDump",    String, {String, Bool},   ffi_synthdefAnalysisDump);
}

} // namespace bridge
