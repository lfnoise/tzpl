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
//  main.cpp
//  app
//
//  Live coding application: Tzopilotl with audio engine and synthdef compiler.
//  Supports GUI mode (Dear ImGui) and headless mode (--nogui).
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <csignal>
#include <optional>
#include <filesystem>
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "diagnostic.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_synthdef_compiler_ffi.hpp"
#include "tzpl_clock_ffi.hpp"
#include "nrt_tempo_scheduler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#if TZPL_HAS_OSC
#include "tzpl_osc_ffi.hpp"
#include "tzpl_osc_vm_handlers.hpp"
#include "tzpl_osc.hpp"
#endif
#if TZPL_HAS_NATS
#include "tzpl_nats_ffi.hpp"
#include "tzpl_nats_vm_handlers.hpp"
#include "tzpl_nats.hpp"
#endif
#if TZPL_HAS_GUI
#include "app_gui.hpp"
#endif
#include "repl_session.hpp"
#include "linenoise.h"

using namespace ts;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct Config {
    std::string deviceName = "default";
    std::string inputDeviceName; // empty = same as output device
    int channels = 2;
    int firstChannel = 0;
    int inputChannels = 0;
    int firstInputChannel = 0;
    int bufferFrames = 512;
    double sampleRate = 44100.0;
    int numSilos = 4;
    int oscPort = 0;  // 0 = disabled
    std::string natsUrl;  // empty = disabled
    std::string engineName;  // empty = single-instance mode (flat subjects only)
    std::string projectDir;
};

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

static bool parseConfigFile(const std::string& path, Config& config) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.starts_with("--")) continue;

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            std::cerr << path << ":" << lineNum << ": expected 'key = value'\n";
            continue;
        }

        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));

        try {
            if (key == "silos")            config.numSilos = std::stoi(value);
            else if (key == "sampleRate")  config.sampleRate = std::stod(value);
            else if (key == "bufferFrames") config.bufferFrames = std::stoi(value);
            else if (key == "channels")    config.channels = std::stoi(value);
            else if (key == "firstChannel") config.firstChannel = std::stoi(value);
            else if (key == "device")      config.deviceName = stripQuotes(value);
            else if (key == "inputChannels") config.inputChannels = std::stoi(value);
            else if (key == "firstInputChannel") config.firstInputChannel = std::stoi(value);
            else if (key == "inputDevice") config.inputDeviceName = stripQuotes(value);
            else if (key == "oscPort")     config.oscPort = std::stoi(value);
            else if (key == "natsUrl")     config.natsUrl = stripQuotes(value);
            else if (key == "engineName") config.engineName = stripQuotes(value);
            else std::cerr << path << ":" << lineNum
                           << ": unknown config key '" << key << "'\n";
        } catch (const std::exception& e) {
            std::cerr << path << ":" << lineNum
                      << ": invalid value for '" << key << "': " << e.what() << "\n";
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void printErrors(const std::vector<CompileError>& errors,
                        const std::string& source,
                        const std::string& filename) {
    bool useColor = isatty(fileno(stderr));
    printDiagnostics(errors, source, filename, std::cerr, useColor);
}

static int runSource(VM& vm, Compiler& compiler, const VMTarget& target,
                     const std::string& source, const std::string& filename,
                     ModuleCompiler* moduleCompiler = nullptr) {
    CompileResult result = compiler.compile(source, filename, target, moduleCompiler);
    if (!result.success) {
        printErrors(result.errors, source, filename);
        return 1;
    }

    vm.makeCurrent();
    vm.install(result);
    vm.execute(result.mainBlock);

    return 0;
}

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: cannot open file '" << path << "'\n";
        return "";
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::vector<std::string> splitPaths(const std::string& paths) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start < paths.size()) {
        size_t end = paths.find(':', start);
        if (end == std::string::npos) end = paths.size();
        std::string dir = paths.substr(start, end - start);
        if (!dir.empty()) {
            result.push_back(std::move(dir));
        }
        start = end + 1;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Signal handling for clean shutdown
// ---------------------------------------------------------------------------

volatile sig_atomic_t gShouldQuit = 0;

static void signalHandler(int) {
    gShouldQuit = 1;
}

// ---------------------------------------------------------------------------
// REPL
// ---------------------------------------------------------------------------

// Check if input has balanced braces/parens/brackets and closed comments
static bool isInputComplete(const std::string& input) {
    int braces = 0, parens = 0, brackets = 0;
    int blockComment = 0;
    bool inLineComment = false;
    bool inString = false;
    bool inTripleString = false;

    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            continue;
        }

        if (blockComment > 0) {
            if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
                ++blockComment;
                ++i;
            } else if (c == '*' && i + 1 < input.size() && input[i + 1] == '/') {
                --blockComment;
                ++i;
            }
            continue;
        }

        if (inTripleString) {
            if (c == '"' && i + 2 < input.size()
                && input[i + 1] == '"' && input[i + 2] == '"') {
                inTripleString = false;
                i += 2;
            }
            continue;
        }

        if (inString) {
            if (c == '\\' && i + 1 < input.size()) {
                ++i; // skip escaped char
            } else if (c == '"') {
                inString = false;
            } else if (c == '\n') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            if (i + 2 < input.size()
                && input[i + 1] == '"' && input[i + 2] == '"') {
                inTripleString = true;
                i += 2;
            } else {
                inString = true;
            }
            continue;
        }
        if (c == '-' && i + 1 < input.size() && input[i + 1] == '-') {
            inLineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            ++blockComment;
            ++i;
            continue;
        }

        if (c == '{') ++braces;
        else if (c == '}') --braces;
        else if (c == '(') ++parens;
        else if (c == ')') --parens;
        else if (c == '[') ++brackets;
        else if (c == ']') --brackets;
    }

    return braces <= 0 && parens <= 0 && brackets <= 0 && blockComment == 0
        && !inTripleString;
}

