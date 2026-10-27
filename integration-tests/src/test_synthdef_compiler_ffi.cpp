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
//  test_synthdef_compiler_ffi.cpp
//  integration-tests
//
//  Integration test for the synthdef-compiler FFI bridge.
//

#include "tzpl_synthdef_compiler_ffi.hpp"
#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_synth_build.hpp"
#include "tzpl_clock_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl.hpp"
#include "module_compiler.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_test_plugins.hpp"
#include "tzpl_ui_node_controls.hpp"
#include <print>
#include <cstdlib>
#include <string_view>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <dlfcn.h>

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

// Compile and run a Tzopilotl source string. Returns true on compilation success.
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

// Create a standard test engine (no audio output).
static engine::Engine* makeTestEngine() {
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
// Tests
// ---------------------------------------------------------------------------

static void test_list_synthdefs() {
    std::print("Test: listSynthDefs returns registered def names\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    // Register a built-in test plugin so we have something to list
    engine::createSineNode(eng);

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    const char* source = R"LANG(
        import audio_engine.*;
        let defs = listSynthDefs();
        println(defs);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "list_synthdefs.x", &moduleCompiler);
    check(ok, "listSynthDefs source compiles and runs");

    // Also verify via C++ that the def is listed
    std::vector<std::string> names;
    engine::listNodeDefs(eng, names);
    // The output node and xfader are always registered, plus SinOsc
    check(names.size() >= 2, "At least 2 defs registered (builtins + SinOsc)");

    fclose(devnull);
    engine::freeEngine(eng);
}

// A revision number only orders builds within one process. Before the fix,
// the counter restarted at 0 every launch (so each run rewrote _r1) while
// listPluginFiles picked the highest revision -- resolving to whatever stale
// dylib an earlier session happened to leave behind at a higher revision.
static void test_plugin_revision_recency() {
    std::print("Test: plugin discovery resolves to the newest build, not the "
               "highest revision\n");
    namespace fs = std::filesystem;

    fs::path dir = fs::temp_directory_path() / "tzpl_rev_recency_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // listPluginFiles never dlopens, so empty files with the right names and
    // timestamps are enough to exercise the selection rule.
    auto touch = [&](char const* stem, int hoursAgo) {
        fs::path p = dir / (std::string(stem) + ".dylib");
        std::ofstream(p).close();
        fs::last_write_time(p, fs::file_time_type::clock::now()
                                   - std::chrono::hours(hoursAgo));
        return p;
    };

    // _r2 is the fossil from an older session; _r1 is today's build.
    touch("revtest_synth_r2", 96);
    fs::path fresh = touch("revtest_synth_r1", 0);

    std::vector<engine::PluginFile> files;
    engine::listPluginFiles({dir.string()}, files);

    std::string picked;
    int matches = 0;
    for (auto const& f : files) {
        if (f.name == "revtest") { picked = f.path; ++matches; }
    }
    check(matches == 1, "revisions of one name collapse to a single entry");
    check(picked == fresh.string(),
          "newer mtime wins over the higher revision number");

    // Ties on mtime fall back to the revision number.
    auto sameTime = fs::file_time_type::clock::now() - std::chrono::hours(3);
    fs::path lo = touch("tietest_synth_r4", 0);
    fs::path hi = touch("tietest_synth_r7", 0);
    fs::last_write_time(lo, sameTime);
    fs::last_write_time(hi, sameTime);

    files.clear();
    engine::listPluginFiles({dir.string()}, files);
    std::string tiePicked;
    for (auto const& f : files) {
        if (f.name == "tietest") tiePicked = f.path;
    }
    check(tiePicked == hi.string(),
          "equal mtimes fall back to the higher revision");

    // compileAndLink seeds from disk, so a fresh process still advances past
    // revisions an earlier session left behind.
    std::string buildDir = synthbuild::getBuildDir();
    fs::create_directories(buildDir + "dylib");
    fs::path fossil = fs::path(buildDir + "dylib") / "revseed_synth_r9.dylib";
    std::ofstream(fossil).close();
    // Nothing has compiled "revseed" in this process, so its counter is unset
    // exactly as it would be right after a restart. The compile itself fails
    // (no .cpp to build) and prints clang's error -- expected; the revision is
    // seeded and bumped before clang runs, which is what is under test.
    (void)synthbuild::compileAndLink(buildDir, "revseed");
    check(synthbuild::dylibPath(buildDir, "revseed")
              == buildDir + "dylib/revseed_synth_r10.dylib",
          "revision counter seeds past dylibs left by an earlier session");
    fs::remove(fossil);

    fs::remove_all(dir);
}

// Old revisions are dropped as later ones are built, so the compile cache stays
// bounded across a long session instead of growing one dylib per rebuild.
static void test_plugin_revision_pruning() {
    std::print("Test: compiling prunes all but the newest revisions\n");
    namespace fs = std::filesystem;

    std::string buildDir = synthbuild::getBuildDir();
    std::string dylibDir = buildDir + "dylib";
    fs::create_directories(dylibDir);

    auto revsOnDisk = [&] {
        std::vector<unsigned long long> revs;
        std::string prefix = "prunetest_synth_r";
        for (auto const& e : fs::directory_iterator(dylibDir)) {
            std::string stem = e.path().stem().string();
            if (e.path().extension() == ".dylib" && stem.starts_with(prefix))
                revs.push_back(std::stoull(stem.substr(prefix.size())));
        }
        std::sort(revs.begin(), revs.end());
        return revs;
    };

    // A session's worth of accumulated revisions, and a foreign unrevisioned
    // artifact that pruning must not touch.
    for (int r = 1; r <= 6; ++r) {
        std::ofstream(fs::path(dylibDir)
                      / ("prunetest_synth_r" + std::to_string(r) + ".dylib"))
            .close();
    }
    fs::path unrevisioned = fs::path(dylibDir) / "prunetest_synth.dylib";
    std::ofstream(unrevisioned).close();

    // Pruning runs only after a successful compile+link, so this needs source
    // that actually builds -- but not a working plugin, since nothing dlopens
    // it here.
    synthbuild::ensureBuildDirs(buildDir);
    synthbuild::writeCodeToFile(buildDir, "prunetest",
                              "extern \"C\" int tzpl_prunetest() { return 0; }\n");
    int err = synthbuild::compileAndLink(buildDir, "prunetest");
    check(err == 0, "prunetest compiles and links");

    if (err == 0) {
        // Seeded to 6, so this build is r7; TZPL_KEEP_REVISIONS is unset in
        // the harness, so the default of 3 keeps r5, r6, r7.
        auto revs = revsOnDisk();
        check(revs.size() == 3, "only the newest 3 revisions survive");
        check(revs.size() == 3 && revs.back() == 7, "the fresh build is kept");
        check(revs.size() == 3 && revs.front() == 5,
              "the kept window is the newest revisions, not the oldest");
        check(fs::exists(synthbuild::dylibPath(buildDir, "prunetest")),
              "the path dylibPath reports still exists");
        check(fs::exists(unrevisioned), "an unrevisioned dylib is left alone");
    }

    for (auto r : revsOnDisk()) {
        fs::remove(fs::path(dylibDir)
                   / ("prunetest_synth_r" + std::to_string(r) + ".dylib"));
    }
    fs::remove(unrevisioned);
    fs::remove(fs::path(buildDir) / "cpp" / "prunetest_synth.cpp");
    fs::remove(fs::path(buildDir) / "obj" / "prunetest_synth.o");
}

static void test_abi_version_refusal() {
    std::print("Test: loaders refuse plugins with a newer ABI version\n");
    namespace fs = std::filesystem;

    // Fabricate a dylib that claims a future ABI version.
    fs::path dir = fs::temp_directory_path() / "tzpl_abi_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path src = dir / "future.cpp";
    fs::path dylib = dir / "future_synth.dylib";
    {
        std::ofstream out(src);
        out << "#include <cstdint>\n"
               "extern \"C\" int64_t tzpl_abi_version = 9999;\n"
               "extern \"C\" void load() {}\n";
    }
    std::string cmd = "clang++ -dynamiclib -o '" + dylib.string() + "' '"
                      + src.string() + "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        std::print("  SKIP: could not compile test dylib\n");
        return;
    }

    engine::Engine* eng = makeTestEngine();
    check(!engine::loadOneDef(eng, dylib.string().c_str()),
          "loadOneDef refuses a newer-ABI plugin");
    engine::DefDesc desc;
    check(!engine::getPluginFileDesc(dylib.string().c_str(), desc),
          "getPluginFileDesc refuses a newer-ABI plugin");
    check(!synthbuild::loadDef(dylib.string()).has_value(),
          "synthbuild::loadDef refuses a newer-ABI plugin");
    engine::freeEngine(eng);
    fs::remove_all(dir, ec);
}

