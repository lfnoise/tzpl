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
//  Audio coding application: Tzopilotl with audio engine and synthdef compiler.
//  Supports GUI mode (Dear ImGui) and headless mode (--nogui).
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <csignal>
#include <optional>
#include <filesystem>
#include "tzpl.hpp"
#include "app_config.hpp"
#include "module_compiler.hpp"
#include "module_paths.hpp"
#include "project_paths.hpp"
#include "diagnostic.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_nrt_render.hpp"
#include "tzpl_synthdef_compiler_ffi.hpp"
#include "tzpl_clock_ffi.hpp"
#include "tzpl_ui_ffi.hpp"
#include "tzpl_ui_state.hpp"
#include "nrt_tempo_scheduler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include "synthdef_compile_link.hpp" // getBuildDir, for plugin browser search paths
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
#if TZPL_GUI_JUCE
#include "juce_audio_backend.hpp"
#endif
#include "repl_session.hpp"
#include "linenoise.h"

using namespace ts;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// The engine settings (tzpl-config keys) plus the project they came from.
// The keys themselves live in app_config.hpp -- shared with the app's
// settings dialog, which writes the same files this reads.
struct Config : tzplapp::AppConfig {
    std::string projectDir;
};

static bool parseConfigFile(const std::string& path, Config& config) {
    std::vector<std::string> problems;
    if (!tzplapp::loadConfigFile(path, config, &problems)) return false;
    for (auto const& p : problems) std::cerr << p << "\n";
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

// Wrap runSource in the NRTVM mutex. Concurrent threads (live tempo
// scheduler, per-render scheduler tickTo on render threads) take the same
// mutex before re-entering the VM; if the script ran without it they would
// race against each other and the script -- crashes follow.
static int runSourceLocked(NRTVM& nrtvm, Compiler& compiler,
                           const VMTarget& target,
                           const std::string& source,
                           const std::string& filename,
                           ModuleCompiler* moduleCompiler = nullptr) {
    std::lock_guard<std::mutex> lk(nrtvm.mtx);
    return runSource(nrtvm.vm, compiler, target, source, filename, moduleCompiler);
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

// Case-insensitive .tzd check: macOS filesystems are case-insensitive, so
// Song.TZD is the same notebook document as song.tzd and must never be fed
// to the compiler as source (matches JUCE's hasFileExtension behavior).
static bool isNotebookDocument(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext == "tzd";
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
                  << "  :gc             Run a full tracing-GC cycle\n";
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
        return false;
    }

    if (cmd == ":gc" || cmd == ":collect") {
        vm.tracingGC().runFullCycle();
        std::printf("  Tracing GC cycle complete.\n");
        std::printf("  Live (black): %u  Reclaimed (white): %u\n",
                    vm.tracingGC().lastBlackCount(),
                    vm.tracingGC().lastWhiteCount());
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
                result.prettyValue.c_str(), result.typeName.c_str());
        }

        vm.gcHeartbeat();
    }
}

// ---------------------------------------------------------------------------
// Audio engine setup
// ---------------------------------------------------------------------------

// Register built-in test plugins on an engine. Used both for the live engine
// and for every per-render NRT engine (via AppContext::initEngine).
static void registerBuiltinDefs(engine::Engine* e) {
    engine::createSineNode(e);
    engine::createAddOpNode(e);
    engine::createMulOpNode(e);
    engine::createVoicerTestNode(e);
}

