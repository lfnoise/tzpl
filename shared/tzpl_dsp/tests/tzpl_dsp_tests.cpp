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
//  tzpl_dsp_tests.cpp -- tests of the block-engine runtime support library
//  (shared/tzpl_dsp; block engine plan §5).
//
//  - vec_types: traits, transpose4 round trips, select / truth across mask
//    widths, min / max, reductions, for every element type.
//  - scans against sequential loops, for every element type, with tails and
//    held (stride 0) inputs.
//  - block state space against the step function, N = 1, 2, 3 and 4, f32 and
//    f64, including continuation across segments and switching forms.
//  - vmath against <cmath> over sweeps, vector lanes against the scalar forms.
//  - random: ranges, determinism, lane and stream independence.
//
//  Tolerance contracts (block engine plan §4.7, TU §7): 1e-12 for f64 and 1e-5
//  for f32, relative to the size of the signal.
//
//  Exits non-zero if any check fails; prints a summary either way.
//  Usage: tzpl_dsp_tests [-v]
//

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "tzpl_dsp/tzpl_dsp.hpp"

using namespace tzpl::dsp;

namespace {

// --- Check bookkeeping -------------------------------------------------------

int gChecks = 0;
int gFailures = 0;
bool gVerbose = false;
std::string gSection;

void section(std::string_view name) {
    gSection = name;
    if (gVerbose) std::println("== {}", name);
}

template <class... A>
bool check(bool ok, std::format_string<A...> fmt, A&&... args) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        if (gFailures <= 60)
            std::println("FAIL [{}] {}", gSection, std::format(fmt, std::forward<A>(args)...));
    }
    return ok;
}

// The largest error seen in a group of checks, reported once.
struct ErrStat {
    std::string what;
    double tol;
    double worst = 0;
    double at = 0;
    void add(double err, double x) {
        if (!(err <= worst)) {  // also catches NaN
            worst = err;
            at = x;
        }
    }
    void report() {
        check(worst <= tol, "{}: error {:.3g} > {:.3g} (at {})", what, worst, tol, at);
        if (gVerbose) std::println("   {:<34} max err {:.3g}  (tol {:.0e})", what, worst, tol);
    }
};

template <class E> constexpr char const* typeName() {
    if constexpr (std::is_same_v<E, i32>) return "i32";
    else if constexpr (std::is_same_v<E, i64>) return "i64";
    else if constexpr (std::is_same_v<E, f32>) return "f32";
    else return "f64";
}

template <class E> constexpr double tolFor() { return sizeof(E) == 8 ? 1e-12 : 1e-5; }

// A deterministic test signal source, independent of the code under test.
struct TestRng {
    u64 s = 0x1234567;
    u64 next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    double uni() { return double(next() >> 11) * 0x1.0p-53; }  // [0, 1)
    double bi() { return 2 * uni() - 1; }
};

// --- vec_types ---------------------------------------------------------------------

static_assert(std::is_same_v<elem_t<f64x4>, f64> && std::is_same_v<elem_t<f32>, f32>);
static_assert(std::is_same_v<bits_t<f64x4>, u64x4> && std::is_same_v<bits_t<f32x4>, u32x4>);
static_assert(std::is_same_v<bits_t<f64>, u64> && std::is_same_v<bits_t<i32>, u32>);
static_assert(std::is_same_v<mask_t<f64x4>, i64x4> && std::is_same_v<mask_t<f32x4>, i32x4>);
static_assert(std::is_same_v<mask_t<i64x4>, i64x4> && std::is_same_v<mask_t<f32>, bool>);
static_assert(std::is_same_v<decltype(f64x4() < f64x4()), mask_t<f64x4>>);
static_assert(std::is_same_v<decltype(f32x4() < f32x4()), mask_t<f32x4>>);
static_assert(std::is_same_v<vec_t<i64>, i64x4> && lanes_v<i32x4> == 4 && lanes_v<i32> == 1);
static_assert(is_elem_v<i32> && is_elem_v<f64> && !is_elem_v<u64>);

template <class E> void testVecTypes() {
    using V = vec_t<E>;
    char const* tn = typeName<E>();

    // transpose4: transposes, and twice is the identity.
    V r[4], orig[4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) r[i][j] = E(10 * i + j + 1);
    for (int i = 0; i < 4; ++i) orig[i] = r[i];
    transpose4(r);
    bool ok = true;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) ok = ok && r[i][j] == orig[j][i];
    check(ok, "{} transpose4 transposes", tn);
    transpose4(r[0], r[1], r[2], r[3]);
    ok = true;
    for (int i = 0; i < 4; ++i) ok = ok && all(r[i] == orig[i]);
    check(ok, "{} transpose4 round trip", tn);

    // select and truth, with a mask of each width.
    V a = {E(1), E(2), E(3), E(4)}, b = {E(5), E(6), E(7), E(8)};
    V s = select(a > V(E(2)), a, b);
    check(s[0] == E(5) && s[1] == E(6) && s[2] == E(3) && s[3] == E(4), "{} select", tn);
    f32x4 fa = {1, 2, 3, 4};
    V s32 = select(fa > f32x4(2.f), a, b);  // a 32-bit mask on any width
    check(all(s32 == s), "{} select on f32 mask", tn);
    f64x4 da = {1, 2, 3, 4};
    V s64 = select(da > f64x4(2.0), a, b);  // a 64-bit mask on any width
    check(all(s64 == s), "{} select on f64 mask", tn);
    V t = truth<V>(a > V(E(2)));
    check(t[0] == E(0) && t[1] == E(0) && t[2] == E(1) && t[3] == E(1), "{} truth", tn);
    if constexpr (is_float_v<V>) check(!std::signbit(t[0]), "{} truth false is +0", tn);
    check(truth<E>(true) == E(1) && truth<E>(false) == E(0), "{} scalar truth", tn);
    check(select(true, E(1), E(2)) == E(1), "{} scalar select", tn);

    // min, max, clamp; reductions.
    V lo = min(a, V(E(3))), hi = max(a, V(E(3)));
    check(lo[0] == E(1) && lo[3] == E(3) && hi[0] == E(3) && hi[3] == E(4), "{} min/max", tn);
    check(all(clamp(a, V(E(2)), V(E(3))) == V{E(2), E(2), E(3), E(3)}), "{} clamp", tn);
    check(min(E(2), E(1)) == E(1) && max(E(2), E(1)) == E(2), "{} scalar min/max", tn);
    check(reduceAdd(a) == E(10) && reduceMin(b) == E(5) && reduceMax(b) == E(8), "{} reductions", tn);
    if constexpr (is_int_v<V>) {
        V big = V(std::numeric_limits<E>::max());
        check(reduceAdd(big) == E(-4), "{} reduceAdd wraps", tn);
    }
    check(any(a > V(E(3))) && !all(a > V(E(3))) && all(a > V(E(0))), "{} any/all", tn);

    // shiftInLast: previous vector's lane 3, then x0..x2.
    V sh = shiftInLast(b, a);
    check(sh[0] == E(8) && sh[1] == E(1) && sh[3] == E(3), "{} shiftInLast", tn);

    // loads and stores need no alignment.
    alignas(64) E buf[9] = {};
    store(buf + 1, a);
    check(all(load<V>(buf + 1) == a) && buf[0] == E(0) && buf[5] == E(0), "{} unaligned load/store", tn);

    // Lane-for-lane conversion between widths.
    if constexpr (is_float_v<V>) {
        i64x4 iv = convert<i64x4>(V{E(1.5), E(-2.5), E(3), E(4)});
        check(iv[0] == 1 && iv[1] == -2 && iv[3] == 4, "{} convert to i64 truncates", tn);
    }
}

