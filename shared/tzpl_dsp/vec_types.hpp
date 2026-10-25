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
//  tzpl_dsp/vec_types.hpp
//  4-lane vector types for the four element types, and the portable
//  primitives the block-engine runtime builds on (block engine plan §5;
//  tensor-ugens ARCHITECTURE.md §9 and §11).
//
//  Every vector has 4 lanes, whatever its element type, so a loop can mix
//  types lane for lane. The types are clang ext_vector_types, which are the
//  same types as Apple's simd:: vectors (simd::double4 is f64x4), so code that
//  includes both mixes them freely.
//
//  Everything here is plain clang vector extensions: no <simd/simd.h>, no
//  intrinsics, the same on arm64 and x86-64.
//

#ifndef tzpl_dsp_vec_types_hpp
#define tzpl_dsp_vec_types_hpp

#include <bit>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace tzpl::dsp {

// --- Scalars ----------------------------------------------------------------

// long, not int64_t, for the 64-bit integers: Apple's simd::long4 and
// tzpl_simd.hpp's i64x4 are vectors of long, and on macOS int64_t is long long,
// a distinct type whose vectors wouldn't convert.
static_assert(sizeof(long) == 8, "tzpl_dsp assumes an LP64 platform");

using i32 = int;
using u32 = unsigned int;
using i64 = long;
using u64 = unsigned long;
using f32 = float;
using f64 = double;

// --- Vectors ----------------------------------------------------------------

inline constexpr int kVecLen = 4;

using i32x4 = i32 __attribute__((ext_vector_type(4)));
using u32x4 = u32 __attribute__((ext_vector_type(4)));
using i64x4 = i64 __attribute__((ext_vector_type(4)));
using u64x4 = u64 __attribute__((ext_vector_type(4)));
using f32x4 = f32 __attribute__((ext_vector_type(4)));
using f64x4 = f64 __attribute__((ext_vector_type(4)));

// Halves, for backends whose kernels are 2 lanes wide (Sleef's d2 on NEON).
using f64x2 = f64 __attribute__((ext_vector_type(2)));
using f32x2 = f32 __attribute__((ext_vector_type(2)));

// --- Traits -----------------------------------------------------------------
//
// For T a scalar or a 4-lane vector of one of the element types:
//  elem_t<T>   the element type (T itself for a scalar);
//  vec_t<E>    the 4-lane vector of element E;
//  bits_t<T>   the unsigned integer type of the same shape and width
//              (bits_t<f64x4> is u64x4, bits_t<f32> is u32);
//  int_t<T>    the signed integer type of the same shape and width;
//  mask_t<T>   the type of a comparison of Ts: an int_t vector of -1 / 0
//              lanes for a vector, bool for a scalar.

template <class T> struct vec_traits {
    using elem = T;
    static constexpr int lanes = 1;
};
template <class E> struct vec_traits<E __attribute__((ext_vector_type(4)))> {
    using elem = E;
    static constexpr int lanes = 4;
};

template <class T> using elem_t = typename vec_traits<T>::elem;
template <class T> inline constexpr int lanes_v = vec_traits<T>::lanes;
template <class T> inline constexpr bool is_vec_v = lanes_v<T> == 4;

template <class E> struct vec_of;
template <> struct vec_of<i32> { using type = i32x4; };
template <> struct vec_of<u32> { using type = u32x4; };
template <> struct vec_of<i64> { using type = i64x4; };
template <> struct vec_of<u64> { using type = u64x4; };
template <> struct vec_of<f32> { using type = f32x4; };
template <> struct vec_of<f64> { using type = f64x4; };
template <class E> using vec_t = typename vec_of<E>::type;

template <int Bytes> struct int_of_size;
template <> struct int_of_size<4> { using s = i32; using u = u32; };
template <> struct int_of_size<8> { using s = i64; using u = u64; };

template <class T> struct shape_like {
    template <class E> using with = E;
};
template <class E> struct shape_like<E __attribute__((ext_vector_type(4)))> {
    template <class F> using with = vec_t<F>;
};

template <class T>
using bits_t = typename shape_like<T>::template with<typename int_of_size<sizeof(elem_t<T>)>::u>;
template <class T>
using int_t = typename shape_like<T>::template with<typename int_of_size<sizeof(elem_t<T>)>::s>;
template <class T>
using mask_t = std::conditional_t<is_vec_v<T>, int_t<T>, bool>;

template <class T> inline constexpr bool is_float_v = std::is_floating_point_v<elem_t<T>>;
template <class T> inline constexpr bool is_int_v = std::is_integral_v<elem_t<T>>;

// The four element types the engine supports, and their vectors.
template <class E>
inline constexpr bool is_elem_v = std::is_same_v<E, i32> || std::is_same_v<E, i64> ||
                                  std::is_same_v<E, f32> || std::is_same_v<E, f64>;

// --- Construction, conversion, loads and stores ------------------------------

// v with every lane x.
template <class V> inline V splat(elem_t<V> x) { return V(x); }

// Lane-for-lane value conversion (float <-> int truncates toward zero; out of
// range and NaN are unspecified, and differ between NEON and x86).
template <class To, class From> inline To convert(From v) {
    if constexpr (is_vec_v<From>) return __builtin_convertvector(v, To);
    else return static_cast<To>(v);
}

// Reinterpretation of the bits (same size).
template <class To, class From> inline To bitcast(From v) {
    static_assert(sizeof(To) == sizeof(From));
    return __builtin_bit_cast(To, v);
}

// Unaligned 4-lane load and store.
template <class V> inline V load(elem_t<V> const* p) {
    V v;
    std::memcpy(&v, p, sizeof(V));
    return v;
}
template <class V> inline void store(elem_t<V>* p, V v) { std::memcpy(p, &v, sizeof(V)); }

