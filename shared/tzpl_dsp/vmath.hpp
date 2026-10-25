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
//  tzpl_dsp/vmath.hpp
//  One math interface for scalars and 4-lane vectors of f32 and f64
//  (tensor-ugens ARCHITECTURE.md §2 and §11).
//
//  tzpl::dsp::vmath gives every function an overload for f32, f64, f32x4 and
//  f64x4, so a kernel written as a template over T calls the same names for a
//  sample and for a vector of 4.
//
//  Backends, chosen at compile time (TZPL_DSP_VMATH_BACKEND says which):
//   - APPLE: <simd/simd.h> math on the vectors (simd::double4 is f64x4).
//   - SLEEF: Sleef's vector functions, where <sleef.h> is available (the
//     tree's Linux build fetches Sleef). f32x4 is one f4 call; f64x4 is two
//     d2 calls, one per half, since the d4 names exist only on AVX builds and
//     not at all on NEON.
//   - LIBM: the scalar <cmath> function on each lane. Always available; used
//     wherever neither of the others is, and by the others for functions they
//     lack (logb, nearbyint, erf, tgamma, ...).
//  Define TZPL_DSP_VMATH_LIBM to force LIBM (the tests build both ways),
//  TZPL_DSP_VMATH_SLEEF to force SLEEF (e.g. on macOS, to test it), or
//  TZPL_DSP_NO_SLEEF to skip Sleef even where its header exists.
//
//  The rounding functions (floor, ceil, trunc, round, rint, nearbyint), abs,
//  sqrt, fma, min and max are __builtin_elementwise_* on every backend: single
//  instructions on NEON and x86, and the same results everywhere.
//
//  sinpi, cospi and tanpi of scalars have no portable libm function, so they
//  are computed here by exact reduction to [-1/4, 1/4] (exact zeros at the
//  integers and half integers). The LIBM vector forms use the same.
//

#ifndef tzpl_dsp_vmath_hpp
#define tzpl_dsp_vmath_hpp

#include <cmath>
#include <numbers>

#include "vec_types.hpp"

#define TZPL_DSP_BACKEND_APPLE 1
#define TZPL_DSP_BACKEND_SLEEF 2
#define TZPL_DSP_BACKEND_LIBM  3

#if defined(TZPL_DSP_VMATH_LIBM)
#  define TZPL_DSP_VMATH_BACKEND TZPL_DSP_BACKEND_LIBM
#elif defined(TZPL_DSP_VMATH_SLEEF)
#  define TZPL_DSP_VMATH_BACKEND TZPL_DSP_BACKEND_SLEEF
#elif defined(__APPLE__)
#  define TZPL_DSP_VMATH_BACKEND TZPL_DSP_BACKEND_APPLE
#elif !defined(TZPL_DSP_NO_SLEEF) && __has_include(<sleef.h>)
#  define TZPL_DSP_VMATH_BACKEND TZPL_DSP_BACKEND_SLEEF
#else
#  define TZPL_DSP_VMATH_BACKEND TZPL_DSP_BACKEND_LIBM
#endif

#if TZPL_DSP_VMATH_BACKEND == TZPL_DSP_BACKEND_APPLE
#  include <simd/simd.h>
#elif TZPL_DSP_VMATH_BACKEND == TZPL_DSP_BACKEND_SLEEF
#  include <sleef.h>
#endif

