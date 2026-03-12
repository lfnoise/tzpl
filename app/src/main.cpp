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
//  Runs scripts or an interactive REPL with full FFI access.
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
#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_synthdef_compiler_ffi.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"

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
// Signal handling for clean shutdown
// ---------------------------------------------------------------------------

static volatile sig_atomic_t gShouldQuit = 0;

static void signalHandler(int) {
    gShouldQuit = 1;
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
        "\n"
        "Project directory layout:\n"
        "  <project>/\n"
        "    config                Audio/engine configuration file\n"
        "    src/                  Tzopilotl source files\n"
        "    modules/              Tzopilotl modules\n"
        "    synthdefs/sexpr/      Generated synthdef s-expressions\n"
        "    synthdefs/cpp/        Generated synthdef C++ sources\n"
        "    synthdefs/dylib/      Compiled synthdef plugins\n"
        "\n"
        "If no file is given, starts an interactive REPL.\n";
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

        Config config;
        std::vector<std::string> includePaths;
        std::string filename;
        bool startAudio = true;
        bool waitAfterScript = false;

        // CLI overrides — collected during parsing, applied after config file
        std::optional<int> cliSilos, cliChannels, cliFirstChannel, cliBufferFrames;
        std::optional<int> cliInputChannels, cliFirstInputChannel;
        std::optional<double> cliSampleRate;
        std::optional<std::string> cliDevice, cliInputDevice;

        // --- Parse command line ---
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                printHelp();
                return 0;
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

        // --- Add project directory paths ---
        if (!config.projectDir.empty()) {
            std::string modulesDir = config.projectDir + "/modules";
            if (fs::is_directory(modulesDir)) {
                includePaths.push_back(modulesDir);
            }
        }

        // --- Create engine and VM ---
        engine::Engine* eng = createEngine(config);

        // Auto-load plugins from project directory
        if (!config.projectDir.empty()) {
            std::string dylibDir = config.projectDir + "/synthdefs/dylib";
            if (fs::is_directory(dylibDir)) {
                engine::loadDefs(eng, dylibDir.c_str());
            }
        }

        VMTarget target = compiler.createTarget();
        VM vm(64 * 1024 * 1024, types, target);
        bridge::setEngineOnVM(&vm, eng);

        if (startAudio) {
            engine::startAudio(eng);
        }

        // Set up signal handler for clean shutdown
        std::signal(SIGINT, signalHandler);

        int exitCode = 0;

        if (!filename.empty()) {
            std::string source = readFile(filename);
            if (source.empty()) {
                engine::freeEngine(eng);
                return 1;
            }
            ModuleCompiler moduleCompiler(compiler, std::move(includePaths));
            exitCode = runSource(vm, compiler, target, source, filename, &moduleCompiler);

            if (waitAfterScript && exitCode == 0 && !gShouldQuit) {
                std::cout << "Running. Press Ctrl-C to stop.\n";
                while (!gShouldQuit) {
                    usleep(100000); // 100ms
                }
                std::cout << "\nStopping.\n";
            }
        } else {
            std::cerr << "tzpl: no input file specified. Use --help for usage.\n";
            exitCode = 1;
        }

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
