// forms_equiv.cpp -- the equivalence-of-forms test (block engine plan M8).
//
// Every dump-corpus synth is compiled twice by run_forms_equivalence_test.sh:
// with the forms the compiler chooses (block scans and state spaces, sample-
// and channel-vector loops), and with TZPL_FORMS=serial (every loop in its
// plain serial form). This program runs both builds of each synth on the same
// inputs, over whole blocks (64), one sample at a time, and odd chunks, and
// checks every run against the serial build run in whole blocks:
//  - the rms of the difference over the reference's rms, below kTolDb;
//  - no non-finite sample where the reference has none.
// Block forms differ from the plain recurrence by rounding only.
//
// Inputs: an input with a spec holds its init value (isConst = 1); any other
// input is a sum of three sines per channel (60 Hz to 3 kHz), except a voiced
// synth's notes, whose rows hold the defaults with voice 0's gate on from the
// first sample (resetVoice). Not white noise: rounding differences in a delay
// time are amplified by the input's slope, and noise's is the largest.
// Random draws are seeded (TZPL_RNG_SEED), so both builds draw alike.
//
// Usage: forms_equiv AUTO_BUILD_DIR SERIAL_BUILD_DIR [name-substring | =name]
//        forms_equiv --time BUILD_DIR [name-substring]
//        forms_equiv --zero BUILD_DIR [name-substring]
//        forms_equiv --idle BUILD_DIR NOIDLE_BUILD_DIR
//   --time: ns per sample of each synth in whole blocks (best of 5), for
//   measuring the code generator's cost table against variant builds.
//   --zero: every output sample of every synth is 0 (within 1e-4), in whole
//   blocks and odd chunks: for tests whose synths output (result - expected),
//   such as run_shape_ops_test.sh.
//   --idle: idle voices (plan M9, run_idle_voices_test.sh). Voice 1 plays
//   from 0 s, is released at 0.5 s and plays again at 1.5 s. Prints when each
//   synth's output becomes exactly 0 for good before 1.5 s, and its largest
//   difference from the TZPL_FORMS=noidle build (no voice skipping).

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "tzpl_plugin_abi.h"

namespace fs = std::filesystem;

namespace {

constexpr int kBlock = 64;
constexpr int kBlocks = 150;
// The worst synth: fxMultiTap, -61 dB on arm64 and -47 dB on x86-64. Its
// delay time (about 38400 samples) comes from a lagged control, and f32 holds
// that to about 0.002 samples in any form, so a different rounding order moves
// the read position by that much. Most synths are below -80 dB; a wrong form
// is far above this limit (+30 dB).
constexpr double kTolDb = -40;

int elemSize(tzpl_ElemType t) { return t == tzpl_kI32 || t == tzpl_kF32 ? 4 : 8; }

double readElem(void const* p, tzpl_ElemType t, int i) {
    switch (t) {
        case tzpl_kI32: return ((int32_t const*)p)[i];
        case tzpl_kI64: return double(((int64_t const*)p)[i]);
        case tzpl_kF32: return ((float const*)p)[i];
        case tzpl_kF64: return ((double const*)p)[i];
    }
    return 0;
}

void writeElem(void* p, tzpl_ElemType t, int i, double v) {
    switch (t) {
        case tzpl_kI32: ((int32_t*)p)[i] = int32_t(v); break;
        case tzpl_kI64: ((int64_t*)p)[i] = int64_t(v); break;
        case tzpl_kF32: ((float*)p)[i] = float(v); break;
        case tzpl_kF64: ((double*)p)[i] = v; break;
    }
}

// The latest revision of each corpus plugin in DIR/dylib, by synth name.
std::map<std::string, fs::path> plugins(std::string const& dir) {
    std::map<std::string, std::pair<long, fs::path>> best;
    std::error_code ec;
    for (auto const& e : fs::directory_iterator(fs::path(dir) / "dylib", ec)) {
        auto ext = e.path().extension().string();
        if (!e.is_regular_file() || (ext != ".dylib" && ext != ".so")) continue;
        std::string stem = e.path().stem().string();
        auto r = stem.rfind("_synth_r");
        if (r == std::string::npos || !stem.starts_with("corpus_")) continue;
        std::string name = stem.substr(7, r - 7);
        long rev = std::atol(stem.c_str() + r + 8);
        auto it = best.find(name);
        if (it == best.end() || rev > it->second.first) best[name] = {rev, e.path()};
    }
    std::map<std::string, fs::path> out;
    for (auto& [k, v] : best) out[k] = v.second;
    return out;
}

// One instance of a plugin with its port buffers.
struct Instance {
    void* lib = nullptr;
    tzpl_SynthDef def{};
    tzpl_SynthData* p = nullptr;
    std::vector<std::vector<std::vector<uint8_t>>> inBuf, outBuf;  // [port][chan] bytes
    std::vector<std::vector<void*>> inPtr, outPtr;
    std::vector<std::vector<uint8_t>> inConst, outConst;
    std::vector<std::vector<uint32_t>> inVer, outVer;
    std::vector<tzpl_InPort> ins;
    std::vector<tzpl_OutPort> outs;

