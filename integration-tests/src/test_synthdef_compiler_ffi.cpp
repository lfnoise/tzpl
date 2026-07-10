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
#include "tzpl_clock_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include <print>
#include <cstdlib>
#include <string_view>
#include <chrono>

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

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char const* argv[]) {
    std::print("=== Synthdef Compiler FFI Integration Tests ===\n\n");

    test_compile_success();
    test_compile_error();
    test_compile_and_load();
    test_caching();
    test_list_synthdefs();
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
