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
//  tzpl_dsp/state_space.hpp
//  Block state space: the sample-vector form of an affine recurrence whose
//  coefficients are constant for the block (block engine plan §4.7, "Affine,
//  block-constant coefficients"; tensor-ugens ARCHITECTURE.md §6.2,
//  BlockFilter in filter.hpp).
//
//  An N-state cycle  y = step(s, x)  that is linear in s and x (s updated in
//  place) is, over a vector of 4 inputs x and the state s before it:
//      y  = Ys s + Yx x        (4 outputs: lane k is sample k)
//      s' = Ss s + Sx x
//  The matrices are found by running step from each unit state with zero
//  input, and from zero state with each unit input. A vector's 4 outputs then
//  don't depend on each other, and the state steps two vectors at a time
//  through Ss^2 and Sx2, so its dependence chain per step is about as long as
//  one sample's recurrence.
//
//  Matrix layout, for Z the element type and V = vec_t<Z>:
//      ys[q]       V: lane k = output k from unit state q
//      yx[j]       V: lane k = output k from unit input at sample j
//      ss[m][q]    Z: state m after 4 samples from unit state q
//      sx[m]       V: lane j = state m after 4 samples from unit input j
//      ss2[m][q]   Z: Ss^2
//      sx2[m][0|1] V: state m after 8 samples from the first / second vector
//
//  Accuracy contract (TU §7): about 1e-14 relative to the output for f64 and
//  1e-5 for f32 against the recurrence, up to 1e-12 and 2e-4 for very low,
//  sharp resonances.
//
//  The state layout is the recurrence's own (an array of N Z), so a block can
//  end with the scalar step for a tail, or switch to the recurrence for a
//  block whose coefficients vary, and continue exactly (block engine plan §4.7,
//  "Shared state layout").
//

#ifndef tzpl_dsp_state_space_hpp
#define tzpl_dsp_state_space_hpp

#include <concepts>

#include "vec_types.hpp"

namespace tzpl::dsp {

// A step function for an N-state cycle over element type Z: called as
// step(Z* s, Z x), it updates s[0..N) in place and returns the output.
template <class F, class Z>
concept StateStep = requires(F f, Z* s, Z x) {
    { f(s, x) } -> std::convertible_to<Z>;
};

template <class Z, int N>
struct BlockStateSpace {
    static_assert(std::is_floating_point_v<Z>, "block state space is for f32 and f64");
    static_assert(N >= 1);
    using V = vec_t<Z>;

    V ys[N], yx[kVecLen], sx[N], sx2[N][2];
    Z ss[N][N], ss2[N][N];

    // Builds the matrices from step, which must be affine (linear here: any
    // constant term belongs in the input) in the state and the input.
    //
    // For an f32 cycle whose step also takes f64 (a generic step, as synthc
    // can emit), the matrices are found in f64 and rounded once, which keeps
    // the 4- and 8-sample products from compounding f32 rounding. A step that
    // takes only Z is run in Z, as tensor-ugens does.
    template <class Step>
        requires StateStep<Step, Z>
    static BlockStateSpace build(Step&& step) {
        if constexpr (std::is_same_v<Z, f32> && StateStep<Step, f64>) return buildIn<f64>(step);
        else return buildIn<Z>(step);
    }

    // The same, computing in W (Z or a wider type) and rounding to Z.
    template <class W, class Step>
        requires StateStep<Step, W>
    static BlockStateSpace buildIn(Step&& step) {
        W ys[N][kVecLen], yx[kVecLen][kVecLen], ss[N][N], sx[N][kVecLen];
        for (int q = 0; q < N; ++q) {
            W s[N] = {};
            s[q] = 1;
            for (int k = 0; k < kVecLen; ++k) ys[q][k] = step(s, W(0));
            for (int m = 0; m < N; ++m) ss[m][q] = s[m];
        }
        for (int j = 0; j < kVecLen; ++j) {
            W s[N] = {};
            for (int k = 0; k < kVecLen; ++k) yx[j][k] = step(s, W(k == j ? 1 : 0));
            for (int m = 0; m < N; ++m) sx[m][j] = s[m];
        }
        BlockStateSpace b;
        for (int q = 0; q < N; ++q)
            for (int k = 0; k < kVecLen; ++k) b.ys[q][k] = Z(ys[q][k]);
        for (int j = 0; j < kVecLen; ++j)
            for (int k = 0; k < kVecLen; ++k) b.yx[j][k] = Z(yx[j][k]);
        // s'' = Ss (Ss s + Sx x0) + Sx x1
        for (int m = 0; m < N; ++m) {
            for (int q = 0; q < N; ++q) {
                W a = 0;
                for (int r = 0; r < N; ++r) a += ss[m][r] * ss[r][q];
                b.ss2[m][q] = Z(a);
                b.ss[m][q] = Z(ss[m][q]);
            }
            for (int j = 0; j < kVecLen; ++j) {
                W a = 0;
                for (int r = 0; r < N; ++r) a += ss[m][r] * sx[r][j];
                b.sx2[m][0][j] = Z(a);
                b.sx[m][j] = b.sx2[m][1][j] = Z(sx[m][j]);
            }
        }
        return b;
    }

