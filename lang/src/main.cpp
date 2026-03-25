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
//  lang
//
//  End-to-end pipeline: source -> lex -> parse -> typecheck -> codegen -> execute
//

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>
#include "linenoise.h"
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "diagnostic.hpp"
#include "repl_session.hpp"

using namespace ts;

// Print compile errors with source context and optional color
static void printErrors(const std::vector<CompileError>& errors,
                        const std::string& source,
                        const std::string& filename) {
    bool useColor = isatty(fileno(stderr));
    printDiagnostics(errors, source, filename, std::cerr, useColor);
}

// Run source code through the compile -> install -> execute pipeline
static int runSource(VM& vm, Compiler& compiler, const VMTarget& target,
                     const std::string& source, const std::string& filename,
                     ModuleCompiler* moduleCompiler = nullptr) {
    // Compile (handles makeCurrent/endCurrent internally)
    CompileResult result = compiler.compile(source, filename, target, moduleCompiler);
    if (!result.success) {
        printErrors(result.errors, source, filename);
        return 1;
    }

    // Switch to VM context, install, and execute
    vm.makeCurrent();
    vm.install(result);
    vm.execute(result.mainBlock);

    return 0;
}

// Read entire file into string
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

// Split a colon-separated path string into individual directories
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

// --- REPL ---

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
            if (c == '"' && i + 2 < input.size() && input[i + 1] == '"' && input[i + 2] == '"') {
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
                // Regular strings don't span lines; treat as closed
                // (the lexer will report the unterminated string error)
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            if (i + 2 < input.size() && input[i + 1] == '"' && input[i + 2] == '"') {
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

    // Don't check inString: regular strings can't span lines, so an
    // unterminated one means the input is complete (the parser reports the error).
    return braces <= 0 && parens <= 0 && brackets <= 0 && blockComment == 0
        && !inTripleString;
}

// Get the history file path (~/.tzpl_history)
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

    // Add completed input to history
    if (!input.empty()) {
        linenoiseHistoryAdd(input.c_str());
        auto path = historyPath();
        if (!path.empty()) linenoiseHistorySave(path.c_str());
    }

    return input;
}

// Handle REPL commands. Returns true if the REPL should exit.
static bool handleREPLCommand(const std::string& input, REPLSession& session,
                               VM& vm, Compiler& compiler) {
    // Trim the command
    std::string cmd = input;
    // Strip trailing whitespace
    while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t' || cmd.back() == '\n'))
        cmd.pop_back();

    if (cmd == ":quit" || cmd == ":q") {
        return true;
    }

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
        if (lines.empty()) {
            std::cout << "No global variables defined.\n";
        } else {
            for (auto& line : lines) {
                std::cout << line << "\n";
            }
        }
        return false;
    }

    if (cmd == ":functions") {
        auto lines = session.listFunctions();
        if (lines.empty()) {
            std::cout << "No user-defined functions.\n";
        } else {
            for (auto& line : lines) {
                std::cout << line << "\n";
            }
        }
        return false;
    }

    if (cmd == ":memory") {
        std::printf("  Allocated: %zu bytes\n", vm.allocator().getAllocated());
        std::printf("  Pool size: %zu bytes\n", vm.allocator().getPoolSize());
        std::printf("  Auto-release pool: %u objects\n", vm.autoReleasePool().size());
        std::printf("  Deferred delete queue: %u objects\n", vm.deferredDeleteQueue().size());
        return false;
    }

    if (cmd == ":gc" || cmd == ":collect") {
        vm.gcHeartbeat();
        std::printf("  ARC heartbeat complete.\n");
        std::printf("  Auto-release pool: %u objects\n", vm.autoReleasePool().size());
        std::printf("  Deferred delete queue: %u objects\n", vm.deferredDeleteQueue().size());
        return false;
    }

    if (cmd.size() > 6 && cmd.substr(0, 6) == ":type ") {
        std::string exprStr = cmd.substr(6);
        auto result = session.queryType(exprStr);
        if (!result.errors.empty()) {
            printErrors(result.errors, exprStr, "<repl>");
        } else if (result.hasValue) {
            std::printf("%s\n", result.typeName.c_str());
        }
        return false;
    }

    std::cerr << "Unknown command: " << cmd << ". Type :help for help.\n";
    return false;
}

// Run the interactive REPL
static void runREPL(VM& vm, Compiler& compiler, const VMTarget& target,
                    std::vector<std::string> includePaths) {
    REPLSession session(compiler, vm, target, std::move(includePaths));

    // Set up linenoise history
    linenoiseHistorySetMaxLen(500);
    auto hpath = historyPath();
    if (!hpath.empty()) linenoiseHistoryLoad(hpath.c_str());

    std::cout << "Tzopilotl REPL. Type :help for commands, :quit to exit.\n";

    while (true) {
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

        // Drain auto-release pool and process deferred deletions after each evaluation
        vm.gcHeartbeat();
    }
}

int main(int argc, const char* argv[]) {
    try {
        // Create TypeUniverse (system-allocated, shared) and Compiler
        TypeUniverse types;
        Compiler compiler(types);

        std::vector<std::string> includePaths;
        std::string filename;
        bool rtRestricted = false;

        // Parse command line arguments
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << "Usage: tzpl [options] [file]\n"
                          << "\n"
                          << "Options:\n"
                          << "  -h, --help         Show this help message\n"
                          << "  -I <path>          Add module include path (colon-separated)\n"
                          << "  --rt               Enable real-time safety enforcement\n"
                          << "  --no-reg-reclaim   Disable register reclamation optimization\n"
                          << "  --no-const-fold    Disable constant folding optimization\n"
                          << "  --no-tco           Disable tail call optimization\n"
                          << "\n"
                          << "If no file is given, starts an interactive REPL.\n";
                return 0;
            } else if (arg == "-I" && i + 1 < argc) {
                auto paths = splitPaths(argv[++i]);
                includePaths.insert(includePaths.end(), paths.begin(), paths.end());
            } else if (arg == "--rt") {
                rtRestricted = true;
            } else if (arg == "--no-reg-reclaim") {
                compiler.enableRegReclaim = false;
            } else if (arg == "--no-const-fold") {
                compiler.enableConstFold = false;
            } else if (arg == "--no-tco") {
                compiler.enableTailCalls = false;
            } else {
                filename = arg;
            }
        }

        // Create target and VM after parsing args (so rtRestricted is known)
        VMTarget target = compiler.createTarget(rtRestricted);
        VM vm(64 * 1024 * 1024, types, target);

        if (!filename.empty()) {
            // Run file with module support
            std::string source = readFile(filename);
            if (source.empty()) return 1;
            ModuleCompiler moduleCompiler(compiler, std::move(includePaths));
            return runSource(vm, compiler, target, source, filename, &moduleCompiler);
        }

        // No file argument: run REPL
        runREPL(vm, compiler, target, std::move(includePaths));
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