    bool open(fs::path const& path, std::string& err) {
        lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) { err = dlerror(); return false; }
        auto load = (tzpl_LoadSynthDefFun)dlsym(lib, "load");
        if (!load) { err = "no load symbol"; return false; }
        def = load();
        p = def.funs.alloc(kBlock);
        if (!p) { err = "alloc failed"; return false; }
        auto ports = [&](int num, tzpl_PortDef const* defs, auto& buf, auto& ptr, auto& cst, auto& ver) {
            buf.resize(num); ptr.resize(num); cst.resize(num); ver.resize(num);
            for (int k = 0; k < num; ++k) {
                int n = tzpl_shapeChans(defs[k].type.shape);
                int es = elemSize(defs[k].type.elem);
                buf[k].assign(n, std::vector<uint8_t>(size_t(kBlock) * es));
                for (int c = 0; c < n; ++c) ptr[k].push_back(buf[k][c].data());
                cst[k].assign(n, 0);
                ver[k].assign(n, 1);
            }
        };
        ports(def.num_ins, def.ins, inBuf, inPtr, inConst, inVer);
        ports(def.num_outs, def.outs, outBuf, outPtr, outConst, outVer);
        for (int k = 0; k < def.num_ins; ++k) ins.push_back({inPtr[k].data(), inConst[k].data(), inVer[k].data()});
        for (int k = 0; k < def.num_outs; ++k) outs.push_back({outPtr[k].data(), outConst[k].data(), outVer[k].data()});
        p->num_ins = def.num_ins;
        p->num_outs = def.num_outs;
        p->maxBlock = kBlock;
        p->ins = ins.data();
        p->outs = outs.data();
        p->fs = 48000;
        p->sd = 1. / 48000;
        if (def.funs.init(p) != tzpl_errNone) { err = "init failed"; return false; }
        return true;
    }
    ~Instance() {
        if (p) def.funs.free(p);
        // FORMS_NO_DLCLOSE: keep plugins loaded (AddressSanitizer mistracks
        // the globals of a library unloaded and loaded again)
        if (lib && !std::getenv("FORMS_NO_DLCLOSE")) dlclose(lib);
    }
};

// Fills block b of every input of `in` (the same values for any instance).
void fillInputs(Instance& in, int b) {
    for (int k = 0; k < in.def.num_ins; ++k) {
        tzpl_PortDef const& pd = in.def.ins[k];
        bool notes = in.def.voiced && !std::strcmp(pd.name, "notes");
        int n = int(in.inBuf[k].size());
        for (int c = 0; c < n; ++c) {
            void* buf = in.inBuf[k][c].data();
            // three sines per channel, 60 Hz to about 3 kHz
            double f[3];
            for (int j = 0; j < 3; ++j) f[j] = 60.0 * (1 + ((k * 7 + c * 3 + j * 11) % 50)) / 48000.0;
            for (int i = 0; i < kBlock; ++i) {
                double v;
                if (notes) {
                    int cols = int(pd.type.shape.dims[3]);
                    int col = c % cols, row = c / cols;
                    v = col == 0 ? (row == 0 ? 1.0 : 0.0) : (in.def.noteDefaults ? in.def.noteDefaults[col] : 0.0);
                } else if (pd.hasSpec) {
                    v = pd.spec.init;
                } else {
                    double t = double(b * kBlock + i);
                    v = 0;
                    for (int j = 0; j < 3; ++j) v += 0.08 * std::sin(2 * M_PI * f[j] * t + j);
                }
                writeElem(buf, pd.type.elem, i, v);
            }
            in.inConst[k][c] = (notes || pd.hasSpec) ? 1 : 0;
        }
    }
}