// A plugin built before the version stamp existed cannot be read safely: its
// tzpl_SynthDef predates swapBuffer being appended to tzpl_SynthFuns, so every
// field after `funs` sits 8 bytes earlier and the port arrays come out null
// beside non-zero counts. Absence of tzpl_abi_version must be a refusal, not
// "version 0". (Regression: crashed the plugin browser on 2026-07-23.)
static void test_unstamped_plugin_refusal() {
    std::print("Test: loaders refuse plugins with no ABI version stamp\n");
    namespace fs = std::filesystem;

    fs::path dir = fs::temp_directory_path() / "tzpl_abi_unstamped_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path src = dir / "unstamped.cpp";
    fs::path dylib = dir / "unstamped_synth.dylib";
    {
        std::ofstream out(src);
        // No tzpl_abi_version symbol at all -- the pre-versioning shape.
        out << "#include <cstdint>\n"
               "extern \"C\" void load() {}\n";
    }
    std::string cmd = "clang++ -dynamiclib -o '" + dylib.string() + "' '"
                      + src.string() + "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        std::print("  SKIP: could not compile test dylib\n");
        return;
    }

    engine::Engine* eng = makeTestEngine();
    check(!engine::loadOneDef(eng, dylib.string().c_str()),
          "loadOneDef refuses an unstamped plugin");
    engine::DefDesc desc;
    check(!engine::getPluginFileDesc(dylib.string().c_str(), desc),
          "getPluginFileDesc refuses an unstamped plugin");
    check(!synthbuild::loadDef(dylib.string()).has_value(),
          "synthbuild::loadDef refuses an unstamped plugin");
    engine::freeEngine(eng);
    fs::remove_all(dir, ec);
}

