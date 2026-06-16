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
#include <dlfcn.h>
#include <memory>
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

// Runs the full synthdef compilation pipeline.
// The sexpr should be in (Synth <name> (Graph ...)) format.
// Returns "" on success, error message on failure.
// On success, sets dylibPath and synthName.
static std::string compileSynthDefPipeline(std::string const& sexpr,
                                            std::string& synthName,
                                            std::string& dylibPath) {
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
    if (it != compilationCache().end()) {
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

    engine::addSynthDef(eng, optDef->def, optDef->dlHandle);

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

    auto optDef = synthdef::loadDef(path);
    if (!optDef.has_value()) {
        returnErrString(vm, dst, std::string("failed to load plugin: ") + path,
                        __func__);
        return;
    }

    engine::Engine* eng = getEngine(vm);
    if (!eng) {
        returnErrString(vm, dst, "no engine attached to VM", __func__);
        return;
    }

    engine::addSynthDef(eng, optDef->def, optDef->dlHandle);
    returnString(vm, dst, "");
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

    // Low-level functions for the Tzopilotl-hosted compiler (synthc modules)
    // and its differential test harness.
    reg("writeAndCompileCpp",      String, {String, String}, ffi_writeAndCompileCpp);
    reg("loadSynthDylib",          String, {String},         ffi_loadSynthDylib);
    reg("compareAudioFiles",       Float,  {String, String}, ffi_compareAudioFiles);
    reg("audioFileMaxAbs",         Float,  {String},         ffi_audioFileMaxAbs);
    reg("synthdefGenCppFromSexpr", String, {String, Int, Bool}, ffi_synthdefGenCppFromSexpr);
    reg("synthdefAnalysisDump",    String, {String, Bool},   ffi_synthdefAnalysisDump);
}

} // namespace bridge