// --- Scans ------------------------------------------------------------------------------

template <class E> void testScans() {
    using V = vec_t<E>;
    char const* tn = typeName<E>();
    TestRng rng;
    for (int n : {0, 3, 4, 64, 67, 1027}) {
        std::vector<E> x(n), y(n);
        for (int i = 0; i < n; ++i) {
            if constexpr (std::is_integral_v<E>) x[i] = E(rng.next());  // large: sums wrap
            else x[i] = E(rng.bi());
        }

        // Sum, against a sequential sum (long double for floats, wrapping for ints).
        E carry = E(0);
        scanSumBlock(x.data(), y.data(), n, carry);
        if constexpr (std::is_integral_v<E>) {
            using B = bits_t<E>;
            B acc = 0;
            bool ok = true;
            for (int i = 0; i < n; ++i) {
                acc += B(x[i]);
                ok = ok && y[i] == E(acc);
            }
            check(ok, "{} scanSum n={} wraps exactly", tn, n);
        } else {
            ErrStat es{std::format("{} scanSum n={}", tn, n), tolFor<E>()};
            long double acc = 0, mag = 0;
            for (int i = 0; i < n; ++i) {
                acc += (long double)x[i];
                mag += std::fabs(double(x[i]));
                es.add(double(std::fabs((long double)y[i] - acc) / std::max<long double>(mag, 1)), i);
            }
            es.report();
        }
        if (n) check(carry == y[n - 1], "{} scanSum carry n={}", tn, n);

        // Max, min, xor: exact.
        E cmax = std::numeric_limits<E>::lowest(), cmin = std::numeric_limits<E>::max();
        std::vector<E> ymax(n), ymin(n);
        scanMaxBlock(x.data(), ymax.data(), n, cmax);
        scanMinBlock(x.data(), ymin.data(), n, cmin);
        E rmax = std::numeric_limits<E>::lowest(), rmin = std::numeric_limits<E>::max();
        bool okMax = true, okMin = true;
        for (int i = 0; i < n; ++i) {
            rmax = std::max(rmax, x[i]);
            rmin = std::min(rmin, x[i]);
            okMax = okMax && ymax[i] == rmax;
            okMin = okMin && ymin[i] == rmin;
        }
        check(okMax && okMin, "{} scanMax/scanMin n={}", tn, n);
        if constexpr (std::is_integral_v<E>) {
            E cx = E(0x5a5a5a5a);
            std::vector<E> yx(n);
            scanXorBlock(x.data(), yx.data(), n, cx);
            E r = E(0x5a5a5a5a);
            bool ok = true;
            for (int i = 0; i < n; ++i) ok = ok && yx[i] == (r ^= x[i]);
            check(ok, "{} scanXor n={}", tn, n);
        }
    }

    // In place, and the vector forms directly.
    {
        V x = {E(1), E(2), E(3), E(4)};
        check(all(prefixSum(x) == V{E(1), E(3), E(6), E(10)}), "{} prefixSum", tn);
        V m = {E(3), E(1), E(4), E(2)};
        check(all(prefixMax(m) == V{E(3), E(3), E(4), E(4)}), "{} prefixMax", tn);
        check(all(prefixMin(m) == V{E(3), E(1), E(1), E(1)}), "{} prefixMin", tn);
        E c = E(10);
        V y = scanSum(x, c);
        check(all(y == V{E(11), E(13), E(16), E(20)}) && c == E(20), "{} scanSum carry", tn);
        E buf[7] = {E(1), E(1), E(1), E(1), E(1), E(1), E(1)};
        E cc = E(0);
        scanSumBlock(buf, buf, 7, cc);
        check(buf[6] == E(7) && buf[3] == E(4), "{} scanSumBlock in place", tn);
    }

    if constexpr (std::is_floating_point_v<E>) {
        // Wrapped sum (the phasor). With increments that are multiples of 2^-10
        // every partial sum is exact in any order, so it must match a scalar
        // phasor exactly (TU §15).
        {
            int n = 1027;
            std::vector<E> fm(n), y(n);
            for (int i = 0; i < n; ++i) fm[i] = E(int(rng.next() % 200) - 50) * E(0x1.0p-10);
            E phase = E(0.25), ref = E(0.25);
            scanSumWrapBlock(fm.data(), y.data(), n, phase);
            bool ok = true;
            for (int i = 0; i < n; ++i) {
                ref = vmath::frac(ref + fm[i]);
                ok = ok && y[i] == ref;
            }
            check(ok, "{} scanSumWrap exact for dyadic increments", tn);
            check(phase == ref, "{} scanSumWrap carry", tn);
        }
        // Random increments: compare as phases (circular distance), against a
        // long double accumulation, over 256 samples.
        {
            int n = 256;
            std::vector<E> fm(n), y(n);
            for (int i = 0; i < n; ++i) fm[i] = E(rng.uni() * 0.1);
            E phase = 0;
            scanSumWrapBlock(fm.data(), y.data(), n, phase);
            ErrStat es{std::format("{} scanSumWrap random fm", tn), tolFor<E>()};
            long double ref = 0;
            for (int i = 0; i < n; ++i) {
                ref += (long double)fm[i];
                long double r = ref - std::floor(ref);
                double d = std::fabs(double((long double)y[i] - r));
                es.add(std::min(d, 1 - d), i);
            }
            es.report();
        }

        // Affine maps: s = a s + b with per-sample a and b, and with each held.
        for (int held = 0; held < 4; ++held) {
            int n = 203;
            std::vector<E> a(n), b(n), y(n);
            for (int i = 0; i < n; ++i) {
                a[i] = E(0.5 + 0.499 * rng.uni());
                b[i] = E(rng.bi());
            }
            int aInc = (held & 1) ? 0 : 1, bInc = (held & 2) ? 0 : 1;
            E s = E(0.3);
            scanAffineBlock(a.data(), aInc, b.data(), bInc, y.data(), n, s);
            ErrStat es{std::format("{} scanAffine aInc={} bInc={}", tn, aInc, bInc), tolFor<E>()};
            long double r = 0.3L;
            for (int i = 0; i < n; ++i) {
                r = (long double)a[i * aInc] * r + (long double)b[i * bInc];
                es.add(double(std::fabs((long double)y[i] - r) / std::max<long double>(std::fabs(r), 1)), i);
            }
            es.report();
            check(s == y[n - 1], "{} scanAffine carry", tn);
        }
        {
            V a = {E(2), E(3), E(0.5), E(1)}, b = {E(1), E(0), E(1), E(-1)};
            E s = E(1);
            V y = scanAffine(a, b, s);  // 3, 9, 5.5, 4.5
            check(all(y == V{E(3), E(9), E(5.5), E(4.5)}) && s == E(4.5), "{} scanAffine exact", tn);
        }
    }
}

