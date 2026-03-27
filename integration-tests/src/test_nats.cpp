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
//  test_nats.cpp
//  integration-tests
//
//  Integration test for NATS support: client, dispatcher, and FFI.
//  Requires a running NATS server on localhost:4222.
//

#include "tzpl_nats.hpp"
#include "tzpl_nats_ffi.hpp"
#include "tzpl_nats_vm_handlers.hpp"
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
static int gTestsSkipped = 0;

static void check(bool condition, std::string_view description) {
    if (condition) {
        std::print("  PASS: {}\n", description);
        ++gTestsPassed;
    } else {
        std::print("  FAIL: {}\n", description);
        ++gTestsFailed;
    }
}

static void skip(std::string_view description) {
    std::print("  SKIP: {}\n", description);
    ++gTestsSkipped;
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
// Check if NATS server is available
// ---------------------------------------------------------------------------

static bool natsServerAvailable() {
    nats::NatsClient client;
    bool ok = client.connect("nats://127.0.0.1:4222");
    if (ok) client.disconnect();
    return ok;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_connect_disconnect() {
    std::print("Test: NATS client connect and disconnect\n");

    nats::NatsClient client;
    check(!client.isConnected(), "client not connected initially");
    check(client.url().empty(), "url is empty when not connected");

    bool ok = client.connect("nats://127.0.0.1:4222");
    check(ok, "client connects to localhost:4222");
    check(client.isConnected(), "client is connected after connect");
    check(client.url() == "nats://127.0.0.1:4222", "url is correct");

    client.disconnect();
    check(!client.isConnected(), "client not connected after disconnect");
    check(client.url().empty(), "url is empty after disconnect");
}

static void test_publish_subscribe() {
    std::print("Test: NATS publish and subscribe\n");

    nats::NatsClient client;
    client.connect("nats://127.0.0.1:4222");

    std::atomic<bool> received{false};
    std::string receivedData;

    nats::NatsDispatcher dispatcher;
    dispatcher.addHandler("test.ping", [&](const char*, const char* data, int len, const char*) {
        receivedData = std::string(data, len);
        received = true;
    });
    dispatcher.subscribeAll(client);

    // Publish a message
    client.publish("test.ping", "hello");

    // Wait briefly for delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    check(received.load(), "subscriber received message");
    check(receivedData == "hello", "received correct payload");

    dispatcher.unsubscribeAll();
    client.disconnect();
}

static void test_request_reply() {
    std::print("Test: NATS request/reply\n");

    nats::NatsClient client;
    client.connect("nats://127.0.0.1:4222");

    nats::NatsDispatcher dispatcher;
    dispatcher.setClient(&client);
    dispatcher.addHandler("test.echo", [&](const char*, const char* data, int len, const char* reply) {
        if (reply && reply[0]) {
            client.publish(reply, data, len);
        }
    });
    dispatcher.subscribeAll(client);

    // Request
    std::string reply = client.request("test.echo", "world", 5, 1000);
    check(reply == "world", "request/reply echoes payload");

    dispatcher.unsubscribeAll();
    client.disconnect();
}

static void test_engine_commands() {
    std::print("Test: NATS engine command dispatch\n");

    engine::Engine* eng = makeTestEngine();

    nats::NatsClient client;
    client.connect("nats://127.0.0.1:4222");

    nats::NatsDispatcher dispatcher;
    dispatcher.setEngine(eng);
    dispatcher.setClient(&client);
    nats::registerEngineHandlers(dispatcher);
    dispatcher.subscribeAll(client);

    // Query isAudioRunning via NATS (should be false)
    std::string reply = client.request("engine.isAudioRunning", "", 0, 1000);
    check(reply == "0", "engine.isAudioRunning returns 0 when not running");

    dispatcher.unsubscribeAll();
    client.disconnect();
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Full NRTVM + NATS test environment
// ---------------------------------------------------------------------------

struct NatsVMTestEnv {
    ts::TypeUniverse types;
    ts::Compiler compiler{types};
    engine::Engine* eng;
    nats::NatsClient natsClient;
    nats::NatsDispatcher natsDispatcher;
    bridge::AppContext appCtx{};
    ts::VMTarget target;
    ts::NRTVM* nrtvm = nullptr;
    ts::ModuleCompiler* moduleCompiler = nullptr;
    FILE* devnull = nullptr;

    NatsVMTestEnv() {
        bridge::registerAudioEngineFFI(compiler);
        bridge::registerNatsFFI(compiler);
        bridge::registerClockFFI(compiler);

        eng = makeTestEngine();

        target = compiler.createTarget();
        nrtvm = new ts::NRTVM(16 * 1024 * 1024, types, target);
        moduleCompiler = new ts::ModuleCompiler(compiler, {MODULES_DIR});

        natsDispatcher.setEngine(eng);
        natsDispatcher.setClient(&natsClient);
        nats::registerEngineHandlers(natsDispatcher);

        appCtx.engine = eng;
        appCtx.nrtvm = nrtvm;
        appCtx.compiler = &compiler;
        appCtx.moduleCompiler = moduleCompiler;
        appCtx.target = target;
        appCtx.natsClient = &natsClient;
        appCtx.natsDispatcher = &natsDispatcher;

        bridge::setAppContextOnVM(&nrtvm->vm, &appCtx);
        bridge::registerVMNatsHandlers(natsDispatcher, appCtx);

        devnull = fopen("/dev/null", "w");
        nrtvm->vm.setPrintOutput(devnull);

        natsClient.connect("nats://127.0.0.1:4222");
        natsDispatcher.subscribeAll(natsClient);
    }

    bool eval(const char* source) {
        auto result = nrtvm->compileAndInstall(
            compiler, source, "<test>", target, moduleCompiler);
        if (result.success && result.mainBlock) {
            nrtvm->execute(result.mainBlock);
        }
        return result.success;
    }

    void capturePrint(FILE* f) {
        nrtvm->vm.setPrintOutput(f);
    }

    ~NatsVMTestEnv() {
        natsDispatcher.unsubscribeAll();
        natsClient.disconnect();
        if (devnull) fclose(devnull);
        delete moduleCompiler;
        delete nrtvm;
        engine::freeEngine(eng);
    }
};

// ---------------------------------------------------------------------------
// VM NATS handler tests
// ---------------------------------------------------------------------------

static void test_tzpl_eval() {
    std::print("Test: tzpl.eval compiles and executes source via NATS\n");

    NatsVMTestEnv env;

    char tmpPath[] = "/tmp/tzpl_test_nats_eval_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    // Send source code via NATS
    env.natsClient.publish("tzpl.eval", "println(42 * 2);");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

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

static void test_ffi_registration() {
    std::print("Test: NATS FFI functions register and compile\n");

    NatsVMTestEnv env;

    const char* source = R"(
        import nats.*;
        let connected = natsIsConnected();
        let url = natsUrl();
    )";

    bool ok = env.eval(source);
    check(ok, "NATS FFI functions compile and execute");
}

static void test_onMessage_handler() {
    std::print("Test: nats.onMessage registers and invokes no-arg handler\n");

    NatsVMTestEnv env;

    bool ok = env.eval(R"(
        import nats.*;
        var `triggered Int = 0;
        onMessage("test.fire", fn() { `triggered = 1; });
    )");
    check(ok, "onMessage handler registration compiles");

    // Re-subscribe to pick up the new handler
    env.natsDispatcher.subscribeAll(env.natsClient);

    // Send message to trigger the handler
    env.natsClient.publish("test.fire", "");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    char tmpPath[] = "/tmp/tzpl_test_nats_onmsg_XXXXXX";
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

static void test_onMessageS_handler() {
    std::print("Test: nats.onMessageS receives string payload\n");

    NatsVMTestEnv env;

    bool ok = env.eval(R"(
        import nats.*;
        var `received_str String = "";
        onMessageS("test.strval", fn(s String) { `received_str = s; });
    )");
    check(ok, "onMessageS handler registration compiles");

    env.natsDispatcher.subscribeAll(env.natsClient);

    env.natsClient.publish("test.strval", "hello nats");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    char tmpPath[] = "/tmp/tzpl_test_nats_oms_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    env.capturePrint(capture);

    env.eval("println(`received_str);");

    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("hello nats") != std::string::npos,
          "handler received string payload 'hello nats'");
}