    // The 4 outputs of input vector x from state s.
    V out(Z const* s, V x) const {
        V y = yx[0] * x[0];
        for (int j = 1; j < kVecLen; ++j) y += yx[j] * x[j];
        for (int q = 0; q < N; ++q) y += ys[q] * s[q];
        return y;
    }

    // s after input vector x. The input terms are summed first, since they
    // don't depend on the state.
    void next(Z* s, V x) const {
        Z n[N];
        for (int m = 0; m < N; ++m) {
            Z a = reduceAdd(sx[m] * x);
            for (int q = 0; q < N; ++q) a += ss[m][q] * s[q];
            n[m] = a;
        }
        for (int m = 0; m < N; ++m) s[m] = n[m];
    }

    // s after input vectors x0 then x1.
    void next2(Z* s, V x0, V x1) const {
        Z n[N];
        for (int m = 0; m < N; ++m) {
            Z a = reduceAdd(sx2[m][0] * x0 + sx2[m][1] * x1);
            for (int q = 0; q < N; ++q) a += ss2[m][q] * s[q];
            n[m] = a;
        }
        for (int m = 0; m < N; ++m) s[m] = n[m];
    }

    // Runs nv vectors of in into y. inc is 1 to step through a buffer, 0 for an
    // input held for the block (a splat; block engine plan D4). Both inputs of
    // a pair are read before either output is written, so y may alias in.
    void run(Z* s, V const* in, int inc, V* y, int nv) const {
        int v = 0;
        for (; v + 2 <= nv; v += 2) {
            V x0 = in[0], x1 = in[inc];
            in += 2 * inc;
            Z mid[N];
            for (int m = 0; m < N; ++m) mid[m] = s[m];
            next(mid, x0);
            y[v] = out(s, x0);
            y[v + 1] = out(mid, x1);
            next2(s, x0, x1);
        }
        if (v < nv) {
            V x = *in;
            y[v] = out(s, x);
            next(s, x);
        }
    }
};

// n samples (any n) of in into y, with state s: whole vectors through bss,
// the tail through step, which is the recurrence bss was built from. inc is 1
// or 0 as for BlockStateSpace::run. in and y are Z arrays; they need no
// alignment, and y may equal in.
template <class Z, int N, class Step>
    requires StateStep<Step, Z>
inline void runStateSpace(BlockStateSpace<Z, N> const& bss, Step&& step, Z* s,
                          Z const* in, int inc, Z* y, int n) {
    using V = vec_t<Z>;
    int nv = n / kVecLen;
    // Through local vectors a pair at a time, so the arrays needn't be aligned.
    int v = 0;
    for (; v + 2 <= nv; v += 2) {
        V x[2] = {inc ? load<V>(in + v * kVecLen) : V(in[0]),
                  inc ? load<V>(in + (v + 1) * kVecLen) : V(in[0])};
        V o[2];
        bss.run(s, x, 1, o, 2);
        store(y + v * kVecLen, o[0]);
        store(y + (v + 1) * kVecLen, o[1]);
    }
    if (v < nv) {
        V x = inc ? load<V>(in + v * kVecLen) : V(in[0]);
        V o;
        bss.run(s, &x, 1, &o, 1);
        store(y + v * kVecLen, o);
    }
    for (int i = nv * kVecLen; i < n; ++i) y[i] = step(s, in[i * inc]);
}

// The plain recurrence over n samples: the reference form, the scalar tail,
// and the form for blocks whose coefficients vary.
template <class Z, class Step>
inline void runRecurrence(Step&& step, Z* s, Z const* in, int inc, Z* y, int n) {
    for (int i = 0; i < n; ++i) y[i] = step(s, in[i * inc]);
}

// --- Several inputs and outputs ---------------------------------------------------------
//
// The same form for a cycle with I inputs and O outputs: step(W* s, W const* x,
// W* y) updates s[0..N) from s and x[0..I) and writes y[0..O). Over a vector of
// 4 samples of every input,
//      y[o] = Ys[o] s + sum_i Yx[o][i] x[i]
//      s'   = Ss s    + sum_i Sx[i] x[i]
// with the matrices found as for BlockStateSpace: from each unit state with
// zero input, and from zero state with a unit sample of each input.
//
// Layout (V = vec_t<Z>):
//      ys[o][q]          V: lane k = output o at sample k from unit state q
//      yx[o][i][j]       V: lane k = output o at sample k from unit input i at sample j
//      ss[m][q]          Z: as BlockStateSpace
//      sx[m][i]          V: lane j = state m after 4 samples from unit input i at sample j
//      ss2[m][q]         Z: Ss^2
//      sx2[m][i][0|1]    V: state m after 8 samples from input i's first / second vector

template <class F, class Z>
concept StateStepIO = requires(F f, Z* s, Z const* x, Z* y) { f(s, x, y); };

template <class Z, int N, int I, int O>
struct BlockStateSpaceIO {
    static_assert(std::is_floating_point_v<Z>, "block state space is for f32 and f64");
    static_assert(N >= 1 && I >= 1 && O >= 1);
    using V = vec_t<Z>;