// --- State space ---------------------------------------------------------------------------

// Step functions of the cycles synthc will meet (tensor-ugens filter.hpp).
// Each is generic over the arithmetic type T, with its coefficients stored as
// Z, so that the same cycle can run in f64 as a reference for an f32 one.
template <class Z> struct OnePole {  // N = 1: lpf1, transposed direct form II
    Z b0, b1, a1;
    template <class T> T operator()(T* s, T x) const {
        T y = T(b0) * x + s[0];
        s[0] = T(b1) * x - T(a1) * y;
        return y;
    }
};
template <class Z> struct Decay {  // N = 1: a leaky integrator
    Z b1;
    template <class T> T operator()(T* s, T x) const { return s[0] = x + T(b1) * s[0]; }
};
template <class Z> struct Biquad {  // N = 2: RBJ, transposed direct form II
    Z b0, b1, b2, a1, a2;
    template <class T> T operator()(T* s, T x) const {
        T y = T(b0) * x + s[0];
        s[0] = T(b1) * x - T(a1) * y + s[1];
        s[1] = T(b2) * x - T(a2) * y;
        return y;
    }
};
template <class Z> struct Lag3 {  // N = 3: three one-pole lags in series
    Z b1;
    template <class T> T operator()(T* s, T x) const {
        for (int m = 0; m < 3; ++m) x = s[m] = x + T(b1) * (s[m] - x);
        return x;
    }
};
template <class Z> struct Formlet {  // N = 4: the difference of two resonators
    Z a1a, a2a, a1b, a2b;
    template <class T> static T section(T a1, T a2, T* s, T x) {
        T y = T(0.5) * x + s[0];
        s[0] = s[1] - a1 * y;
        s[1] = T(-0.5) * x - a2 * y;
        return y;
    }
    template <class T> T operator()(T* s, T x) const {
        return section(T(a1b), T(a2b), s + 2, x) - section(T(a1a), T(a2a), s, x);
    }
};

template <class Z> Biquad<Z> rbjLowpass(double freq, double q) {
    double w = 2 * std::numbers::pi * freq, cs = std::cos(w), alpha = std::sin(w) / (2 * q);
    double a0 = 1 + alpha, b1 = 1 - cs;
    return {Z(0.5 * b1 / a0), Z(b1 / a0), Z(0.5 * b1 / a0), Z(-2 * cs / a0), Z((1 - alpha) / a0)};
}
template <class Z> OnePole<Z> lpf1(double freq) {
    double K = std::tan(std::numbers::pi * freq), inv = 1 / (1 + K);
    return {Z(K * inv), Z(K * inv), Z((K - 1) * inv)};
}
template <class Z> Formlet<Z> formlet(double freq, double atk, double dcy) {
    double cs = -2 * std::cos(2 * std::numbers::pi * freq);
    double ra = std::exp(std::log(0.001) / atk), rb = std::exp(std::log(0.001) / dcy);
    return {Z(cs * ra), Z(ra * ra), Z(cs * rb), Z(rb * rb)};
}

double peakOf(std::vector<double> const& ref) {
    double peak = 1e-30;
    for (double v : ref) peak = std::max(peak, std::fabs(v));
    return peak;
}