static void test_publish_from_tzpl() {
    std::print("Test: natsPub sends message from Tzopilotl\n");

    NatsVMTestEnv env;

    // Set up a C++ subscriber to receive
    std::atomic<bool> received{false};
    std::string receivedData;
    env.natsDispatcher.addHandler("test.from_tzpl",
        [&](const char*, const char* data, int len, const char*) {
            receivedData = std::string(data, len);
            received = true;
        });
    env.natsDispatcher.subscribeAll(env.natsClient);

    bool ok = env.eval(R"(
        import nats.*;
        natsPub("test.from_tzpl", "from tzopilotl");
    )");
    check(ok, "natsPub compiles and executes");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    check(received.load(), "C++ subscriber received message from Tzopilotl");
    check(receivedData == "from tzopilotl", "received correct payload");
}

static void test_removeHandler() {
    std::print("Test: nats.removeHandler unregisters a handler\n");

    NatsVMTestEnv env;

    bool ok = env.eval(R"(
        import nats.*;
        var `remove_count Int = 0;
        onMessage("test.removeme", fn() { `remove_count = `remove_count + 1; });
    )");
    check(ok, "handler registration compiles");

    env.natsDispatcher.subscribeAll(env.natsClient);

    // Fire once
    env.natsClient.publish("test.removeme", "");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Remove the handler
    env.eval(R"(
        import nats.*;
        removeHandler("test.removeme");
    )");

    // Fire again -- should not increment
    env.natsClient.publish("test.removeme", "");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    char tmpPath[] = "/tmp/tzpl_test_nats_rm_XXXXXX";
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

// ---------------------------------------------------------------------------
// Namespaced subject tests
// ---------------------------------------------------------------------------

static void test_namespaced_engine_command() {
    std::print("Test: namespaced engine command via engines.{{name}}.*\n");

    engine::Engine* eng = makeTestEngine();

    nats::NatsClient client;
    client.connect("nats://127.0.0.1:4222");

    nats::NatsDispatcher dispatcher;
    dispatcher.setEngine(eng);
    dispatcher.setClient(&client);
    nats::registerEngineHandlers(dispatcher, "piano");
    dispatcher.subscribeAll(client);

    // Query via namespaced subject
    std::string reply = client.request("engines.piano.engine.isAudioRunning", "", 0, 1000);
    check(reply == "0", "engines.piano.engine.isAudioRunning returns 0");

    // Also verify flat subject still works
    reply = client.request("engine.isAudioRunning", "", 0, 1000);
    check(reply == "0", "flat engine.isAudioRunning still works alongside namespaced");

    dispatcher.unsubscribeAll();
    client.disconnect();
    engine::freeEngine(eng);
}

static void test_broadcast_engine_command() {
    std::print("Test: broadcast engine command via engines.all.*\n");

    engine::Engine* eng = makeTestEngine();

    nats::NatsClient client;
    client.connect("nats://127.0.0.1:4222");

    nats::NatsDispatcher dispatcher;
    dispatcher.setEngine(eng);
    dispatcher.setClient(&client);
    nats::registerEngineHandlers(dispatcher, "drums");
    dispatcher.subscribeAll(client);

    // Query via broadcast subject
    std::string reply = client.request("engines.all.engine.isAudioRunning", "", 0, 1000);
    check(reply == "0", "engines.all.engine.isAudioRunning reaches named engine");

    dispatcher.unsubscribeAll();
    client.disconnect();
    engine::freeEngine(eng);
}

static void test_namespaced_tzpl_eval() {
    std::print("Test: tzpl.eval via engines.{{name}}.tzpl.eval\n");

    // Reuse NatsVMTestEnv but with engine name
    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerNatsFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();
    auto target = compiler.createTarget();
    auto* nrtvm = new ts::NRTVM(16 * 1024 * 1024, types, target);
    auto* moduleCompiler = new ts::ModuleCompiler(compiler, {MODULES_DIR});

    nats::NatsClient natsClient;
    nats::NatsDispatcher natsDispatcher;
    natsDispatcher.setEngine(eng);
    natsDispatcher.setClient(&natsClient);
    nats::registerEngineHandlers(natsDispatcher, "synth1");

    bridge::AppContext appCtx{};
    appCtx.engine = eng;
    appCtx.nrtvm = nrtvm;
    appCtx.compiler = &compiler;
    appCtx.moduleCompiler = moduleCompiler;
    appCtx.target = target;
    appCtx.natsClient = &natsClient;
    appCtx.natsDispatcher = &natsDispatcher;
    appCtx.engineName = "synth1";

    bridge::setAppContextOnVM(&nrtvm->vm, &appCtx);
    bridge::registerVMNatsHandlers(natsDispatcher, appCtx, "synth1");

    char tmpPath[] = "/tmp/tzpl_test_nats_ns_eval_XXXXXX";
    int fd = mkstemp(tmpPath);
    FILE* capture = fdopen(fd, "w+");
    nrtvm->vm.setPrintOutput(capture);

    natsClient.connect("nats://127.0.0.1:4222");
    natsDispatcher.subscribeAll(natsClient);

    // Send eval via namespaced subject
    natsClient.publish("engines.synth1.tzpl.eval", "println(7 * 7);");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    fflush(capture);
    fseek(capture, 0, SEEK_END);
    long len = ftell(capture);
    fseek(capture, 0, SEEK_SET);
    std::string output(len, '\0');
    fread(output.data(), 1, len, capture);
    fclose(capture);
    unlink(tmpPath);

    check(output.find("49") != std::string::npos,
          "engines.synth1.tzpl.eval produced '49'");

    natsDispatcher.unsubscribeAll();
    natsClient.disconnect();
    delete moduleCompiler;
    delete nrtvm;
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    std::print("=== NATS Integration Tests ===\n\n");

    if (!natsServerAvailable()) {
        std::print("NATS server not available on 127.0.0.1:4222.\n");
        std::print("Start one with: nats-server\n");
        std::print("Or install with: brew install nats-server\n\n");
        skip("all tests (no NATS server)");
        std::print("\n=== Results: {} passed, {} failed, {} skipped ===\n",
                   gTestsPassed, gTestsFailed, gTestsSkipped);
        return 0;  // Don't fail CI if no NATS server
    }

    test_connect_disconnect();
    test_publish_subscribe();
    test_request_reply();
    test_engine_commands();

    std::print("\n--- VM NATS Handler Tests ---\n\n");

    test_tzpl_eval();
    test_ffi_registration();
    test_onMessage_handler();
    test_onMessageS_handler();
    test_publish_from_tzpl();
    test_removeHandler();

    std::print("\n--- Namespaced Subject Tests ---\n\n");

    test_namespaced_engine_command();
    test_broadcast_engine_command();
    test_namespaced_tzpl_eval();

    std::print("\n=== Results: {} passed, {} failed, {} skipped ===\n",
               gTestsPassed, gTestsFailed, gTestsSkipped);
    return gTestsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
