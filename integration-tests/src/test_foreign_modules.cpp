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
//  test_foreign_modules.cpp
//  integration-tests
//
//  Tests for foreign module registration: pure foreign modules,
//  merged foreign + .x modules, and '_' privacy convention.
//

#include "compiler.hpp"
#include "vm.hpp"
#include "type_universe.hpp"
#include "module_compiler.hpp"
#include <print>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

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

static bool compileAndRun(ts::Compiler& compiler, ts::VM& vm,
                          const char* source, const char* testName,
                          ts::ModuleCompiler* moduleCompiler = nullptr) {
    auto target = vm.target();
    auto result = compiler.compile(source, testName, target, moduleCompiler);
    if (!result.success) {
        std::print("  Compilation FAILED for '{}':\n", testName);
        for (auto& err : result.errors) {
            std::print("    {}\n", err.message);
        }
        return false;
    }
    vm.makeCurrent();
    vm.install(result);
    vm.execute(result.mainBlock);
    return true;
}

// ---------------------------------------------------------------------------
// Foreign functions for testing
// ---------------------------------------------------------------------------

// add_values(a Int, b Int) Int — returns a + b
static void ffi_add_values(ts::VM& vm, u16 dst, u16 argc, u16 argBase) {
    i64 a = vm.reg(argBase).i;
    i64 b = vm.reg(argBase + 1).i;
    vm.reg(dst).i = a + b;
}

// mul_values(a Int, b Int) Int — returns a * b
static void ffi_mul_values(ts::VM& vm, u16 dst, u16 argc, u16 argBase) {
    i64 a = vm.reg(argBase).i;
    i64 b = vm.reg(argBase + 1).i;
    vm.reg(dst).i = a * b;
}

// _internal_helper(x Int) Int — private helper, returns x * 10
static void ffi_internal_helper(ts::VM& vm, u16 dst, u16 argc, u16 argBase) {
    i64 x = vm.reg(argBase).i;
    vm.reg(dst).i = x * 10;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_pure_foreign_module() {
    std::print("Test: Pure foreign module (no .x file)\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    // Register a foreign module "mathext"
    compiler.registerForeignModuleFunction("mathext", "add_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_add_values, true, true);
    compiler.registerForeignModuleFunction("mathext", "mul_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_mul_values, true, true);

    ts::ModuleCompiler moduleCompiler(compiler, {});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    // Test qualified import
    const char* source1 = R"(
        import mathext;
        println(mathext.add_values(3, 4));
        println(mathext.mul_values(5, 6));
    )";

    std::stringstream ss;
    FILE* memfile = tmpfile();
    vm.setPrintOutput(memfile);

    bool ok = compileAndRun(compiler, vm, source1, "pure_foreign_qualified.x", &moduleCompiler);
    check(ok, "Pure foreign module compiles with qualified import");

    // Read captured output
    fseek(memfile, 0, SEEK_SET);
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), memfile)) output += buf;
    fclose(memfile);

    check(output == "7\n30\n", "Qualified import produces correct output");
}

static void test_foreign_module_wildcard_import() {
    std::print("Test: Foreign module wildcard import\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    compiler.registerForeignModuleFunction("mathext", "add_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_add_values, true, true);

    ts::ModuleCompiler moduleCompiler(compiler, {});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    const char* source = R"(
        import mathext.*;
        println(add_values(10, 20));
    )";

    FILE* memfile = tmpfile();
    vm.setPrintOutput(memfile);

    bool ok = compileAndRun(compiler, vm, source, "foreign_wildcard.x", &moduleCompiler);
    check(ok, "Wildcard import compiles");

    fseek(memfile, 0, SEEK_SET);
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), memfile)) output += buf;
    fclose(memfile);

    check(output == "30\n", "Wildcard import produces correct output");
}