static std::string historyPath() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.tzpl_history";
}

// Read a possibly multi-line REPL input using linenoise
static std::string readREPLInput(bool& eof) {
    eof = false;

    char* first = linenoise("");
    if (!first) { eof = true; return ""; }

    std::string input(first);
    linenoiseFree(first);

    while (!isInputComplete(input)) {
        char* cont = linenoise("");
        if (!cont) { eof = true; break; }
        input += '\n';
        input += cont;
        linenoiseFree(cont);
    }

    if (!input.empty()) {
        linenoiseHistoryAdd(input.c_str());
        auto path = historyPath();
        if (!path.empty()) linenoiseHistorySave(path.c_str());
    }

    return input;
}

// Handle REPL : commands. Returns true if the REPL should exit.
static bool handleREPLCommand(const std::string& input, REPLSession& session,
                               VM& vm, Compiler& compiler) {
    std::string cmd = input;
    while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t'
                            || cmd.back() == '\n'))
        cmd.pop_back();

    if (cmd == ":quit" || cmd == ":q") return true;

    if (cmd == ":help" || cmd == ":h") {
        std::cout << "Commands:\n"
                  << "  :help, :h       Show this help\n"
                  << "  :quit, :q       Exit the REPL\n"
                  << "  :type <expr>    Show the type of an expression\n"
                  << "  :globals        List global variables\n"
                  << "  :functions      List user-defined functions\n"
                  << "  :memory         Show memory usage\n"
                  << "  :gc             Run ARC heartbeat\n";
        return false;
    }

    if (cmd == ":globals") {
        auto lines = session.listGlobals();
        if (lines.empty()) std::cout << "No global variables defined.\n";
        else for (auto& line : lines) std::cout << line << "\n";
        return false;
    }

    if (cmd == ":functions") {
        auto lines = session.listFunctions();
        if (lines.empty()) std::cout << "No user-defined functions.\n";
        else for (auto& line : lines) std::cout << line << "\n";
        return false;
    }

    if (cmd == ":memory") {
        std::printf("  Allocated: %zu bytes\n", vm.allocator().getAllocated());
        std::printf("  Pool size: %zu bytes\n", vm.allocator().getPoolSize());
        std::printf("  Auto-release pool: %u objects\n",
                    vm.autoReleasePool().size());
        std::printf("  Deferred delete queue: %u objects\n",
                    vm.deferredDeleteQueue().size());
        return false;
    }

    if (cmd == ":gc" || cmd == ":collect") {
        vm.gcHeartbeat();
        std::printf("  ARC heartbeat complete.\n");
        std::printf("  Auto-release pool: %u objects\n",
                    vm.autoReleasePool().size());
        std::printf("  Deferred delete queue: %u objects\n",
                    vm.deferredDeleteQueue().size());
        return false;
    }

    if (cmd.size() > 6 && cmd.substr(0, 6) == ":type ") {
        std::string exprStr = cmd.substr(6);
        auto result = session.queryType(exprStr);
        if (!result.errors.empty()) printErrors(result.errors, exprStr, "<repl>");
        else if (result.hasValue) std::printf("%s\n", result.typeName.c_str());
        return false;
    }

    std::cerr << "Unknown command: " << cmd << ". Type :help for help.\n";
    return false;
}