// Runs `in` for kBlocks blocks in chunks `chunks` (cycled within each block),
// returning every output channel's samples as doubles.
std::vector<std::vector<double>> run(Instance& in, std::vector<int> const& chunks) {
    size_t nout = 0;
    for (auto& port : in.outBuf) nout += port.size();
    std::vector<std::vector<double>> y(nout);
    size_t ci = 0;
    for (int b = 0; b < kBlocks; ++b) {
        fillInputs(in, b);
        if (b == 0 && in.def.voiced && in.def.funs.resetVoice) in.def.funs.resetVoice(in.p, 0, 0);
        for (int off = 0; off < kBlock; ) {
            int n = std::min(chunks[ci++ % chunks.size()], kBlock - off);
            in.def.funs.process(in.p, off, n);
            off += n;
        }
        size_t o = 0;
        for (int k = 0; k < in.def.num_outs; ++k) {
            for (auto& ch : in.outBuf[k]) {
                for (int i = 0; i < kBlock; ++i) y[o].push_back(readElem(ch.data(), in.def.outs[k].type.elem, i));
                ++o;
            }
        }
    }
    return y;
}

}  // namespace

// --time: ns per sample of each synth's process, whole blocks, best of 5.
int timeAll(char const* dir, char const* filter) {
    for (auto& [name, path] : plugins(dir)) {
        if (filter && name.find(filter) == std::string::npos) continue;
        Instance in;
        std::string err;
        if (!in.open(path, err)) { std::println("TIME {} error {}", name, err); continue; }
        if (in.def.voiced && in.def.funs.resetVoice) { fillInputs(in, 0); in.def.funs.resetVoice(in.p, 0, 0); }
        fillInputs(in, 0);
        for (int b = 0; b < 200; ++b) in.def.funs.process(in.p, 0, kBlock);
        double best = 1e30;
        for (int r = 0; r < 5; ++r) {
            auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < 2000; ++b) in.def.funs.process(in.p, 0, kBlock);
            auto t1 = std::chrono::steady_clock::now();
            best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / (2000.0 * kBlock));
        }
        std::println("TIME {} {:.3f}", name, best);
    }
    return 0;
}

// --zero: every output of every synth stays within 1e-4 of 0.
int zeroAll(char const* dir, char const* filter) {
    int failures = 0, checked = 0;
    for (auto& [name, path] : plugins(dir)) {
        if (filter && name.find(filter) == std::string::npos) continue;
        for (auto const& chunks : {std::vector<int>{64}, std::vector<int>{7, 13, 1, 30, 5, 3, 64}}) {
            Instance in;
            std::string err;
            if (!in.open(path, err)) { std::println("FAIL {}: {}", name, err); ++failures; break; }
            auto y = run(in, chunks);
            double worst = 0;
            int at = -1;
            for (size_t o = 0; o < y.size(); ++o)
                for (double v : y[o])
                    if (!(std::fabs(v) <= worst)) { worst = std::isfinite(v) ? std::fabs(v) : INFINITY; at = int(o); }
            if (worst > 1e-4) {
                std::println("FAIL {}: output channel {} off by {} (chunks {})", name, at, worst, chunks.size());
                ++failures;
                break;
            }
        }
        ++checked;
    }
    if (checked == 0) { std::println("FAIL: no plugins in {}", dir); return 1; }
    std::println("{} {} synths", failures ? "ZERO FAIL" : "ZERO PASS", checked);
    return failures ? 1 : 0;
}

// --idle: see the header.
namespace {
std::vector<double> runIdle(Instance& in) {
    constexpr int kOn = 0, kOff = 375, kAgain = 1125, kEnd = 1500;   // blocks of 64 at 48 kHz
    std::vector<double> y;
    int notes = -1;
    for (int k = 0; k < in.def.num_ins; ++k)
        if (!std::strcmp(in.def.ins[k].name, "notes")) notes = k;
    for (int b = 0; b < kEnd; ++b) {
        fillInputs(in, 0);
        if (notes >= 0) {
            tzpl_PortDef const& pd = in.def.ins[notes];
            int cols = int(pd.type.shape.dims[3]);
            int n = int(in.inBuf[notes].size());
            bool on = b < kOff || b >= kAgain;
            for (int c = 0; c < n; ++c) {
                if (c % cols != 0) continue;
                float g = (c / cols == 1 && on) ? 1.0f : 0.0f;
                for (int i = 0; i < kBlock; ++i) ((float*)in.inBuf[notes][c].data())[i] = g;
                in.inVer[notes][c] = (b < kOff ? 1 : b < kAgain ? 2 : 3);
            }
        }
        if ((b == kOn || b == kAgain) && in.def.funs.resetVoice) in.def.funs.resetVoice(in.p, 1, 0);
        in.def.funs.process(in.p, 0, kBlock);
        auto& ch0 = in.outBuf[0][0];
        for (int i = 0; i < kBlock; ++i) y.push_back(readElem(ch0.data(), in.def.outs[0].type.elem, i));
    }
    return y;
}
}  // namespace