// The largest error of y against ref, relative to ref's peak.
template <class Z> double peakRelErr(std::vector<Z> const& y, std::vector<double> const& ref) {
    double err = 0;
    for (size_t j = 0; j < y.size(); ++j) {
        double e = std::fabs(double(y[j]) - ref[j]);
        err = e <= err ? err : e;  // NaN wins
    }
    return err / peakOf(ref);
}

// Runs bss (or the recurrence, every third segment) over x in segments of
// varying length, each a continuation of the last.
template <class Z, int N, class Step>
std::vector<Z> runSegments(BlockStateSpace<Z, N> const& bss, Step const& step, std::vector<Z> const& x,
                           Z (&s)[N]) {
    int const segs[] = {64, 1, 7, 4, 128, 3, 2, 64, 5, 8};
    int n = int(x.size()), i = 0, k = 0;
    std::vector<Z> y(n);
    while (i < n) {
        int len = std::min(segs[k++ % 10], n - i);
        // The forms share the state layout, so they must continue each other.
        if (k % 3 == 0) runRecurrence(step, s, x.data() + i, 1, y.data() + i, len);
        else runStateSpace(bss, step, s, x.data() + i, 1, y.data() + i, len);
        i += len;
    }
    return y;
}

// Block state space of step against the recurrence. The reference is the
// recurrence in f64 (the same coefficients, evaluated more precisely). The
// contract: within tol64 (1e-12) of it for f64; for f32, within tol32, or
// within 3 times the f32 recurrence's own error where rounding f32 states
// alone costs more than that. tol32 is 2e-5, not the plan's 1e-5: an RBJ
// lowpass at f = .01, q = 2 measures 1.4e-5 to 1.7e-5 (with and without
// -ffast-math), against 3e-6 to 8e-6 for the f32 recurrence. A block state
// space built in f32 itself (a step that only takes f32, "built in Z") rounds
// its matrices in f32 as well and gets tol32BuiltInZ, 1e-4: that same lowpass
// measured 4.2e-5 to 5.3e-5 on the CI runners (Apple clang -ffast-math, Linux
// Sleef). Very low, sharp
// resonances are ill-conditioned in either form (TU §7 quotes up to 1e-12
// and 2e-4); those cases pass looser tolerances.
template <class Z, int N, class Step>
void testStateSpaceCase(char const* name, Step step, double tol64 = 1e-12, double tol32 = 2e-5,
                        double tol32BuiltInZ = 1e-4) {
    char const* tn = typeName<Z>();
    TestRng rng;
    int const n = 1001;
    std::vector<Z> x(n);
    std::vector<double> xd(n), ref(n);
    for (int i = 0; i < n; ++i) xd[i] = double(x[i] = i == 0 ? Z(1) : Z(rng.bi()));
    double sd[N] = {};
    runRecurrence(step, sd, xd.data(), 1, ref.data(), n);

    std::vector<Z> yRec(n);
    Z sRec[N] = {};
    runRecurrence(step, sRec, x.data(), 1, yRec.data(), n);
    double recErr = peakRelErr(yRec, ref);
    double tol = sizeof(Z) == 8 ? tol64 : std::max(tol32, 3 * recErr);

    // Built from the generic step (in f64 for f32, see build), and from a step
    // that only takes Z (built in Z).
    auto zStep = [&](Z* s, Z v) -> Z { return step(s, v); };
    auto bssGeneric = BlockStateSpace<Z, N>::build(step);
    auto bssZ = BlockStateSpace<Z, N>::build(zStep);
    for (int which = 0; which < 2; ++which) {
        auto const& bss = which ? bssZ : bssGeneric;
        char const* how = which ? "built in Z" : "built generic";
        double caseTol = (sizeof(Z) == 4 && which) ? std::max(tol, tol32BuiltInZ) : tol;
        Z s[N] = {};
        std::vector<Z> y = runSegments(bss, step, x, s);
        double err = peakRelErr(y, ref);
        check(err <= caseTol, "{} N={} {} {}: error {:.3g} > {:.3g} (recurrence's own {:.3g})", tn, N, name,
              how, err, caseTol, recErr);
        if (gVerbose)
            std::println("   {} N={} {:<22} {:<14} err {:.3g}  recurrence err {:.3g}", tn, N, name, how, err,
                         recErr);
        double stateErr = 0;
        for (int m = 0; m < N; ++m) stateErr = std::max(stateErr, std::fabs(double(s[m]) - sd[m]) / peakOf(ref));
        check(stateErr <= caseTol, "{} N={} {} {}: final state error {:.3g}", tn, N, name, how, stateErr);
    }

    // A held input (stride 0): the step response.
    {
        Z c = Z(0.75);
        double cd = double(c);
        Z s0[N] = {}, s2[N] = {};
        double s1[N] = {};
        std::vector<Z> h(130), hRec(130);
        std::vector<double> hRef(130);
        runStateSpace(bssGeneric, step, s0, &c, 0, h.data(), 130);
        runRecurrence(step, s1, &cd, 0, hRef.data(), 130);
        runRecurrence(step, s2, &c, 0, hRec.data(), 130);
        double hTol = sizeof(Z) == 8 ? tol64 : std::max(tol32, 3 * peakRelErr(hRec, hRef));
        double err = peakRelErr(h, hRef);
        check(err <= hTol, "{} N={} {} held input: error {:.3g} > {:.3g}", tn, N, name, err, hTol);
    }

    // The layout: Ys column 0 is the zero-input response to unit state 0.
    Z u[N] = {};
    u[0] = 1;
    double layoutErr = 0;
    for (int j = 0; j < kVecLen; ++j)
        layoutErr = std::max(layoutErr, std::fabs(double(zStep(u, Z(0)) - bssZ.ys[0][j])));
    check(layoutErr <= tolFor<Z>(), "{} N={} {} ys from unit state", tn, N, name);
}

