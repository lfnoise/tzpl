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
//  test_audio_engine_ffi.cpp
//  integration-tests
//
//  Integration test for the engine FFI bridge.
//  Runs both quick compilation tests and script-based audio tests.
//

#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include <print>
#include <cstdlib>
#include <string_view>
#include <fstream>
#include <sstream>

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

// Read a file from disk into a string.
static std::string readFile(std::string const& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::print("  Cannot open file: {}\n", path);
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------
// Quick compilation / binding tests (no audio output)
// ---------------------------------------------------------------------------

static void test_constants() {
    std::print("Test: Enum constants are accessible via module import\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});

    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);

    const char* source = R"(
        import audio_engine.*;
        let s1 = ordinal(SchedPolicy.schedImmediate);
        let s2 = ordinal(SchedPolicy.schedBetterLateThanNever);
        let s3 = ordinal(SchedPolicy.schedOnTimeOnly);
        let f1 = ordinal(FadeCurve.fadeLinear);
        let f2 = ordinal(FadeCurve.fadeExponential);
        let f3 = ordinal(FadeCurve.fadeSmoothstep);
        let f4 = ordinal(FadeCurve.fadeEaseInCubic);
        let f5 = ordinal(FadeCurve.fadeEaseOutCubic);
        let f6 = ordinal(FadeCurve.fadeOutIn);
        let e0 = ordinal(Err.errNone);
    )";

    bool ok = compileAndRun(compiler, vm, source, "constants.x", &moduleCompiler);
    check(ok, "Enum constants compile and run via module import");
}