// Run the interactive REPL
static void runREPL(VM& vm, Compiler& compiler, const VMTarget& target,
                    std::vector<std::string> includePaths) {
    REPLSession session(compiler, vm, target, std::move(includePaths));

    linenoiseHistorySetMaxLen(500);
    auto hpath = historyPath();
    if (!hpath.empty()) linenoiseHistoryLoad(hpath.c_str());

    std::cout << "Tzopilotl REPL. Type :help for commands, :quit to exit.\n";

    while (!gShouldQuit) {
        bool eof = false;
        std::string input = readREPLInput(eof);
        if (input.empty()) {
            if (eof) break;
            continue;
        }

        if (input[0] == ':') {
            if (handleREPLCommand(input, session, vm, compiler)) break;
            continue;
        }

        auto result = session.eval(input);

        if (!result.errors.empty()) {
            printErrors(result.errors, input, "<repl>");
            continue;
        }

        if (result.hasValue) {
            std::printf("\xe2\x86\x92 %s : %s\n",
                result.formattedValue.c_str(), result.typeName.c_str());
        }

        vm.gcHeartbeat();
    }
}

// ---------------------------------------------------------------------------
// Audio engine setup
// ---------------------------------------------------------------------------

static engine::Engine* createEngine(const Config& config) {
    engine::AudioStreamParameters params{};
    params.channels = config.channels;
    params.bufferFrames = config.bufferFrames;
    params.sampleRate = config.sampleRate;
    params.deviceName = config.deviceName.c_str();
    params.inputDeviceName = config.inputDeviceName.empty()
        ? nullptr : config.inputDeviceName.c_str();
    params.firstChannel = config.firstChannel;
    params.inputChannels = config.inputChannels;
    params.firstInputChannel = config.firstInputChannel;

    engine::EngineConfig engineConfig;
    engineConfig.numSilos = config.numSilos;

    engine::Engine* e = engine::newEngine(engineConfig, params);

    // Register built-in node definitions
    engine::createSineNode(e);
    engine::createAddOpNode(e);
    engine::createMulOpNode(e);
    engine::createVoicerTestNode(e);

    return e;
}

// ---------------------------------------------------------------------------
// Help text
// ---------------------------------------------------------------------------

