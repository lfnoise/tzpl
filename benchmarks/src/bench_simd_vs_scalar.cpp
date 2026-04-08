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
//  bench_simd_vs_scalar.cpp
//  benchmarks
//
//  Benchmark harness comparing SIMD vs scalar code generation for synthdef plugins.
//  Compiles each synthdef twice (SIMD width 4 and width 0), loads both dylibs,
//  and measures processAudio throughput.
//

#include "synthdef_synth.hpp"
#include "synthdef_cpp_codegen.hpp"
#include "synthdef_compile_link.hpp"
#include "synthdef_common_ops.hpp"
#include "tzpl_plugin_abi.h"
#include <chrono>
#include <print>
#include <dlfcn.h>
#include <cstring>

using namespace synthdef;
using hrclock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// Plugin setup / teardown helpers (adapted from synthdef_compile.cpp)
// ---------------------------------------------------------------------------

static size_t elemSize(tzpl_ElemType elem) {
    switch (elem) {
        case tzpl_kI32: return sizeof(int32_t);
        case tzpl_kI64: return sizeof(int64_t);
        case tzpl_kF32: return sizeof(float);
        case tzpl_kF64: return sizeof(double);
        default: return sizeof(float);
    }
}

static tzpl_SynthData* setupPlugin(tzpl_SynthDef const& def, double sampleRate) {
    tzpl_SynthData* data = def.funs.alloc();
    data->engine = nullptr;
    data->node = nullptr;
    data->funs = def.funs;
    data->num_ins = def.num_ins;
    data->num_outs = def.num_outs;
    data->num_controls = def.num_controls;
    data->fs = sampleRate;
    data->sd = 1.0 / sampleRate;

    data->inlets = (void**)calloc(def.num_ins, sizeof(void*));
    data->outlets = (void**)calloc(def.num_outs, sizeof(void*));
    data->controls = (void**)calloc(def.num_controls, sizeof(void*));

    for (int i = 0; i < def.num_ins; ++i) {
        data->inlets[i] = calloc(def.ins[i].type.chans, elemSize(def.ins[i].type.elem));
    }
    for (int i = 0; i < def.num_outs; ++i) {
        data->outlets[i] = calloc(def.outs[i].type.chans, elemSize(def.outs[i].type.elem));
    }
    for (int i = 0; i < def.num_controls; ++i) {
        data->controls[i] = calloc(def.controls[i].type.chans, elemSize(def.controls[i].type.elem));
    }

    def.funs.init(data);
    return data;
}

static void teardownPlugin(tzpl_SynthDef const& def, tzpl_SynthData* data) {
    if (def.funs.uninit) def.funs.uninit(data);
    for (int i = 0; i < data->num_ins; ++i) free(data->inlets[i]);
    for (int i = 0; i < data->num_outs; ++i) free(data->outlets[i]);
    for (int i = 0; i < data->num_controls; ++i) free(data->controls[i]);
    free(data->inlets);
    free(data->outlets);
    free(data->controls);
    def.funs.free(data);
}

// ---------------------------------------------------------------------------
// Compile a synthdef with the C++ DSL at a given SIMD width
// ---------------------------------------------------------------------------

struct CompiledPlugin {
    tzpl_SynthDef def;
    void* dlHandle = nullptr;
    bool valid = false;
};

static CompiledPlugin compileDSL(string const& name, std::function<void()> dslFun, int simdWidth) {
    CompiledPlugin result{};

    // Use a distinct name so scalar and SIMD dylibs don't collide
    string fullName = name + (simdWidth > 0 ? "_simd" : "_scalar");

    try {
        string cppCode;
        {
            PushSynth ps(new Synth(fullName));
            dslFun();
            gSynth->graphAnalysis();
            cppCode = cppCodeGen(gSynth, simdWidth);
        }

        string buildDir = getBuildDir();
        ensureBuildDirs(buildDir);
        writeCodeToFile(buildDir, fullName, cppCode);

        int err = compileAndLink(buildDir, fullName);
        if (err) {
            std::println("  ERROR: compile/link failed for {} (simdWidth={})", fullName, simdWidth);
            return result;
        }

        string path = dylibPath(buildDir, fullName);
        auto opt = loadDef(path);
        if (!opt.has_value()) {
            std::println("  ERROR: loadDef failed for {}", path);
            return result;
        }

        result.def = opt->def;
        result.dlHandle = opt->dlHandle;
        result.valid = true;
    } catch (std::exception const& e) {
        std::println("  EXCEPTION compiling {}: {}", fullName, e.what());
    }

    return result;
}

// ---------------------------------------------------------------------------
// Benchmark runner
// ---------------------------------------------------------------------------

struct BenchResult {
    string name;
    double scalarNsPerSample = 0;
    double simdNsPerSample = 0;
    double speedup = 0;
};

static double benchProcessAudio(tzpl_SynthDef const& def, int numSamples) {
    constexpr double sampleRate = 48000.0;
    tzpl_SynthData* data = setupPlugin(def, sampleRate);

    // Warm up
    for (int i = 0; i < 1000; ++i) {
        def.funs.processAudio(data);
    }

    // Measure
    auto start = hrclock::now();
    for (int i = 0; i < numSamples; ++i) {
        def.funs.processAudio(data);
    }
    auto end = hrclock::now();

    double totalNs = std::chrono::duration<double, std::nano>(end - start).count();
    double nsPerSample = totalNs / numSamples;

    teardownPlugin(def, data);
    return nsPerSample;
}

// ---------------------------------------------------------------------------
// Synthdef definitions using the C++ DSL
// ---------------------------------------------------------------------------

