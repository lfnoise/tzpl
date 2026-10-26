// bench_ugen_compare.cpp -- our common_ugens.x / filters.x ugens against their
// tensor-ugens references, for accuracy and speed (block engine plan M7).
//
// ugen_cases.x writes a synthc plugin per case and channel count, and
// run_ugen_compare.sh compiles them into PLUGIN_DIR/uc_<case>_<chans>.dylib.
// For each case this program runs the plugin and the tensor-ugens graph of the
// same ugen on the same input (seeded white noise whose first sample is 0, so
// a filter whose state starts at its first input starts at 0 as ours do; and
// for "_af" cases a frequency swept around 1 kHz), then reports
//  - err dB: the rms of the difference over the rms of the reference, in dB.
//    Our plugins compute in f32 and the references in f64, so about -130 dB
//    is the floor. Where our output runs a sample behind the reference's (our
//    phasors output the previous phase, TU's the new one), "shift 1" says the
//    error was measured with ours one sample later.
//  - ns per sample per channel for both, at 1 and 4 channels, best of
//    kRepeats. TU's time is its graph's less a graph of the input source alone,
//    as in bench_tensor_ugens.
// Noises have no common sequence, so they are timed only.
//
// Usage: bench_ugen_compare [--check] PLUGIN_DIR [case-substring]
//   --check: accuracy only, no timing; exits 1 if any case's error is above
//   its limit (-80 dB; -35 dB for ringz_af, whose direct form I differs from
//   TU's transposed form under a moving frequency).

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <print>
#include <string_view>
#include <string>
#include <vector>

#include "src/filter.hpp"
#include "src/noise.hpp"
#include "src/ops.hpp"
#include "src/phasor.hpp"
#include "tzpl_plugin_abi.h"

namespace {

constexpr int kBlock = 64;
constexpr double kFs = 48000.;
constexpr long kCompareLen = 96000;     // samples compared per channel
constexpr long kTimeLen = 1L << 20;     // samples timed per channel per repeat
constexpr int kRepeats = 5;

// ---------------------------------------------------------------- inputs

// Per channel: the audio input in [-1, 1) and a frequency in Hertz, both f32
// so that we and TU see the same values.
struct Inputs {
    int chans = 0;
    std::vector<std::vector<float>> in, freq;
};

Inputs makeInputs(int chans) {
    Inputs x;
    x.chans = chans;
    for (int c = 0; c < chans; ++c) {
        uint64_t s = 0x9e3779b97f4a7c15ull * (c + 1);
        std::vector<float> in(kCompareLen), fr(kCompareLen);
        for (long i = 0; i < kCompareLen; ++i) {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            in[i] = i == 0 ? 0.f : float(double(s >> 11) * 0x1p-52 - 1.);
            fr[i] = float(1000. * (1. + .5 * std::sin(2. * M_PI * 3. * double(i) / kFs + c)));
        }
        x.in.push_back(std::move(in));
        x.freq.push_back(std::move(fr));
    }
    return x;
}

// ---------------------------------------------------------------- TU side

// A source UGen playing per-channel sample arrays, cycling.
struct SourceState {
    std::vector<std::vector<Z>> data;
    long pos = 0;
};

// Graph_free frees a UGen's state, so the UGen holds a malloc'd pointer to
// the SourceState, which the TUGraph owns.
void sourceRun(UGen* u, int n) {
    auto* s = *(SourceState**)u->state;
    long len = (long)s->data[0].size();
    for (int c = 0; c < u->outs[0].chans; ++c) {
        Z* y = u->outs[0].wires[c].buffer;
        Z const* d = s->data[c].data();
        for (int i = 0; i < n; ++i) y[i] = d[(s->pos + i) % len];
    }
    s->pos = (s->pos + n) % len;
}

void sourceRun1(UGen* u) { sourceRun(u, 1); }

S source(SourceState* st, int chans) {
    Shape sh = Shape_row(chans);
    UGen* u = newUGen(gGraph, 0, nullptr, 1, &sh);
    u->run = sourceRun;
    u->run1 = sourceRun1;
    u->state = malloc(sizeof(SourceState*));
    *(SourceState**)u->state = st;
    return S(&u->outs[0]);
}

S konst(int chans, Z value) {
    std::vector<Z> v(chans, value);
    return S(Shape_row(chans), v.data());
}

enum class Kind { osc, filt, af };

struct Case {
    std::string name;
    Kind kind;
    bool compare;  // false for noises: speed only
    // The TU ugen, given its input (filt, af) and frequency in cycles per
    // sample (af).
    std::function<S(S in, S freq, int chans)> tu;
};

// A TU graph of a case, with its source state.
struct TUGraph {
    Graph g{};
    SourceState in, freq;
    Output* out = nullptr;
    bool ok = false;