// A correctly stamped but malformed plugin: a non-zero port count beside a
// null array base. The ABI admits hand-written plugins, so the loaders must
// reject this rather than walking the null array.
static void test_malformed_def_refusal() {
    std::print("Test: loaders refuse a def whose counts and arrays disagree\n");
    namespace fs = std::filesystem;

    fs::path dir = fs::temp_directory_path() / "tzpl_abi_malformed_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path src = dir / "malformed.cpp";
    fs::path dylib = dir / "malformed_synth.dylib";
    {
        std::ofstream out(src);
        out << "#include \"tzpl_plugin_abi.h\"\n"
               "extern \"C\" int64_t tzpl_abi_version = TZPL_PLUGIN_ABI_VERSION;\n"
               "extern \"C\" tzpl_SynthDef load() {\n"
               "    tzpl_SynthDef d{};\n"
               "    d.name = \"malformed\";\n"
               "    d.num_outs = 1;   /* claims one output... */\n"
               "    d.outs = nullptr; /* ...but supplies no array */\n"
               "    return d;\n"
               "}\n";
    }
    std::string cmd = "clang++ -std=c++23 -dynamiclib -I'"
                      + std::string(SHARED_INCLUDE_DIR) + "' -o '"
                      + dylib.string() + "' '" + src.string() + "' 2>/dev/null";
    if (std::system(cmd.c_str()) != 0) {
        std::print("  SKIP: could not compile test dylib\n");
        return;
    }

    engine::Engine* eng = makeTestEngine();
    engine::DefDesc desc;
    check(!engine::getPluginFileDesc(dylib.string().c_str(), desc),
          "getPluginFileDesc refuses a null port array");
    check(!engine::loadOneDef(eng, dylib.string().c_str()),
          "loadOneDef refuses a null port array");
    engine::freeEngine(eng);
    fs::remove_all(dir, ec);
}