static void test_foreign_module_named_import() {
    std::print("Test: Foreign module named import with alias\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    compiler.registerForeignModuleFunction("mathext", "add_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_add_values, true, true);
    compiler.registerForeignModuleFunction("mathext", "mul_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_mul_values, true, true);

    ts::ModuleCompiler moduleCompiler(compiler, {});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    const char* source = R"(
        import mathext.{add_values, mul_values as times};
        println(add_values(2, 3));
        println(times(4, 5));
    )";

    FILE* memfile = tmpfile();
    vm.setPrintOutput(memfile);

    bool ok = compileAndRun(compiler, vm, source, "foreign_named.x", &moduleCompiler);
    check(ok, "Named import with alias compiles");

    fseek(memfile, 0, SEEK_SET);
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), memfile)) output += buf;
    fclose(memfile);

    check(output == "5\n20\n", "Named import produces correct output");
}

static void test_foreign_module_underscore_privacy() {
    std::print("Test: Underscore-prefixed foreign functions are private\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    // Register both public and private functions
    compiler.registerForeignModuleFunction("mathext", "add_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_add_values, true, true);
    compiler.registerForeignModuleFunction("mathext", "_internal_helper",
        compiler.intType(), {compiler.intType()},
        ffi_internal_helper, true, true);

    ts::ModuleCompiler moduleCompiler(compiler, {});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    // Trying to use the private function via wildcard import should fail
    const char* source = R"(
        import mathext.*;
        println(_internal_helper(5));
    )";

    auto result = compiler.compile(source, "privacy_test.x", target, &moduleCompiler);
    check(!result.success, "Private function '_internal_helper' is not accessible via wildcard import");
}

static void test_foreign_module_single_arg() {
    std::print("Test: Single-argument foreign function\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    compiler.registerForeignModuleFunction("mathext", "times_ten",
        compiler.intType(), {compiler.intType()},
        ffi_internal_helper, true, true);

    ts::ModuleCompiler moduleCompiler(compiler, {});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    const char* source = R"(
        import mathext.*;
        println(times_ten(5));
    )";

    FILE* memfile = tmpfile();
    vm.setPrintOutput(memfile);

    bool ok = compileAndRun(compiler, vm, source, "single_arg.x", &moduleCompiler);
    check(ok, "Single-arg foreign function compiles");

    fseek(memfile, 0, SEEK_SET);
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), memfile)) output += buf;
    fclose(memfile);

    check(output == "50\n", "Single-arg foreign function produces correct output (got: '" + output + "')");
}

static void test_foreign_module_alias() {
    std::print("Test: Foreign module with alias\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    compiler.registerForeignModuleFunction("mathext", "add_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_add_values, true, true);

    ts::ModuleCompiler moduleCompiler(compiler, {});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    const char* source = R"(
        import mathext as mx;
        println(mx.add_values(100, 200));
    )";

    FILE* memfile = tmpfile();
    vm.setPrintOutput(memfile);

    bool ok = compileAndRun(compiler, vm, source, "foreign_alias.x", &moduleCompiler);
    check(ok, "Module alias import compiles");

    fseek(memfile, 0, SEEK_SET);
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), memfile)) output += buf;
    fclose(memfile);

    check(output == "300\n", "Module alias produces correct output");
}

static void test_plain_module_control() {
    std::print("Test: Plain module control (no foreign functions)\n");

    // Write a temp module with no foreign functions
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/tzpl_test_modules";
    std::filesystem::create_directories(tmpDir);
    {
        std::ofstream f(tmpDir + "/plainmod.x");
        f << "fn triple(x Int) Int { x * 3 }\n";
    }

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    ts::ModuleCompiler moduleCompiler(compiler, {tmpDir});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    const char* source = R"(
        import plainmod.*;
        println(triple(5));
    )";

    FILE* memfile = tmpfile();
    vm.setPrintOutput(memfile);

    bool ok = compileAndRun(compiler, vm, source, "plain_control.x", &moduleCompiler);
    check(ok, "Plain module compiles");

    fseek(memfile, 0, SEEK_SET);
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), memfile)) output += buf;
    fclose(memfile);

    check(output == "15\n", "Plain module produces correct output (got: '" + output + "')");

    std::filesystem::remove_all(tmpDir);
}