static engine::Engine* createEngine(const Config& config, bool nrt = false) {
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
    engineConfig.numTempoClocks = config.numTempoClocks;

#if TZPL_GUI_JUCE
    engine::Engine* e = nrt ? engine::newEngineNRT(engineConfig, params)
                            : engine::newEngine(engineConfig, params,
                                                tzplapp::makeJuceAudioBackend());
#else
    engine::Engine* e = nrt ? engine::newEngineNRT(engineConfig, params)
                            : engine::newEngine(engineConfig, params);
#endif

    registerBuiltinDefs(e);

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
        "  --nrt <path>            Render to WAV (32-bit float) at <path> instead\n"
        "                          of opening an audio device. Forces headless\n"
        "                          mode. Without --duration, renders until the\n"
        "                          script calls endRender() / engineStop() or the\n"
        "                          tempo scheduler goes idle.\n"
        "  --duration <seconds>    NRT hard cap in seconds. If absent, the render\n"
        "                          runs open-ended (see --nrt).\n"
        "  --tail <seconds>        Tail rendered after the script signals stop\n"
        "                          (default 1.0). Increase for long reverbs/decays.\n"
        "  --nrt-safety-cap <sec>  Upper-bound when no --duration is given\n"
        "                          (default 3600s). Render aborts if exceeded.\n"
        "\n"
        "Audio options (override config file):\n"
        "  --silos <n>             Number of parallel audio threads (default: 4)\n"
        "  --tempo-clocks <n>      Number of tempo-clock slots per silo (default: 1)\n"
        "  --sample-rate <hz>      Sample rate (default: 48000)\n"
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
        "    tzpl-config           Project marker + engine configuration\n"
        "                          (key = value per line, -- comments)\n"
        "    modules/              Project modules (searched before the stdlib)\n"
        "    synthdefs/dylib/      Plugins auto-loaded at startup (optional)\n"
        "\n"
        "  Per-user defaults for every project (same keys) are read first from\n"
        "  ~/Library/Application Support/Tzopilotl/tzpl-config on macOS,\n"
        "  $XDG_CONFIG_HOME/tzpl/tzpl-config elsewhere.\n"
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
        bridge::registerNRTRenderFFI(compiler);
        bridge::registerSynthdefCompilerFFI(compiler);
        bridge::registerClockFFI(compiler);
        bridge::registerUIFFI(compiler);
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

        // Non-real-time render mode: when nrtOutputPath is set, the engine
        // runs offline and writes audio to a WAV file. nrtDuration > 0 sets a
        // hard cap; otherwise the renderer stops when the script calls
        // endRender()/engineStop() or the tempo scheduler goes idle.
        std::string nrtOutputPath;
        double nrtDuration = 0.0;          // 0 = open-ended (until idle/endRender)
        double nrtTailSeconds = 1.0;       // tail rendered after stop signal
        double nrtSafetyCapSeconds = 3600; // upper bound when no --duration

#if TZPL_HAS_GUI
        bool guiMode = true;
#else
        bool guiMode = false;
#endif

        // CLI overrides -- collected during parsing, applied after config file
        std::optional<int> cliSilos, cliTempoClocks, cliChannels, cliFirstChannel, cliBufferFrames;
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
            } else if (arg == "--tempo-clocks" && i + 1 < argc) {
                cliTempoClocks = std::stoi(argv[++i]);
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
            } else if (arg == "--nrt" && i + 1 < argc) {
                nrtOutputPath = argv[++i];
            } else if (arg == "--duration" && i + 1 < argc) {
                nrtDuration = std::stod(argv[++i]);
            } else if (arg == "--tail" && i + 1 < argc) {
                nrtTailSeconds = std::stod(argv[++i]);
            } else if (arg == "--nrt-safety-cap" && i + 1 < argc) {
                nrtSafetyCapSeconds = std::stod(argv[++i]);
            } else {
                filename = arg;
            }
        }

        // --- Auto-detect the project from the opened file ---
        // GUI users can't pass -P; opening any file inside a project (the
        // nearest ancestor directory with a `tzpl-config` file) applies that
        // project's engine config and paths. -P still overrides.
        if (config.projectDir.empty() && !filename.empty()) {
            std::string root = tzplapp::findProjectRoot(filename);
            if (!root.empty()) {
                config.projectDir = root;
                std::cerr << "project: " << root << "\n";
            }
        }

        // --- Load the per-user config, then let the project's override it ---
        // The user file is where the settings dialog writes when no project
        // is open, so a scratch session can still configure audio input.
        if (std::string userCfg = tzplapp::userConfigFile(); !userCfg.empty()) {
            parseConfigFile(userCfg, config);
        }
        if (!config.projectDir.empty()) {
            std::string configPath = tzplapp::projectConfigFile(config.projectDir);
            if (!configPath.empty()) {
                parseConfigFile(configPath, config);
            }
        }

        // --- Apply CLI overrides (take priority over config file) ---
        if (cliSilos)        config.numSilos = *cliSilos;
        if (cliTempoClocks)  config.numTempoClocks = *cliTempoClocks;
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

        // --- Complete the module search path ---
        // User paths: -I, the project's modules/, then $TZPL_PATH. The stdlib
        // (TZPL_HOME override / executable-relative walk-up for a distribution
        // folder / source tree for dev builds) goes in a separate system list
        // that the ModuleCompiler always searches last, so user paths -- even
        // ones added at runtime when the GUI opens a project -- can shadow it.
        for (auto& p : envModulePaths()) includePaths.push_back(std::move(p));
        std::vector<std::string> stdlibFallbacks;