static void test_engine_lifecycle() {
    std::print("Test: Engine lifecycle FFI functions\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    // Create a real engine for this test
    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    engine::EngineConfig config;
    config.numSilos = 1;

    engine::Engine* eng = engine::newEngine(config, params);
    check(eng != nullptr, "Engine created successfully");

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    // Test isAudioRunning via Tzopilotl
    const char* source = R"(
        import audio_engine.*;
        let running = isAudioRunning();
    )";

    // Redirect VM output to /dev/null for clean test output
    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    bool ok = compileAndRun(compiler, vm, source, "lifecycle.x", &moduleCompiler);
    check(ok, "Lifecycle source compiles and runs");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_command_bundling() {
    std::print("Test: Command bundling FFI (begin/go)\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    engine::EngineConfig config;
    config.numSilos = 1;

    engine::Engine* eng = engine::newEngine(config, params);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Test begin/go without starting audio (commands run synchronously)
    const char* source = R"(
        import audio_engine.*;
        let err1 = begin(0);
        let err2 = sched();
    )";

    bool ok = compileAndRun(compiler, vm, source, "bundling.x", &moduleCompiler);
    check(ok, "Command bundling source compiles and runs");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_type_checking() {
    std::print("Test: FFI type checking\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});
    auto target = compiler.createTarget();

    // This should fail to compile: wrong argument type
    const char* badSource = R"(
        import audio_engine.*;
        let err = begin(3.14);
    )";

    auto result = compiler.compile(badSource, "bad_types.x", target, &moduleCompiler);
    check(!result.success, "Type mismatch correctly rejected at compile time");
}

static void test_node_and_connect() {
    std::print("Test: Node creation and connection FFI\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    engine::EngineConfig config;
    config.numSilos = 1;

    engine::Engine* eng = engine::newEngine(config, params);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Try to create a node with a def name that doesn't exist yet — should error
    const char* source = R"(
        import audio_engine.*;
        let e1 = begin(0);
        let e2 = newNode("NonExistent", 100);
        let e3 = sched();
        println(e2);
    )";

    bool ok = compileAndRun(compiler, vm, source, "node_connect.x", &moduleCompiler);
    check(ok, "Node/connect source compiles and runs (error code expected for missing def)");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_master_gain() {
    std::print("Test: masterGain FFI\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    engine::EngineConfig config;
    config.numSilos = 1;

    engine::Engine* eng = engine::newEngine(config, params);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    const char* source = R"(
        import audio_engine.*;
        masterGain(0.5);
        safetyLimiter(true);
    )";

    bool ok = compileAndRun(compiler, vm, source, "master_gain.x", &moduleCompiler);
    check(ok, "masterGain/safetyLimiter calls compile and run");

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Script-based audio tests
// ---------------------------------------------------------------------------

struct ScriptTestConfig {
    const char* scriptFile;
    int numSilos;
    double sampleRate;
    int bufferFrames;
    bool needSine;
    bool needAdd;
    bool needMul;
    bool needVoicer;
};

static void runScriptTest(ScriptTestConfig const& cfg) {
    std::string scriptDir = SCRIPTS_DIR;
    std::string path = scriptDir + "/" + cfg.scriptFile;
    std::print("\nTest: Running script {}\n", cfg.scriptFile);

    std::string source = readFile(path);
    if (source.empty()) {
        check(false, std::string("Script loaded: ") + cfg.scriptFile);
        return;
    }
    check(true, std::string("Script loaded: ") + cfg.scriptFile);

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});

    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = cfg.bufferFrames;
    params.sampleRate = cfg.sampleRate;
    params.deviceName = "default";
    params.firstChannel = 0;

    engine::EngineConfig config;
    config.numSilos = cfg.numSilos;

    engine::Engine* eng = engine::newEngine(config, params);
    if (!eng) {
        check(false, std::string("Engine created: ") + cfg.scriptFile);
        return;
    }

    if (cfg.needSine)   engine::createSineNode(eng);
    if (cfg.needAdd)    engine::createAddOpNode(eng);
    if (cfg.needMul)    engine::createMulOpNode(eng);
    if (cfg.needVoicer) engine::createVoicerTestNode(eng);

    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    auto result = compiler.compile(source, cfg.scriptFile, target, &moduleCompiler);
    if (!result.success) {
        std::print("  Compilation FAILED for '{}':\n", cfg.scriptFile);
        for (auto& err : result.errors) {
            std::print("    {}\n", err.message);
        }
        check(false, std::string("Script compiles: ") + cfg.scriptFile);
        engine::freeEngine(eng);
        return;
    }
    check(true, std::string("Script compiles: ") + cfg.scriptFile);

    vm.makeCurrent();
    vm.install(result);
    vm.execute(result.mainBlock);

    check(true, std::string("Script executes: ") + cfg.scriptFile);
    engine::freeEngine(eng);
}

static constexpr ScriptTestConfig kScriptTests[] = {
    // scriptFile  silos  rate     buf  sine  add   mul   voicer
    {"test0.x",     1,    48000.,  256, true, false, false, false},
    {"test5.x",     1,    48000.,  256, true, true,  false, false},
    {"test1.x",     2,    96000.,  256, true, true,  true,  false},
    {"test2.x",     8,    96000.,  256, true, true,  false, false},
    {"test4.x",    10,    48000.,  256, true, false, false, false},
    {"test3.x",     1,    96000.,  256, false, false, false, true},
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char const* argv[]) {
    std::print("=== Audio Engine FFI Integration Tests ===\n\n");

    // Quick compilation tests (no audio)
    test_constants();
    test_engine_lifecycle();
    test_command_bundling();
    test_type_checking();
    test_node_and_connect();
    test_master_gain();

    // Script-based audio tests
    std::print("\n=== Script-Based Audio Tests ===\n");

    if (argc > 1) {
        // Run a specific script by name
        std::string_view requested = argv[1];
        for (auto const& cfg : kScriptTests) {
            if (requested == cfg.scriptFile) {
                runScriptTest(cfg);
                break;
            }
        }
    } else {
        // Run all scripts in order
        for (auto const& cfg : kScriptTests) {
            runScriptTest(cfg);
        }
    }

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? 1 : 0;
}