    TUGraph(Case const& c, Inputs const& x, bool withUGen) {
        for (int ch = 0; ch < x.chans; ++ch) {
            in.data.emplace_back(x.in[ch].begin(), x.in[ch].end());
            std::vector<Z> f(x.freq[ch].size());
            for (size_t i = 0; i < f.size(); ++i) f[i] = Z(x.freq[ch][i]) / kFs;
            freq.data.push_back(std::move(f));
        }
        gGraph = &g;
        std::vector<Output*> outs;
        S sin = c.kind != Kind::osc ? source(&in, x.chans) : S(Z(0));
        S sf = c.kind == Kind::af ? source(&freq, x.chans) : S(Z(0));
        if (c.kind != Kind::osc) outs.push_back(sin.o);
        if (c.kind == Kind::af) outs.push_back(sf.o);
        if (withUGen) {
            out = c.tu(sin, sf, x.chans).o;
            outs.push_back(out);
        }
        if (!outs.empty()) ok = Graph_compile(&g, outs.data(), (int)outs.size(), kBlock);
        gGraph = nullptr;
    }
    ~TUGraph() {
        if (ok) Graph_free(&g);
    }
    void run() { Graph_run(&g); }
};

std::vector<std::vector<double>> runTU(Case const& c, Inputs const& x) {
    TUGraph t(c, x, true);
    std::vector<std::vector<double>> y(x.chans, std::vector<double>(kCompareLen));
    for (long b = 0; b < kCompareLen; b += kBlock) {
        t.run();
        for (int ch = 0; ch < x.chans; ++ch) {
            Wire* w = outputWire(t.out, ch);
            for (int i = 0; i < kBlock; ++i) y[ch][b + i] = w->stride ? w->buffer[i] : w->scalar;
        }
    }
    return y;
}

template <class F>
double bestNs(F&& runBlock, int chans) {
    long blocks = kTimeLen / kBlock;
    for (long i = 0; i < blocks / 8; ++i) runBlock();
    double best = 1e30;
    for (int r = 0; r < kRepeats; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (long i = 0; i < blocks; ++i) runBlock();
        auto t1 = std::chrono::steady_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        best = std::min(best, ns / (double(blocks) * kBlock * chans));
    }
    return best;
}

double timeTU(Case const& c, Inputs const& x) {
    double total, base = 0;
    {
        TUGraph t(c, x, true);
        total = bestNs([&] { t.run(); }, x.chans);
    }
    if (c.kind != Kind::osc) {
        TUGraph t(c, x, false);
        base = bestNs([&] { t.run(); }, x.chans);
    }
    return total - base;
}

// ---------------------------------------------------------------- our side

struct Plugin {
    void* lib = nullptr;
    tzpl_SynthDef def{};
    tzpl_SynthData* p = nullptr;
    // Port tables: per port, per channel, a pointer and its flags.
    std::vector<std::vector<void*>> inPtr, outPtr;
    std::vector<std::vector<uint8_t>> inConst, outConst;
    std::vector<std::vector<uint32_t>> inVer, outVer;
    std::vector<tzpl_InPort> ins;
    std::vector<tzpl_OutPort> outs;
    std::vector<std::vector<float>> outBuf;  // per output channel, kBlock
    int inPort = -1, freqPort = -1;

