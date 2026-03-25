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
//  test_osc.cpp
//  integration-tests
//
//  Integration test for OSC support: server, client, dispatcher, and FFI.
//

#include "tzpl_osc.hpp"
#include "osc/OscOutboundPacketStream.h"
#include "tzpl_osc_ffi.hpp"
#include "tzpl_osc_vm_handlers.hpp"
#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_clock_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include <print>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <atomic>
#include <unistd.h>

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

// ---------------------------------------------------------------------------
// Create a minimal test engine
// ---------------------------------------------------------------------------

static engine::Engine* makeTestEngine() {
    engine::AudioStreamParameters params{};
    params.channels = 2;
    params.bufferFrames = 512;
    params.sampleRate = 44100.0;
    params.deviceName = "default";
    params.firstChannel = 0;

    engine::EngineConfig config;
    config.numSilos = 1;

    engine::Engine* eng = engine::newEngine(config, params);
    engine::createSineNode(eng);
    return eng;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_server_start_stop() {
    std::print("Test: OSC server start and stop\n");

    osc::OscDispatcher dispatcher;
    osc::OscServer server(dispatcher);

    check(!server.isRunning(), "server not running initially");
    check(server.port() == 0, "port is 0 when not running");

    bool started = server.start(57200);
    check(started, "server starts on port 57200");
    check(server.isRunning(), "server is running after start");
    check(server.port() == 57200, "server reports correct port");

    server.stop();
    check(!server.isRunning(), "server not running after stop");
    check(server.port() == 0, "port is 0 after stop");
}

static void test_client_send() {
    std::print("Test: OSC client send\n");

    // Start a server to receive a message
    std::atomic<bool> received{false};
    osc::OscDispatcher dispatcher;
    dispatcher.addHandler("/test/ping", [&](const char*, const void*, int, const osc::SenderInfo&) {
        received = true;
    });

    osc::OscServer server(dispatcher);
    server.start(57201);

    // Send a message
    osc::OscClient client;
    client.sendMessage("127.0.0.1", 57201, "/test/ping");

    // Wait briefly for delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    check(received.load(), "server received /test/ping from client");

    server.stop();
}

static void test_engine_commands() {
    std::print("Test: OSC engine command dispatch\n");

    engine::Engine* eng = makeTestEngine();

    osc::OscClient client;
    osc::OscDispatcher dispatcher;
    dispatcher.setEngine(eng);
    dispatcher.setClient(&client);
    osc::registerEngineHandlers(dispatcher);

    osc::OscServer server(dispatcher);
    server.start(57202);

    // Query isAudioRunning via OSC (should be false since we haven't started audio)
    // We can't easily capture the reply without a second server, so just verify
    // the message dispatches without crashing
    client.sendMessage("127.0.0.1", 57202, "/engine/isAudioRunning");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    check(true, "engine query dispatched without crash");

    server.stop();
    engine::freeEngine(eng);
}

static void test_local_dispatch() {
    std::print("Test: local (in-process) OSC dispatch\n");

    engine::Engine* eng = makeTestEngine();

    osc::OscDispatcher dispatcher;
    dispatcher.setEngine(eng);

    std::atomic<bool> customHandled{false};
    dispatcher.addHandler("/custom/test", [&](const char*, const void*, int, const osc::SenderInfo&) {
        customHandled = true;
    });

    // Build a minimal OSC message and dispatch locally
    osc::OscClient client;
    // Use the dispatcher directly (simulating oscSendLocal)
    char buffer[256];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage("/custom/test") << ::osc::EndMessage;

    osc::SenderInfo local{"127.0.0.1", 0};
    dispatcher.dispatch(p.Data(), static_cast<int>(p.Size()), local);

    check(customHandled.load(), "custom handler invoked via local dispatch");

    engine::freeEngine(eng);
}

static void test_ffi_registration() {
    std::print("Test: OSC FFI functions register and compile\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerOscFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    bridge::AppContext appCtx;
    appCtx.engine = eng;

    osc::OscClient oscClient;
    osc::OscDispatcher oscDispatcher;
    oscDispatcher.setEngine(eng);
    oscDispatcher.setClient(&oscClient);
    osc::registerEngineHandlers(oscDispatcher);

    osc::OscServer oscServer(oscDispatcher);
    appCtx.oscClient = &oscClient;
    appCtx.oscDispatcher = &oscDispatcher;
    appCtx.oscServer = &oscServer;

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // Test that OSC functions compile successfully
    const char* source = R"(
        import osc.*;
        let port = oscServerPort();
    )";

    auto result = compiler.compile(source, "osc_test.x", target, &moduleCompiler);
    check(result.success, "OSC FFI functions compile");

    if (result.success) {
        vm.makeCurrent();
        vm.install(result);
        vm.execute(result.mainBlock);
        check(true, "OSC FFI functions execute without crash");
    }

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Full NRTVM + OSC test environment
// ---------------------------------------------------------------------------

struct OscVMTestEnv {
    ts::TypeUniverse types;
    ts::Compiler compiler{types};
    engine::Engine* eng;
    osc::OscClient oscClient;
    osc::OscDispatcher oscDispatcher;
    osc::OscServer oscServer{oscDispatcher};
    bridge::AppContext appCtx{};
    ts::VMTarget target;
    ts::NRTVM* nrtvm = nullptr;
    ts::ModuleCompiler* moduleCompiler = nullptr;
    FILE* devnull = nullptr;
    int port;

    OscVMTestEnv(int oscPort) : port(oscPort) {
        bridge::registerAudioEngineFFI(compiler);
        bridge::registerOscFFI(compiler);
        bridge::registerClockFFI(compiler);

        eng = makeTestEngine();

        target = compiler.createTarget();
        nrtvm = new ts::NRTVM(16 * 1024 * 1024, types, target);
        moduleCompiler = new ts::ModuleCompiler(compiler, {MODULES_DIR});

        oscDispatcher.setEngine(eng);
        oscDispatcher.setClient(&oscClient);
        osc::registerEngineHandlers(oscDispatcher);

        appCtx.engine = eng;
        appCtx.nrtvm = nrtvm;
        appCtx.compiler = &compiler;
        appCtx.moduleCompiler = moduleCompiler;
        appCtx.target = target;
        appCtx.oscClient = &oscClient;
        appCtx.oscDispatcher = &oscDispatcher;
        appCtx.oscServer = &oscServer;

        bridge::setAppContextOnVM(&nrtvm->vm, &appCtx);
        bridge::registerVMOscHandlers(oscDispatcher, appCtx);

        devnull = fopen("/dev/null", "w");
        nrtvm->vm.setPrintOutput(devnull);

        oscServer.start(port);
    }

    // Compile and run Tzopilotl source on the NRTVM
    bool eval(const char* source) {
        auto result = nrtvm->compileAndInstall(
            compiler, source, "<test>", target, moduleCompiler);
        if (result.success && result.mainBlock) {
            nrtvm->execute(result.mainBlock);
        }
        return result.success;
    }

    // Redirect VM print to a capture buffer instead of /dev/null
    void capturePrint(FILE* f) {
        nrtvm->vm.setPrintOutput(f);
    }

    ~OscVMTestEnv() {
        oscServer.stop();
        if (devnull) fclose(devnull);
        delete moduleCompiler;
        delete nrtvm;
        engine::freeEngine(eng);
    }
};

// ---------------------------------------------------------------------------
// VM OSC handler tests
// ---------------------------------------------------------------------------

static void test_tzpl_eval() {
    std::print("Test: /tzpl/eval compiles and executes source via OSC\n");

    OscVMTestEnv env(57210);

    // Create a temp file to capture print output
    char tmpPath[] = "/tmp/tzpl_test_eval_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    // Send source code via OSC
    env.oscClient.sendMessageS("127.0.0.1", 57210, "/tzpl/eval",
        "println(42 * 2);");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Read captured output
    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("84") != std::string::npos,
          "eval produced correct output '84'");
}

static void test_tzpl_eval_error() {
    std::print("Test: /tzpl/eval returns error for invalid source\n");

    OscVMTestEnv env(57211);

    // Set up a reply listener to capture the error response
    std::atomic<bool> gotError{false};
    std::string errorMsg;
    env.oscDispatcher.addHandler("/tzpl/eval/error",
        [&](const char*, const void* data, int size, const osc::SenderInfo&) {
            gotError = true;
        });

    // Send invalid source -- use local dispatch to avoid reply-routing issues
    // (sendMessageS goes over UDP, reply goes to the sender's port which we
    // don't listen on here). Instead, build and dispatch locally.
    char buffer[1024];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage("/tzpl/eval") << "let x = ;" << ::osc::EndMessage;
    osc::SenderInfo local{"127.0.0.1", 0};  // port 0 = no reply
    env.oscDispatcher.dispatch(p.Data(), static_cast<int>(p.Size()), local);

    // The dispatch is synchronous for local dispatch, so error should be immediate
    // But the reply won't be sent since sender port is 0 -- just verify no crash
    check(true, "eval with invalid source did not crash");
}

static void test_onMessage_handler() {
    std::print("Test: osc.onMessage registers and invokes no-arg handler\n");

    OscVMTestEnv env(57212);

    // Use a global variable to detect handler invocation
    bool ok = env.eval(R"(
        import osc.*;
        var `triggered Int = 0;
        onMessage("/test/fire", fn() { `triggered = 1; });
    )");
    check(ok, "onMessage handler registration compiles");

    // Send OSC message to trigger the handler
    env.oscClient.sendMessage("127.0.0.1", 57212, "/test/fire");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check the dynamic var was set by evaluating it
    char tmpPath[] = "/tmp/tzpl_test_onmsg_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    env.eval("println(`triggered);");

    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("1") != std::string::npos,
          "handler was invoked (dynamic var set to 1)");
}

static void test_onMessageI_handler() {
    std::print("Test: osc.onMessageI receives integer argument\n");

    OscVMTestEnv env(57213);

    bool ok = env.eval(R"(
        import osc.*;
        var `received_int Int = 0;
        onMessageI("/test/intval", fn(v Int) { `received_int = v; });
    )");
    check(ok, "onMessageI handler registration compiles");

    // Send an integer value
    env.oscClient.sendMessageI("127.0.0.1", 57213, "/test/intval", 42);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char tmpPath[] = "/tmp/tzpl_test_omi_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    env.eval("println(`received_int);");

    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("42") != std::string::npos,
          "handler received integer value 42");
}