// --- Lane shuffles -----------------------------------------------------------

// Transposes the 4 x 4 matrix whose rows are r0..r3: converts 4 vectors of 4
// samples of one channel each into 4 vectors of one sample of 4 channels, and
// back. 8 shuffles, for any element type.
template <class V> inline void transpose4(V& r0, V& r1, V& r2, V& r3) {
    static_assert(is_vec_v<V>);
    V t0 = __builtin_shufflevector(r0, r1, 0, 4, 2, 6);
    V t1 = __builtin_shufflevector(r0, r1, 1, 5, 3, 7);
    V t2 = __builtin_shufflevector(r2, r3, 0, 4, 2, 6);
    V t3 = __builtin_shufflevector(r2, r3, 1, 5, 3, 7);
    r0 = __builtin_shufflevector(t0, t2, 0, 1, 4, 5);
    r1 = __builtin_shufflevector(t1, t3, 0, 1, 4, 5);
    r2 = __builtin_shufflevector(t0, t2, 2, 3, 6, 7);
    r3 = __builtin_shufflevector(t1, t3, 2, 3, 6, 7);
}

template <class V> inline void transpose4(V (&r)[4]) { transpose4(r[0], r[1], r[2], r[3]); }

// x's lanes shifted up by one, lane 0 taking `prev`'s lane 3:
// (prev3, x0, x1, x2). Used for previous-sample values along samples.
template <class V> inline V shiftInLast(V prev, V x) {
    return __builtin_shufflevector(prev, x, 3, 4, 5, 6);
}

// --- Masks -------------------------------------------------------------------

// A mask of one width as a mask of another (-1 stays -1, 0 stays 0). Needed
// to select f64 values on an f32 comparison and the like.
template <class ToMask, class FromMask> inline ToMask convertMask(FromMask m) {
    if constexpr (std::is_same_v<ToMask, FromMask>) return m;
    else return __builtin_convertvector(m, ToMask);
}

// True if any / all lanes of mask m are set.
template <class M> inline bool any(M m) {
    if constexpr (is_vec_v<M>) return __builtin_reduce_or(m) != 0;
    else return bool(m);
}
template <class M> inline bool all(M m) {
    if constexpr (is_vec_v<M>) return __builtin_reduce_and(m) != 0;
    else return bool(m);
}

// c ? a : b, lane by lane. Both a and b are evaluated. c is a comparison's
// result, of any lane width (it is widened or narrowed to a's). Bitwise, so it
// is the same on every ISA and for every element type.
template <class M, class V>
    requires is_vec_v<V>
inline V select(M c, V a, V b) {
    using B = bits_t<V>;
    B m = bitcast<B>(convertMask<int_t<V>>(c));
    return bitcast<V>((m & bitcast<B>(a)) | (~m & bitcast<B>(b)));
}
template <class T>
    requires(!is_vec_v<T>)
inline T select(bool c, T a, T b) {
    return c ? a : b;
}

// A comparison's result as a value: 1 for true, 0 for false (+0, not -0).
template <class V, class M>
    requires is_vec_v<V>
inline V truth(M c) {
    // An integer comparison is -1 / 0; negating gives 1 / 0 before converting.
    return convert<V>(-convertMask<int_t<V>>(c));
}
template <class T>
    requires(!is_vec_v<T>)
inline T truth(bool c) {
    return c ? T(1) : T(0);
}

// --- min, max, clamp -----------------------------------------------------------
//
// Vectors use __builtin_elementwise_min / max, single instructions on NEON and
// x86 for every element type (f64 and i64 on AVX2 need compare and select,
// which the compiler emits). For floats these are IEEE minNum / maxNum: if one
// operand is NaN the other is returned. Scalars use the same rule (std::fmin),
// so scalar tails and vector bodies agree.

template <class T> inline T min(T a, T b) {
    if constexpr (is_vec_v<T>) return __builtin_elementwise_min(a, b);
    else if constexpr (std::is_floating_point_v<T>) return (b < a || a != a) ? b : a;
    else return b < a ? b : a;
}
template <class T> inline T max(T a, T b) {
    if constexpr (is_vec_v<T>) return __builtin_elementwise_max(a, b);
    else if constexpr (std::is_floating_point_v<T>) return (a < b || a != a) ? b : a;
    else return a < b ? b : a;
}
template <class T> inline T clamp(T x, T lo, T hi) { return min(max(x, lo), hi); }

// --- Reductions ----------------------------------------------------------------
//
// Float sums are added in a fixed order, (x0 + x1) + (x2 + x3), so a result
// doesn't depend on the platform or on how the compiler would reassociate.
// Integer sums wrap.

template <class V> inline elem_t<V> reduceAdd(V x) {
    static_assert(is_vec_v<V>);
    if constexpr (is_float_v<V>) return (x[0] + x[1]) + (x[2] + x[3]);
    else return elem_t<V>(__builtin_reduce_add(bitcast<bits_t<V>>(x)));
}
template <class V> inline elem_t<V> reduceMin(V x) {
    static_assert(is_vec_v<V>);
    using E = elem_t<V>;
    return min<E>(min<E>(x[0], x[1]), min<E>(x[2], x[3]));
}
template <class V> inline elem_t<V> reduceMax(V x) {
    static_assert(is_vec_v<V>);
    using E = elem_t<V>;
    return max<E>(max<E>(x[0], x[1]), max<E>(x[2], x[3]));
}

}  // namespace tzpl::dsp

#endif  // tzpl_dsp_vec_types_hpp
