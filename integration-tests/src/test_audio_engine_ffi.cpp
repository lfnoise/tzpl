//
//  test_audio_engine_ffi.cpp
//  integration-tests
//
//  Integration test for the audio-engine FFI bridge.
//  Creates a Language X VM with registered engine FFI functions,
//  compiles and executes Language X code that exercises the bindings.
//

#include "langx_audio_engine_ffi.hpp"
#include "langx.hpp"
#include "jscs_client_interface.hpp"
#include <print>
#include <cstdlib>
#include <string_view>

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

// Compile and run a Language X source string. Returns true on compilation success.
static bool compileAndRun(ts::Compiler& compiler, ts::VM& vm,
                          const char* source, const char* testName) {
    auto target = vm.target();
    auto result = compiler.compile(source, testName, target);
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_constants() {
    std::print("Test: FFI constants are accessible\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);

    const char* source = R"(
        let s1 = schedImmediate();
        let s2 = schedBetterLateThanNever();
        let s3 = schedOnTimeOnly();
        let f1 = fadeLinear();
        let f2 = fadeExponential();
        let e0 = errNone();
    )";

    bool ok = compileAndRun(compiler, vm, source, "constants.x");
    check(ok, "Constants source compiles and runs");
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

    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::setEngineOnVM(&vm, eng);

    // Test isAudioRunning via Language X
    const char* source = R"(
        let running = isAudioRunning();
    )";

    // Redirect VM output to /dev/null for clean test output
    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    bool ok = compileAndRun(compiler, vm, source, "lifecycle.x");
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

    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::setEngineOnVM(&vm, eng);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Test begin/go without starting audio (commands run synchronously)
    const char* source = R"(
        let err1 = begin(0);
        let err2 = go();
    )";

    bool ok = compileAndRun(compiler, vm, source, "bundling.x");
    check(ok, "Command bundling source compiles and runs");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_type_checking() {
    std::print("Test: FFI type checking\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);

    auto target = compiler.createTarget();

    // This should fail to compile: wrong argument type
    const char* badSource = R"(
        let err = begin(3.14);
    )";

    auto result = compiler.compile(badSource, "bad_types.x", target);
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

    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::setEngineOnVM(&vm, eng);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Try to create a node with a def name that doesn't exist yet — should error
    const char* source = R"(
        let e1 = begin(0);
        let e2 = newNode("NonExistent", 100);
        let e3 = go();
        println(e2);
    )";

    bool ok = compileAndRun(compiler, vm, source, "node_connect.x");
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

    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::setEngineOnVM(&vm, eng);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    const char* source = R"(
        masterGain(0.5);
        safetyLimiter(true);
    )";

    bool ok = compileAndRun(compiler, vm, source, "master_gain.x");
    check(ok, "masterGain/safetyLimiter calls compile and run");

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::print("=== Audio Engine FFI Integration Tests ===\n\n");

    test_constants();
    test_engine_lifecycle();
    test_command_bundling();
    test_type_checking();
    test_node_and_connect();
    test_master_gain();

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? 1 : 0;
}
