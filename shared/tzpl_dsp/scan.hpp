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
//  tzpl_dsp/scan.hpp
//  Lane prefix scans: the sample-vector form of an associative recurrence
//  (block engine plan §4.7, "Scan" and "Affine, first order, varying
//  coefficients"; tensor-ugens ARCHITECTURE.md §6.3).
//
//  A vector holds 4 consecutive samples of one channel. For s[k] = s[k-1] (+) x[k]
//  with (+) associative, the 4 new states are the carried state combined with
//  the running combination of x's lanes, found in 2 shuffle-and-combine steps.
//  Only lane 3 carries over to the next vector.
//
//  Each scan has three forms:
//   - prefixOp(x): running combination of the lanes alone;
//   - scanOp(x, carry): carry (+) prefixOp(x), with carry updated to lane 3;
//   - scanOpBlock(in, out, n, carry): n samples (any n), a vector at a time
//     with a scalar tail, carry updated. in may equal out.
//
//  Integer sums wrap (computed on the unsigned bits). Float sums round
//  differently from a sequential loop: the running sums are added in a tree,
//  which keeps the error at about the sequential loop's (TU §7: ~1e-12 cycles
//  per second of phase drift for f64).
//

#ifndef tzpl_dsp_scan_hpp
#define tzpl_dsp_scan_hpp

#include "vec_types.hpp"
#include "vmath.hpp"