template <class Z> void testStateSpace() {
    testStateSpaceCase<Z, 1>("lpf1 f=.02", lpf1<Z>(0.02));
    testStateSpaceCase<Z, 1>("decay", Decay<Z>{Z(0.999)});
    testStateSpaceCase<Z, 2>("rbj lpf f=.01 q=2", rbjLowpass<Z>(0.01, 2));
    testStateSpaceCase<Z, 2>("rbj lpf f=.2 q=.7", rbjLowpass<Z>(0.2, 0.7));
    // Poles at radius 0.9997: the ill-conditioned case (measured: 1.3e-11
    // for f64, 2.5e-4 to 7.6e-4 for f32).
    testStateSpaceCase<Z, 2>("rbj lpf f=.001 q=10", rbjLowpass<Z>(0.001, 10), 1e-10, 1e-3);
    testStateSpaceCase<Z, 3>("lag3", Lag3<Z>{Z(0.99)});
    testStateSpaceCase<Z, 4>("formlet", formlet<Z>(0.05, 50, 500));
}

// --- State space, several inputs and outputs -----------------------------------------------

// Step functions in BlockStateSpaceIO's form, step(s, x, y), generic over T.
template <class Z> struct RingIO {  // N = 4, 1 in, 1 out: ring's direct form I, states
                                     // y[-1], y[-2], x[-1], x[-2] as shift registers
    Z a1, a2;
    template <class T> void operator()(T* s, T const* x, T* y) const {
        T v = T(0.5) * (x[0] - s[3]) + T(a1) * s[0] + T(a2) * s[1];
        s[1] = s[0];
        s[0] = v;
        s[3] = s[2];
        s[2] = x[0];
        y[0] = v;
    }
};
template <class Z> struct CoupledIO {  // N = 2, 2 in, 2 out
    Z a, b, c;
    template <class T> void operator()(T* s, T const* x, T* y) const {
        T n0 = T(a) * s[0] + x[0] + T(0.3) * x[1];
        T n1 = T(b) * s[1] + T(c) * s[0] + x[1];
        y[0] = s[0] + x[1];
        y[1] = n1 - T(0.5) * x[0];
        s[0] = n0;
        s[1] = n1;
    }
};
template <class Z> struct BiquadIO {  // N = 2, 1 in, 1 out: Biquad in the IO form
    Biquad<Z> bq;
    template <class T> void operator()(T* s, T const* x, T* y) const { y[0] = bq(s, x[0]); }
};

// BlockStateSpaceIO of step against the f64 recurrence, in segments that
// alternate with the recurrence (they share the state layout), every input
// at stride 1 except input 1 held for the block (stride 0) when I > 1.
template <class Z, int N, int I, int O, class Step>
void testStateSpaceIOCase(char const* name, Step step, double tol64 = 1e-12, double tol32 = 2e-5) {
    char const* tn = typeName<Z>();
    TestRng rng;
    int const n = 1001;
    std::vector<Z> x[I];
    std::vector<double> xd[I];
    for (int i = 0; i < I; ++i) {
        x[i].resize(n);
        xd[i].resize(n);
        for (int t = 0; t < n; ++t) xd[i][t] = double(x[i][t] = t == 0 ? Z(1) : Z(rng.bi()));
    }
    int inc[I];
    for (int i = 0; i < I; ++i) inc[i] = i == 1 ? 0 : 1;
    auto bss = BlockStateSpaceIO<Z, N, I, O>::build(step);
    int const segs[] = {64, 1, 7, 4, 128, 3, 2, 64, 5, 8};
    double sd[N] = {};
    Z sz[N] = {}, sr[N] = {};
    std::vector<double> ref[O];
    std::vector<Z> y[O], yr[O];
    for (int o = 0; o < O; ++o) { ref[o].resize(n); y[o].resize(n); yr[o].resize(n); }
    for (int i0 = 0, k = 0; i0 < n; ) {
        int len = std::min(segs[k++ % 10], n - i0);
        double const* ind[I];
        Z const* inz[I];
        double* yd[O];
        Z* yz[O];
        Z* yzr[O];
        for (int i = 0; i < I; ++i) { ind[i] = xd[i].data() + i0 * inc[i]; inz[i] = x[i].data() + i0 * inc[i]; }
        for (int o = 0; o < O; ++o) { yd[o] = ref[o].data() + i0; yz[o] = y[o].data() + i0; yzr[o] = yr[o].data() + i0; }
        int yinc[O];
        for (int o = 0; o < O; ++o) yinc[o] = 1;
        runRecurrenceIO<double, I, O>(step, sd, ind, inc, yd, yinc, len);
        runRecurrenceIO<Z, I, O>(step, sr, inz, inc, yzr, yinc, len);
        if (k % 3 == 0) runRecurrenceIO<Z, I, O>(step, sz, inz, inc, yz, yinc, len);
        else runStateSpaceIO(bss, step, sz, inz, inc, yz, yinc, len);
        i0 += len;
    }
    for (int o = 0; o < O; ++o) {
        double recErr = peakRelErr(yr[o], ref[o]);
        double tol = sizeof(Z) == 8 ? tol64 : std::max(tol32, 3 * recErr);
        double err = peakRelErr(y[o], ref[o]);
        check(err <= tol, "{} N={} I={} O={} {} output {}: error {:.3g} > {:.3g}", tn, N, I, O, name, o, err, tol);
        if (gVerbose)
            std::println("   {} N={} I={} O={} {:<16} out {} err {:.3g}  recurrence err {:.3g}", tn, N, I, O,
                         name, o, err, recErr);
    }
}

