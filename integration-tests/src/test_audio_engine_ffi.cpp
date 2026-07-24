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
#include "tzpl_clock_ffi.hpp"
#include "tzpl_nrt_render.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include "tzpl_mixer.hpp"
#include "tzpl_xfader.hpp"
#include "tzpl_engine.hpp"
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

    // Test begin/sched without starting audio (commands run synchronously).
    // The silo is chosen at submit time.
    const char* source = R"(
        import audio_engine.*;
        let err1 = begin();
        let err2 = sched(0);
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
        let err = go(3.14);
    )";

    auto result = compiler.compile(badSource, "bad_types.x", target, &moduleCompiler);
    check(!result.success, "Type mismatch correctly rejected at compile time");

    // The old API shape must not compile: begin no longer takes a silo.
    const char* oldApiSource = R"(
        import audio_engine.*;
        let err = begin(0);
    )";

    auto result2 = compiler.compile(oldApiSource, "old_api.x", target, &moduleCompiler);
    check(!result2.success, "Old begin(silo) form correctly rejected at compile time");
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

    // Try to create a node with a def name that doesn't exist. The builder
    // itself succeeds (it only records the op); the validation error
    // surfaces when the bundle is submitted.
    const char* source = R"(
        import audio_engine.*;
        let e1 = begin();
        let e2 = newNode("NonExistent", 100);
        let e3 = sched(0);
        println(e1);
        println(e2);
        println(e3);
    )";

    bool ok = compileAndRun(compiler, vm, source, "node_connect.x", &moduleCompiler);
    check(ok, "Node/connect source compiles and runs (error code expected for missing def)");
    check(eng->silos_[0].nrt_getNode(100) == nullptr,
          "Failed bundle left no node behind");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_atomic_abort() {
    std::print("Test: Bundle validation at submit (atomic abort)\n");

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
    engine::createSineNode(eng); // registers the "sinosc" def

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    const char* source = R"(
        import audio_engine.*;
        -- A valid newNode followed by a failing one: the whole bundle must
        -- be discarded, including the already-materialized node 100.
        begin();
        newNode("sinosc", 100);
        newNode("NonExistent", 101);
        let e1 = sched(0);

        -- Re-creating the same node ID must succeed: the aborted bundle
        -- unwound node 100 from the silo's table.
        begin();
        newNode("sinosc", 100);
        let e2 = go(0);

        -- Out-of-range silo at submit discards and closes the bundle.
        begin();
        newNode("sinosc", 200);
        let e3 = sched(7);
        let e4 = begin();
        let e5 = go(0);
    )";

    bool ok = compileAndRun(compiler, vm, source, "atomic_abort.x", &moduleCompiler);
    check(ok, "Atomic abort source compiles and runs");
    check(eng->silos_[0].nrt_getNode(100) != nullptr,
          "Node 100 exists after abort-then-recreate (abort unwound the first attempt)");
    check(eng->silos_[0].nrt_getNode(101) == nullptr, "Node 101 was never created");
    check(eng->silos_[0].nrt_getNode(200) == nullptr,
          "Bad-silo submit discarded its bundle (node 200 absent)");

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Mixer fan-in tests -- exercise hidden mixer node creation and lifecycle
// using low-level Silo primitives (no audio thread needed).
// ---------------------------------------------------------------------------

// Helper: create a minimal 1-output node (like a source generator)
static engine::Node* makeTestSource(engine::Engine* e, engine::Silo* silo,
                                     tzpl_SignalType type) {
    tzpl_SynthFuns funs = {0};
    funs.alloc = []() -> tzpl_SynthData* { return new tzpl_SynthData(); };
    funs.free = [](tzpl_SynthData* s) -> tzpl_SErr { delete s; return tzpl_errNone; };
    funs.processAudio = [](tzpl_SynthData*) {};

    engine::NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    info.name = "test_src";
    info.num_outs = 1;
    info.outs = (engine::PortInfo*)calloc(1, sizeof(engine::PortInfo));
    info.outs[0] = engine::PortInfo{"out", type};
    info.funs = funs;

    auto* node = new engine::Node(e, silo, info);
    free(info.outs);
    return node;
}