    bool open(std::string const& path) {
        lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        auto load = (tzpl_LoadSynthDefFun)dlsym(lib, "load");
        if (!load) return false;
        def = load();
        p = def.funs.alloc(kBlock);
        if (!p) return false;
        ins.resize(def.num_ins);
        inPtr.resize(def.num_ins);
        inConst.resize(def.num_ins);
        inVer.resize(def.num_ins);
        for (int k = 0; k < def.num_ins; ++k) {
            int n = tzpl_shapeChans(def.ins[k].type.shape);
            inPtr[k].assign(n, nullptr);
            inConst[k].assign(n, 0);
            inVer[k].assign(n, 0);
            ins[k] = {inPtr[k].data(), inConst[k].data(), inVer[k].data()};
            if (!std::strcmp(def.ins[k].name, "in")) inPort = k;
            if (!std::strcmp(def.ins[k].name, "freq")) freqPort = k;
        }
        outs.resize(def.num_outs);
        outPtr.resize(def.num_outs);
        outConst.resize(def.num_outs);
        outVer.resize(def.num_outs);
        for (int k = 0; k < def.num_outs; ++k) {
            int n = tzpl_shapeChans(def.outs[k].type.shape);
            outPtr[k].assign(n, nullptr);
            outConst[k].assign(n, 0);
            outVer[k].assign(n, 0);
            outs[k] = {outPtr[k].data(), outConst[k].data(), outVer[k].data()};
            for (int c = 0; c < n; ++c) {
                outBuf.emplace_back(kBlock);
                outPtr[k][c] = outBuf.back().data();
            }
        }
        p->num_ins = def.num_ins;
        p->num_outs = def.num_outs;
        p->maxBlock = kBlock;
        p->ins = ins.data();
        p->outs = outs.data();
        p->fs = kFs;
        p->sd = 1. / kFs;
        return def.funs.init(p) == tzpl_errNone;
    }
    ~Plugin() {
        if (p) def.funs.free(p);
        if (lib) dlclose(lib);
    }
    // Points the inputs at block b of x and runs it.
    void runBlock(Inputs& x, long b) {
        for (int c = 0; c < x.chans; ++c) {
            if (inPort >= 0) inPtr[inPort][c] = x.in[c].data() + b;
            if (freqPort >= 0) inPtr[freqPort][c] = x.freq[c].data() + b;
        }
        def.funs.process(p, 0, kBlock);
    }
    float out(int c, int i) const { return ((float const*)outPtr[0][c])[i]; }
};

// ---------------------------------------------------------------- cases

std::vector<Case> cases() {
    Z fm = 440. / kFs, fc = 1000. / kFs, q = 2.;
    auto osc = [](std::string n, auto f, bool cmp = true) {
        return Case{n, Kind::osc, cmp, [f](S, S, int ch) { return f(ch); }};
    };
    auto filt = [](std::string n, auto f) {
        return Case{n, Kind::filt, true, [f](S in, S, int ch) { return f(in, ch); }};
    };
    auto af = [](std::string n, auto f) {
        return Case{n, Kind::af, true, [f](S in, S fr, int ch) { return f(in, fr, ch); }};
    };
    return {
        osc("phasor", [=](int ch) { return phasor(konst(ch, fm)); }),
        osc("sinosc", [=](int ch) { return sinosc(konst(ch, fm)); }),
        osc("fsinosc", [=](int ch) { return fsinosc(konst(ch, fm)); }),
        // TU's lftri is our lfupar: the parabola (2x - 1)^2.
        osc("lfupar", [=](int ch) { return lftri(konst(ch, fm)); }),
        osc("lfvsaw", [=](int ch) { return lfvarsaw(konst(ch, fm), konst(ch, .3)); }),
        // Our lfupulse keeps a sample of each level per cycle: TU's lfupulse1.
        osc("lfupulse", [=](int ch) { return lfupulse1(konst(ch, fm), konst(ch, .3)); }),
        osc("lfzpulse", [=](int ch) { return lfzpulse(konst(ch, fm), konst(ch, .3)); }),
        osc("white", [](int ch) { return white(Shape_row(ch)); }, false),
        osc("pink", [](int ch) { return pink(Shape_row(ch)); }, false),
        // The plan's M8 benchmark synth: sinosc -> lpf.
        osc("sinlpf", [=](int ch) { return lpf(sinosc(konst(ch, fm)), konst(ch, fc), konst(ch, M_SQRT1_2)); }),

        filt("lag", [](S x, int ch) { return lag(x, konst(ch, 480)); }),
        filt("lag3", [](S x, int ch) { return lag3(x, konst(ch, 480)); }),
        filt("lagud", [](S x, int ch) { return lagud(x, konst(ch, 48), konst(ch, 4800)); }),
        filt("decay", [](S x, int ch) { return decay(x, konst(ch, 4800)); }),
        filt("decay2", [](S x, int ch) { return decay2(x, konst(ch, 48), konst(ch, 4800)); }),
        filt("leakdc", [](S x, int ch) { return leakdc(x, konst(ch, .995)); }),
        filt("leaky", [](S x, int ch) { return leaky(x, konst(ch, .995)); }),
        filt("lpf", [=](S x, int ch) { return lpf(x, konst(ch, fc), konst(ch, M_SQRT1_2)); }),
        filt("rlpf", [=](S x, int ch) { return lpf(x, konst(ch, fc), konst(ch, q)); }),
        filt("rhpf", [=](S x, int ch) { return hpf(x, konst(ch, fc), konst(ch, q)); }),
        filt("bpf", [=](S x, int ch) { return bpf(x, konst(ch, fc), konst(ch, q)); }),
        filt("brf", [=](S x, int ch) { return brf(x, konst(ch, fc), konst(ch, q)); }),
        filt("apf", [=](S x, int ch) { return apf(x, konst(ch, fc), konst(ch, q)); }),
        filt("peq", [=](S x, int ch) { return peq(x, konst(ch, fc), konst(ch, q), konst(ch, 6)); }),
        filt("lsf", [=](S x, int ch) { return lsf(x, konst(ch, fc), konst(ch, q), konst(ch, 6)); }),
        filt("hsf", [=](S x, int ch) { return hsf(x, konst(ch, fc), konst(ch, q), konst(ch, 6)); }),
        // Our ring and pling are TU's ringz and formlet.
        filt("ringz", [=](S x, int ch) { return ringz(x, konst(ch, fc), konst(ch, 4800)); }),
        filt("formlet", [=](S x, int ch) {
            return formlet(x, konst(ch, fc), konst(ch, 48), konst(ch, 4800));
        }),

        af("rlpf_af", [=](S x, S f, int ch) { return lpf(x, f, konst(ch, q)); }),
        af("ringz_af", [](S x, S f, int ch) { return ringz(x, f, konst(ch, 4800)); }),
    };
}

// rms(ours shifted by `shift` - ref) / rms(ref), in dB.
double errDb(std::vector<std::vector<double>> const& ref,
             std::vector<std::vector<float>> const& ours, int shift) {
    double e = 0, r = 0;
    for (size_t c = 0; c < ref.size(); ++c) {
        for (long i = 0; i + shift < kCompareLen; ++i) {
            double d = double(ours[c][i + shift]) - ref[c][i];
            e += d * d;
            r += ref[c][i] * ref[c][i];
        }
    }
    if (r == 0) return e == 0 ? -INFINITY : INFINITY;
    return e == 0 ? -INFINITY : 10. * std::log10(e / r);
}

}  // namespace