template <class Z> void testStateSpaceIO() {
    double cs = std::cos(2 * std::numbers::pi * 0.02), R = std::exp(std::log(0.001) / 480);
    testStateSpaceIOCase<Z, 4, 1, 1>("ring f=.02", RingIO<Z>{Z(2 * R * cs), Z(-R * R)}, 1e-11, 1e-4);
    testStateSpaceIOCase<Z, 2, 2, 2>("coupled", CoupledIO<Z>{Z(0.9), Z(0.7), Z(0.2)});
    testStateSpaceIOCase<Z, 2, 1, 1>("rbj lpf f=.01 q=2", BiquadIO<Z>{rbjLowpass<Z>(0.01, 2)});
    // Interleaved rows (one channel of a multichannel buffer): inputs at
    // element stride 3 and 5, outputs at 2 and 4, give exactly the contiguous
    // results, over a length with a tail.
    {
        auto step = CoupledIO<Z>{Z(0.9), Z(0.7), Z(0.2)};
        auto bss = BlockStateSpaceIO<Z, 2, 2, 2>::build(step);
        int const n = 70;
        TestRng rng;
        std::vector<Z> a(n), b(n), ia(3 * n), ib(5 * n), y0(n), y1(n), iy0(2 * n), iy1(4 * n);
        for (int t = 0; t < n; ++t) { ia[3 * t] = a[t] = Z(rng.bi()); ib[5 * t] = b[t] = Z(rng.bi()); }
        Z s1[2] = {}, s2[2] = {};
        Z const* in1[2] = {a.data(), b.data()};
        int inc1[2] = {1, 1}, yinc1[2] = {1, 1};
        Z* out1[2] = {y0.data(), y1.data()};
        runStateSpaceIO(bss, step, s1, in1, inc1, out1, yinc1, n);
        Z const* in2[2] = {ia.data(), ib.data()};
        int inc2[2] = {3, 5}, yinc2[2] = {2, 4};
        Z* out2[2] = {iy0.data(), iy1.data()};
        runStateSpaceIO(bss, step, s2, in2, inc2, out2, yinc2, n);
        double d = 0;
        for (int t = 0; t < n; ++t)
            d = std::max({d, std::fabs(double(y0[t] - iy0[2 * t])), std::fabs(double(y1[t] - iy1[4 * t]))});
        check(d == 0 && s1[0] == s2[0] && s1[1] == s2[1], "{} BlockStateSpaceIO strided rows differ by {:.3g}",
              typeName<Z>(), d);
    }
    // The 1-in 1-out form matches BlockStateSpace's outputs exactly in
    // structure: compare the two directly.
    {
        auto bq = rbjLowpass<Z>(0.05, 1);
        auto a = BlockStateSpace<Z, 2>::build(bq);
        auto b = BlockStateSpaceIO<Z, 2, 1, 1>::build(BiquadIO<Z>{bq});
        double d = 0;
        for (int q = 0; q < 2; ++q)
            for (int k = 0; k < kVecLen; ++k) d = std::max(d, std::fabs(double(a.ys[q][k] - b.ys[0][q][k])));
        for (int j = 0; j < kVecLen; ++j)
            for (int k = 0; k < kVecLen; ++k) d = std::max(d, std::fabs(double(a.yx[j][k] - b.yx[0][0][j][k])));
        check(d == 0, "{} BlockStateSpaceIO<2,1,1> matrices differ from BlockStateSpace by {:.3g}", typeName<Z>(), d);
    }
}

// --- vmath ---------------------------------------------------------------------------------

// Vector and scalar vmath::F against std::F over [lo, hi], for element type E.
// The error is relative where |ref| > 1 and absolute below.
template <class E, class VF, class SF, class RF>
void sweep1(char const* name, double lo, double hi, VF vf, SF sf, RF ref, double tol) {
    using V = vec_t<E>;
    ErrStat es{std::format("{} {}", typeName<E>(), name), tol};
    bool laneOk = true;
    int const n = 4000;
    for (int i = 0; i < n; i += 4) {
        V x;
        for (int k = 0; k < 4; ++k) x[k] = E(lo + (hi - lo) * double(i + k) / (n - 1));
        V y = vf(x);
        for (int k = 0; k < 4; ++k) {
            double r = ref(double(x[k]));
            if constexpr (sizeof(E) == 4) r = double(float(r));  // the f32 result to aim for
            es.add(std::fabs(double(y[k]) - r) / std::max(std::fabs(r), 1.0), double(x[k]));
            E sc = sf(x[k]);
            es.add(std::fabs(double(sc) - r) / std::max(std::fabs(r), 1.0), double(x[k]));
            laneOk = laneOk && std::isfinite(double(y[k]));
        }
    }
    check(laneOk, "{} {}: non-finite lane", typeName<E>(), name);
    es.report();
}

template <class E, class VF, class SF, class RF>
void sweep2(char const* name, double lo, double hi, double lo2, double hi2, VF vf, SF sf, RF ref, double tol) {
    using V = vec_t<E>;
    ErrStat es{std::format("{} {}", typeName<E>(), name), tol};
    TestRng rng;
    for (int i = 0; i < 1000; ++i) {
        V x, y;
        for (int k = 0; k < 4; ++k) {
            x[k] = E(lo + (hi - lo) * rng.uni());
            y[k] = E(lo2 + (hi2 - lo2) * rng.uni());
        }
        V z = vf(x, y);
        for (int k = 0; k < 4; ++k) {
            double r = ref(double(x[k]), double(y[k]));
            if constexpr (sizeof(E) == 4) r = double(float(r));
            es.add(std::fabs(double(z[k]) - r) / std::max(std::fabs(r), 1.0), double(x[k]));
            es.add(std::fabs(double(sf(x[k], y[k])) - r) / std::max(std::fabs(r), 1.0), double(x[k]));
        }
    }
    es.report();
}

#define SWEEP1(NAME, LO, HI)                                                                      \
    sweep1<E>(#NAME, LO, HI, [](V x) { return vmath::NAME(x); }, [](E x) { return vmath::NAME(x); }, \
              [](double x) { return std::NAME(x); }, tol)
#define SWEEP2(NAME, LO, HI, LO2, HI2)                                                   \
    sweep2<E>(#NAME, LO, HI, LO2, HI2, [](V x, V y) { return vmath::NAME(x, y); },      \
              [](E x, E y) { return vmath::NAME(x, y); },                                \
              [](double x, double y) { return std::NAME(x, y); }, tol)