namespace tzpl::dsp::vmath {

inline constexpr char const* kBackendName =
#if TZPL_DSP_VMATH_BACKEND == TZPL_DSP_BACKEND_APPLE
    "apple-simd";
#elif TZPL_DSP_VMATH_BACKEND == TZPL_DSP_BACKEND_SLEEF
    "sleef";
#else
    "libm";
#endif

// Re-exported so kernels can say vmath::min, vmath::select, etc. throughout.
using dsp::all;
using dsp::any;
using dsp::clamp;
using dsp::max;
using dsp::min;
using dsp::select;
using dsp::truth;

namespace detail {

// f applied to each lane.
template <class V, class F> inline V lanes1(V x, F f) {
    return V{f(x[0]), f(x[1]), f(x[2]), f(x[3])};
}
template <class V, class F> inline V lanes2(V x, V y, F f) {
    return V{f(x[0], y[0]), f(x[1], y[1]), f(x[2], y[2]), f(x[3], y[3])};
}

// sin(pi x) and cos(pi x) for a scalar. x = n/2 + r exactly, with n an
// integer and |r| <= 1/4 (x - n/2 is exact for |x| < 2^52, and for larger x
// every float is an integer or half integer anyway); then the quadrant n mod 4
// picks among +-sin(pi r) and +-cos(pi r). Every step but the final sin or
// cos is exact, for any finite x.
template <class T> inline T sinpi1(T x) {
    T n = std::rint(x * T(2));
    T r = x - n * T(0.5);
    T t = r * std::numbers::pi_v<T>;
    int q = int(n - T(4) * std::floor(n * T(0.25)));  // n mod 4, exactly
    T s = std::sin(t), c = std::cos(t);
    switch (q) {
        case 0: return s;
        case 1: return c;
        case 2: return -s;
        default: return -c;
    }
}
template <class T> inline T cospi1(T x) {
    T n = std::rint(x * T(2));
    T r = x - n * T(0.5);
    T t = r * std::numbers::pi_v<T>;
    int q = int(n - T(4) * std::floor(n * T(0.25)));  // n mod 4, exactly
    T s = std::sin(t), c = std::cos(t);
    switch (q) {
        case 0: return c;
        case 1: return -s;
        case 2: return -c;
        default: return s;
    }
}

}  // namespace detail

// --- Elementwise builtins: every backend ---------------------------------------

#define TZPL_VMATH_BUILTIN1(NAME, BUILTIN, SCALAR)                      \
    inline f32 NAME(f32 x) { return SCALAR(x); }                       \
    inline f64 NAME(f64 x) { return SCALAR(x); }                       \
    inline f32x4 NAME(f32x4 x) { return BUILTIN(x); }                  \
    inline f64x4 NAME(f64x4 x) { return BUILTIN(x); }

TZPL_VMATH_BUILTIN1(floor, __builtin_elementwise_floor, std::floor)
TZPL_VMATH_BUILTIN1(ceil, __builtin_elementwise_ceil, std::ceil)
TZPL_VMATH_BUILTIN1(trunc, __builtin_elementwise_trunc, std::trunc)
TZPL_VMATH_BUILTIN1(round, __builtin_elementwise_round, std::round)
TZPL_VMATH_BUILTIN1(rint, __builtin_elementwise_rint, std::rint)
TZPL_VMATH_BUILTIN1(nearbyint, __builtin_elementwise_nearbyint, std::nearbyint)
TZPL_VMATH_BUILTIN1(abs, __builtin_elementwise_abs, std::fabs)
TZPL_VMATH_BUILTIN1(fabs, __builtin_elementwise_abs, std::fabs)
TZPL_VMATH_BUILTIN1(sqrt, __builtin_elementwise_sqrt, std::sqrt)
#undef TZPL_VMATH_BUILTIN1

// Integer abs, for completeness of the generic kernels (wraps at the minimum).
inline i32x4 abs(i32x4 x) { return __builtin_elementwise_abs(x); }
inline i64x4 abs(i64x4 x) { return __builtin_elementwise_abs(x); }

inline f32 fma(f32 a, f32 b, f32 c) { return std::fma(a, b, c); }
inline f64 fma(f64 a, f64 b, f64 c) { return std::fma(a, b, c); }
inline f32x4 fma(f32x4 a, f32x4 b, f32x4 c) { return __builtin_elementwise_fma(a, b, c); }
inline f64x4 fma(f64x4 a, f64x4 b, f64x4 c) { return __builtin_elementwise_fma(a, b, c); }

inline f32 fmin(f32 a, f32 b) { return std::fmin(a, b); }
inline f64 fmin(f64 a, f64 b) { return std::fmin(a, b); }
inline f32x4 fmin(f32x4 a, f32x4 b) { return __builtin_elementwise_min(a, b); }
inline f64x4 fmin(f64x4 a, f64x4 b) { return __builtin_elementwise_min(a, b); }
inline f32 fmax(f32 a, f32 b) { return std::fmax(a, b); }
inline f64 fmax(f64 a, f64 b) { return std::fmax(a, b); }
inline f32x4 fmax(f32x4 a, f32x4 b) { return __builtin_elementwise_max(a, b); }
inline f64x4 fmax(f64x4 a, f64x4 b) { return __builtin_elementwise_max(a, b); }

inline f32 copysign(f32 a, f32 b) { return std::copysign(a, b); }
inline f64 copysign(f64 a, f64 b) { return std::copysign(a, b); }
template <class V>
    requires(is_vec_v<V> && is_float_v<V>)
inline V copysign(V a, V b) {
    using B = bits_t<V>;
    B sign = B(B(1) << (sizeof(elem_t<V>) * 8 - 1));
    return bitcast<V>((bitcast<B>(a) & ~sign) | (bitcast<B>(b) & sign));
}

// --- Transcendentals: scalars ------------------------------------------------------

#define TZPL_VMATH_SCALAR1(NAME)                       \
    inline f32 NAME(f32 x) { return std::NAME(x); }   \
    inline f64 NAME(f64 x) { return std::NAME(x); }
#define TZPL_VMATH_SCALAR2(NAME)                                   \
    inline f32 NAME(f32 x, f32 y) { return std::NAME(x, y); }     \
    inline f64 NAME(f64 x, f64 y) { return std::NAME(x, y); }

#define TZPL_VMATH_UNARY_NAMES(X)                                                    \
    X(sin) X(cos) X(tan) X(asin) X(acos) X(atan) X(sinh) X(cosh) X(tanh) X(asinh)   \
    X(acosh) X(atanh) X(exp) X(exp2) X(expm1) X(log) X(log10) X(log2) X(log1p)      \
    X(logb) X(cbrt) X(erf) X(erfc) X(tgamma) X(lgamma)
#define TZPL_VMATH_BINARY_NAMES(X) \
    X(pow) X(atan2) X(hypot) X(fmod) X(remainder) X(fdim) X(nextafter)

TZPL_VMATH_UNARY_NAMES(TZPL_VMATH_SCALAR1)
TZPL_VMATH_BINARY_NAMES(TZPL_VMATH_SCALAR2)
#undef TZPL_VMATH_SCALAR1
#undef TZPL_VMATH_SCALAR2

inline f32 sinpi(f32 x) { return detail::sinpi1(x); }
inline f64 sinpi(f64 x) { return detail::sinpi1(x); }
inline f32 cospi(f32 x) { return detail::cospi1(x); }
inline f64 cospi(f64 x) { return detail::cospi1(x); }
inline f32 tanpi(f32 x) { return detail::sinpi1(x) / detail::cospi1(x); }
inline f64 tanpi(f64 x) { return detail::sinpi1(x) / detail::cospi1(x); }

// --- Transcendentals: vectors ---------------------------------------------------------

// The scalar function on each lane: the LIBM backend, and the fallback for
// functions a backend lacks.
#define TZPL_VMATH_LANES1(NAME)                                                       \
    inline f32x4 NAME(f32x4 x) { return detail::lanes1(x, [](f32 a) { return NAME(a); }); } \
    inline f64x4 NAME(f64x4 x) { return detail::lanes1(x, [](f64 a) { return NAME(a); }); }
#define TZPL_VMATH_LANES2(NAME)                                                                \
    inline f32x4 NAME(f32x4 x, f32x4 y) {                                                      \
        return detail::lanes2(x, y, [](f32 a, f32 b) { return NAME(a, b); });                 \
    }                                                                                           \
    inline f64x4 NAME(f64x4 x, f64x4 y) {                                                      \
        return detail::lanes2(x, y, [](f64 a, f64 b) { return NAME(a, b); });                 \
    }

#if TZPL_DSP_VMATH_BACKEND == TZPL_DSP_BACKEND_APPLE

#define TZPL_VMATH_SIMD1(NAME)                                    \
    inline f32x4 NAME(f32x4 x) { return ::simd::NAME(x); }       \
    inline f64x4 NAME(f64x4 x) { return ::simd::NAME(x); }
#define TZPL_VMATH_SIMD2(NAME)                                              \
    inline f32x4 NAME(f32x4 x, f32x4 y) { return ::simd::NAME(x, y); }     \
    inline f64x4 NAME(f64x4 x, f64x4 y) { return ::simd::NAME(x, y); }

TZPL_VMATH_SIMD1(sin) TZPL_VMATH_SIMD1(cos) TZPL_VMATH_SIMD1(tan)
TZPL_VMATH_SIMD1(asin) TZPL_VMATH_SIMD1(acos) TZPL_VMATH_SIMD1(atan)
TZPL_VMATH_SIMD1(sinh) TZPL_VMATH_SIMD1(cosh) TZPL_VMATH_SIMD1(tanh)
TZPL_VMATH_SIMD1(asinh) TZPL_VMATH_SIMD1(acosh) TZPL_VMATH_SIMD1(atanh)
TZPL_VMATH_SIMD1(exp) TZPL_VMATH_SIMD1(exp2) TZPL_VMATH_SIMD1(expm1)
TZPL_VMATH_SIMD1(log) TZPL_VMATH_SIMD1(log10) TZPL_VMATH_SIMD1(log2) TZPL_VMATH_SIMD1(log1p)
TZPL_VMATH_LANES1(logb)
TZPL_VMATH_SIMD1(cbrt)
TZPL_VMATH_SIMD1(erf) TZPL_VMATH_SIMD1(erfc) TZPL_VMATH_SIMD1(tgamma) TZPL_VMATH_SIMD1(lgamma)
TZPL_VMATH_SIMD1(sinpi) TZPL_VMATH_SIMD1(cospi) TZPL_VMATH_SIMD1(tanpi)
TZPL_VMATH_SIMD2(pow) TZPL_VMATH_SIMD2(atan2) TZPL_VMATH_SIMD2(hypot)
TZPL_VMATH_SIMD2(fmod) TZPL_VMATH_SIMD2(remainder) TZPL_VMATH_SIMD2(fdim)
TZPL_VMATH_SIMD2(nextafter)

#undef TZPL_VMATH_SIMD1
#undef TZPL_VMATH_SIMD2

#elif TZPL_DSP_VMATH_BACKEND == TZPL_DSP_BACKEND_SLEEF

namespace detail {
inline f64x2 lo(f64x4 v) { return __builtin_shufflevector(v, v, 0, 1); }
inline f64x2 hi(f64x4 v) { return __builtin_shufflevector(v, v, 2, 3); }
inline f64x4 join(f64x2 a, f64x2 b) { return __builtin_shufflevector(a, b, 0, 1, 2, 3); }
}  // namespace detail

// f32x4: one Sleef f4 call. f64x4: a d2 call on each half.
#define TZPL_VMATH_SLEEF1(NAME, F4, D2)                                            \
    inline f32x4 NAME(f32x4 x) { return F4(x); }                                  \
    inline f64x4 NAME(f64x4 x) {                                                   \
        return detail::join(D2(detail::lo(x)), D2(detail::hi(x)));                 \
    }
#define TZPL_VMATH_SLEEF2(NAME, F4, D2)                                            \
    inline f32x4 NAME(f32x4 x, f32x4 y) { return F4(x, y); }                      \
    inline f64x4 NAME(f64x4 x, f64x4 y) {                                          \
        return detail::join(D2(detail::lo(x), detail::lo(y)),                      \
                            D2(detail::hi(x), detail::hi(y)));                     \
    }

// 1.0 ULP variants throughout (u10), the tier Apple's simd math meets; the
// pi-scaled trig and hypot only come at 0.5 ULP.
TZPL_VMATH_SLEEF1(sin, Sleef_sinf4_u10, Sleef_sind2_u10)
TZPL_VMATH_SLEEF1(cos, Sleef_cosf4_u10, Sleef_cosd2_u10)
TZPL_VMATH_SLEEF1(tan, Sleef_tanf4_u10, Sleef_tand2_u10)
TZPL_VMATH_SLEEF1(asin, Sleef_asinf4_u10, Sleef_asind2_u10)
TZPL_VMATH_SLEEF1(acos, Sleef_acosf4_u10, Sleef_acosd2_u10)
TZPL_VMATH_SLEEF1(atan, Sleef_atanf4_u10, Sleef_atand2_u10)
TZPL_VMATH_SLEEF1(sinh, Sleef_sinhf4_u10, Sleef_sinhd2_u10)
TZPL_VMATH_SLEEF1(cosh, Sleef_coshf4_u10, Sleef_coshd2_u10)
TZPL_VMATH_SLEEF1(tanh, Sleef_tanhf4_u10, Sleef_tanhd2_u10)
TZPL_VMATH_SLEEF1(asinh, Sleef_asinhf4_u10, Sleef_asinhd2_u10)
TZPL_VMATH_SLEEF1(acosh, Sleef_acoshf4_u10, Sleef_acoshd2_u10)
TZPL_VMATH_SLEEF1(atanh, Sleef_atanhf4_u10, Sleef_atanhd2_u10)
TZPL_VMATH_SLEEF1(exp, Sleef_expf4_u10, Sleef_expd2_u10)
TZPL_VMATH_SLEEF1(exp2, Sleef_exp2f4_u10, Sleef_exp2d2_u10)
TZPL_VMATH_SLEEF1(expm1, Sleef_expm1f4_u10, Sleef_expm1d2_u10)
TZPL_VMATH_SLEEF1(log, Sleef_logf4_u10, Sleef_logd2_u10)
TZPL_VMATH_SLEEF1(log10, Sleef_log10f4_u10, Sleef_log10d2_u10)
TZPL_VMATH_SLEEF1(log2, Sleef_log2f4_u10, Sleef_log2d2_u10)
TZPL_VMATH_SLEEF1(log1p, Sleef_log1pf4_u10, Sleef_log1pd2_u10)
TZPL_VMATH_SLEEF1(cbrt, Sleef_cbrtf4_u10, Sleef_cbrtd2_u10)
TZPL_VMATH_SLEEF1(sinpi, Sleef_sinpif4_u05, Sleef_sinpid2_u05)
TZPL_VMATH_SLEEF1(cospi, Sleef_cospif4_u05, Sleef_cospid2_u05)
TZPL_VMATH_SLEEF2(pow, Sleef_powf4_u10, Sleef_powd2_u10)
TZPL_VMATH_SLEEF2(atan2, Sleef_atan2f4_u10, Sleef_atan2d2_u10)
TZPL_VMATH_SLEEF2(hypot, Sleef_hypotf4_u05, Sleef_hypotd2_u05)
TZPL_VMATH_SLEEF2(fmod, Sleef_fmodf4, Sleef_fmodd2)
// Gaps: no Sleef form used, so lane by lane.
TZPL_VMATH_LANES1(logb)
TZPL_VMATH_LANES1(erf) TZPL_VMATH_LANES1(erfc)
TZPL_VMATH_LANES1(tgamma) TZPL_VMATH_LANES1(lgamma)
TZPL_VMATH_LANES2(remainder) TZPL_VMATH_LANES2(fdim) TZPL_VMATH_LANES2(nextafter)
inline f32x4 tanpi(f32x4 x) { return sinpi(x) / cospi(x); }
inline f64x4 tanpi(f64x4 x) { return sinpi(x) / cospi(x); }

#undef TZPL_VMATH_SLEEF1
#undef TZPL_VMATH_SLEEF2

#else  // LIBM

TZPL_VMATH_UNARY_NAMES(TZPL_VMATH_LANES1)
TZPL_VMATH_BINARY_NAMES(TZPL_VMATH_LANES2)
TZPL_VMATH_LANES1(sinpi) TZPL_VMATH_LANES1(cospi) TZPL_VMATH_LANES1(tanpi)

#endif

#undef TZPL_VMATH_LANES1
#undef TZPL_VMATH_LANES2
#undef TZPL_VMATH_UNARY_NAMES
#undef TZPL_VMATH_BINARY_NAMES

// --- Derived ----------------------------------------------------------------------

// x - floor(x), in [0, 1) (it can round to 1.0 for tiny negative x).
template <class T> inline T frac(T x) { return x - floor(x); }

}  // namespace tzpl::dsp::vmath

#endif  // tzpl_dsp_vmath_hpp