int idleAll(char const* dir, char const* noidleDir) {
    auto noidle = plugins(noidleDir);
    int checked = 0;
    for (auto& [name, path] : plugins(dir)) {
        if (!noidle.count(name)) continue;
        Instance a, b;
        std::string err;
        if (!a.open(path, err) || !b.open(noidle[name], err)) { std::println("IDLE {} error {}", name, err); continue; }
        auto ya = runIdle(a), yb = runIdle(b);
        constexpr size_t kAgainAt = 1125 * kBlock;
        size_t last = 0;
        bool any = false;
        for (size_t i = 0; i < kAgainAt; ++i) if (ya[i] != 0.0) { last = i; any = true; }
        double zeroFrom = any ? (last + 1 == kAgainAt ? -1.0 : double(last + 1) / 48000.0) : 0.0;
        double diff = 0, after = 0;
        for (size_t i = 0; i < ya.size(); ++i) {
            double d = std::fabs(ya[i] - yb[i]);
            diff = std::isfinite(d) ? std::max(diff, d) : INFINITY;
            if (i >= kAgainAt) after = std::max(after, std::fabs(ya[i]));
        }
        std::println("IDLE {} zero_from {:.4f} max_diff {:.3g} after {:.3g}", name, zeroFrom, diff, after);
        ++checked;
    }
    return checked ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string_view(argv[1]) == "--idle") return idleAll(argv[2], argv[3]);
    if (argc >= 3 && std::string_view(argv[1]) == "--time") return timeAll(argv[2], argc > 3 ? argv[3] : nullptr);
    if (argc >= 3 && std::string_view(argv[1]) == "--zero") return zeroAll(argv[2], argc > 3 ? argv[3] : nullptr);
    if (argc < 3) {
        std::println(stderr, "usage: forms_equiv AUTO_BUILD_DIR SERIAL_BUILD_DIR [name-substring]");
        return 2;
    }
    auto autoP = plugins(argv[1]), serialP = plugins(argv[2]);
    char const* filter = argc > 3 ? argv[3] : nullptr;
    std::vector<std::pair<char const*, std::vector<int>>> schedules = {
        {"64", {64}}, {"1", {1}}, {"odd", {7, 13, 1, 30, 5, 3, 64}}};
    int failures = 0, checked = 0;
    for (auto& [name, apath] : autoP) {
        if (filter && (filter[0] == '=' ? name != filter + 1 : name.find(filter) == std::string::npos)) continue;
        auto sit = serialP.find(name);
        if (sit == serialP.end()) {
            std::println("FAIL {}: no serial build", name);
            ++failures;
            continue;
        }
        std::string err;
        std::vector<std::vector<double>> ref;
        {
            Instance r;
            if (!r.open(sit->second, err)) { std::println("FAIL {}: serial: {}", name, err); ++failures; continue; }
            ref = run(r, {64});
        }
        bool refFinite = true;
        for (auto& ch : ref) for (double v : ch) if (!std::isfinite(v)) refFinite = false;
        bool ok = true;
        double maxDb = -INFINITY;
        for (int which = 0; which < 2 && ok; ++which) {
            for (auto& [sname, chunks] : schedules) {
                if (which == 1 && sname == std::string("64")) continue;  // the reference itself
                Instance in;
                if (!in.open(which ? sit->second : apath, err)) {
                    std::println("FAIL {}: {}", name, err);
                    ok = false;
                    break;
                }
                auto y = run(in, chunks);
                for (size_t o = 0; o < y.size(); ++o) {
                    double e = 0, r = 0;
                    bool finite = true;
                    for (size_t i = 0; i < y[o].size(); ++i) {
                        if (!std::isfinite(y[o][i])) finite = false;
                        if (std::isfinite(ref[o][i]) && std::isfinite(y[o][i])) {
                            double d = y[o][i] - ref[o][i];
                            e += d * d;
                            r += ref[o][i] * ref[o][i];
                        }
                    }
                    double db = e == 0 ? -INFINITY : 10 * std::log10(e / std::max(r, 1e-20));
                    maxDb = std::max(maxDb, db);
                    if (db > kTolDb || (!finite && refFinite)) {
                        std::println("FAIL {}: {} build, chunks {}, output {}: {:.1f} dB{}", name,
                                     which ? "serial" : "auto", sname, o, db, finite ? "" : " (non-finite)");
                        ok = false;
                    }
                }
            }
        }
        ++checked;
        if (ok) std::println("PASS {} (worst {:.1f} dB)", name, maxDb);
        else ++failures;
    }
    if (failures) std::println("FORMS EQUIVALENCE FAIL ({} of {})", failures, checked);
    else std::println("FORMS EQUIVALENCE PASS ({} synths)", checked);
    return failures ? 1 : 0;
}