int main(int argc, char** argv) {
    bool checkOnly = argc > 1 && std::string_view(argv[1]) == "--check";
    if (checkOnly) { ++argv; --argc; }
    if (argc < 2) {
        std::println(stderr, "usage: bench_ugen_compare [--check] PLUGIN_DIR [case-substring]");
        return 2;
    }
    initialize();
    std::string dir = argv[1];
    char const* filter = argc > 2 ? argv[2] : nullptr;
    int failures = 0;
    int const chanCounts[] = {1, 4, 16};

    std::println("ours (synthc, f32) vs tensor-ugens (f64), block {}, ns per sample per channel",
                 kBlock);
    if (checkOnly) std::println("{:<10}{:>10}{:>7}", "ugen", "err dB", "shift");
    else std::println("{:<10}{:>10}{:>7}  {:>8}{:>8}{:>7}  {:>8}{:>8}{:>7}  {:>8}{:>8}{:>7}", "ugen",
                      "err dB", "shift", "ours 1", "TU 1", "ratio", "ours 4", "TU 4", "ratio", "ours 16",
                      "TU 16", "ratio");
    for (Case const& c : cases()) {
        if (filter && c.name.find(filter) == std::string::npos) continue;
        std::string errCol = "-", shiftCol = "";
        std::string speed;
        bool failed = false;
        for (int ch : chanCounts) {
            Inputs x = makeInputs(ch);
            Plugin pl;
            std::string path = std::format("{}/uc_{}_{}.dylib", dir, c.name, ch);
            if (!pl.open(path)) {
                char const* e = dlerror();
                std::println("{:<10} cannot load {}: {}", c.name, path, e ? e : "");
                failed = true;
                break;
            }
            if (c.compare && ch == chanCounts[0]) {
                // Accuracy, over all channels at the first channel count.
                std::vector<std::vector<float>> ours(ch, std::vector<float>(kCompareLen));
                for (long b = 0; b < kCompareLen; b += kBlock) {
                    pl.runBlock(x, b);
                    for (int k = 0; k < ch; ++k)
                        for (int i = 0; i < kBlock; ++i) ours[k][b + i] = pl.out(k, i);
                }
                auto ref = runTU(c, x);
                double e0 = errDb(ref, ours, 0), e1 = errDb(ref, ours, 1);
                bool s = e1 < e0 - 20;
                double e = s ? e1 : e0;
                errCol = std::format("{:.1f}", e);
                shiftCol = s ? "1" : "0";
                double limit = c.name == "ringz_af" ? -35 : -80;
                if (!(e <= limit)) {
                    errCol += " FAIL";
                    ++failures;
                }
            }
            if (checkOnly) continue;
            long b = 0;
            double ours = bestNs([&] {
                pl.runBlock(x, b);
                b = (b + kBlock) % kCompareLen;
            }, ch);
            double tu = timeTU(c, x);
            speed += std::format("  {:>8.3f}{:>8.3f}{:>7.2f}", ours, tu, ours / tu);
        }
        if (failed) ++failures;
        else std::println("{:<10}{:>10}{:>7}{}", c.name, errCol, shiftCol, speed);
        std::fflush(stdout);
    }
    if (checkOnly) {
        if (failures) std::println("UGEN COMPARE FAIL ({})", failures);
        else std::println("UGEN COMPARE PASS");
    }
    return failures && checkOnly ? 1 : 0;
}