static void test_merged_foreign_and_x_module() {
    std::print("Test: Foreign functions merged with .x module file\n");

    // Write a temporary .x module that wraps the foreign _internal_helper
    const char* moduleSource =
        "-- mathext.x: wraps FFI functions with public functions\n"
        "fn helper(x Int) Int { _internal_helper(x) }\n"
        "fn doubled(x Int) Int { add_values(x, x) }\n"
        "fn pure_fn(x Int) Int { x * 10 }\n";

    // Write to a temp directory
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/tzpl_test_modules";
    std::filesystem::create_directories(tmpDir);
    std::string modulePath = tmpDir + "/mathext.x";
    {
        std::ofstream f(modulePath);
        f << moduleSource;
    }

    ts::TypeUniverse types;
    ts::Compiler compiler(types);

    // Register private FFI function and a public one
    compiler.registerForeignModuleFunction("mathext", "_internal_helper",
        compiler.intType(), {compiler.intType()},
        ffi_internal_helper, true, true);
    compiler.registerForeignModuleFunction("mathext", "add_values",
        compiler.intType(), {compiler.intType(), compiler.intType()},
        ffi_add_values, true, true);

    ts::ModuleCompiler moduleCompiler(compiler, {tmpDir});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    // User code imports the merged module — can use both the .x wrapper and
    // the public FFI function, but NOT the private _internal_helper
    const char* source = R"(
        import mathext.*;
        println(pure_fn(5));
        println(helper(5));
        println(doubled(5));
        println(add_values(1, 2));
    )";

    FILE* memfile = tmpfile();
    vm.setPrintOutput(memfile);

    bool ok = compileAndRun(compiler, vm, source, "merged_test.x", &moduleCompiler);
    check(ok, "Merged foreign + .x module compiles");

    fseek(memfile, 0, SEEK_SET);
    char buf[256];
    std::string output;
    while (fgets(buf, sizeof(buf), memfile)) output += buf;
    fclose(memfile);

    check(output == "50\n50\n10\n3\n", "Merged module produces correct output (got: '" + output + "')");

    // Verify _internal_helper is NOT directly accessible
    const char* source2 = R"(
        import mathext.*;
        println(_internal_helper(5));
    )";
    auto result = compiler.compile(source2, "merged_privacy.x", target, &moduleCompiler);
    check(!result.success, "Private _internal_helper not accessible from merged module");

    std::filesystem::remove_all(tmpDir);
}