namespace tzpl::dsp {

// --- Shifts with a fill value -------------------------------------------------

// (f, x0, x1, x2) and (f, f, x0, x1).
template <class V> inline V shiftUp1(V x, V fill) { return __builtin_shufflevector(x, fill, 4, 0, 1, 2); }
template <class V> inline V shiftUp2(V x, V fill) { return __builtin_shufflevector(x, fill, 4, 5, 0, 1); }

// --- Sum --------------------------------------------------------------------------

// (x0, x0+x1, x0+x1+x2, x0+x1+x2+x3): 2 shuffles and 2 adds.
template <class V> inline V prefixSum(V x) {
    static_assert(is_vec_v<V>);
    if constexpr (is_int_v<V>) {
        using B = bits_t<V>;
        B b = bitcast<B>(x);
        B z = 0;
        b += shiftUp1(b, z);
        b += shiftUp2(b, z);
        return bitcast<V>(b);
    } else {
        V z = 0;
        x += shiftUp1(x, z);
        x += shiftUp2(x, z);
        return x;
    }
}

// --- Max, min: idempotent, so lane 0 fills the shifts ------------------------------

template <class V> inline V prefixMax(V x) {
    static_assert(is_vec_v<V>);
    x = max(x, __builtin_shufflevector(x, x, 0, 0, 1, 2));
    x = max(x, __builtin_shufflevector(x, x, 0, 1, 0, 1));
    return x;
}
template <class V> inline V prefixMin(V x) {
    static_assert(is_vec_v<V>);
    x = min(x, __builtin_shufflevector(x, x, 0, 0, 1, 2));
    x = min(x, __builtin_shufflevector(x, x, 0, 1, 0, 1));
    return x;
}

// --- Xor (integers) --------------------------------------------------------------------

template <class V> inline V prefixXor(V x) {
    static_assert(is_vec_v<V> && is_int_v<V>);
    V z = 0;
    x ^= shiftUp1(x, z);
    x ^= shiftUp2(x, z);
    return x;
}

// --- With a carry -------------------------------------------------------------------------

namespace detail {
template <class V> inline V wrapAdd(V a, V b) {
    if constexpr (is_int_v<V>) return bitcast<V>(bitcast<bits_t<V>>(a) + bitcast<bits_t<V>>(b));
    else return a + b;
}
template <class E> inline E wrapAdd1(E a, E b) {
    if constexpr (std::is_integral_v<E>) return E(bits_t<E>(a) + bits_t<E>(b));
    else return a + b;
}
}  // namespace detail

template <class V> inline V scanSum(V x, elem_t<V>& carry) {
    V y = detail::wrapAdd(V(carry), prefixSum(x));
    carry = y[3];
    return y;
}
template <class V> inline V scanMax(V x, elem_t<V>& carry) {
    V y = max(V(carry), prefixMax(x));
    carry = y[3];
    return y;
}
template <class V> inline V scanMin(V x, elem_t<V>& carry) {
    V y = min(V(carry), prefixMin(x));
    carry = y[3];
    return y;
}
template <class V> inline V scanXor(V x, elem_t<V>& carry) {
    V y = V(carry) ^ prefixXor(x);
    carry = y[3];
    return y;
}

// The phasor's accumulation: s[k] = frac(s[k-1] + x[k]). Since
// frac(frac(a) + b) == frac(a + b), this is frac(phase + prefixSum(x)), and
// only lane 3 carries over.
template <class V> inline V scanSumWrap(V x, elem_t<V>& phase) {
    static_assert(is_float_v<V>);
    V y = vmath::frac(V(phase) + prefixSum(x));
    phase = y[3];
    return y;
}

// --- Blocks of any length ---------------------------------------------------------------

namespace detail {
// Runs vector scan f over the whole vectors of in[0..n) and scalar step g over
// the tail.
template <class E, class F, class G>
inline void scanBlock(E const* in, E* out, int n, E& carry, F f, G g) {
    using V = vec_t<E>;
    int i = 0;
    for (; i + kVecLen <= n; i += kVecLen) store(out + i, f(load<V>(in + i), carry));
    for (; i < n; ++i) out[i] = carry = g(carry, in[i]);
}
}  // namespace detail

template <class E> inline void scanSumBlock(E const* in, E* out, int n, E& carry) {
    detail::scanBlock(in, out, n, carry, [](vec_t<E> x, E& c) { return scanSum(x, c); },
                      [](E c, E x) { return detail::wrapAdd1(c, x); });
}
template <class E> inline void scanMaxBlock(E const* in, E* out, int n, E& carry) {
    detail::scanBlock(in, out, n, carry, [](vec_t<E> x, E& c) { return scanMax(x, c); },
                      [](E c, E x) { return max(c, x); });
}
template <class E> inline void scanMinBlock(E const* in, E* out, int n, E& carry) {
    detail::scanBlock(in, out, n, carry, [](vec_t<E> x, E& c) { return scanMin(x, c); },
                      [](E c, E x) { return min(c, x); });
}
template <class E> inline void scanXorBlock(E const* in, E* out, int n, E& carry) {
    detail::scanBlock(in, out, n, carry, [](vec_t<E> x, E& c) { return scanXor(x, c); },
                      [](E c, E x) { return E(c ^ x); });
}
template <class E> inline void scanSumWrapBlock(E const* in, E* out, int n, E& phase) {
    detail::scanBlock(in, out, n, phase, [](vec_t<E> x, E& c) { return scanSumWrap(x, c); },
                      [](E c, E x) { return vmath::frac(c + x); });
}

// --- Affine maps ------------------------------------------------------------------------------
//
// s[k] = a[k] s[k-1] + b[k]: a first-order recurrence whose coefficients vary
// per sample. The map x -> a x + b composes associatively:
//   (a2, b2) after (a1, b1) = (a2 a1, a2 b1 + b2),
// so lane k of the prefix composition is (A[k], B[k]) with
// s[k] = A[k] s[-1] + B[k]. The shifts fill with the identity map (1, 0).
//
// This is a different rounding order from the recurrence: each B[k] is a sum
// of products of a's, so for |a| < 1 the relative error stays near the
// recurrence's (a few ulps per vector), but it isn't bit-exact.

template <class V> struct AffineMap {
    V a, b;
};

template <class V> inline AffineMap<V> prefixAffine(V a, V b) {
    static_assert(is_vec_v<V> && is_float_v<V>);
    V one = 1, zero = 0;
    // (a, b) after shifted (sa, sb): (a sa, a sb + b).
    V sa = shiftUp1(a, one), sb = shiftUp1(b, zero);
    b = a * sb + b;
    a = a * sa;
    sa = shiftUp2(a, one);
    sb = shiftUp2(b, zero);
    b = a * sb + b;
    a = a * sa;
    return {a, b};
}

// The 4 states s[0..3] after s, which becomes s[3].
template <class V> inline V scanAffine(V a, V b, elem_t<V>& s) {
    AffineMap<V> m = prefixAffine(a, b);
    V y = m.a * V(s) + m.b;
    s = y[3];
    return y;
}

// n states (any n) into out, from coefficient arrays a and b. aInc and bInc are
// each 1 to step through an array, or 0 for a value held for the block (a
// stride-0 input, block engine plan D4). out may alias a or b.
template <class E>
inline void scanAffineBlock(E const* a, int aInc, E const* b, int bInc, E* out, int n, E& s) {
    using V = vec_t<E>;
    int i = 0;
    for (; i + kVecLen <= n; i += kVecLen) {
        V va = aInc ? load<V>(a + i) : V(a[0]);
        V vb = bInc ? load<V>(b + i) : V(b[0]);
        store(out + i, scanAffine(va, vb, s));
    }
    for (; i < n; ++i) out[i] = s = a[i * aInc] * s + b[i * bInc];
}

}  // namespace tzpl::dsp

#endif  // tzpl_dsp_scan_hpp
