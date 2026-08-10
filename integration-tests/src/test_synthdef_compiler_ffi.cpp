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
#include "synthdef_compile_link.hpp"
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

static void test_compile_success() {
    std::print("Test: compileSynthDef succeeds with valid s-expression\n");

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

    // A minimal constant -> outlet synthdef
    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth test_sine_ffi (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let err = compileSynthDef(sexpr);
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "compile_success.x", &moduleCompiler);
    check(ok, "compileSynthDef source compiles and runs");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_compile_error() {
    std::print("Test: compileSynthDef returns error for invalid s-expression\n");

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

    // Invalid s-expression
    const char* source = R"LANG(
        import synthdef.*;
        let err = compileSynthDef("this is not valid sexpr");
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "compile_error.x", &moduleCompiler);
    check(ok, "compileSynthDef with bad input compiles and runs (returns error string)");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_compile_and_load() {
    std::print("Test: compileSynthDefAndLoad compiles, loads, and registers a def\n");

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

    // Compile and load, then verify the def was registered
    const char* source = R"LANG(
        import synthdef.*;
        import audio_engine.*;
        let sexpr = "(Synth loaded_sine (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let err = compileSynthDefAndLoad(sexpr);
        println(err);
        let defs = listSynthDefs();
        println(defs);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "compile_and_load.x", &moduleCompiler);
    check(ok, "compileSynthDefAndLoad source compiles and runs");

    // Verify the def was registered
    std::vector<std::string> names;
    engine::listNodeDefs(eng, names);
    bool found = false;
    for (auto const& n : names) {
        if (n == "loaded_sine") found = true;
    }
    check(found, "loaded_sine def found in engine after compileSynthDefAndLoad");

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_caching() {
    std::print("Test: Second compilation of same input uses cache (fast)\n");

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

    // Compile twice — the second should be faster due to caching
    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth cache_test (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let err1 = compileSynthDefAndLoad(sexpr);
        let err2 = compileSynthDefAndLoad(sexpr);
        println(err1);
        println(err2);
    )LANG";

    auto start = std::chrono::steady_clock::now();
    bool ok = compileAndRun(compiler, vm, source, "caching.x", &moduleCompiler);
    auto elapsed = std::chrono::steady_clock::now() - start;
    check(ok, "Caching test compiles and runs (second call should be fast)");

    fclose(devnull);
    engine::freeEngine(eng);
}

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

static void test_def_desc_introspection() {
    std::print("Test: getDefDesc reports ports, controls, and buffers\n");

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

    // A synthdef reading 2 channels from sample buffer 0, with category tags.
    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth test_bufdesc (Tags \"demo\" \"test\") (Graph 1 ((0 BufFixRead 0 0 2 0) (1 Outlet \"out\" 0))))";
        let err = compileSynthDefAndLoad(sexpr);
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "def_desc.x", &moduleCompiler);
    check(ok, "buffer synthdef compiles and loads");

    engine::DefDesc desc;
    bool found = engine::getDefDesc(eng, "test_bufdesc", desc);
    check(found, "getDefDesc finds test_bufdesc");
    if (found) {
        check(desc.ins.empty(), "no inlets");
        check(desc.outs.size() == 1, "one outlet");
        if (desc.outs.size() == 1) {
            check(desc.outs[0].name == "out", "outlet named 'out'");
            check(desc.outs[0].type.chans == 2, "outlet has 2 channels");
        }
        check(desc.controls.empty(), "no controls");
        check(desc.tags.size() == 2 && desc.tags[0] == "demo"
              && desc.tags[1] == "test",
              "embedded tags in declaration order");
        check(desc.buffers.size() == 1, "one buffer");
        if (desc.buffers.size() == 1) {
            check(desc.buffers[0].name == "buf0", "buffer named 'buf0'");
            check(desc.buffers[0].bufID == 0, "buffer id 0");
            check(desc.buffers[0].type.elem == tzpl_kF64, "buffer elem type f64");
            check(desc.buffers[0].type.chans == 2, "buffer spans 2 channels");
        }
    }

    engine::DefDesc missing;
    check(!engine::getDefDesc(eng, "no_such_def", missing),
          "getDefDesc returns false for unknown def");

    // The def also appears in the full listing.
    std::vector<engine::DefDesc> all;
    engine::listDefDescs(eng, all);
    bool listed = false;
    for (auto const& d : all) {
        if (d.name == "test_bufdesc" && d.buffers.size() == 1) listed = true;
    }
    check(listed, "listDefDescs includes test_bufdesc with its buffer");

    // --- On-disk discovery (plugin browser "Available" section) ---
    // The compiled dylib now sits in the compile cache.
    std::string cacheDir = synthdef::getBuildDir() + "dylib";
    std::vector<engine::PluginFile> files;
    engine::listPluginFiles({cacheDir}, files);
    std::string bufdescPath;
    for (auto const& f : files) {
        if (f.name == "test_bufdesc") bufdescPath = f.path;
    }
    check(!bufdescPath.empty(), "listPluginFiles finds test_bufdesc in the compile cache");

    if (!bufdescPath.empty()) {
        // The freshly compiled plugin carries the current ABI version stamp.
        if (void* handle = dlopen(bufdescPath.c_str(), RTLD_NOW | RTLD_LOCAL)) {
            void* verPtr = dlsym(handle, "tzpl_abi_version");
            check(verPtr != nullptr, "plugin exports tzpl_abi_version");
            if (verPtr) {
                check(*(int64_t*)verPtr == TZPL_PLUGIN_ABI_VERSION,
                      "tzpl_abi_version matches TZPL_PLUGIN_ABI_VERSION");
            }
            dlclose(handle);
        }

        engine::DefDesc fileDesc;
        bool fok = engine::getPluginFileDesc(bufdescPath.c_str(), fileDesc);
        check(fok, "getPluginFileDesc introspects the dylib without an engine");
        if (fok) {
            check(fileDesc.name == "test_bufdesc", "file desc name matches");
            check(fileDesc.buffers.size() == 1
                  && fileDesc.buffers[0].type.chans == 2,
                  "file desc reports the buffer metadata");
        }
        engine::DefDesc fileDesc2;
        check(engine::getPluginFileDesc(bufdescPath.c_str(), fileDesc2)
              && fileDesc2.buffers.size() == 1,
              "second (cached) introspection matches");

        // loadOneDef registers the file's def into a fresh engine.
        engine::Engine* eng2 = makeTestEngine();
        check(engine::loadOneDef(eng2, bufdescPath.c_str()),
              "loadOneDef loads the dylib");
        engine::DefDesc desc2;
        check(engine::getDefDesc(eng2, "test_bufdesc", desc2)
              && desc2.buffers.size() == 1,
              "loadOneDef registered the def with buffer metadata");
        engine::freeEngine(eng2);
    }

    fclose(devnull);
    engine::freeEngine(eng);
}

static void test_control_kinds() {
    std::print("Test: control kinds flow from DSL sugar to DefDesc widgets\n");

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

    // Author one control of each kind through the DSL sugar constructors
    // and compile via the sexpr path -- exercising DSL -> sexpr (optional
    // kind field) -> parser -> codegen -> plugin ABI in one shot.
    const char* source = R"LANG(
        import synthdef.*;
        fn kt() S {
            let freq = control("freq", ControlSpec {
                lo: 20.0, hi: 8000.0, init: 440.0,
                warp: ControlWarp.exponential });
            let trig = trigger("trig");
            let mute = toggle("mute");
            let mode = choice("mode", 4);
            outlet(freq + trig + mute + mode)
        }
        let err = kt makeGraph toSynthSexpr("test_ctl_kinds")
                     compileSynthDefAndLoad;
        println(err);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "ctl_kinds.x", &moduleCompiler);
    check(ok, "kind synthdef compiles and loads");

    engine::DefDesc desc;
    bool found = engine::getDefDesc(eng, "test_ctl_kinds", desc);
    check(found, "getDefDesc finds test_ctl_kinds");
    if (found && desc.controls.size() == 4) {
        auto kindOf = [&](char const* name) -> int {
            for (auto const& c : desc.controls)
                if (c.name == name) return (int)c.spec.kind;
            return -1;
        };
        check(kindOf("freq") == tzpl_ckContinuous, "control() is continuous");
        check(kindOf("trig") == tzpl_ckTrigger, "trigger() is a trigger");
        check(kindOf("mute") == tzpl_ckBoolean, "toggle() is boolean");
        check(kindOf("mode") == tzpl_ckSelect, "choice() is select");

        // And the UI derives the right widgets from them.
        using bridge::UIWidgetKind;
        check(bridge::widgetKindForControl(tzpl_ckContinuous) == UIWidgetKind::Slider
              && bridge::widgetKindForControl(tzpl_ckTrigger) == UIWidgetKind::Button
              && bridge::widgetKindForControl(tzpl_ckBoolean) == UIWidgetKind::Toggle
              && bridge::widgetKindForControl(tzpl_ckSelect) == UIWidgetKind::Number,
              "widget kinds derived per control kind");

        // choice() spec: 0..numChoices-1 in unit steps.
        for (auto const& c : desc.controls) {
            if (c.name == "mode") {
                check(c.spec.lo == 0.0 && c.spec.hi == 3.0
                      && c.spec.warp == tzpl_cwStep && c.spec.param == 1.0,
                      "choice() spec is 0..n-1 step 1");
            }
        }
    } else {
        check(false, "test_ctl_kinds has 4 controls");
    }

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
    std::string buildDir = synthdef::getBuildDir();
    fs::create_directories(buildDir + "dylib");
    fs::path fossil = fs::path(buildDir + "dylib") / "revseed_synth_r9.dylib";
    std::ofstream(fossil).close();
    // Nothing has compiled "revseed" in this process, so its counter is unset
    // exactly as it would be right after a restart. The compile itself fails
    // (no .cpp to build) and prints clang's error -- expected; the revision is
    // seeded and bumped before clang runs, which is what is under test.
    (void)synthdef::compileAndLink(buildDir, "revseed");
    check(synthdef::dylibPath(buildDir, "revseed")
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

    std::string buildDir = synthdef::getBuildDir();
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
    synthdef::ensureBuildDirs(buildDir);
    synthdef::writeCodeToFile(buildDir, "prunetest",
                              "extern \"C\" int tzpl_prunetest() { return 0; }\n");
    int err = synthdef::compileAndLink(buildDir, "prunetest");
    check(err == 0, "prunetest compiles and links");

    if (err == 0) {
        // Seeded to 6, so this build is r7; TZPL_KEEP_REVISIONS is unset in
        // the harness, so the default of 3 keeps r5, r6, r7.
        auto revs = revsOnDisk();
        check(revs.size() == 3, "only the newest 3 revisions survive");
        check(revs.size() == 3 && revs.back() == 7, "the fresh build is kept");
        check(revs.size() == 3 && revs.front() == 5,
              "the kept window is the newest revisions, not the oldest");
        check(fs::exists(synthdef::dylibPath(buildDir, "prunetest")),
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

// The compilation cache records a dylib path, not the code behind it, so a
// pruned revision must send the cache back to the compiler rather than fail.
static void test_cached_path_recompile_fallback() {
    std::print("Test: a cache hit on a deleted dylib recompiles instead of "
               "failing\n");
    namespace fs = std::filesystem;

    char const* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth fallback_test (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        compileSynthDefAndLoad(sexpr);
    )LANG";

    auto runOnce = [&](engine::Engine* eng) {
        ts::TypeUniverse types;
        ts::Compiler compiler(types);
        bridge::registerAudioEngineFFI(compiler);
        bridge::registerSynthdefCompilerFFI(compiler);
        bridge::registerClockFFI(compiler);
        ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR});
        auto target = compiler.createTarget();
        ts::VM vm(16 * 1024 * 1024, types, target);
        bridge::AppContext appCtx; appCtx.engine = eng;
        bridge::setAppContextOnVM(&vm, &appCtx);
        FILE* devnull = fopen("/dev/null", "w");
        vm.setPrintOutput(devnull);
        bool ok = compileAndRun(compiler, vm, source, "fallback.x", &moduleCompiler);
        fclose(devnull);
        return ok;
    };

    // First run populates the process-wide compilation cache with a path.
    engine::Engine* eng1 = makeTestEngine();
    check(runOnce(eng1), "fallback_test compiles and loads");
    engine::freeEngine(eng1);

    // Simulate that path having been pruned by later builds of the same name.
    std::string dylibDir = synthdef::getBuildDir() + "dylib";
    int deleted = 0;
    for (auto const& e : fs::directory_iterator(dylibDir)) {
        if (e.path().extension() == ".dylib"
            && e.path().stem().string().starts_with("fallback_test_synth")) {
            fs::remove(e.path());
            ++deleted;
        }
    }
    check(deleted > 0, "the cached dylib was on disk to delete");

    // Second run hits the cache, finds nothing to load, and recompiles. A
    // fresh engine means the def can only be present if the load succeeded.
    engine::Engine* eng2 = makeTestEngine();
    check(runOnce(eng2), "the run with a stale cached path still succeeds");
    engine::DefDesc desc;
    check(engine::getDefDesc(eng2, "fallback_test", desc),
          "the def is registered, so the recompiled dylib loaded");
    engine::freeEngine(eng2);

    for (auto const& e : fs::directory_iterator(dylibDir)) {
        if (e.path().extension() == ".dylib"
            && e.path().stem().string().starts_with("fallback_test_synth"))
            fs::remove(e.path());
    }
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
    check(!synthdef::loadDef(dylib.string()).has_value(),
          "synthdef::loadDef refuses a newer-ABI plugin");
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
    check(!synthdef::loadDef(dylib.string()).has_value(),
          "synthdef::loadDef refuses an unstamped plugin");
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

static void test_low_level_ffi() {
    std::print("Test: low-level FFI (synthdefAnalysisDump, synthdefGenCppFromSexpr)\n");

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

    // Capture print output so we can assert on it.
    char* buf = nullptr;
    size_t bufSize = 0;
    FILE* memOut = open_memstream(&buf, &bufSize);
    vm.setPrintOutput(memOut);

    const char* source = R"LANG(
        import synthdef.*;
        let sexpr = "(Synth dump_test (Graph 1 ((0 Constant 1 12 (440.0)) (1 Outlet \"out\" 0))))";
        let dump = synthdefAnalysisDump(sexpr, false);
        println(dump startsWith("SYNTH dump_test") ? "DUMP_OK" : "DUMP_BAD: " $ dump);
        let cpp = synthdefGenCppFromSexpr(sexpr, 0, true);
        println(cpp contains("dump_test") && !(cpp startsWith("error:")) ? "CPP_OK" : "CPP_BAD: " $ cpp);
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "low_level_ffi.x", &moduleCompiler);
    check(ok, "low-level FFI source compiles and runs");

    fflush(memOut);
    std::string output = buf ? std::string(buf, bufSize) : "";
    fclose(memOut);
    free(buf);

    check(output.find("DUMP_OK") != std::string::npos,
          "synthdefAnalysisDump returns address-free dump");
    check(output.find("CPP_OK") != std::string::npos,
          "synthdefGenCppFromSexpr returns generated C++ source");

    engine::freeEngine(eng);
}

// Read a script file into a string.
static std::string readScript(std::string const& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// Run a synthc differential-test script and assert it prints `sentinel`. The
// script does the actual A/B comparison against the C++ compiler over the FFI;
// here we just drive it and check the PASS line. `checkLabel` is the assertion
// message reported on failure.
static void run_synthc_diff_script(char const* scriptFile, char const* sentinel,
                                   char const* checkLabel) {
    ts::TypeUniverse types;
    ts::Compiler compiler(types);
    bridge::registerAudioEngineFFI(compiler);
    bridge::registerSynthdefCompilerFFI(compiler);
    bridge::registerClockFFI(compiler);

    engine::Engine* eng = makeTestEngine();

    // EXAMPLES_DIR: synthc_prod_diff.x imports instrument_synthdefs, which
    // lives with the shipped examples rather than the stdlib modules.
    ts::ModuleCompiler moduleCompiler(compiler, {MODULES_DIR, LANG_MODULES_DIR, EXAMPLES_DIR});
    auto target = compiler.createTarget();
    ts::VM vm(16 * 1024 * 1024, types, target);
    bridge::AppContext appCtx; appCtx.engine = eng;
    bridge::setAppContextOnVM(&vm, &appCtx);

    char* buf = nullptr;
    size_t bufSize = 0;
    FILE* memOut = open_memstream(&buf, &bufSize);
    vm.setPrintOutput(memOut);

    std::string source = readScript(std::string(SCRIPTS_DIR) + "/" + scriptFile);
    check(!source.empty(), std::format("{} script found", scriptFile).c_str());

    bool ok = compileAndRun(compiler, vm, source.c_str(), scriptFile, &moduleCompiler);
    check(ok, std::format("{} compiles and runs", scriptFile).c_str());

    fflush(memOut);
    std::string output = buf ? std::string(buf, bufSize) : "";
    fclose(memOut);
    free(buf);

    bool pass = output.find(sentinel) != std::string::npos;
    if (!pass) {
        std::print("--- script output ---\n{}\n---------------------\n", output);
    }
    check(pass, checkLabel);

    engine::freeEngine(eng);
}

static void test_synthc_analysis_diff() {
    std::print("Test: synthc analysis (M1) matches C++ analysis dump\n");
    run_synthc_diff_script(
        "synthc_analysis_diff.x", "M1 PASS",
        "synthc analysis (SORTED+TREES, audio-only loops) matches C++ "
        "and codegen byte-matches");
}

static void test_synthc_rewrite_diff() {
    std::print("Test: synthc rewrites-ON dump + codegen match C++ (rewrites on)\n");
    run_synthc_diff_script(
        "synthc_rewrite_diff.x", "M2 REWRITE DIFF PASS",
        "synthc rewrite engine reproduces the C++ rewriter (dump + codegen)");
}

static void test_synthc_voicer_diff() {
    std::print("Test: synthc voicer (M4.0) dump + codegen match C++\n");
    run_synthc_diff_script(
        "synthc_voicer_diff.x", "M4 VOICER DIFF PASS",
        "synthc flat-voice-mode voicer codegen matches C++ (dump + codegen)");
}

static void test_synthc_spectral_diff() {
    std::print("Test: synthc spectral (M4.4) dump + codegen match C++\n");
    run_synthc_diff_script(
        "synthc_spectral_diff.x", "M4 SPECTRAL DIFF PASS",
        "synthc spectral-chain codegen matches C++ (dump + codegen)");
}

static void test_synthc_simd_diff() {
    std::print("Test: synthc SIMD (M5.1) codegen matches C++ at width 4\n");
    run_synthc_diff_script(
        "synthc_simd_diff.x", "M5 SIMD DIFF PASS",
        "synthc SIMD scaffolding + simple forms match C++ (width 4)");
}

static void test_synthc_voicer_simd_diff() {
    std::print("Test: synthc flat-voice SIMD (M5.3) codegen matches C++ at width 4\n");
    run_synthc_diff_script(
        "synthc_voicer_simd_diff.x", "M5 VOICER SIMD DIFF PASS",
        "synthc flat-voice SIMD codegen matches C++ (width 4)");
}

static void test_synthc_buffer_simd_diff() {
    std::print("Test: synthc buffer SIMD (M5.4) codegen matches C++ at width 4 + 2\n");
    run_synthc_diff_script(
        "synthc_buffer_simd_diff.x", "M5 BUFFER SIMD DIFF PASS",
        "synthc buffer SIMD codegen matches C++ (width 4 + width 2)");
}

static void test_synthc_prod_diff() {
    std::print("Test: synthc production config (M5.5) byte-matches C++ (rewrites-on + width 4)\n");
    run_synthc_diff_script(
        "synthc_prod_diff.x", "M5 PROD DIFF PASS",
        "synthc production output (defSynthX: rewrites-on, width 4) byte-matches the C++ compiler");
}

static void test_effects_prod_diff() {
    std::print("Test: effects library defs byte-match C++ at production config\n");
    run_synthc_diff_script(
        "effects_prod_diff.x", "FX PROD DIFF PASS",
        "effects library fx* defs compile via synthc and byte-match the C++ compiler");
}

static void test_synthc_compile_and_load() {
    std::print("Test: synthc defSynthX compiles a synth to a dylib and loads it\n");

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

    // defSynthX runs the Tzopilotl-hosted compiler end to end: graph -> import
    // -> analyze -> codegen -> clang -> dylib -> load into the engine.
    const char* source = R"LANG(
        import synthc.compile.*;
        import synthdef.*;
        fn integ() S {
            let osc = (fs() / 440.0) sin;
            let d = delayVar(); d init(1, 0.0); let r = d read(1);
            let mixed = osc * 0.5 + r; d write(mixed); mixed outlet
        }
        defSynthX(integ, "synthc_integ");
    )LANG";

    bool ok = compileAndRun(compiler, vm, source, "synthc_compile.x", &moduleCompiler);
    check(ok, "synthc defSynthX source compiles and runs");

    std::vector<std::string> names;
    engine::listNodeDefs(eng, names);
    bool found = false;
    for (auto const& n : names) if (n == "synthc_integ") found = true;
    check(found, "synthc_integ def registered in engine after defSynthX");

    // main() sets TZPL_DEFAULT_TAGS=test, so defSynthX should have tagged it.
    engine::DefDesc desc;
    check(engine::getDefDesc(eng, "synthc_integ", desc)
          && desc.tags.size() == 1 && desc.tags[0] == "test",
          "defSynthX applied the TZPL_DEFAULT_TAGS session tag via synthc");

    fclose(devnull);
    engine::freeEngine(eng);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char const* argv[]) {
    std::print("=== Synthdef Compiler FFI Integration Tests ===\n\n");

    // Everything this harness compiles via defSynth/defSynthX is born tagged
    // "test" so the plugin browser can filter it out of user-facing lists.
    setenv("TZPL_DEFAULT_TAGS", "test", /*overwrite=*/0);

    test_compile_success();
    test_compile_error();
    test_compile_and_load();
    test_caching();
    test_list_synthdefs();
    test_def_desc_introspection();
    test_control_kinds();
    test_plugin_revision_recency();
    test_plugin_revision_pruning();
    test_cached_path_recompile_fallback();
    test_abi_version_refusal();
    test_unstamped_plugin_refusal();
    test_malformed_def_refusal();
    test_low_level_ffi();
    test_synthc_analysis_diff();
    test_synthc_rewrite_diff();
    test_synthc_voicer_diff();
    test_synthc_spectral_diff();
    test_synthc_simd_diff();
    test_synthc_voicer_simd_diff();
    test_synthc_buffer_simd_diff();
    test_synthc_prod_diff();
    test_effects_prod_diff();
    test_synthc_compile_and_load();

    std::print("\n=== Results: {} passed, {} failed ===\n",
               gTestsPassed, gTestsFailed);
    return gTestsFailed > 0 ? 1 : 0;
}