static void printHelp() {
    std::cout <<
        "Usage: tzpl [options] [file]\n"
        "\n"
        "Options:\n"
        "  -h, --help              Show this help message\n"
#if TZPL_HAS_GUI
        "  --nogui                 Run in headless mode (no GUI window)\n"
#endif
        "  -P, --project <dir>     Set project directory\n"
        "  -I <path>               Add module include path (colon-separated)\n"
        "  --no-audio              Don't start audio output\n"
        "  --wait                  Wait for Ctrl-C after running script\n"
        "\n"
        "Audio options (override config file):\n"
        "  --silos <n>             Number of parallel audio threads (default: 4)\n"
        "  --sample-rate <hz>      Sample rate (default: 44100)\n"
        "  --buffer-frames <n>     Audio buffer size in frames (default: 512)\n"
        "  --channels <n>          Output channels (default: 2)\n"
        "  --first-channel <n>     First output channel (default: 0)\n"
        "  --input-channels <n>    Input channels (default: 0, disabled)\n"
        "  --first-input-channel <n> First input channel (default: 0)\n"
        "  --device <name>         Audio output device name (default: \"default\")\n"
        "  --input-device <name>   Audio input device (default: same as output)\n"
#if TZPL_HAS_OSC
        "\n"
        "OSC options:\n"
        "  --osc-port <port>       Start OSC server on this UDP port (default: off)\n"
#endif
#if TZPL_HAS_NATS
        "\n"
        "NATS options:\n"
        "  --nats-url <url>        Connect to NATS server (e.g. nats://127.0.0.1:4222)\n"
        "  --engine-name <name>    Set engine name for namespaced NATS subjects\n"
#endif
        "\n"
        "Project directory layout:\n"
        "  <project>/\n"
        "    config                Audio/engine configuration file\n"
        "    src/                  Tzopilotl source files\n"
        "    modules/              Tzopilotl modules\n"
        "    synthdefs/sexpr/      Generated synthdef s-expressions\n"
        "    synthdefs/cpp/        Generated synthdef C++ sources\n"
        "    synthdefs/dylib/      Compiled synthdef plugins\n"
#if TZPL_HAS_GUI
        "\n"
        "By default, launches the GUI. Use --nogui for headless mode.\n"
#endif
        "\n"
        "If no file is given, starts an interactive REPL.\n"
        ;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
    try {
        TypeUniverse types;
        Compiler compiler(types);

        // Register FFI bridges before any compilation
        bridge::registerAudioEngineFFI(compiler);
        bridge::registerSynthdefCompilerFFI(compiler);
        bridge::registerClockFFI(compiler);
#if TZPL_HAS_OSC
        bridge::registerOscFFI(compiler);
#endif
#if TZPL_HAS_NATS
        bridge::registerNatsFFI(compiler);
#endif

        Config config;
        std::vector<std::string> includePaths;
        std::string filename;
        bool startAudio = true;
        bool waitAfterScript = false;

#if TZPL_HAS_GUI
        bool guiMode = true;
#else
        bool guiMode = false;
#endif

        // CLI overrides -- collected during parsing, applied after config file
        std::optional<int> cliSilos, cliChannels, cliFirstChannel, cliBufferFrames;
        std::optional<int> cliInputChannels, cliFirstInputChannel;
        std::optional<double> cliSampleRate;
        std::optional<std::string> cliDevice, cliInputDevice;
        std::optional<int> cliOscPort;
        std::optional<std::string> cliNatsUrl;
        std::optional<std::string> cliEngineName;

        // --- Parse command line ---
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                printHelp();
                return 0;
            } else if (arg == "--nogui") {
                guiMode = false;
            } else if ((arg == "--project" || arg == "-P") && i + 1 < argc) {
                config.projectDir = argv[++i];
            } else if (arg == "-I" && i + 1 < argc) {
                auto paths = splitPaths(argv[++i]);
                includePaths.insert(includePaths.end(), paths.begin(), paths.end());
            } else if (arg == "--no-audio") {
                startAudio = false;
            } else if (arg == "--wait") {
                waitAfterScript = true;
            } else if (arg == "--silos" && i + 1 < argc) {
                cliSilos = std::stoi(argv[++i]);
            } else if (arg == "--sample-rate" && i + 1 < argc) {
                cliSampleRate = std::stod(argv[++i]);
            } else if (arg == "--buffer-frames" && i + 1 < argc) {
                cliBufferFrames = std::stoi(argv[++i]);
            } else if (arg == "--channels" && i + 1 < argc) {
                cliChannels = std::stoi(argv[++i]);
            } else if (arg == "--first-channel" && i + 1 < argc) {
                cliFirstChannel = std::stoi(argv[++i]);
            } else if (arg == "--device" && i + 1 < argc) {
                cliDevice = argv[++i];
            } else if (arg == "--input-channels" && i + 1 < argc) {
                cliInputChannels = std::stoi(argv[++i]);
            } else if (arg == "--first-input-channel" && i + 1 < argc) {
                cliFirstInputChannel = std::stoi(argv[++i]);
            } else if (arg == "--input-device" && i + 1 < argc) {
                cliInputDevice = argv[++i];
            } else if (arg == "--osc-port" && i + 1 < argc) {
                cliOscPort = std::stoi(argv[++i]);
            } else if (arg == "--nats-url" && i + 1 < argc) {
                cliNatsUrl = argv[++i];
            } else if (arg == "--engine-name" && i + 1 < argc) {
                cliEngineName = argv[++i];
            } else {
                filename = arg;
            }
        }

        // --- Load config file from project directory ---
        if (!config.projectDir.empty()) {
            std::string configPath = config.projectDir + "/config";
            if (fs::exists(configPath)) {
                parseConfigFile(configPath, config);
            }
        }

        // --- Apply CLI overrides (take priority over config file) ---
        if (cliSilos)        config.numSilos = *cliSilos;
        if (cliSampleRate)   config.sampleRate = *cliSampleRate;
        if (cliBufferFrames) config.bufferFrames = *cliBufferFrames;
        if (cliChannels)     config.channels = *cliChannels;
        if (cliFirstChannel) config.firstChannel = *cliFirstChannel;
        if (cliDevice)            config.deviceName = *cliDevice;
        if (cliInputDevice)       config.inputDeviceName = *cliInputDevice;
        if (cliInputChannels)     config.inputChannels = *cliInputChannels;
        if (cliFirstInputChannel) config.firstInputChannel = *cliFirstInputChannel;
        if (cliOscPort)           config.oscPort = *cliOscPort;
        if (cliNatsUrl)           config.natsUrl = *cliNatsUrl;
        if (cliEngineName)        config.engineName = *cliEngineName;

        // --- Add project directory paths ---
        if (!config.projectDir.empty()) {
            std::string modulesDir = config.projectDir + "/modules";
            if (fs::is_directory(modulesDir)) {
                includePaths.push_back(modulesDir);
            }
        }

        // --- Create engine and AppContext ---
        engine::Engine* eng = createEngine(config);

        // Auto-load plugins from project directory
        if (!config.projectDir.empty()) {
            std::string dylibDir = config.projectDir + "/synthdefs/dylib";
            if (fs::is_directory(dylibDir)) {
                engine::loadDefs(eng, dylibDir.c_str());
            }
        }

        bridge::AppContext appCtx;
        appCtx.engine = eng;