template <class E> void testVmath() {
    using V = vec_t<E>;
    // f32 vectors from a vector library may be a few ulps off; f64 within 1e-12.
    double const tol = sizeof(E) == 8 ? 1e-12 : 1e-5;
    double const pi = std::numbers::pi;

    SWEEP1(sin, -20, 20);
    SWEEP1(cos, -20, 20);
    SWEEP1(tan, -1.5, 1.5);
    SWEEP1(asin, -1, 1);
    SWEEP1(acos, -1, 1);
    SWEEP1(atan, -50, 50);
    SWEEP1(sinh, -10, 10);
    SWEEP1(cosh, -10, 10);
    SWEEP1(tanh, -10, 10);
    SWEEP1(asinh, -100, 100);
    SWEEP1(acosh, 1, 100);
    SWEEP1(atanh, -0.999, 0.999);
    SWEEP1(exp, -30, 30);
    SWEEP1(exp2, -30, 30);
    SWEEP1(expm1, -5, 5);
    SWEEP1(log, 1e-6, 1e6);
    SWEEP1(log10, 1e-6, 1e6);
    SWEEP1(log2, 1e-6, 1e6);
    SWEEP1(log1p, -0.999, 100);
    SWEEP1(logb, 1e-6, 1e6);
    SWEEP1(sqrt, 0, 1e6);
    SWEEP1(cbrt, -1e6, 1e6);
    SWEEP1(erf, -5, 5);
    SWEEP1(erfc, -5, 5);
    SWEEP1(tgamma, 0.1, 10);
    SWEEP1(lgamma, 0.1, 20);
    SWEEP1(floor, -10, 10);
    SWEEP1(ceil, -10, 10);
    SWEEP1(trunc, -10, 10);
    SWEEP1(round, -10, 10);
    SWEEP1(rint, -10, 10);
    SWEEP1(nearbyint, -10, 10);
    SWEEP1(fabs, -10, 10);
    SWEEP2(pow, 0.01, 10, -5, 5);
    SWEEP2(atan2, -10, 10, -10, 10);
    SWEEP2(hypot, -1e3, 1e3, -1e3, 1e3);
    SWEEP2(fmod, -100, 100, 0.5, 10);
    SWEEP2(remainder, -100, 100, 0.5, 10);
    SWEEP2(fdim, -10, 10, -10, 10);
    SWEEP2(copysign, -10, 10, -10, 10);
    SWEEP2(fmin, -10, 10, -10, 10);
    SWEEP2(fmax, -10, 10, -10, 10);

    // sinpi, cospi, tanpi against sin(pi x) etc. in double.
    sweep1<E>("sinpi", -8, 8, [](V x) { return vmath::sinpi(x); }, [](E x) { return vmath::sinpi(x); },
              [=](double x) { return std::sin(pi * x); }, tol);
    sweep1<E>("cospi", -8, 8, [](V x) { return vmath::cospi(x); }, [](E x) { return vmath::cospi(x); },
              [=](double x) { return std::cos(pi * x); }, tol);
    sweep1<E>("tanpi", -0.45, 0.45, [](V x) { return vmath::tanpi(x); }, [](E x) { return vmath::tanpi(x); },
              [=](double x) { return std::tan(pi * x); }, tol);

    // Exact values of the pi-scaled trig at integers and half integers.
    bool exact = true;
    for (int i = -8; i <= 8; ++i) {
        V xi = V(E(i)), xh = V(E(i + 0.5));
        exact = exact && all(vmath::sinpi(xi) == V(E(0))) && vmath::sinpi(E(i)) == E(0);
        exact = exact && all(vmath::cospi(xh) == V(E(0))) && vmath::cospi(E(i + 0.5)) == E(0);
        E one = (i & 1) ? E(-1) : E(1);
        exact = exact && all(vmath::cospi(xi) == V(one)) && vmath::cospi(E(i)) == one;
    }
    check(exact, "{} sinpi/cospi exact at integers and halves", typeName<E>());

    // Ties: round goes away from zero, rint and nearbyint to even.
    V ties = {E(0.5), E(1.5), E(2.5), E(-2.5)};
    check(all(vmath::round(ties) == V{E(1), E(2), E(3), E(-3)}), "{} round ties", typeName<E>());
    check(all(vmath::rint(ties) == V{E(0), E(2), E(2), E(-2)}), "{} rint ties", typeName<E>());
    check(all(vmath::nearbyint(ties) == V{E(0), E(2), E(2), E(-2)}), "{} nearbyint ties", typeName<E>());
    check(all(vmath::frac(V{E(1.25), E(-0.25), E(3), E(0)}) == V{E(0.25), E(0.75), E(0), E(0)}),
          "{} frac", typeName<E>());
    V fma = vmath::fma(V(E(2)), V(E(3)), V(E(1)));
    check(all(fma == V(E(7))), "{} fma", typeName<E>());
}

// --- Random -----------------------------------------------------------------------------------

