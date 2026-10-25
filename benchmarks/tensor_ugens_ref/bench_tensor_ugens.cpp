// bench_tensor_ugens.cpp -- ns per sample per channel for the tensor-ugens
// reference UGens (block engine plan §5; tensor-ugens ARCHITECTURE.md §8).
//
// Each case builds a graph around one UGen, compiles it with a block of 64
// and runs it for about kSamples samples per channel, keeping the best of
// kRepeats timings. A case whose UGen reads audio-rate sources (a filter's
// white input, an audio-rate frequency) also times a graph of the sources
// alone and subtracts it. Every source is a graph output, so in-place reuse
// can't overwrite it after the first block (TU §8's caveat).
//
// Usage: bench_tensor_ugens [filter-substring]

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <print>
#include <string>
#include <vector>

#include "src/filter.hpp"
#include "src/noise.hpp"
#include "src/ops.hpp"
#include "src/phasor.hpp"

namespace {

constexpr int kBlock = 64;
constexpr long kSamples = 1L << 21;
constexpr int kRepeats = 5;

// A graph under construction: outputs collects every Output the graph reads.
struct Built {
    std::vector<Output*> outputs;
};

using BuildFn = std::function<void(Built&, int chans)>;

// A constant of chans channels, each value.
S konst(int chans, Z value) {
    std::vector<Z> v(chans, value);
    return S(Shape_row(chans), v.data());
}

// The best time, in ns per sample per channel, of running build's graph.
double timeGraph(BuildFn const& build, int chans) {
    Graph g{};
    gGraph = &g;
    Built b;
    build(b, chans);
    if (b.outputs.empty()) {
        gGraph = nullptr;
        return 0;
    }
    if (!Graph_compile(&g, b.outputs.data(), (int)b.outputs.size(), kBlock)) {
        std::println(stderr, "Graph_compile failed");
        std::exit(1);
    }
    long blocks = kSamples / kBlock;
    for (long i = 0; i < blocks / 8; ++i) Graph_run(&g);  // warm up
    double best = 1e30;
    for (int r = 0; r < kRepeats; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (long i = 0; i < blocks; ++i) Graph_run(&g);
        auto t1 = std::chrono::steady_clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        best = std::min(best, ns / (double(blocks) * kBlock * chans));
    }
    Graph_free(&g);
    gGraph = nullptr;
    return best;
}

struct Case {
    std::string name;
    BuildFn sources;  // audio-rate inputs, timed alone and subtracted; may be empty
    std::function<S(std::vector<S> const&, int chans)> ugen;
};

// Runs c for chans channels: the graph of its sources and the UGen, less the
// graph of its sources alone.
double runCase(Case const& c, int chans) {
    auto full = [&](Built& b, int ch) {
        std::vector<S> srcs;
        if (c.sources) {
            Built sb;
            c.sources(sb, ch);
            for (Output* o : sb.outputs) {
                srcs.push_back(S(o));
                b.outputs.push_back(o);
            }
        }
        S y = c.ugen(srcs, ch);
        b.outputs.push_back(y.o);
    };
    double total = timeGraph(full, chans);
    double base = c.sources ? timeGraph(c.sources, chans) : 0;
    return total - base;
}

// Sources: white noise of chans channels.
void whiteSource(Built& b, int chans) { b.outputs.push_back(white(Shape_row(chans)).o); }

// Sources: an audio-rate frequency around f, which a phasor's tiny wobble keeps
// from being a constant.
BuildFn audioFreq(Z f) {
    return [f](Built& b, int chans) {
        S x = konst(chans, f) + phasor(konst(chans, 1e-5)) * 1e-6;
        b.outputs.push_back(x.o);
    };
}

// Sources: white noise, then an audio-rate frequency around f.
BuildFn whiteAndAudioFreq(Z f) {
    return [f](Built& b, int chans) {
        whiteSource(b, chans);
        audioFreq(f)(b, chans);
    };
}

std::vector<Case> cases() {
    std::vector<Case> cs;
    auto osc = [&](std::string name, std::function<S(int)> f) {
        cs.push_back({std::move(name), nullptr, [f](std::vector<S> const&, int ch) { return f(ch); }});
    };
    auto filt = [&](std::string name, std::function<S(S, int)> f) {
        cs.push_back({std::move(name), whiteSource, [f](std::vector<S> const& s, int ch) { return f(s[0], ch); }});
    };
    Z fm = 440. / 48000.;

    // Phasors and oscillators (TU §8 rows).
    osc("phasor", [=](int ch) { return phasor(konst(ch, fm)); });
    osc("sinosc", [=](int ch) { return sinosc(konst(ch, fm)); });
    osc("fsinosc", [=](int ch) { return fsinosc(konst(ch, fm)); });
    osc("lftri", [=](int ch) { return lftri(konst(ch, fm)); });
    osc("lfvarsaw", [=](int ch) { return lfvarsaw(konst(ch, fm), konst(ch, .3)); });
    osc("lfupulse", [=](int ch) { return lfupulse(konst(ch, fm), konst(ch, .3)); });
    osc("lfzpulse1", [=](int ch) { return lfzpulse1(konst(ch, fm), konst(ch, .3)); });

    // Noises.
    osc("white", [](int ch) { return white(Shape_row(ch)); });
    osc("pink", [](int ch) { return pink(Shape_row(ch)); });
    osc("brown", [](int ch) { return brown(Shape_row(ch)); });
    osc("gray", [](int ch) { return gray(Shape_row(ch)); });
    osc("violet", [](int ch) { return violet(Shape_row(ch)); });
    osc("blue", [](int ch) { return blue(Shape_row(ch)); });
    osc("dust", [](int ch) { return dust(konst(ch, .01)); });
    osc("coin", [](int ch) { return coin(konst(ch, .5)); });
    osc("rand", [](int ch) { return rand(konst(ch, -1), konst(ch, 1)); });
    osc("rand2", [](int ch) { return rand2(konst(ch, 1)); });
    osc("irand", [](int ch) { return irand(konst(ch, 0), konst(ch, 10)); });
    osc("irand2", [](int ch) { return irand2(konst(ch, 5)); });
    osc("xrand", [](int ch) { return xrand(konst(ch, 100), konst(ch, 1000)); });
    osc("lfnoise0 f=.001", [](int ch) { return lfnoise0(konst(ch, .001)); });
    osc("lfnoise1 f=.001", [](int ch) { return lfnoise1(konst(ch, .001)); });
    osc("lfnoise3 f=.001", [](int ch) { return lfnoise3(konst(ch, .001)); });
    cs.push_back({"lfnoise3 f=.05 audio", audioFreq(.05),
                  [](std::vector<S> const& s, int) { return lfnoise3(s[0]); }});

    // Filters with constant parameters, on white noise.
    Z fc = 1000. / 48000.;
    filt("lpf1", [=](S x, int ch) { return lpf1(x, konst(ch, fc)); });
    filt("hpf1", [=](S x, int ch) { return hpf1(x, konst(ch, fc)); });
    filt("lsf1", [=](S x, int ch) { return lsf1(x, konst(ch, fc), konst(ch, 6)); });
    filt("lpf", [=](S x, int ch) { return lpf(x, konst(ch, fc), konst(ch, 2)); });
    filt("bpf", [=](S x, int ch) { return bpf(x, konst(ch, fc), konst(ch, 2)); });
    filt("peq", [=](S x, int ch) { return peq(x, konst(ch, fc), konst(ch, 2), konst(ch, 6)); });
    filt("lag", [](S x, int ch) { return lag(x, konst(ch, 480)); });
    filt("lag3", [](S x, int ch) { return lag3(x, konst(ch, 480)); });
    filt("lagud", [](S x, int ch) { return lagud(x, konst(ch, 48), konst(ch, 4800)); });
    filt("decay", [](S x, int ch) { return decay(x, konst(ch, 4800)); });
    filt("decay2", [](S x, int ch) { return decay2(x, konst(ch, 48), konst(ch, 4800)); });
    filt("leakdc", [](S x, int ch) { return leakdc(x); });
    filt("resonz", [=](S x, int ch) { return resonz(x, konst(ch, fc), konst(ch, .1)); });
    filt("ringz", [=](S x, int ch) { return ringz(x, konst(ch, fc), konst(ch, 4800)); });
    filt("formlet", [=](S x, int ch) { return formlet(x, konst(ch, fc), konst(ch, 48), konst(ch, 4800)); });

    // Filters with an audio-rate frequency.
    cs.push_back({"lpf1 audio f", whiteAndAudioFreq(fc),
                  [](std::vector<S> const& s, int) { return lpf1(s[0], s[1]); }});
    cs.push_back({"lpf audio f", whiteAndAudioFreq(fc),
                  [](std::vector<S> const& s, int ch) { return lpf(s[0], s[1], konst(ch, 2)); }});
    cs.push_back({"ringz audio f", whiteAndAudioFreq(fc),
                  [](std::vector<S> const& s, int ch) { return ringz(s[0], s[1], konst(ch, 4800)); }});
    return cs;
}

}  // namespace

int main(int argc, char** argv) {
    initialize();
    char const* filter = argc > 1 ? argv[1] : nullptr;
    int const chanCounts[] = {1, 2, 4, 16};
    std::println("tensor-ugens reference, {} (Z = {}), block {}, ns per sample per channel",
                 sizeof(Z) == 8 ? "f64" : "f32", sizeof(Z) == 8 ? "double" : "float", kBlock);
    std::println("{:<24}{:>9}{:>9}{:>9}{:>9}", "ugen", "1 ch", "2 ch", "4 ch", "16 ch");
    for (Case const& c : cases()) {
        if (filter && c.name.find(filter) == std::string::npos) continue;
        std::print("{:<24}", c.name);
        for (int ch : chanCounts) {
            std::print("{:>9.3f}", runCase(c, ch));
            std::fflush(stdout);
        }
        std::println("");
    }
    return 0;
}