// ---------------------------------------------------------------------------
// Regression: dynamic variables holding user-defined struct types must
// survive a re-compile against the same Compiler. Each TypeChecker creates
// fresh StructType pointers, so the Type* stored in Compiler::dynamicVars_
// from the first compile becomes stale on the second compile. Without the
// prescanDynVars refresh fix, the second compile produces a cascade of
// "No matching overload for push" and "redeclared with different type"
// errors against the same source.
static void test_dynvar_recompile_no_stale_cascade() {
    std::print("Test: Dynamic var with user struct type survives re-compile\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    const char* source = R"(
        struct Item { id Int }

        fn addItem(it Item) Item {
            `myItems = `myItems push(it);
            it
        }

        fn build() [Item] {
            var `myItems [Item] = [];
            addItem(Item { id: 1 });
            `myItems
        }

        println(build());
    )";

    auto result1 = compiler.compile(source, "dynvar_recompile_1.x", target);
    check(result1.success, "First compile of source with dynamic var [Item] succeeds");
    if (!result1.success) {
        for (auto& err : result1.errors) std::print("    error: {}\n", err.message);
    }

    // Re-compile the same source with the same Compiler. The second TypeChecker
    // creates a fresh StructType for Item, but compiler.dynamicVars_ still holds
    // the ArrayType[Item_v1] from the first compile. The fix in prescanDynVars
    // refreshes this stale pointer to ArrayType[Item_v2] so push() type-checks.
    auto result2 = compiler.compile(source, "dynvar_recompile_2.x", target);
    check(result2.success, "Re-compile of same source succeeds (no stale-pointer cascade)");
    if (!result2.success) {
        for (auto& err : result2.errors) std::print("    error: {}\n", err.message);
    }
}

// Regression: editing a module that is a TRANSITIVE dependency (imported by a
// module you are importing) must cascade-invalidate every cached module that
// has stale declNode/type pointers into the destroyed AST. Without the cascade,
// the cached intermediate module's allFunctions still references the destroyed
// AST and a later use produces "Unknown type 'A'" errors or segfaults.
static void test_cascade_invalidate_transitive_dependency() {
    std::print("Test: Editing a transitive dependency cascade-invalidates dependents\n");

    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/tzpl_cascade_test";
    std::filesystem::remove_all(tmpDir);
    std::filesystem::create_directories(tmpDir);

    auto writeFile = [](const std::string& path, const std::string& body) {
        std::ofstream f(path);
        f << body;
    };

    // A.x defines a struct and a helper. B.x imports A and uses both.
    std::string pathA = tmpDir + "/A.x";
    std::string pathB = tmpDir + "/B.x";
    writeFile(pathA,
        "struct Item { id Int }\n"
        "fn makeItem(n Int) Item { Item { id: n } }\n"
    );
    writeFile(pathB,
        "import A.*;\n"
        "fn build() Item { makeItem(1) }\n"
    );

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    ts::ModuleCompiler moduleCompiler(compiler, {tmpDir});
    auto target = compiler.createTarget();
    ts::VM vm(64 * 1024 * 1024, types, target);

    const char* source = R"(
        import B.*;
        let result = build();
        println(result.id);
    )";

    FILE* memfile1 = tmpfile();
    vm.setPrintOutput(memfile1);
    bool ok1 = compileAndRun(compiler, vm, source, "top.x", &moduleCompiler);
    check(ok1, "Initial compile of top-level importing B (which imports A) succeeds");

    fseek(memfile1, 0, SEEK_SET);
    char buf[256];
    std::string out1;
    while (fgets(buf, sizeof(buf), memfile1)) out1 += buf;
    fclose(memfile1);
    check(out1 == "1\n", "Initial run produces id = 1");

    // "Edit" A.x by rewriting it with new (still-valid) content and bumping
    // its mod time. The new TypeChecker will create a fresh StructType* for
    // Item — without cascade invalidation, B's cached allFunctions still
    // references the destroyed StructType from the previous compile.
    writeFile(pathA,
        "struct Item { id Int }\n"
        "fn makeItem(n Int) Item { Item { id: n + 100 } }\n"
    );
    auto newTime = std::filesystem::last_write_time(pathA) + std::chrono::seconds(1);
    std::filesystem::last_write_time(pathA, newTime);

    FILE* memfile2 = tmpfile();
    vm.setPrintOutput(memfile2);
    bool ok2 = compileAndRun(compiler, vm, source, "top.x", &moduleCompiler);
    check(ok2, "After editing A, re-compile of top-level importing B succeeds (cascade re-compiles B)");

    fseek(memfile2, 0, SEEK_SET);
    std::string out2;
    while (fgets(buf, sizeof(buf), memfile2)) out2 += buf;
    fclose(memfile2);
    check(out2 == "101\n", "Re-run picks up the edited A.x (got: '" + out2 + "')");

    std::filesystem::remove_all(tmpDir);
}

// Main
// ---------------------------------------------------------------------------

int main() {
    std::print("=== Foreign Module Integration Tests ===\n\n");

    test_pure_foreign_module();
    test_foreign_module_wildcard_import();
    test_foreign_module_named_import();
    test_foreign_module_underscore_privacy();
    test_foreign_module_single_arg();
    test_foreign_module_alias();
    test_plain_module_control();
    test_merged_foreign_and_x_module();
    test_dynvar_recompile_no_stale_cascade();
    test_cascade_invalidate_transitive_dependency();

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? 1 : 0;
}
