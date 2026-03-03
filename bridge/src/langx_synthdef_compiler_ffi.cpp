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
// Returns "" on success, error message on failure.
// On success, sets dylibPath to the path of the compiled .dylib.
static std::string compileSynthDefPipeline(std::string const& name,
                                            std::string const& sexpr,
                                            std::string& dylibPath) {
    // Parse s-expression and build synth graph
    auto result = synthdef::synthFromSExprText(sexpr, name);
    if (!result) {
        return std::string("parse error: ") + result.error();
    }

    synthdef::Synth* synth = result.value();

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
        synthdef::writeCodeToFile(dir, name, cppCode);
    } catch (std::exception const& e) {
        return std::string("write error: ") + e.what();
    }

    // Compile and link to .dylib
    int err = synthdef::compileAndLink(dir, name);
    if (err) {
        return std::string("compile/link failed with exit code ") + std::to_string(err);
    }

    dylibPath = dir + name + "_synth.dylib";
    return "";
}

// ---------------------------------------------------------------------------
// FFI functions
// ---------------------------------------------------------------------------

// fn compileSynthDef(name String, sexpr String) String
// Returns "" on success, error message on failure.
static void ffi_compileSynthDef(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string name = regString(vm, argBase);
    std::string sexpr = regString(vm, argBase + 1);

    std::string dylibPath;
    std::string error = compileSynthDefPipeline(name, sexpr, dylibPath);

    if (error.empty()) {
        // Cache the result
        compilationCache()[cacheKey(name, sexpr)] = CacheEntry{dylibPath};
    }

    returnString(vm, dst, error);
}

// fn compileSynthDefAndLoad(name String, sexpr String) String
// Compiles, loads the .dylib, and registers the def with the engine.
// Returns "" on success, error message on failure.
static void ffi_compileSynthDefAndLoad(ts::VM& vm, u16 dst, u16, u16 argBase) {
    std::string name = regString(vm, argBase);
    std::string sexpr = regString(vm, argBase + 1);

    std::string key = cacheKey(name, sexpr);
    std::string dylibPath;

    // Check cache
    auto it = compilationCache().find(key);
    if (it != compilationCache().end()) {
        dylibPath = it->second.dylibPath;
    } else {
        // Compile
        std::string error = compileSynthDefPipeline(name, sexpr, dylibPath);
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

// fn listSynthDefs() Array[String]
// Returns an array of all registered node def names.
static void ffi_listSynthDefs(ts::VM& vm, u16 dst, u16, u16) {
    engine::Engine* eng = getEngine(vm);

    std::vector<std::string> names;
    if (eng) {
        engine::listNodeDefs(eng, names);
    }

    // Create a Language X Array[String]
    auto* arrType = vm.arrayType(vm.stringType());
    auto* arr = new ts::ObjArray(arrType);
    for (auto const& name : names) {
        auto* s = new ts::StringObj(name);
        arr->v.push_back(s);
    }
    vm.reg(dst).o = arr;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerSynthdefCompilerFFI(ts::Compiler& compiler) {
    auto* String = compiler.stringType();
    ts::Type* StringArray = reinterpret_cast<ts::Type*>(compiler.arrayType(String));

    using R = void (*)(ts::VM&, u16, u16, u16);

    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignFunction(name, retType, std::move(params), fn,
                                          /*pure=*/false, /*rtSafe=*/false);
    };

    reg("compileSynthDef",        String,      {String, String}, ffi_compileSynthDef);
    reg("compileSynthDefAndLoad", String,      {String, String}, ffi_compileSynthDefAndLoad);
    reg("listSynthDefs",          StringArray, {},               ffi_listSynthDefs);
}

} // namespace bridge
