//
//  langx_synthdef_compiler_ffi.cpp
//  bridge
//
//  FFI bridge: wraps synthdef-compiler functions into the CFun signature
//  expected by the Language X VM, and registers them with the compiler.
//

#include "langx_synthdef_compiler_ffi.hpp"
#include "langx.hpp"
#include "value.hpp"
#include "jscs_client_interface.hpp"
#include "synthdef_compile_link.hpp"
#include "synthdef_from_sexpr.hpp"
#include "synthdef_cpp_codegen.hpp"
#include "synthdef_synth.hpp"
#include <dlfcn.h>
#include <unordered_map>
#include <functional>

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static engine::Engine* getEngine(ts::VM& vm) {
    return static_cast<engine::Engine*>(vm.userData());
}

static const char* regString(ts::VM& vm, u16 reg) {
    return ts::stringData(vm.reg(reg).o);
}

// Return a Language X String to the caller.
static void returnString(ts::VM& vm, u16 dst, std::string const& str) {
    auto* result = new ts::StringObj(str);
    vm.reg(dst).o = result;
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

    synthdef::Synth* synth = result.value();
    synthName = synth->name;

    // Graph analysis and code generation (need PushSynth scope)
    std::string cppCode;
    try {
        synthdef::PushSynth ps(synth);
        synth->graphAnalysis();
        cppCode = synthdef::cppCodeGen(synth);
    } catch (std::exception const& e) {
        return std::string("codegen error: ") + e.what();
    }

    // Write generated code to file
    std::string dir = synthdef::getBuildDir();
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

    dylibPath = dir + synthName + "_synth.dylib";
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

    returnString(vm, dst, error);
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
            returnString(vm, dst, error);
            return;
        }
        compilationCache()[key] = CacheEntry{dylibPath};
    }

    // Load the .dylib
    auto optDef = synthdef::loadDef(dylibPath);
    if (!optDef.has_value()) {
        returnString(vm, dst, std::string("failed to load plugin: ") + dylibPath);
        return;
    }

    // Register with the engine
    engine::Engine* eng = getEngine(vm);
    if (!eng) {
        returnString(vm, dst, "no engine attached to VM");
        return;
    }

    engine::addSynthDef(eng, optDef.value());

    returnString(vm, dst, "");
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerSynthdefCompilerFFI(ts::Compiler& compiler) {
    auto* String = compiler.stringType();
    ts::Type* StringArray = reinterpret_cast<ts::Type*>(compiler.arrayType(String));

    using R = void (*)(ts::VM&, u16, u16, u16);

    // All functions go into the "synthdef" module namespace.
    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignModuleFunction("synthdef", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, /*rtSafe=*/false);
    };

    reg("compileSynthDef",        String,      {String}, ffi_compileSynthDef);
    reg("compileSynthDefAndLoad", String,      {String}, ffi_compileSynthDefAndLoad);
}

} // namespace bridge