static void test_synthc_compile_and_load() {
    std::print("Test: synthc defSynth compiles a synth to a dylib and loads it\n");

    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    FILE* devnull = fopen("/dev/null", "w");
    vm.setPrintOutput(devnull);

    // defSynth runs the Tzopilotl-hosted compiler end to end: graph -> import
    // -> analyze -> codegen -> clang -> dylib -> load into the engine.
    const char* source = R"LANG(
        import synthc.compile.*;
        import synthdef.*;
        fn integ() S {
            let osc = (fs() / 440.0) sin;
            let d = delayVar(); d init(1, 0.0); let r = d read(1);
            let mixed = osc * 0.5 + r; d write(mixed); mixed outlet
        }
        defSynth(integ, "synthc_integ");
        defSynthX(integ, "synthc_integ_x");   -- the former name still works
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "synthc_compile.x", &moduleCompiler);
    check(ok, "synthc defSynth source compiles and runs");

    std::vector<std::string> names;
    engine::listNodeDefs(eng, names);
    bool found = false;
    for (auto const& n : names) if (n == "synthc_integ") found = true;
    check(found, "synthc_integ def registered in engine after defSynth");
    bool foundX = false;
    for (auto const& n : names) if (n == "synthc_integ_x") foundX = true;
    check(foundX, "defSynthX (the former name) still compiles and registers a def");

    // main() sets TZPL_DEFAULT_TAGS=test, so defSynth should have tagged it.
    engine::DefDesc desc;
    check(engine::getDefDesc(eng, "synthc_integ", desc)
          && desc.tags.size() == 1 && desc.tags[0] == "test",
          "defSynth applied the TZPL_DEFAULT_TAGS session tag via synthc");

    // synthDefReady: an already-resolved future for a name with no compile in
    // flight -- both a loaded def and one that never existed -- so `play` on
    // either passes straight through. (This harness has no async executor, so
    // compiles run inline and nothing is ever pending here; the pending path
    // is exercised by the app.)
    const char* readySource = R"LANG(
        import synthdef.*;
        let r1 = synthDefReady("synthc_integ") await;
        let r2 = synthDefReady("no_such_def_ever") await;
        if (r1 length != 0 || r2 length != 0) {
            panic("synthDefReady should resolve to an empty string");
        }
        let node = play("synthc_integ");
        if (node < 1000) { panic("play should allocate a node id"); }
    )LANG";
    ok = compileAndRun(compiler, vm, readySource, "synthdef_ready.x", &moduleCompiler);
    check(ok && !vm.isErrorHalted(),
          "synthDefReady resolves at once for loaded and unknown defs; play proceeds");

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char const* argv[]) {
    std::print("=== Synthdef Compiler FFI Integration Tests ===\n\n");

    // Everything this harness compiles via defSynth is born tagged
    // "test" so the plugin browser can filter it out of user-facing lists.
    setenv("TZPL_DEFAULT_TAGS", "test", /*overwrite=*/0);

    test_list_synthdefs();
    test_plugin_revision_recency();
    test_plugin_revision_pruning();
    test_abi_version_refusal();
    test_unstamped_plugin_refusal();
    test_malformed_def_refusal();
    test_synthc_compile_and_load();

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? 1 : 0;
}
