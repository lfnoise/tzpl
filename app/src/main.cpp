//
//  main.cpp
//  app
//
//  Live coding application: Language X with audio engine and synthdef compiler.
//  Runs scripts or an interactive REPL with full FFI access.
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include <csignal>
#include "langx.hpp"
#include "module_compiler.hpp"
#include "diagnostic.hpp"
#include "langx_audio_engine_ffi.hpp"
#include "langx_synthdef_compiler_ffi.hpp"
#include "jscs_client_interface.hpp"

using namespace ts;

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

static engine::Engine* createEngine() {
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
// Signal handling for clean shutdown
// ---------------------------------------------------------------------------

static volatile sig_atomic_t gShouldQuit = 0;

static void signalHandler(int) {
    gShouldQuit = 1;
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

        std::vector<std::string> includePaths;
        std::string filename;
        bool startAudio = true;
        bool waitAfterScript = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: jscs [options] [file]\n"
                          << "\n"
                          << "Options:\n"
                          << "  -h, --help         Show this help message\n"
                          << "  -I <path>          Add module include path (colon-separated)\n"
                          << "  --no-audio         Don't start audio output\n"
                          << "  --wait             Wait for Ctrl-C after running script\n"
                          << "\n"
                          << "If no file is given, starts an interactive REPL.\n";
                return 0;
            } else if (arg == "-I" && i + 1 < argc) {
                auto paths = splitPaths(argv[++i]);
                includePaths.insert(includePaths.end(), paths.begin(), paths.end());
            } else if (arg == "--no-audio") {
                startAudio = false;
            } else if (arg == "--wait") {
                waitAfterScript = true;
            } else {
                filename = arg;
            }
        }

        // Create engine and VM
        engine::Engine* eng = createEngine();

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
            std::cerr << "jscs: no input file specified. Use --help for usage.\n";
            exitCode = 1;
        }

        if (startAudio) {
            engine::stopAudio(eng);
        }
        engine::freeEngine(eng);
        return exitCode;

    } catch (jscs_SErr err) {
        std::cerr << "Fatal audio engine error (jscs_SErr code " << err << ")\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