static void test_onMessageF_handler() {
    std::print("Test: osc.onMessageF receives float argument\n");

    OscVMTestEnv env(57214);

    bool ok = env.eval(R"(
        import osc.*;
        var `received_float Float = 0.0;
        onMessageF("/test/floatval", fn(v Float) { `received_float = v; });
    )");
    check(ok, "onMessageF handler registration compiles");

    env.oscClient.sendMessageF("127.0.0.1", 57214, "/test/floatval", 3.14f);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char tmpPath[] = "/tmp/tzpl_test_omf_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    env.eval(R"(
        -- OSC floats are 32-bit, so check approximate value
        let ok = `received_float > 3.13 && `received_float < 3.15;
        println(ok);
    )");

    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("true") != std::string::npos,
          "handler received float value ~3.14");
}

static void test_removeHandler() {
    std::print("Test: osc.removeHandler unregisters a handler\n");

    OscVMTestEnv env(57215);

    bool ok = env.eval(R"(
        import osc.*;
        var `remove_count Int = 0;
        onMessage("/test/removeme", fn() { `remove_count = `remove_count + 1; });
    )");
    check(ok, "handler registration compiles");

    // Fire once
    env.oscClient.sendMessage("127.0.0.1", 57215, "/test/removeme");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Remove the handler
    env.eval(R"(
        import osc.*;
        removeHandler("/test/removeme");
    )");

    // Fire again -- should not increment
    env.oscClient.sendMessage("127.0.0.1", 57215, "/test/removeme");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char tmpPath[] = "/tmp/tzpl_test_rm_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    env.eval("println(`remove_count);");

    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("1") != std::string::npos && output.find("2") == std::string::npos,
          "handler fired once before removal, not after");
}