// Helper: create a minimal 1-input, 1-output node (like a processor/destination)
static engine::Node* makeTestDest(engine::Engine* e, engine::Silo* silo,
                                   tzpl_SignalType type) {
    tzpl_SynthFuns funs = {0};
    funs.alloc = []() -> tzpl_SynthData* { return new tzpl_SynthData(); };
    funs.free = [](tzpl_SynthData* s) -> tzpl_SErr { delete s; return tzpl_errNone; };
    funs.processAudio = [](tzpl_SynthData*) {};

    engine::NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    info.name = "test_dst";
    info.num_ins = 1;
    info.num_outs = 1;
    info.ins = (engine::PortInfo*)calloc(1, sizeof(engine::PortInfo));
    info.ins[0] = engine::PortInfo{"in", type};
    info.outs = (engine::PortInfo*)calloc(1, sizeof(engine::PortInfo));
    info.outs[0] = engine::PortInfo{"out", type};
    info.funs = funs;

    auto* node = new engine::Node(e, silo, info);
    free(info.ins);
    free(info.outs);
    return node;
}

static void test_mixer_fanin() {
    using namespace engine;
    std::print("Test: Mixer fan-in\n");

    AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 64;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    EngineConfig config;
    config.numSilos = 1;

    Engine* eng = newEngine(config, params);
    Silo& silo = eng->silos_[0];
    tzpl_SignalType stereoF32{tzpl_kF32, tzpl_audioRate, 2};

    // --- Test 1: 2-source fan-in ---
    {
        std::print("  [1] 2-source fan-in\n");
        Node* A = makeTestSource(eng, &silo, stereoF32);
        Node* B = makeTestSource(eng, &silo, stereoF32);
        // Use the output node (silo.outputNode_, nodeID=0) as destination
        InPort* dst = &silo.outputNode_->ins[0];

        // First connect: direct (no mixer)
        silo.connect(&A->outs[0], dst);
        check(dst->mixerNode_ == nullptr, "1st connect: no mixer");
        check(dst->srcPort_ == &A->outs[0], "1st connect: direct to A");

        // Second connect: should create mixer via spliceMixer
        Node* mixer = newMixerNode(eng, &silo, stereoF32, 4);
        spliceMixer(&silo, mixer, &B->outs[0], dst);
        check(dst->mixerNode_ == mixer, "2nd connect: mixer created");
        check(dst->srcPort_ == &mixer->outs[0], "2nd connect: dst reads from mixer");
        check(mixer->ins[0].srcPort_ == &A->outs[0], "2nd connect: mixer.in[0] = A");
        check(mixer->ins[1].srcPort_ == &B->outs[0], "2nd connect: mixer.in[1] = B");

        // Clean up
        silo.disconnectNode(mixer);
        silo.disconnectNode(A);
        silo.disconnectNode(B);
        dst->mixerNode_ = nullptr;
        delete mixer; delete A; delete B;
    }

    // --- Test 2: 3+ source fan-in ---
    {
        std::print("  [2] 3-source fan-in\n");
        Node* A = makeTestSource(eng, &silo, stereoF32);
        Node* B = makeTestSource(eng, &silo, stereoF32);
        Node* C = makeTestSource(eng, &silo, stereoF32);
        InPort* dst = &silo.outputNode_->ins[0];

        silo.connect(&A->outs[0], dst);
        Node* mixer = newMixerNode(eng, &silo, stereoF32, 4);
        spliceMixer(&silo, mixer, &B->outs[0], dst);

        // Third source: add to existing mixer
        MixerSlot slot = findFreeMixerSlot(mixer);
        check((bool)slot, "3rd connect: free slot found");
        addToMixer(&silo, slot.mixer, &C->outs[0], slot.slot);
        check(mixer->ins[slot.slot].srcPort_ == &C->outs[0], "3rd connect: mixer.in[slot] = C");
        check(countActiveMixerInputs(mixer) == 3, "3rd connect: 3 active inputs");

        silo.disconnectNode(mixer);
        silo.disconnectNode(A);
        silo.disconnectNode(B);
        silo.disconnectNode(C);
        dst->mixerNode_ = nullptr;
        delete mixer; delete A; delete B; delete C;
    }

    // --- Test 3: Remove source, collapse to direct ---
    {
        std::print("  [3] Remove source, collapse mixer\n");
        Node* A = makeTestSource(eng, &silo, stereoF32);
        Node* B = makeTestSource(eng, &silo, stereoF32);
        InPort* dst = &silo.outputNode_->ins[0];

        silo.connect(&A->outs[0], dst);
        Node* mixer = newMixerNode(eng, &silo, stereoF32, 4);
        spliceMixer(&silo, mixer, &B->outs[0], dst);
        check(dst->mixerNode_ == mixer, "before collapse: mixer exists");

        // Remove B from mixer
        removeFromMixer(&silo, mixer, &B->outs[0]);
        check(countActiveMixerInputs(mixer) == 1, "after remove: 1 active input");

        // Collapse: should restore direct A -> dst
        collapseMixer(&silo, dst);
        check(dst->mixerNode_ == nullptr, "after collapse: no mixer");
        check(dst->srcPort_ == &A->outs[0], "after collapse: direct to A");

        // mixer was pushed to dead nodes by collapseMixer, but we handle
        // cleanup manually in tests
        silo.disconnectNode(A);
        silo.disconnectNode(B);
        delete A; delete B;
        // mixer deleted by collapseMixer -> pushDeadNode; skip manual delete
    }

    // --- Test 4: freeNode on source (simulated) ---
    {
        std::print("  [4] Disconnect source from mixer, collapse\n");
        Node* A = makeTestSource(eng, &silo, stereoF32);
        Node* B = makeTestSource(eng, &silo, stereoF32);
        InPort* dst = &silo.outputNode_->ins[0];

        silo.connect(&A->outs[0], dst);
        Node* mixer = newMixerNode(eng, &silo, stereoF32, 4);
        spliceMixer(&silo, mixer, &B->outs[0], dst);

        // Simulate freeNode(A): disconnect A's output, then check mixer
        silo.disconnect(&A->outs[0]);
        check(mixer->ins[0].srcPort_ == nullptr, "A disconnected from mixer");
        check(countActiveMixerInputs(mixer) == 1, "1 active input remains");

        // Collapse
        collapseMixer(&silo, dst);
        check(dst->mixerNode_ == nullptr, "collapsed after A freed");
        check(dst->srcPort_ == &B->outs[0], "B directly connected");

        silo.disconnectNode(B);
        delete A; delete B;
    }

    // --- Test 5: reconnectOutput through mixer ---
    {
        std::print("  [5] reconnectOutput through mixer\n");
        Node* A = makeTestSource(eng, &silo, stereoF32);
        Node* B = makeTestSource(eng, &silo, stereoF32);
        Node* C = makeTestSource(eng, &silo, stereoF32);
        InPort* dst = &silo.outputNode_->ins[0];

        silo.connect(&A->outs[0], dst);
        Node* mixer = newMixerNode(eng, &silo, stereoF32, 4);
        spliceMixer(&silo, mixer, &B->outs[0], dst);
        check(mixer->ins[0].srcPort_ == &A->outs[0], "before reconnect: A on slot 0");

        // reconnectOutput: move A's connections to C
        // This uses Silo::reconnectOutput which calls Silo::connect (replacement)
        silo.reconnectOutput(&A->outs[0], &C->outs[0]);
        check(mixer->ins[0].srcPort_ == &C->outs[0], "after reconnect: C on slot 0");
        check(mixer->ins[1].srcPort_ == &B->outs[0], "after reconnect: B still on slot 1");

        silo.disconnectNode(mixer);
        silo.disconnectNode(A);
        silo.disconnectNode(B);
        silo.disconnectNode(C);
        dst->mixerNode_ = nullptr;
        delete mixer; delete A; delete B; delete C;
    }

    // --- Test 6: findFreeMixerSlot and countActiveMixerInputs ---
    {
        std::print("  [6] Slot management\n");
        Node* A = makeTestSource(eng, &silo, stereoF32);
        Node* B = makeTestSource(eng, &silo, stereoF32);
        InPort* dst = &silo.outputNode_->ins[0];

        Node* mixer = newMixerNode(eng, &silo, stereoF32, 4);
        check(countActiveMixerInputs(mixer) == 0, "new mixer: 0 active");
        check(findFreeMixerSlot(mixer).slot == 0, "new mixer: slot 0 free");

        silo.connect(&A->outs[0], &mixer->ins[0]);
        silo.connect(&B->outs[0], &mixer->ins[1]);
        check(countActiveMixerInputs(mixer) == 2, "2 connected: 2 active");
        check(findFreeMixerSlot(mixer).slot == 2, "2 connected: slot 2 free");

        silo.disconnect(&mixer->ins[0]);
        check(countActiveMixerInputs(mixer) == 1, "1 disconnected: 1 active");
        check(findFreeMixerSlot(mixer).slot == 0, "1 disconnected: slot 0 free (reused)");

        silo.disconnectNode(mixer);
        silo.disconnectNode(A);
        silo.disconnectNode(B);
        delete mixer; delete A; delete B;
    }

    freeEngine(eng);
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
    // The scripts import clock.*, which re-exports the clock_ffi foreign
    // module, so it must be registered for them to compile. No tempo
    // scheduler is installed in the AppContext, so clock callbacks never
    // fire here -- these tests assert that the scripts compile and their
    // main blocks execute, not the full timed sequences. test3.x also
    // references the NRT-render FFI (renderNRT / renderDone).
    bridge::registerClockFFI(compiler);
    bridge::registerNRTRenderFFI(compiler);

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
    {"bundle_test.x", 1,  48000.,  256, true, false, false, false},
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
    test_atomic_abort();
    test_mixer_fanin();
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