#if TZPL_HAS_OSC
        // Create OSC subsystem
        osc::OscClient oscClient;
        osc::OscDispatcher oscDispatcher;
        oscDispatcher.setEngine(eng);
        oscDispatcher.setClient(&oscClient);
        osc::registerEngineHandlers(oscDispatcher);

        osc::OscServer oscServer(oscDispatcher);

        appCtx.oscClient = &oscClient;
        appCtx.oscDispatcher = &oscDispatcher;
        appCtx.oscServer = &oscServer;
#endif

#if TZPL_HAS_NATS
        // Create NATS subsystem
        nats::NatsClient natsClient;
        nats::NatsDispatcher natsDispatcher;
        natsDispatcher.setEngine(eng);
        natsDispatcher.setClient(&natsClient);
        nats::registerEngineHandlers(natsDispatcher, config.engineName);

        appCtx.natsClient = &natsClient;
        appCtx.natsDispatcher = &natsDispatcher;
        appCtx.engineName = config.engineName;
#endif

        VMTarget target = compiler.createTarget();

        // Use NRTVM for mutex-serialized access from multiple threads
        // (main thread for REPL/script, scheduler thread for timed events).
        NRTVM nrtvm(64 * 1024 * 1024, types, target);

        // Create tempo-based NRT scheduler (120 BPM default, 50ms latency)
        ts::NRTTempoScheduler tempoScheduler(&nrtvm);
        appCtx.tempoScheduler = &tempoScheduler;

        appCtx.nrtvm = &nrtvm;
        appCtx.compiler = &compiler;
        appCtx.target = target;

        // Create module compiler (available to both GUI and headless modes)
        ModuleCompiler moduleCompiler(compiler, std::vector<std::string>(includePaths));
        appCtx.moduleCompiler = &moduleCompiler;

        bridge::setAppContextOnVM(&nrtvm.vm, &appCtx);
        tempoScheduler.start();

        if (startAudio) {
            engine::startAudio(eng);
        }