#ifdef MODULES_DIR
        stdlibFallbacks.push_back(MODULES_DIR);
#endif
#ifdef LANG_MODULES_DIR
        stdlibFallbacks.push_back(LANG_MODULES_DIR);
#endif
#ifdef EXAMPLES_DIR
        stdlibFallbacks.push_back(EXAMPLES_DIR);
#endif
        std::vector<std::string> systemPaths = defaultModulePaths(stdlibFallbacks);

        // --- NRT mode: force headless, never open an audio device ---
        // The "live" engine is created in NRT-mode as a placeholder so the
        // AppContext has a valid engine pointer; it's never used because the
        // script runs inside a render context that creates its own engine.
        bool nrtMode = !nrtOutputPath.empty();
        if (nrtMode) {
            guiMode = false;
            startAudio = false;
        }

        // Notebook documents are binary; they can only be opened in the GUI,
        // never evaluated as source.
        if (!guiMode && isNotebookDocument(filename)) {
            std::cerr << "notebook documents (.tzd) require the GUI; "
                         "run without --nogui/--nrt\n";
            return 1;
        }

        // --- Create engine and AppContext ---
        engine::Engine* eng = createEngine(config, /*nrt=*/nrtMode);

        // Auto-load plugins from project directory
        if (!config.projectDir.empty()) {
            std::string dylibDir = config.projectDir + "/synthdefs/dylib";
            if (fs::is_directory(dylibDir)) {
                engine::loadDefs(eng, dylibDir.c_str());
            }
        }

        bridge::AppContext appCtx;
        appCtx.engine = eng;
        // Each per-render NRT engine spawned via renderNRT FFI / CLI gets the
        // same built-in defs (and project-loaded plugins) as the live engine.
        std::string projectDylibDir = config.projectDir.empty()
            ? std::string{} : config.projectDir + "/synthdefs/dylib";

        // Where the plugin browser looks for loadable-but-not-loaded plugins:
        // the project's dylib dir, then the synthdef compile cache.
        if (!projectDylibDir.empty()) {
            appCtx.pluginSearchPaths.push_back(projectDylibDir);
        }
        appCtx.pluginSearchPaths.push_back(synthdef::getBuildDir() + "dylib");

        appCtx.initEngine = [projectDylibDir](engine::Engine* e) {
            registerBuiltinDefs(e);
            if (!projectDylibDir.empty()
                && fs::is_directory(projectDylibDir)) {
                engine::loadDefs(e, projectDylibDir.c_str());
            }
        };

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

        // Live-control widget registry for the `ui` module. Declared before
        // the NRTVM: its GC root scanner references uiState, and the GC
        // heartbeat runs until the NRTVM destructor stops it.
        bridge::UIState uiState;

        // Advanced language settings -> per-VM construction configs. Shared
        // knobs (stack limits, GC tuning, graph limits) apply to both the NRT
        // VM and the silo VMs; pool size and the MMU governor differ per kind.
        auto langVMConfig = [&config](bool rtSilo) {
            ts::VMConfig c;
            c.poolSize = (usize)std::max(rtSilo ? config.langSiloHeapMB
                                                : config.langNrtHeapMB, 1) << 20;
            c.growthChunkMin = (usize)std::max(config.langHeapChunkMinMB, 1) << 20;
            c.growthChunkMax = (usize)std::max(config.langHeapChunkMaxMB,
                                               config.langHeapChunkMinMB) << 20;
            c.maxRegs = (u32)std::max(config.langMaxRegisters, 256);
            c.maxFrames = (u32)std::max(config.langMaxCallDepth, 16);
            c.maxDynStack = (u32)std::max(config.langMaxDynScope, 16);
            c.maxDynStackPayload = (u32)std::max(config.langMaxDynScopeWords, 16);
            c.gcStepBudgetNanos = (u64)std::max(config.langGcStepBudgetUs, 1) * 1000;
            c.gcMinTriggerAllocs = (u32)std::max(config.langGcMinTriggerAllocs, 1);
            c.gcGrowthFactor = (u32)std::max(config.langGcGrowthFactor, 1);
            c.mmuEnabled = rtSilo && config.langSiloMmu != 0;
            c.mmuMutatorPermille = (u32)std::clamp(config.langSiloMmuMutatorPct, 0, 100) * 10;
            c.mmuWindowNanos = (u64)std::max(config.langSiloMmuWindowMs, 1) * 1'000'000;
            c.graphMaxDepth = (u32)std::max(config.langGraphMaxDepth, 1);
            c.lazyForceLimit = std::max(config.langLazyForceLimit, 1);
            c.printMaxDepth = (u32)std::max(config.langPrintMaxDepth, 1);
            c.listPrintLimit = std::max(config.langListPrintLimit, 1);
            return c;
        };

        // Use NRTVM for mutex-serialized access from multiple threads
        // (main thread for REPL/script, scheduler thread for timed events).
        NRTVM nrtvm(langVMConfig(/*rtSilo=*/false), types, target);

        // Live tempo scheduler (60 BPM default, 50ms latency). Wall-clock
        // driven on its own thread. Each NRT render creates and drives its
        // OWN tempo scheduler (in manual mode) so renders use logical audio
        // time without disturbing the live scheduler.
        ts::NRTTempoScheduler tempoScheduler(&nrtvm);
        appCtx.tempoScheduler = &tempoScheduler;

        appCtx.nrtvm = &nrtvm;
        appCtx.compiler = &compiler;
        appCtx.target = target;

        // Create module compiler (available to both GUI and headless modes)
        ModuleCompiler moduleCompiler(compiler, std::vector<std::string>(includePaths),
                                      std::vector<std::string>(systemPaths));
        appCtx.moduleCompiler = &moduleCompiler;

        // Initialize per-silo VM slots (populated later by attachVM()),
        // and the construction config attachVM applies to each silo VM.
        appCtx.siloVMs.resize(config.numSilos);
        appCtx.siloVMConfig = langVMConfig(/*rtSilo=*/true);

        // Wire up the `ui` widget registry. Present in both GUI and headless
        // modes (headless scripts can build widgets; nothing renders them).
        // The root scanner keeps onChange closures alive across GC cycles.
        appCtx.uiState = &uiState;
        appCtx.projectDir = config.projectDir;
        bridge::registerUIRootScanner(nrtvm, uiState);

        bridge::setAppContextOnVM(&nrtvm.vm, &appCtx);
        nrtvm.startHeartbeat();  // drain deferred deletes at ~50Hz
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
            // A .x file is evaluated before the window opens (audio starts
            // first) and then shown in an editor tab; a .tzd is a binary
            // notebook document -- never evaluated as source, just opened.
            if (!filename.empty()) {
                if (!isNotebookDocument(filename)) {
                    std::string source = readFile(filename);
                    if (!source.empty()) {
                        runSourceLocked(nrtvm, compiler, target, source,
                                        filename, &moduleCompiler);
                    }
                }
                appCtx.startupDocument = fs::absolute(filename).string();
            }
            exitCode = runGui(appCtx);
        } else
