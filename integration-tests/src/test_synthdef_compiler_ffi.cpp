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
//  test_synthdef_compiler_ffi.cpp
//  integration-tests
//
//  Integration test for the synthdef-compiler FFI bridge.
//

#include "tzpl_synthdef_compiler_ffi.hpp"
#include "tzpl_audio_engine_ffi.hpp"
#include "synthdef_compile_link.hpp"
#include "tzpl_clock_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include "tzpl_ui_node_controls.hpp"
#include <print>
#include <cstdlib>
#include <string_view>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <dlfcn.h>

// ---------------------------------------------------------------------------
// Test runner helpers
// ---------------------------------------------------------------------------

static int gTestsPassed = 0;
static int gTestsFailed = 0;

static void check(bool condition, std::string_view description) {
    if (condition) {
        std::print("  PASS: {}\n", description);
        ++gTestsPassed;
    } else {
        std::print("  FAIL: {}\n", description);
        ++gTestsFailed;
    }
}

// Compile and run a Tzopilotl source string. Returns true on compilation success.
static bool compileAndRun(ts::Compiler& compiler, ts::VM& vm,
                          const char* source, const char* testName,
                          ts::ModuleCompiler* moduleCompiler = nullptr) {
    auto target = vm.target();
    auto result = compiler.compile(source, testName, target, moduleCompiler);
    if (!result.success) {
        std::print("  Compilation FAILED for '{}':\n", testName);
        for (auto& err : result.errors) {
            std::print("    {}\n", err.message);
        }
        return false;
    }
    vm.makeCurrent();
    vm.install(result);
    vm.execute(result.mainBlock);
    return true;
}