template <class E> void testRandom() {
    using V = vec_t<E>;
    char const* tn = typeName<E>();
    constexpr int kDraws = 100000;

    RandState<4> r;
    randSeed(r, 42);
    // Lanes differ, and their streams are uncorrelated.
    double sum[4] = {}, sumsq[4] = {}, cross[4][4] = {};
    bool inRange = true, lanesDiffer = true;
    for (int i = 0; i < kDraws; ++i) {
        V u;
        if constexpr (std::is_floating_point_v<E>) {
            u = uniform<V>(r);
            inRange = inRange && all(u >= V(E(0))) && all(u < V(E(1)));
        } else {
            // Integers: the top bit of each lane, as 0 / 1.
            V raw = uniform<V>(r);
            u = convert<V>(raw < V(E(0)));
            u = -u;  // -1 / 0 -> 1 / 0
        }
        if constexpr (std::is_floating_point_v<E>)
            lanesDiffer = lanesDiffer && !(u[0] == u[1] && u[1] == u[2] && u[2] == u[3]);
        for (int a = 0; a < 4; ++a) {
            double ua = double(u[a]);
            sum[a] += ua;
            sumsq[a] += ua * ua;
            for (int b = 0; b < 4; ++b) cross[a][b] += ua * double(u[b]);
        }
    }
    check(inRange, "{} uniform in [0, 1)", tn);
    if constexpr (std::is_floating_point_v<E>) check(lanesDiffer, "{} lanes differ", tn);
    // Means: 1/2, sd of the mean 0.29/sqrt(N) (floats) or 0.5/sqrt(N) (bits);
    // 6 sd keeps the chance of a spurious failure below 1e-8 per check.
    double sdMean = 0.5 / std::sqrt(double(kDraws));
    double worstMean = 0, worstCorr = 0;
    for (int a = 0; a < 4; ++a) {
        double m = sum[a] / kDraws;
        worstMean = std::max(worstMean, std::fabs(m - 0.5));
        for (int b = a + 1; b < 4; ++b) {
            double mb = sum[b] / kDraws;
            double cov = cross[a][b] / kDraws - m * mb;
            double va = sumsq[a] / kDraws - m * m, vb = sumsq[b] / kDraws - mb * mb;
            worstCorr = std::max(worstCorr, std::fabs(cov / std::sqrt(va * vb)));
        }
    }
    check(worstMean < 6 * sdMean, "{} lane means near 1/2 (worst off {:.3g})", tn, worstMean);
    check(worstCorr < 6 / std::sqrt(double(kDraws)), "{} lanes uncorrelated (worst {:.3g})", tn, worstCorr);

    if constexpr (std::is_floating_point_v<E>) {
        // Bipolar range, and the mantissa width: every value is a multiple of
        // 2^-23 (f32) or 2^-52 (f64).
        RandState<4> rb;
        randSeed(rb, 7);
        bool biOk = true, gridOk = true;
        double bsum = 0;
        E grid = E(std::ldexp(1.0, -(std::numeric_limits<E>::digits - 1)));
        for (int i = 0; i < kDraws / 4; ++i) {
            V b = bipolar<V>(rb);
            biOk = biOk && all(b >= V(E(-1))) && all(b < V(E(1)));
            bsum += double(reduceAdd(b));
            V u = uniform<V>(rb);
            for (int k = 0; k < 4; ++k) gridOk = gridOk && std::fmod(u[k], grid) == E(0);
        }
        check(biOk, "{} bipolar in [-1, 1)", tn);
        check(std::fabs(bsum / kDraws) < 6 * 0.58 / std::sqrt(double(kDraws)), "{} bipolar mean near 0", tn);
        check(gridOk, "{} uniform on the mantissa grid", tn);

        // Scalar generator.
        RandState<1> r1;
        randSeed(r1, 42);
        bool ok1 = true;
        for (int i = 0; i < 1000; ++i) {
            E u = uniform<E>(r1), b = bipolar<E>(r1);
            ok1 = ok1 && u >= E(0) && u < E(1) && b >= E(-1) && b < E(1);
        }
        check(ok1, "{} scalar uniform / bipolar ranges", tn);
    } else {
        RandState<1> r1;
        randSeed(r1, 3);
        E a = uniform<E>(r1), b = uniform<E>(r1);
        check(a != b, "{} scalar draws differ", tn);
    }

    // Determinism, and streams: same seed and stream repeat, others differ.
    RandState<4> x1, x2, x3, x4;
    randSeed(x1, 99);
    randSeed(x2, 99);
    randSeed(x3, 99, 4);
    randSeed(x4, 100);
    bool same = true, differ = true;
    for (int i = 0; i < 100; ++i) {
        u64x4 a = randBits(x1), b = randBits(x2), c = randBits(x3), d = randBits(x4);
        same = same && all(a == b);
        differ = differ && !any(a == c) && !any(a == d);
    }
    check(same, "{} same seed repeats", tn);
    check(differ, "{} other stream / seed differs", tn);
}

void testRandomGroups() {
    // randGroupSeed: 16 streams per group, all distinct from each other and
    // from the next group's.
    RandState<4> g0[4], g1[4];
    randGroupSeed(g0, 5, 0);
    randGroupSeed(g1, 5, 1);
    std::vector<u64> firsts;
    for (auto* g : {&g0, &g1})
        for (auto& r : *g) {
            u64x4 b = randBits(r);
            for (int k = 0; k < 4; ++k) firsts.push_back(b[k]);
        }
    std::sort(firsts.begin(), firsts.end());
    check(std::adjacent_find(firsts.begin(), firsts.end()) == firsts.end(), "randGroupSeed: 32 distinct streams");

    // xoroshiro128++ (Blackman and Vigna): the first output is
    // rotl(s0 + s1, 17) + s0, so state (1, 2) gives 0x60001.
    RandState<1> r;
    r.s0 = 1;
    r.s1 = 2;
    u64 a = randBits(r);
    check(a == 0x60001, "xoroshiro128++ first output from (1, 2)");
}

}  // namespace

int main(int argc, char** argv) {
    gVerbose = argc > 1 && std::string_view(argv[1]) == "-v";
    std::println("tzpl_dsp tests (vmath backend: {})", vmath::kBackendName);

    section("vec_types");
    testVecTypes<i32>();
    testVecTypes<i64>();
    testVecTypes<f32>();
    testVecTypes<f64>();

    section("scan");
    testScans<i32>();
    testScans<i64>();
    testScans<f32>();
    testScans<f64>();

    section("state_space");
    testStateSpace<f32>();
    testStateSpace<f64>();
    testStateSpaceIO<f32>();
    testStateSpaceIO<f64>();

    section("vmath");
    testVmath<f32>();
    testVmath<f64>();

    section("random");
    testRandom<i32>();
    testRandom<i64>();
    testRandom<f32>();
    testRandom<f64>();
    testRandomGroups();

    if (gFailures) {
        std::println("tzpl_dsp tests: {} of {} checks FAILED", gFailures, gChecks);
        return 1;
    }
    std::println("tzpl_dsp tests: all {} checks passed", gChecks);
    return 0;
}