// 2-channel stereo synth -- simple oscillator pair
static void dsl_bubbles_lite() {
    S freq = nnhz(lfsaw(.4) * 24 + lfsaw(vec(8,7.23)) * 3 + 81);
    S out = .2 * fsinosc(freq);
    outlet(out);
}

// 2-channel with delay lines
static void dsl_bubbles() {
    S freq = nnhz(lfsaw(.4) * 24 + lfsaw(vec(8,7.23)) * 3 + 81);
    S out = .04 * fsinosc(freq);
    out = combn(fadein(out, .1), .2, 4);
    outlet(out);
}

// 2-channel with dust/decay envelope
static void dsl_dustone() {
    S amp = .2 * decay2(dust(4, 2), .04, .3);
    S out = amp * fsinxosc(800);
    outlet(out);
}

// 2-channel filter chain
static void dsl_mod1() {
    S o = bilin(fsinosc(1.4, 0), .1, .95);
    S in = .1 * lfsaw(vec(60,61), vec(0,.25));
    S out = in.chain(4, [=](S x){ return onepole(x, o); });
    outlet(out);
}

// 2-channel tremolo
static void dsl_violet_tremolo() {
    S out = tremolo(.2 * violet(2), 2, .5, vec(0, .25));
    outlet(out);
}

// 2-channel with modulated delay
static void dsl_mod4() {
    S o = bilin(fsinosc(.4, 0), .1, .95);
    S amp = .6 * decay2(lfimp(1./1.2), .01, .2);
    S in = lfzig(vec(200,251), vec(0,.25));
    S fout = amp * in.chain(4, [=](S x){ return onepole(x, o); });
    S out = combn(fout, vec(.3,.15), 4);
    outlet(out);
}

// 8-channel init-rate random oscillators summed to 2-channel
static void dsl_init_urand() {
    S detune = vec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand(16, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, 16, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(oscs) * 0.1;
    outlet(osc_sum);
}

// 2-channel filtered noise
static void dsl_dust1() {
    S f = biexp(sinosc(.3), 100, 6000);
    S out = .2 * velvet(f, 2);
    outlet(out);
}

// 2-channel sah + comb
static void dsl_sahtone1() {
    S freq = lag(biexp(sah(white(2), coin(3*sd(), 1)), 120, 800), .04);
    S in = .2 * lftri(freq);
    S out = combn(in, .2, 2);
    outlet(out);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
    std::println("================================================================");
    std::println("  SIMD vs Scalar Benchmark -- synthdef plugin processAudio");
    std::println("================================================================\n");

    // Number of samples to process per benchmark
    // 48000 * 10 = 10 seconds of audio at 48kHz
    int numSamples = 48000 * 10;

    if (argc > 1) {
        numSamples = atoi(argv[1]);
        if (numSamples <= 0) numSamples = 48000 * 10;
    }

    std::println("Processing {} samples per benchmark ({:.1f} seconds at 48kHz)\n",
                 numSamples, numSamples / 48000.0);

    // Define benchmark cases: name + DSL function
    struct BenchCase {
        string name;
        std::function<void()> dslFun;
    };

    vector<BenchCase> cases = {
        {"bubbles_lite",    dsl_bubbles_lite},
        {"bubbles",         dsl_bubbles},
        {"dustone",         dsl_dustone},
        {"mod1",            dsl_mod1},
        {"violet_tremolo",  dsl_violet_tremolo},
        {"mod4",            dsl_mod4},
        {"init_urand",      dsl_init_urand},
        {"dust1",           dsl_dust1},
        {"sahtone1",        dsl_sahtone1},
    };

    std::println("--- Compiling {} synthdefs (scalar + SIMD) ---\n", cases.size());

    vector<BenchResult> results;

    for (auto const& bc : cases) {
        std::println("Benchmarking: {}", bc.name);

        auto scalarPlugin = compileDSL(bc.name, bc.dslFun, 0);
        auto simdPlugin   = compileDSL(bc.name, bc.dslFun, 4);

        if (!scalarPlugin.valid || !simdPlugin.valid) {
            std::println("  SKIPPED (compilation failed)\n");
            continue;
        }

        double scalarNs = benchProcessAudio(scalarPlugin.def, numSamples);
        double simdNs   = benchProcessAudio(simdPlugin.def, numSamples);
        double speedup  = scalarNs / simdNs;

        results.push_back({bc.name, scalarNs, simdNs, speedup});

        std::println("  scalar: {:.2f} ns/sample", scalarNs);
        std::println("  SIMD:   {:.2f} ns/sample", simdNs);
        std::println("  speedup: {:.2f}x\n", speedup);
    }

    // Summary table
    std::println("\n================================================================");
    std::println("  Summary");
    std::println("================================================================");
    std::println("{:<20s} {:>14s} {:>14s} {:>10s}", "Synthdef", "Scalar ns/samp", "SIMD ns/samp", "Speedup");
    std::println("{:-<20s} {:-<14s} {:-<14s} {:-<10s}", "", "", "", "");

    for (auto const& r : results) {
        std::println("{:<20s} {:>14.2f} {:>14.2f} {:>9.2f}x", r.name, r.scalarNsPerSample, r.simdNsPerSample, r.speedup);
    }

    // Geometric mean of speedups
    if (!results.empty()) {
        double logSum = 0;
        for (auto const& r : results) logSum += std::log(r.speedup);
        double geoMean = std::exp(logSum / results.size());
        std::println("\n  Geometric mean speedup: {:.2f}x", geoMean);
    }

    std::println("");
    return 0;
}