    V ys[O][N], yx[O][I][kVecLen], sx[N][I], sx2[N][I][2];
    Z ss[N][N], ss2[N][N];

    // As BlockStateSpace::build: in f64 for an f32 cycle whose step takes f64.
    template <class Step>
        requires StateStepIO<Step, Z>
    static BlockStateSpaceIO build(Step&& step) {
        if constexpr (std::is_same_v<Z, f32> && StateStepIO<Step, f64>) return buildIn<f64>(step);
        else return buildIn<Z>(step);
    }

    template <class W, class Step>
        requires StateStepIO<Step, W>
    static BlockStateSpaceIO buildIn(Step&& step) {
        W ys[O][N][kVecLen], yx[O][I][kVecLen][kVecLen], ss[N][N], sx[N][I][kVecLen];
        W x[I], y[O];
        for (int q = 0; q < N; ++q) {
            W s[N] = {};
            s[q] = 1;
            for (int i = 0; i < I; ++i) x[i] = 0;
            for (int k = 0; k < kVecLen; ++k) {
                step(s, x, y);
                for (int o = 0; o < O; ++o) ys[o][q][k] = y[o];
            }
            for (int m = 0; m < N; ++m) ss[m][q] = s[m];
        }
        for (int i = 0; i < I; ++i) {
            for (int j = 0; j < kVecLen; ++j) {
                W s[N] = {};
                for (int k = 0; k < kVecLen; ++k) {
                    for (int ii = 0; ii < I; ++ii) x[ii] = ii == i && k == j ? 1 : 0;
                    step(s, x, y);
                    for (int o = 0; o < O; ++o) yx[o][i][j][k] = y[o];
                }
                for (int m = 0; m < N; ++m) sx[m][i][j] = s[m];
            }
        }
        BlockStateSpaceIO b;
        for (int o = 0; o < O; ++o) {
            for (int q = 0; q < N; ++q)
                for (int k = 0; k < kVecLen; ++k) b.ys[o][q][k] = Z(ys[o][q][k]);
            for (int i = 0; i < I; ++i)
                for (int j = 0; j < kVecLen; ++j)
                    for (int k = 0; k < kVecLen; ++k) b.yx[o][i][j][k] = Z(yx[o][i][j][k]);
        }
        for (int m = 0; m < N; ++m) {
            for (int q = 0; q < N; ++q) {
                W a = 0;
                for (int r = 0; r < N; ++r) a += ss[m][r] * ss[r][q];
                b.ss2[m][q] = Z(a);
                b.ss[m][q] = Z(ss[m][q]);
            }
            for (int i = 0; i < I; ++i) {
                for (int j = 0; j < kVecLen; ++j) {
                    W a = 0;
                    for (int r = 0; r < N; ++r) a += ss[m][r] * sx[r][i][j];
                    b.sx2[m][i][0][j] = Z(a);
                    b.sx[m][i][j] = b.sx2[m][i][1][j] = Z(sx[m][i][j]);
                }
            }
        }
        return b;
    }

    // Output o's 4 samples from state s and input vectors x[0..I).
    V out(int o, Z const* s, V const* x) const {
        V y = ys[o][0] * s[0];
        for (int q = 1; q < N; ++q) y += ys[o][q] * s[q];
        for (int i = 0; i < I; ++i)
            for (int j = 0; j < kVecLen; ++j) y += yx[o][i][j] * x[i][j];
        return y;
    }