#if TZPL_HAS_OSC
        // Register VM OSC handlers and start server
        bridge::registerVMOscHandlers(oscDispatcher, appCtx);
        if (config.oscPort > 0) {
            oscServer.start(config.oscPort);
        }
#endif

#if TZPL_HAS_NATS
        // Register VM NATS handlers and connect
        bridge::registerVMNatsHandlers(natsDispatcher, appCtx, config.engineName);
        if (!config.natsUrl.empty()) {
            if (natsClient.connect(config.natsUrl.c_str())) {
                natsDispatcher.subscribeAll(natsClient);
            }
        }
#endif

        // Set up signal handler for clean shutdown
        std::signal(SIGINT, signalHandler);

        // Check whether any network listeners are active
        bool hasActiveListeners = false;
#if TZPL_HAS_OSC
        if (config.oscPort > 0) hasActiveListeners = true;
#endif
#if TZPL_HAS_NATS
        if (!config.natsUrl.empty()) hasActiveListeners = true;
#endif

        int exitCode = 0;

        // =================================================================
        // Dispatch: GUI mode vs headless mode
        // =================================================================

#if TZPL_HAS_GUI
        if (guiMode) {
            // --- GUI mode --------------------------------------------------
            // Run file if given (audio starts before the window opens)
            if (!filename.empty()) {
                std::string source = readFile(filename);
                if (!source.empty()) {
                    runSource(nrtvm.vm, compiler, target, source, filename,
                              &moduleCompiler);
                }
            }
            exitCode = runGui(appCtx);
        } else
#endif
        {
            // --- Headless mode ---------------------------------------------
            // Run file if given
            if (!filename.empty()) {
                std::string source = readFile(filename);
                if (source.empty()) {
                    exitCode = 1;
                } else {
                    exitCode = runSource(nrtvm.vm, compiler, target, source,
                                         filename, &moduleCompiler);
                }
            }

            // After file execution (or with no file), decide what to do next
            if (exitCode == 0 && !gShouldQuit) {
                bool stayAlive = waitAfterScript || hasActiveListeners
                                 || filename.empty();

                if (isatty(STDIN_FILENO) && stayAlive) {
                    // Interactive terminal: run REPL
                    runREPL(nrtvm.vm, compiler, target,
                            std::vector<std::string>(includePaths));
                } else if (stayAlive && hasActiveListeners) {
                    // Non-interactive but listeners active: wait for messages
                    std::cout << "Running headless. Press Ctrl-C to stop.\n";
                    while (!gShouldQuit) {
                        usleep(100000); // 100ms
                    }
                    std::cout << "\nStopping.\n";
                }
                // Otherwise: file ran, no reason to stay -- exit cleanly
            }
        }

        // =================================================================
        // Shutdown
        // =================================================================

        tempoScheduler.stop();

#if TZPL_HAS_NATS
        natsDispatcher.unsubscribeAll();
        natsClient.disconnect();
#endif

#if TZPL_HAS_OSC
        oscServer.stop();
#endif

        if (startAudio) {
            engine::stopAudio(eng);
        }
        engine::freeEngine(eng);
        return exitCode;

    } catch (tzpl_SErr err) {
        std::cerr << "Fatal audio engine error (tzpl_SErr code " << err << ")\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