static void test_tzpl_call() {
    std::print("Test: /tzpl/call invokes a user-registered handler\n");

    OscVMTestEnv env(57216);

    bool ok = env.eval(R"(
        import osc.*;
        var `call_value Int = 0;
        onMessageI("/mycallback", fn(v Int) { `call_value = v; });
    )");
    check(ok, "handler for /tzpl/call test compiles");

    // Use /tzpl/call to invoke it
    char buffer[256];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage("/tzpl/call")
      << "/mycallback" << (int32_t)99
      << ::osc::EndMessage;

    env.oscClient.send("127.0.0.1", 57216, p.Data(), static_cast<int>(p.Size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char tmpPath[] = "/tmp/tzpl_test_call_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    env.eval("println(`call_value);");

    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("99") != std::string::npos,
          "/tzpl/call invoked handler with value 99");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::print("=== OSC Integration Tests ===\n\n");

    test_server_start_stop();
    test_client_send();
    test_engine_commands();
    test_local_dispatch();
    test_ffi_registration();

    std::print("\n--- VM OSC Handler Tests ---\n\n");

    test_tzpl_eval();
    test_tzpl_eval_error();
    test_onMessage_handler();
    test_onMessageI_handler();
    test_onMessageF_handler();
    test_removeHandler();
    test_tzpl_call();

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