    // s after input vectors x[0..I).
    void next(Z* s, V const* x) const {
        Z n[N];
        for (int m = 0; m < N; ++m) {
            V acc = sx[m][0] * x[0];
            for (int i = 1; i < I; ++i) acc += sx[m][i] * x[i];
            Z a = reduceAdd(acc);
            for (int q = 0; q < N; ++q) a += ss[m][q] * s[q];
            n[m] = a;
        }
        for (int m = 0; m < N; ++m) s[m] = n[m];
    }

    // s after input vectors x0[0..I) then x1[0..I).
    void next2(Z* s, V const* x0, V const* x1) const {
        Z n[N];
        for (int m = 0; m < N; ++m) {
            V acc = sx2[m][0][0] * x0[0] + sx2[m][0][1] * x1[0];
            for (int i = 1; i < I; ++i) acc += sx2[m][i][0] * x0[i] + sx2[m][i][1] * x1[i];
            Z a = reduceAdd(acc);
            for (int q = 0; q < N; ++q) a += ss2[m][q] * s[q];
            n[m] = a;
        }
        for (int m = 0; m < N; ++m) s[m] = n[m];
    }
};

// A vector of 4 samples k apart from p: held (k = 0), contiguous (k = 1) or
// gathered from interleaved rows (k > 1).
template <class V> inline V loadStrided(elem_t<V> const* p, int k) {
    if (k == 1) return load<V>(p);
    if (k == 0) return V(p[0]);
    V v;
    for (int j = 0; j < kVecLen; ++j) v[j] = p[j * k];
    return v;
}
template <class V> inline void storeStrided(elem_t<V>* p, int k, V v) {
    if (k == 1) return store(p, v);
    for (int j = 0; j < kVecLen; ++j) p[j * k] = v[j];
}

// n samples (any n): input i from in[i] at element stride inc[i] (0 for an
// input held for the block, 1 for a buffer, the row length for one channel of
// interleaved rows), output o into y[o] at stride yinc[o] (1 or a row
// length), with state s. Whole vectors through bss, two at a time; the tail
// through step. Every input of a pair is read before any output is written,
// so an output may alias an input.
template <class Z, int N, int I, int O, class Step>
    requires StateStepIO<Step, Z>
inline void runStateSpaceIO(BlockStateSpaceIO<Z, N, I, O> const& bss, Step&& step, Z* s,
                            Z const* const* in, int const* inc, Z* const* y, int const* yinc, int n) {
    using V = vec_t<Z>;
    int nv = n / kVecLen;
    auto ld = [&](int i, int v) { return loadStrided<V>(in[i] + v * kVecLen * inc[i], inc[i]); };
    auto st = [&](int o, int v, V x) { storeStrided(y[o] + v * kVecLen * yinc[o], yinc[o], x); };
    int v = 0;
    for (; v + 2 <= nv; v += 2) {
        V x0[I], x1[I];
        for (int i = 0; i < I; ++i) { x0[i] = ld(i, v); x1[i] = ld(i, v + 1); }
        Z mid[N];
        for (int m = 0; m < N; ++m) mid[m] = s[m];
        bss.next(mid, x0);
        for (int o = 0; o < O; ++o) {
            V y0 = bss.out(o, s, x0), y1 = bss.out(o, mid, x1);
            st(o, v, y0);
            st(o, v + 1, y1);
        }
        bss.next2(s, x0, x1);
    }
    if (v < nv) {
        V x[I];
        for (int i = 0; i < I; ++i) x[i] = ld(i, v);
        for (int o = 0; o < O; ++o) st(o, v, bss.out(o, s, x));
        bss.next(s, x);
    }
    for (int t = nv * kVecLen; t < n; ++t) {
        Z x[I], yo[O];
        for (int i = 0; i < I; ++i) x[i] = in[i][t * inc[i]];
        step(s, x, yo);
        for (int o = 0; o < O; ++o) y[o][t * yinc[o]] = yo[o];
    }
}

// The plain recurrence over n samples, for blocks whose coefficients vary.
template <class Z, int I, int O, class Step>
inline void runRecurrenceIO(Step&& step, Z* s, Z const* const* in, int const* inc, Z* const* y,
                            int const* yinc, int n) {
    for (int t = 0; t < n; ++t) {
        Z x[I], yo[O];
        for (int i = 0; i < I; ++i) x[i] = in[i][t * inc[i]];
        step(s, x, yo);
        for (int o = 0; o < O; ++o) y[o][t * yinc[o]] = yo[o];
    }
}

}  // namespace tzpl::dsp

#endif  // tzpl_dsp_state_space_hpp
