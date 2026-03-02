//
//  synthdef_from_sexpr_test.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 1/11/25.
//

#include "synthdef_from_sexpr.hpp"
#include "synthdef_compile.hpp"
#include <print>
#include <fstream>

namespace synthdef {

void test_sexpr_simple() {
    std::println("=== Testing simple s-expression parsing ===");

    // Simple synth: constant -> outlet
    // (0 Constant 1 12 (440.0))
    // (1 Outlet "out" 0)
    std::string sexprText = R"(
        ((0 Constant 1 12 (440.0))
         (1 Outlet "out" 0))
    )";

    auto result = synthFromSExprText(sexprText, "test_simple");
    if (!result) {
        std::println("ERROR: {}", result.error());
        return;
    }

    Synth* synth = result.value();
    std::println("SUCCESS: Created synth with {} expressions", synth->exprs.size());

    // Run graph analysis
    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        std::println("Graph analysis completed");
        synth->dump();
    }
}

void test_sexpr_binary_op() {
    std::println("\n=== Testing binary operation ===");

    // Synth: 440 + 1 -> outlet
    // (0 Constant 1 12 (440.0))
    // (1 Constant 1 12 (1.0))
    // (2 BinaryOp + (0 1))
    // (3 Outlet "out" 2)
    std::string sexprText = R"(
        ((0 Constant 1 12 (440.0))
         (1 Constant 1 12 (1.0))
         (2 BinaryOp + (0 1))
         (3 Outlet "out" 2))
    )";

    auto result = synthFromSExprText(sexprText, "test_binop");
    if (!result) {
        std::println("ERROR: {}", result.error());
        return;
    }

    Synth* synth = result.value();
    std::println("SUCCESS: Created synth with {} expressions", synth->exprs.size());

    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        synth->dump();
    }
}

void test_sexpr_from_language() {
    std::println("\n=== Testing s-expression from language output ===");

    // Example of what the language might generate for:
    // let freq = 440.0
    // let out = fsinosc(freq)
    // outlet(out)
    //
    // This is a simplified version - actual output would include more nodes
    std::string sexprText = R"(
        ((0 SampleRate)
         (1 Constant 1 12 (440.0))
         (2 Outlet "out" 1))
    )";

    auto result = synthFromSExprText(sexprText, "from_language");
    if (!result) {
        std::println("ERROR: {}", result.error());
        return;
    }

    Synth* synth = result.value();
    std::println("SUCCESS: Created synth with {} expressions", synth->exprs.size());

    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        synth->dump();
    }
}

void test_sexpr_from_file() {
    std::println("\n=== Testing s-expression from file ===");

    // Read the file that was written by the language test
    std::ifstream file("/tmp/test_synth.sexpr");
    if (!file) {
        std::println("ERROR: Could not open /tmp/test_synth.sexpr");
        std::println("Run the language test first: ./whatever test_sexpr_io.sapf3");
        return;
    }

    std::string sexprText((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());

    std::println("Read s-expression from file:\n{}", sexprText);

    auto result = synthFromSExprText(sexprText, "from_file");
    if (!result) {
        std::println("ERROR: {}", result.error());
        return;
    }

    Synth* synth = result.value();
    std::println("SUCCESS: Created synth from file with {} expressions", synth->exprs.size());

    synth->graphAnalysis();
    synth->dump();
}

void test_sexpr_integration() {
    test_sexpr_simple();
    test_sexpr_binary_op();
    test_sexpr_from_language();
    test_sexpr_from_file();

    std::println("\n=== S-Expression Integration Tests Complete ===");
}

} // namespace synthdef
