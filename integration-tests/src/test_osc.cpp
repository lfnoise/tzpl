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
#include "tzpl_audio_engine_ffi.hpp"
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
// Main
// ---------------------------------------------------------------------------

int main() {
    std::print("=== OSC Integration Tests ===\n\n");

    test_server_start_stop();
    test_client_send();
    test_engine_commands();
    test_local_dispatch();
    test_ffi_registration();

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