#endif
        {
            // --- Headless mode ---------------------------------------------
            if (nrtMode) {
                // --- NRT render path -------------------------------------
                // Wrap the entire script in a render: the script's main
                // execution is the render's setup callback. Per-render
                // engine + manual-mode tempo scheduler + render thread are
                // created by renderNRTAsync. The CLI then waits at the C++
                // level (no language primitive blocks) for all in-flight
                // renders to finish before exiting.
                if (filename.empty()) {
                    std::cerr << "--nrt requires a script file\n";
                    exitCode = 1;
                } else {
                    std::string source = readFile(filename);
                    if (source.empty()) {
                        exitCode = 1;
                    } else {
                        bridge::RenderJobOpts opts;
                        opts.path = nrtOutputPath;
                        opts.sampleRate  = config.sampleRate;
                        opts.channels    = config.channels;
                        opts.bufferFrames = config.bufferFrames;
                        opts.numSilos    = config.numSilos;
                        opts.numTempoClocks = config.numTempoClocks;
                        opts.durationSeconds  = nrtDuration;
                        opts.tailSeconds      = nrtTailSeconds;
                        opts.safetyCapSeconds = nrtSafetyCapSeconds;

                        if (nrtDuration > 0.0) {
                            std::cerr << "Rendering " << nrtDuration << "s to "
                                      << nrtOutputPath << " ...\n";
                        } else {
                            std::cerr << "Rendering to " << nrtOutputPath
                                      << " until script signals stop or scheduler"
                                         " goes idle (safety cap "
                                      << nrtSafetyCapSeconds << "s) ...\n";
                        }

                        // Capture the script source & filename by value so the
                        // render thread sees stable storage regardless of
                        // when it dispatches.
                        auto* compilerPtr = &compiler;
                        auto* mc = &moduleCompiler;
                        auto* vmPtr = &nrtvm.vm;
                        std::string src = source;
                        std::string fname = filename;
                        VMTarget tgt = target;
                        int* exitOut = &exitCode;

                        // The setup runs on the calling (main) thread. Hold
                        // the NRTVM mutex around it so the render thread's
                        // per-block tickTo (which also takes the mutex)
                        // serialises with VM access -- otherwise both
                        // threads would touch the VM concurrently and crash.
                        auto* nrtvmPtr = &nrtvm;
                        bridge::renderNRTAsync(opts, &appCtx,
                            [vmPtr, compilerPtr, tgt, src, fname, mc,
                             exitOut, nrtvmPtr]() {
                                std::lock_guard<std::mutex> lk(nrtvmPtr->mtx);
                                int rc = runSource(*vmPtr, *compilerPtr, tgt,
                                                   src, fname, mc);
                                if (rc != 0) *exitOut = rc;
                            });

                        // Wait for the render thread to finish before exit.
                        // This is a C++-level join, not a language block.
                        bridge::joinAllRenders();
                    }
                }
                std::cerr << "Done.\n";
            } else {
                // --- Live (RT) headless mode ------------------------------
                if (!filename.empty()) {
                    std::string source = readFile(filename);
                    if (source.empty()) {
                        exitCode = 1;
                    } else {
                        exitCode = runSourceLocked(nrtvm, compiler, target,
                                                   source, filename,
                                                   &moduleCompiler);
                    }
                }
            }

            // After file execution (or with no file), decide what to do next.
            // NRT mode never enters the REPL or wait-for-listeners loop.
            if (!nrtMode && exitCode == 0 && !gShouldQuit) {
                bool stayAlive = waitAfterScript || hasActiveListeners
                                 || filename.empty();

                if (std::getenv("TZPL_EVAL_CELLS") && stayAlive) {
                    // Headless notebook-cell simulation: read stdin, split
                    // on lines containing only "%%", and evaluate each
                    // chunk as ONE REPLSession eval (exactly how the app's
                    // notebook runs a cell). For reproducing notebook bugs.
                    std::vector<std::string> replPaths(includePaths);
                    replPaths.insert(replPaths.end(), systemPaths.begin(),
                                     systemPaths.end());
                    REPLSession session(compiler, nrtvm.vm, target,
                                        std::move(replPaths));
                    std::string cell, line;
                    int n = 0;
                    auto evalCell = [&] {
                        if (cell.find_first_not_of(" \t\n") == std::string::npos)
                            { cell.clear(); return; }
                        std::printf("== cell %d ==\n", ++n);
                        // Same discipline as the app's cell evals: hold the
                        // VM mutex so scheduler-thread callbacks serialize.
                        std::lock_guard<std::mutex> lk(nrtvm.mtx);
                        auto result = session.eval(cell);
                        if (!result.errors.empty())
                            printErrors(result.errors, cell, "<cell>");
                        else if (result.hasValue)
                            std::printf("\xe2\x86\x92 %s : %s\n",
                                        result.prettyValue.c_str(),
                                        result.typeName.c_str());
                        std::fflush(stdout);
                        cell.clear();
                    };
                    while (std::getline(std::cin, line)) {
                        if (line == "%%") evalCell();
                        else { cell += line; cell += '\n'; }
                    }
                    evalCell();
                } else if (isatty(STDIN_FILENO) && stayAlive) {
                    // Interactive terminal: run REPL. Its REPLSession builds
                    // its own ModuleCompiler from one flat list, so merge the
                    // user and system paths (order still user-first).
                    std::vector<std::string> replPaths(includePaths);
                    replPaths.insert(replPaths.end(), systemPaths.begin(),
                                     systemPaths.end());
                    runREPL(nrtvm.vm, compiler, target, std::move(replPaths));
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

        // Tear down all in-flight NRT renders before destroying the live VM
        // and tempo scheduler. Each render's per-render scheduler captures a
        // pointer into the NRTVM's mutex; we must join their threads first.
        bridge::shutdownRenderRegistry();

        tempoScheduler.stop();
        nrtvm.stopHeartbeat();

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
