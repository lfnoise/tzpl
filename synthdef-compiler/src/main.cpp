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
//  synthdef-compiler
//
//  Created by James McCartney on 10/9/25.
//

#include <iostream>
#include <fstream>
#include <filesystem>
#include <print>
#include "synthdef_from_sexpr.hpp"
#include "synthdef_cpp_codegen.hpp"
#include "synthdef_compile.hpp"

void all_tests();

namespace fs = std::filesystem;

int main(int argc, const char * argv[]) {
    std::println("---- synthdef-compiler version 0.1 ----");
    if (argc < 2) {
        std::println("Usage: synthdef-compiler <sexpr_file1> [sexpr_file2] ...");
        std::println("   or: synthdef-compiler --test");
        return 1;
    }

    // Check for test flag
    if (std::string(argv[1]) == "--test") {
        all_tests();
        return 0;
    }

    // Process each sexpr file
    for (int i = 1; i < argc; i++) {
        std::string sexprPath = argv[i];
        std::println("\nProcessing: {}", sexprPath);

        // Read the sexpr file
        std::ifstream file(sexprPath);
        if (!file) {
            std::println("ERROR: Could not open file: {}", sexprPath);
            continue;
        }

        std::string sexprText((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
        file.close();

        // Extract synth name from filename (remove path and .sexpr extension)
        fs::path path(sexprPath);
        std::string synthName = path.stem().string();

        std::println("  Synth name: {}", synthName);
        std::println("  S-expr size: {} chars", sexprText.size());

        // Parse the s-expression
        auto result = synthdef::synthFromSExprText(sexprText, synthName);
        if (!result) {
            std::println("  ERROR: Failed to parse s-expression: {}", result.error());
            continue;
        }

        synthdef::Synth* synth = result.value();
        std::println("  SUCCESS: Created synth with {} expressions", synth->exprs.size());

        // Run graph analysis and code generation (both need PushSynth)
        std::string cppCode;
        try {
            synthdef::PushSynth ps(synth);
            synth->graphAnalysis();

            cppCode = synthdef::cppCodeGen(synth);
        } catch (std::exception const& e) {
            std::println("  ERROR: Graph analysis or code generation failed: {}", e.what());
            continue;
        }

        // Write C++ file (same directory as sexpr, _synth.cpp naming convention)
        std::string outDir = path.parent_path().string();
        if (!outDir.empty() && outDir.back() != '/') {
            outDir += '/';
        }

        fs::path outPath = path.parent_path() / (synthName + "_synth.cpp");
        std::ofstream outFile(outPath);
        if (!outFile) {
            std::println("  ERROR: Could not write to: {}", outPath.string());
            continue;
        }

        outFile << cppCode;
        outFile.close();

        std::println("  SUCCESS: Wrote C++ code to: {}", outPath.string());

        // Compile and link to .dylib
        try {
            synthdef::compileAndLink(outDir, synthName);
            std::println("  SUCCESS: Compiled and linked to: {}",
                        (path.parent_path() / (synthName + "_synth.dylib")).string());
        } catch (std::exception const& e) {
            std::println("  ERROR: Compilation or linking failed: {}", e.what());
            continue;
        }
    }

    return 0;
}
