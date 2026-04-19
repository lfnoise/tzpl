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
//  synthdef_from_sexpr_test.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 1/11/25.
//

#include "synthdef_from_sexpr.hpp"
#include "synthdef_compile.hpp"
#include <print>
#include <fstream>

// The test suite is built with NDEBUG, which makes TEST_ASSERT() a no-op.
// TEST_ASSERT aborts on failure in release builds too, with a clear message.
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "TEST FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        abort(); \
    } \
} while (0)

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
        std::println("Run the language test first: ./whatever test_sexpr_io.tzpl");
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

void test_sexpr_synth_wrapper() {
    std::println("\n=== Testing Synth/Graph wrapper format ===");

    std::string sexprText = R"(
        (Synth test_wrapped
            (Graph 3 (
                (0 Constant 1 12 (440.0))
                (1 Constant 1 12 (1.0))
                (2 BinaryOp add (0 1))
                (3 Outlet "out" 2))))
    )";

    auto result = synthFromSExprText(sexprText);
    TEST_ASSERT(result.has_value());

    Synth* synth = result.value();
    TEST_ASSERT(synth->name == "test_wrapped");
    std::println("SUCCESS: Synth name = {}, {} expressions", synth->name, synth->exprs.size());

    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        synth->dump();
    }
}

void test_sexpr_if_expr() {
    std::println("\n=== Testing IfExpr with subgraphs ===");

    std::string sexprText = R"(
        (Synth test_if
            (Graph 5 (
                (0 Constant 1 15 (1))
                (1 Constant 1 15 (0))
                (2 CompareOp gt (0 1))
                (4 IfExpr (2)
                    (Graph 3 (
                        (3 Constant 1 12 (440.0))))
                    (Graph 100 (
                        (100 Constant 1 12 (220.0)))))
                (5 Outlet "out" 4))))
    )";

    auto result = synthFromSExprText(sexprText);
    TEST_ASSERT(result.has_value());

    Synth* synth = result.value();
    TEST_ASSERT(synth->name == "test_if");
    std::println("SUCCESS: Synth name = {}, {} expressions, {} graphs",
        synth->name, synth->exprs.size(), synth->graphs.size());

    {
        PushSynth ps(synth);
        synth->graphAnalysis();
        synth->dump();
    }
}

void test_sexpr_vec_ops() {
    std::println("\n=== Testing vector operations ===");

    // Test VecTake: take first 2 of 4-channel signal
    {
        std::string sexprText = R"(
            ((0 Constant 4 12 (1.0 2.0 3.0 4.0))
             (1 VecTake 2 (0))
             (2 Outlet "out" 1))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_take");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        std::println("  VecTake: OK");
    }

    // Test VecDrop: drop first 2 of 4-channel signal
    {
        std::string sexprText = R"(
            ((0 Constant 4 12 (1.0 2.0 3.0 4.0))
             (1 VecDrop 2 (0))
             (2 Outlet "out" 1))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_drop");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        std::println("  VecDrop: OK");
    }

    // Test VecReverse
    {
        std::string sexprText = R"(
            ((0 Constant 4 12 (1.0 2.0 3.0 4.0))
             (1 VecReverse (0))
             (2 Outlet "out" 1))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_reverse");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        std::println("  VecReverse: OK");
    }

    // Test VecStride
    {
        std::string sexprText = R"(
            ((0 Constant 4 12 (1.0 2.0 3.0 4.0))
             (1 VecStride 2 (0))
             (2 Outlet "out" 1))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_stride");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        std::println("  VecStride: OK");
    }

    // Test VecStutter
    {
        std::string sexprText = R"(
            ((0 Constant 2 12 (1.0 2.0))
             (1 VecStutter 2 (0))
             (2 Outlet "out" 1))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_stutter");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        std::println("  VecStutter: OK");
    }

    // Test VecNCyc
    {
        std::string sexprText = R"(
            ((0 Constant 2 12 (1.0 2.0))
             (1 VecNCyc 3 (0))
             (2 Outlet "out" 1))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_ncyc");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        std::println("  VecNCyc: OK");
    }

    // Test VecTranspose: 2x2 transpose
    {
        std::string sexprText = R"(
            ((0 Constant 4 12 (1.0 2.0 3.0 4.0))
             (1 VecTranspose 2 (0))
             (2 Outlet "out" 1))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_transpose");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        std::println("  VecTranspose: OK");
    }

    // Test VecJoin
    {
        std::string sexprText = R"(
            ((0 Constant 2 12 (1.0 2.0))
             (1 Constant 2 12 (3.0 4.0))
             (2 VecJoin (0 1))
             (3 Outlet "out" 2))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_join");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        TEST_ASSERT(result.value()->exprs[2]->chans == 4); // 2+2 = 4, already power of two
        std::println("  VecJoin: OK");
    }

    // Test VecJoin with non-power-of-two total (rounds up, zero-fills padding)
    {
        std::string sexprText = R"(
            ((0 Constant 2 12 (1.0 2.0))
             (1 Constant 1 12 (3.0))
             (2 VecJoin (0 1))
             (3 Outlet "out" 2))
        )";
        auto result = synthFromSExprText(sexprText, "test_vec_join_pad");
        TEST_ASSERT(result.has_value());
        PushSynth ps(result.value());
        result.value()->graphAnalysis();
        TEST_ASSERT(result.value()->exprs[2]->chans == 4); // 2+1 = 3, rounds up to 4
        std::println("  VecJoin (non-pow2 padding): OK");
    }

    std::println("  All vector operation tests passed!");
}

void test_sexpr_integration() {
    test_sexpr_simple();
    test_sexpr_binary_op();
    test_sexpr_from_language();
    test_sexpr_synth_wrapper();
    test_sexpr_if_expr();
    test_sexpr_from_file();
    test_sexpr_vec_ops();

    std::println("\n=== S-Expression Integration Tests Complete ===");
}

} // namespace synthdef