// Create a standard test engine (no audio output).
static engine::Engine* makeTestEngine() {
    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    engine::EngineConfig config;
    config.numSilos = 1;

    return engine::newEngine(config, params);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_compile_success() {
    std::print("Test: compileSynthDef succeeds with valid s-expression\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // A minimal constant -> outlet synthdef
    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth test_sine_ffi (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let err = compileSynthDef(sexpr);
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "compile_success.x", &moduleCompiler);
    check(ok, "compileSynthDef source compiles and runs");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_compile_error() {
    std::print("Test: compileSynthDef returns error for invalid s-expression\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Invalid s-expression
    const char* source = R"LANG(
        import synthdef.*;
        let err = compileSynthDef("this is not valid sexpr");
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "compile_error.x", &moduleCompiler);
    check(ok, "compileSynthDef with bad input compiles and runs (returns error string)");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_compile_and_load() {
    std::print("Test: compileSynthDefAndLoad compiles, loads, and registers a def\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Compile and load, then verify the def was registered
    const char* source = R"LANG(
        import synthdef.*;
        import audio_engine.*;
        let sexpr = "(Synth loaded_sine (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let err = compileSynthDefAndLoad(sexpr);
        println(err);
        let defs = listSynthDefs();
        println(defs);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "compile_and_load.x", &moduleCompiler);
    check(ok, "compileSynthDefAndLoad source compiles and runs");

    // Verify the def was registered
    std::vector<std::string> names;
    engine::listNodeDefs(eng, names);
    bool found = false;
    for (auto const& n : names) {
        if (n == "loaded_sine") found = true;
    }
    check(found, "loaded_sine def found in engine after compileSynthDefAndLoad");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_caching() {
    std::print("Test: Second compilation of same input uses cache (fast)\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Compile twice — the second should be faster due to caching
    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth cache_test (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let err1 = compileSynthDefAndLoad(sexpr);
        let err2 = compileSynthDefAndLoad(sexpr);
        println(err1);
        println(err2);
    )LANG";

    auto start = std::chrono::steady_clock::now();
    bool ok = compileAndRun(compiler, vm, source, "caching.x", &moduleCompiler);
    auto elapsed = std::chrono::steady_clock::now() - start;
    check(ok, "Caching test compiles and runs (second call should be fast)");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_list_synthdefs() {
    std::print("Test: listSynthDefs returns registered def names\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    // Register a built-in test plugin so we have something to list
    engine::createSineNode(eng);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    const char* source = R"LANG(
        import audio_engine.*;
        let defs = listSynthDefs();
        println(defs);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "list_synthdefs.x", &moduleCompiler);
    check(ok, "listSynthDefs source compiles and runs");

    // Also verify via C++ that the def is listed
    std::vector<std::string> names;
    engine::listNodeDefs(eng, names);
    // The output node and xfader are always registered, plus SinOsc
    check(names.size() >= 2, "At least 2 defs registered (builtins + SinOsc)");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_def_desc_introspection() {
    std::print("Test: getDefDesc reports ports, controls, and buffers\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // A synthdef reading 2 channels from sample buffer 0, with category tags.
    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth test_bufdesc (Tags \"demo\" \"test\") (Graph 1 ((0 BufFixRead 0 0 2 0) (1 Outlet \"out\" 0))))";
        let err = compileSynthDefAndLoad(sexpr);
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "def_desc.x", &moduleCompiler);
    check(ok, "buffer synthdef compiles and loads");

    engine::DefDesc desc;
    bool found = engine::getDefDesc(eng, "test_bufdesc", desc);
    check(found, "getDefDesc finds test_bufdesc");
    if (found) {
        check(desc.ins.empty(), "no inlets");
        check(desc.outs.size() == 1, "one outlet");
        if (desc.outs.size() == 1) {
            check(desc.outs[0].name == "out", "outlet named 'out'");
            check(desc.outs[0].type.chans == 2, "outlet has 2 channels");
        }
        check(desc.controls.empty(), "no controls");
        check(desc.tags.size() == 2 && desc.tags[0] == "demo"
              && desc.tags[1] == "test",
              "embedded tags in declaration order");
        check(desc.buffers.size() == 1, "one buffer");
        if (desc.buffers.size() == 1) {
            check(desc.buffers[0].name == "buf0", "buffer named 'buf0'");
            check(desc.buffers[0].bufID == 0, "buffer id 0");
            check(desc.buffers[0].type.elem == tzpl_kF64, "buffer elem type f64");
            check(desc.buffers[0].type.chans == 2, "buffer spans 2 channels");
        }
    }

    engine::DefDesc missing;
    check(!engine::getDefDesc(eng, "no_such_def", missing),
          "getDefDesc returns false for unknown def");

    // The def also appears in the full listing.
    std::vector<engine::DefDesc> all;
    engine::listDefDescs(eng, all);
    bool listed = false;
    for (auto const& d : all) {
        if (d.name == "test_bufdesc" && d.buffers.size() == 1) listed = true;
    }
    check(listed, "listDefDescs includes test_bufdesc with its buffer");

    // --- On-disk discovery (plugin browser "Available" section) ---
    // The compiled dylib now sits in the compile cache.
    std::string cacheDir = synthdef::getBuildDir() + "dylib";
    std::vector<engine::PluginFile> files;
    engine::listPluginFiles({cacheDir}, files);
    std::string bufdescPath;
    for (auto const& f : files) {
        if (f.name == "test_bufdesc") bufdescPath = f.path;
    }
    check(!bufdescPath.empty(), "listPluginFiles finds test_bufdesc in the compile cache");

    if (!bufdescPath.empty()) {
        // The freshly compiled plugin carries the current ABI version stamp.
        if (void* handle = dlopen(bufdescPath.c_str(), RTLD_NOW | RTLD_LOCAL)) {
            void* verPtr = dlsym(handle, "tzpl_abi_version");
            check(verPtr != nullptr, "plugin exports tzpl_abi_version");
            if (verPtr) {
                check(*(int64_t*)verPtr == TZPL_PLUGIN_ABI_VERSION,
                      "tzpl_abi_version matches TZPL_PLUGIN_ABI_VERSION");
            }
            dlclose(handle);
        }

        engine::DefDesc fileDesc;
        bool fok = engine::getPluginFileDesc(bufdescPath.c_str(), fileDesc);
        check(fok, "getPluginFileDesc introspects the dylib without an engine");
        if (fok) {
            check(fileDesc.name == "test_bufdesc", "file desc name matches");
            check(fileDesc.buffers.size() == 1
                  && fileDesc.buffers[0].type.chans == 2,
                  "file desc reports the buffer metadata");
        }
        engine::DefDesc fileDesc2;
        check(engine::getPluginFileDesc(bufdescPath.c_str(), fileDesc2)
              && fileDesc2.buffers.size() == 1,
              "second (cached) introspection matches");

        // loadOneDef registers the file's def into a fresh engine.
        engine::Engine* eng2 = makeTestEngine();
        check(engine::loadOneDef(eng2, bufdescPath.c_str()),
              "loadOneDef loads the dylib");
        engine::DefDesc desc2;
        check(engine::getDefDesc(eng2, "test_bufdesc", desc2)
              && desc2.buffers.size() == 1,
              "loadOneDef registered the def with buffer metadata");
        engine::freeEngine(eng2);
    }

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_control_kinds() {
    std::print("Test: control kinds flow from DSL sugar to DefDesc widgets\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Author one control of each kind through the DSL sugar constructors
    // and compile via the sexpr path -- exercising DSL -> sexpr (optional
    // kind field) -> parser -> codegen -> plugin ABI in one shot.
    const char* source = R"LANG(
        import synthdef.*;
        fn kt() S {
            let freq = control("freq", ControlSpec {
                lo: 20.0, hi: 8000.0, init: 440.0,
                warp: ControlWarp.exponential });
            let trig = trigger("trig");
            let mute = toggle("mute");
            let mode = choice("mode", 4);
            outlet(freq + trig + mute + mode)
        }
        let err = kt makeGraph toSynthSexpr("test_ctl_kinds")
                     compileSynthDefAndLoad;
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "ctl_kinds.x", &moduleCompiler);
    check(ok, "kind synthdef compiles and loads");

    engine::DefDesc desc;
    bool found = engine::getDefDesc(eng, "test_ctl_kinds", desc);
    check(found, "getDefDesc finds test_ctl_kinds");
    if (found && desc.controls.size() == 4) {
        auto kindOf = [&](char const* name) -> int {
            for (auto const& c : desc.controls)
                if (c.name == name) return (int)c.spec.kind;
            return -1;
        };
        check(kindOf("freq") == tzpl_ckContinuous, "control() is continuous");
        check(kindOf("trig") == tzpl_ckTrigger, "trigger() is a trigger");
        check(kindOf("mute") == tzpl_ckBoolean, "toggle() is boolean");
        check(kindOf("mode") == tzpl_ckSelect, "choice() is select");

        // And the UI derives the right widgets from them.
        using bridge::UIWidgetKind;
        check(bridge::widgetKindForControl(tzpl_ckContinuous) == UIWidgetKind::Slider
              && bridge::widgetKindForControl(tzpl_ckTrigger) == UIWidgetKind::Button
              && bridge::widgetKindForControl(tzpl_ckBoolean) == UIWidgetKind::Toggle
              && bridge::widgetKindForControl(tzpl_ckSelect) == UIWidgetKind::Number,
              "widget kinds derived per control kind");

        // choice() spec: 0..numChoices-1 in unit steps.
        for (auto const& c : desc.controls) {
            if (c.name == "mode") {
                check(c.spec.lo == 0.0 && c.spec.hi == 3.0
                      && c.spec.warp == tzpl_cwStep && c.spec.param == 1.0,
                      "choice() spec is 0..n-1 step 1");
            }
        }
    } else {
        check(false, "test_ctl_kinds has 4 controls");
    }

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_abi_version_refusal() {
    std::print("Test: loaders refuse plugins with a newer ABI version\n");
    namespace fs = std::filesystem;

    // Fabricate a dylib that claims a future ABI version.
    fs::path dir = fs::temp_directory_path() / "tzpl_abi_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path src = dir / "future.cpp";
    fs::path dylib = dir / "future_synth.dylib";
    {
        std::ofstream out(src);
        out << "#include <cstdint>\n"
               "extern \"C\" int64_t tzpl_abi_version = 9999;\n"
               "extern \"C\" void load() {}\n";
    }
    std::string cmd = "clang++ -dynamiclib -o '" + dylib.string() + "' '"
                      + src.string() + "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        std::print("  SKIP: could not compile test dylib\n");
        return;
    }

    engine::Engine* eng = makeTestEngine();
    check(!engine::loadOneDef(eng, dylib.string().c_str()),
          "loadOneDef refuses a newer-ABI plugin");
    engine::DefDesc desc;
    check(!engine::getPluginFileDesc(dylib.string().c_str(), desc),
          "getPluginFileDesc refuses a newer-ABI plugin");
    check(!synthdef::loadDef(dylib.string()).has_value(),
          "synthdef::loadDef refuses a newer-ABI plugin");
    engine::freeEngine(eng);
    fs::remove_all(dir, ec);
}

static void test_low_level_ffi() {
    std::print("Test: low-level FFI (synthdefAnalysisDump, synthdefGenCppFromSexpr)\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    // Capture print output so we can assert on it.
    char* buf = nullptr;
    size_t bufSize = 0;
    FILE* memOut = open_memstream(&buf, &bufSize);
    vm.setPrintOutput(memOut);

    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth dump_test (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let dump = synthdefAnalysisDump(sexpr, false);
        println(dump startsWith("SYNTH dump_test") ? "DUMP_OK" : "DUMP_BAD: " $ dump);
        let cpp = synthdefGenCppFromSexpr(sexpr, 0, true);
        println(cpp contains("dump_test") && !(cpp startsWith("error:")) ? "CPP_OK" : "CPP_BAD: " $ cpp);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "low_level_ffi.x", &moduleCompiler);
    check(ok, "low-level FFI source compiles and runs");

    fflush(memOut);
    std::string output = buf ? std::string(buf, bufSize) : "";
    fclose(memOut);
    free(buf);

    check(output.find("DUMP_OK") != std::string::npos,
          "synthdefAnalysisDump returns address-free dump");
    check(output.find("CPP_OK") != std::string::npos,
          "synthdefGenCppFromSexpr returns generated C++ source");

    engine::freeEngine(eng);
}

// Read a script file into a string.
static std::string readScript(std::string const& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// Run a synthc differential-test script and assert it prints `sentinel`. The
// script does the actual A/B comparison against the C++ compiler over the FFI;
// here we just drive it and check the PASS line. `checkLabel` is the assertion
// message reported on failure.
static void run_synthc_diff_script(char const* scriptFile, char const* sentinel,
                                   char const* checkLabel) {
    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    // EXAMPLES_DIR: synthc_prod_diff.x imports instrument_synthdefs, which
    // lives with the shipped examples rather than the stdlib modules.
    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR, EXAMPLES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    char* buf = nullptr;
    size_t bufSize = 0;
    FILE* memOut = open_memstream(&buf, &bufSize);
    vm.setPrintOutput(memOut);

    std::string source = readScript(std::string(SCRIPTS_DIR) + "/" + scriptFile);
    check(!source.empty(), std::format("{} script found", scriptFile).c_str());

    bool ok = compileAndRun(compiler, vm, source.c_str(), scriptFile, &moduleCompiler);
    check(ok, std::format("{} compiles and runs", scriptFile).c_str());

    fflush(memOut);
    std::string output = buf ? std::string(buf, bufSize) : "";
    fclose(memOut);
    free(buf);

    bool pass = output.find(sentinel) != std::string::npos;
    if (!pass) {
        std::print("--- script output ---\n{}\n---------------------\n", output);
    }
    check(pass, checkLabel);

    engine::freeEngine(eng);
}

static void test_synthc_analysis_diff() {
    std::print("Test: synthc analysis (M1) matches C++ analysis dump\n");
    run_synthc_diff_script(
        "synthc_analysis_diff.x", "M1 PASS",
        "synthc analysis (SORTED+TREES, audio-only loops) matches C++ "
        "and codegen byte-matches");
}

static void test_synthc_rewrite_diff() {
    std::print("Test: synthc rewrites-ON dump + codegen match C++ (rewrites on)\n");
    run_synthc_diff_script(
        "synthc_rewrite_diff.x", "M2 REWRITE DIFF PASS",
        "synthc rewrite engine reproduces the C++ rewriter (dump + codegen)");
}

static void test_synthc_voicer_diff() {
    std::print("Test: synthc voicer (M4.0) dump + codegen match C++\n");
    run_synthc_diff_script(
        "synthc_voicer_diff.x", "M4 VOICER DIFF PASS",
        "synthc flat-voice-mode voicer codegen matches C++ (dump + codegen)");
}

static void test_synthc_spectral_diff() {
    std::print("Test: synthc spectral (M4.4) dump + codegen match C++\n");
    run_synthc_diff_script(
        "synthc_spectral_diff.x", "M4 SPECTRAL DIFF PASS",
        "synthc spectral-chain codegen matches C++ (dump + codegen)");
}

static void test_synthc_simd_diff() {
    std::print("Test: synthc SIMD (M5.1) codegen matches C++ at width 4\n");
    run_synthc_diff_script(
        "synthc_simd_diff.x", "M5 SIMD DIFF PASS",
        "synthc SIMD scaffolding + simple forms match C++ (width 4)");
}

static void test_synthc_voicer_simd_diff() {
    std::print("Test: synthc flat-voice SIMD (M5.3) codegen matches C++ at width 4\n");
    run_synthc_diff_script(
        "synthc_voicer_simd_diff.x", "M5 VOICER SIMD DIFF PASS",
        "synthc flat-voice SIMD codegen matches C++ (width 4)");
}

static void test_synthc_buffer_simd_diff() {
    std::print("Test: synthc buffer SIMD (M5.4) codegen matches C++ at width 4 + 2\n");
    run_synthc_diff_script(
        "synthc_buffer_simd_diff.x", "M5 BUFFER SIMD DIFF PASS",
        "synthc buffer SIMD codegen matches C++ (width 4 + width 2)");
}

static void test_synthc_prod_diff() {
    std::print("Test: synthc production config (M5.5) byte-matches C++ (rewrites-on + width 4)\n");
    run_synthc_diff_script(
        "synthc_prod_diff.x", "M5 PROD DIFF PASS",
        "synthc production output (defSynthX: rewrites-on, width 4) byte-matches the C++ compiler");
}

static void test_synthc_compile_and_load() {
    std::print("Test: synthc defSynthX compiles a synth to a dylib and loads it\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // defSynthX runs the Tzopilotl-hosted compiler end to end: graph -> import
    // -> analyze -> codegen -> clang -> dylib -> load into the engine.
    const char* source = R"LANG(
        import synthc.compile.*;
        import synthdef.*;
        fn integ() S {
            let osc = (fs() / 440.0) sin;
            let d = delayVar(); d init(1, 0.0); let r = d read(1);
            let mixed = osc * 0.5 + r; d write(mixed); mixed outlet
        }
        defSynthX(integ, "synthc_integ");
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "synthc_compile.x", &moduleCompiler);
    check(ok, "synthc defSynthX source compiles and runs");

    std::vector<std::string> names;
    engine::listNodeDefs(eng, names);
    bool found = false;
    for (auto const& n : names) if (n == "synthc_integ") found = true;
    check(found, "synthc_integ def registered in engine after defSynthX");

    // main() sets TZPL_DEFAULT_TAGS=test, so defSynthX should have tagged it.
    engine::DefDesc desc;
    check(engine::getDefDesc(eng, "synthc_integ", desc)
          && desc.tags.size() == 1 && desc.tags[0] == "test",
          "defSynthX applied the TZPL_DEFAULT_TAGS session tag via synthc");

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char const* argv[]) {
    std::print("=== Synthdef Compiler FFI Integration Tests ===\n\n");

    // Everything this harness compiles via defSynth/defSynthX is born tagged
    // "test" so the plugin browser can filter it out of user-facing lists.
    setenv("TZPL_DEFAULT_TAGS", "test", /*overwrite=*/0);

    test_compile_success();
    test_compile_error();
    test_compile_and_load();
    test_caching();
    test_list_synthdefs();
    test_def_desc_introspection();
    test_control_kinds();
    test_abi_version_refusal();
    test_low_level_ffi();
    test_synthc_analysis_diff();
    test_synthc_rewrite_diff();
    test_synthc_voicer_diff();
    test_synthc_spectral_diff();
    test_synthc_simd_diff();
    test_synthc_voicer_simd_diff();
    test_synthc_buffer_simd_diff();
    test_synthc_prod_diff();
    test_synthc_compile_and_load();

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? 1 : 0;
}
