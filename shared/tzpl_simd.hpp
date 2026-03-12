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
//  tzpl_simd.hpp
//  SIMD platform abstraction
//
//  On Apple: wraps <simd/simd.h> types and functions.
//  On Linux/Clang: uses ext_vector_type (same swizzle syntax) + Sleef for math.
//

#ifndef tzpl_simd_hpp
#define tzpl_simd_hpp

#ifdef __APPLE__

// ============================================================================
// SECTION A — Apple: types from <simd/simd.h>
// ============================================================================

#include <simd/simd.h>

using u32x2 = simd::uint2;
using u32x4 = simd::uint4;
using u32x8 = simd::uint8;

using u64x2 = simd::ulong2;
using u64x4 = simd::ulong4;
using u64x8 = simd::ulong8;

using i32x2 = simd::int2;
using i32x4 = simd::int4;
using i32x8 = simd::int8;

using i64x2 = simd::long2;
using i64x4 = simd::long4;
using i64x8 = simd::long8;

using f32x2 = simd::float2;
using f32x4 = simd::float4;
using f32x8 = simd::float8;

using f64x2 = simd::double2;
using f64x4 = simd::double4;
using f64x8 = simd::double8;

// Apple: overloads for functions not provided by Apple's simd framework
#include <cmath>

inline f32x2 log1p(f32x2 x) { return f32x2{std::log1p(x[0]), std::log1p(x[1])}; }
inline f32x4 log1p(f32x4 x) { return f32x4{std::log1p(x[0]), std::log1p(x[1]), std::log1p(x[2]), std::log1p(x[3])}; }
inline f64x2 log1p(f64x2 x) { return f64x2{std::log1p(x[0]), std::log1p(x[1])}; }
inline f64x4 log1p(f64x4 x) { return f64x4{std::log1p(x[0]), std::log1p(x[1]), std::log1p(x[2]), std::log1p(x[3])}; }

inline f32x2 expm1(f32x2 x) { return f32x2{std::expm1(x[0]), std::expm1(x[1])}; }
inline f32x4 expm1(f32x4 x) { return f32x4{std::expm1(x[0]), std::expm1(x[1]), std::expm1(x[2]), std::expm1(x[3])}; }
inline f64x2 expm1(f64x2 x) { return f64x2{std::expm1(x[0]), std::expm1(x[1])}; }
inline f64x4 expm1(f64x4 x) { return f64x4{std::expm1(x[0]), std::expm1(x[1]), std::expm1(x[2]), std::expm1(x[3])}; }

#else // !__APPLE__

// ============================================================================
// SECTION A — Linux/Clang: ext_vector_type (supports .s0, .xy, .xyxy swizzle)
// ============================================================================

using u64x2 = unsigned long __attribute__((ext_vector_type(2)));
using u64x4 = unsigned long __attribute__((ext_vector_type(4)));
using u64x8 = unsigned long __attribute__((ext_vector_type(8)));

using i32x2 = int           __attribute__((ext_vector_type(2)));
using i32x4 = int           __attribute__((ext_vector_type(4)));
using i32x8 = int           __attribute__((ext_vector_type(8)));

using i64x2 = long          __attribute__((ext_vector_type(2)));
using i64x4 = long          __attribute__((ext_vector_type(4)));
using i64x8 = long          __attribute__((ext_vector_type(8)));

using f32x2 = float         __attribute__((ext_vector_type(2)));
using f32x4 = float         __attribute__((ext_vector_type(4)));
using f32x8 = float         __attribute__((ext_vector_type(8)));

using f64x2 = double        __attribute__((ext_vector_type(2)));
using f64x4 = double        __attribute__((ext_vector_type(4)));
using f64x8 = double        __attribute__((ext_vector_type(8)));

#endif // !__APPLE__ (Section A)

// ============================================================================
// SECTION B — Traits: get_traits<T> and Vector<S,N>
// ============================================================================

#ifdef __APPLE__

// Apple path: the real simd namespace already provides these.
// No additional work needed.

#else // !__APPLE__

#include <type_traits>

namespace simd_compat {

// --- get_traits: maps a type to { count, scalar_t } ---

template <typename T>
struct get_traits;

// Scalar specializations (count = 1)
template <> struct get_traits<float>         { struct type { static constexpr int count = 1; using scalar_t = float; }; };
template <> struct get_traits<double>        { struct type { static constexpr int count = 1; using scalar_t = double; }; };
template <> struct get_traits<int>           { struct type { static constexpr int count = 1; using scalar_t = int; }; };
template <> struct get_traits<long>          { struct type { static constexpr int count = 1; using scalar_t = long; }; };
template <> struct get_traits<unsigned long> { struct type { static constexpr int count = 1; using scalar_t = unsigned long; }; };

// Vector specializations
template <> struct get_traits<f32x2> { struct type { static constexpr int count = 2; using scalar_t = float; }; };
template <> struct get_traits<f32x4> { struct type { static constexpr int count = 4; using scalar_t = float; }; };
template <> struct get_traits<f32x8> { struct type { static constexpr int count = 8; using scalar_t = float; }; };

template <> struct get_traits<f64x2> { struct type { static constexpr int count = 2; using scalar_t = double; }; };
template <> struct get_traits<f64x4> { struct type { static constexpr int count = 4; using scalar_t = double; }; };
template <> struct get_traits<f64x8> { struct type { static constexpr int count = 8; using scalar_t = double; }; };

template <> struct get_traits<i32x2> { struct type { static constexpr int count = 2; using scalar_t = int; }; };
template <> struct get_traits<i32x4> { struct type { static constexpr int count = 4; using scalar_t = int; }; };
template <> struct get_traits<i32x8> { struct type { static constexpr int count = 8; using scalar_t = int; }; };

template <> struct get_traits<i64x2> { struct type { static constexpr int count = 2; using scalar_t = long; }; };
template <> struct get_traits<i64x4> { struct type { static constexpr int count = 4; using scalar_t = long; }; };
template <> struct get_traits<i64x8> { struct type { static constexpr int count = 8; using scalar_t = long; }; };

template <> struct get_traits<u64x2> { struct type { static constexpr int count = 2; using scalar_t = unsigned long; }; };
template <> struct get_traits<u64x4> { struct type { static constexpr int count = 4; using scalar_t = unsigned long; }; };
template <> struct get_traits<u64x8> { struct type { static constexpr int count = 8; using scalar_t = unsigned long; }; };

// --- Vector<Scalar, N>::type: maps (scalar, width) to a vector type ---

template <typename S, int N>
struct Vector;

template <> struct Vector<float, 1>  { using type = float; };
template <> struct Vector<float, 2>  { using type = f32x2; };
template <> struct Vector<float, 4>  { using type = f32x4; };
template <> struct Vector<float, 8>  { using type = f32x8; };

template <> struct Vector<double, 1> { using type = double; };
template <> struct Vector<double, 2> { using type = f64x2; };
template <> struct Vector<double, 4> { using type = f64x4; };
template <> struct Vector<double, 8> { using type = f64x8; };

template <> struct Vector<int, 1>    { using type = int; };
template <> struct Vector<int, 2>    { using type = i32x2; };
template <> struct Vector<int, 4>    { using type = i32x4; };
template <> struct Vector<int, 8>    { using type = i32x8; };

template <> struct Vector<long, 1>   { using type = long; };
template <> struct Vector<long, 2>   { using type = i64x2; };
template <> struct Vector<long, 4>   { using type = i64x4; };
template <> struct Vector<long, 8>   { using type = i64x8; };

template <> struct Vector<unsigned long, 1> { using type = unsigned long; };
template <> struct Vector<unsigned long, 2> { using type = u64x2; };
template <> struct Vector<unsigned long, 4> { using type = u64x4; };
template <> struct Vector<unsigned long, 8> { using type = u64x8; };


// ============================================================================
// SECTION C — Math wrappers (4-wide only, via Sleef)
// ============================================================================

#include <sleef.h>
#include <cmath>

// --- f32x4 math ---
inline f32x4 sin(f32x4 x)        { return Sleef_sinf4_u10(x); }
inline f32x4 cos(f32x4 x)        { return Sleef_cosf4_u10(x); }
inline f32x4 exp(f32x4 x)        { return Sleef_expf4_u10(x); }
inline f32x4 exp2(f32x4 x)       { return Sleef_exp2f4_u10(x); }
inline f32x4 exp10(f32x4 x)      { return Sleef_exp10f4_u10(x); }
inline f32x4 log(f32x4 x)        { return Sleef_logf4_u10(x); }
inline f32x4 sqrt(f32x4 x)       { return Sleef_sqrtf4_u05(x); }
inline f32x4 cbrt(f32x4 x)       { return Sleef_cbrtf4_u10(x); }
inline f32x4 hypot(f32x4 x, f32x4 y) { return Sleef_hypotf4_u05(x, y); }
inline f32x4 atan2(f32x4 y, f32x4 x) { return Sleef_atan2f4_u10(y, x); }
inline f32x4 sinh(f32x4 x)       { return Sleef_sinhf4_u10(x); }
inline f32x4 cosh(f32x4 x)       { return Sleef_coshf4_u10(x); }
inline f32x4 copysign(f32x4 x, f32x4 y) { return Sleef_copysignf4(x, y); }
inline f32x4 abs(f32x4 x)        { return Sleef_fabsf4(x); }
inline f32x4 floor(f32x4 x)      { return Sleef_floorf4(x); }
inline f32x4 tan(f32x4 x)        { return Sleef_tanf4_u10(x); }
inline f32x4 asin(f32x4 x)       { return Sleef_asinf4_u10(x); }
inline f32x4 acos(f32x4 x)       { return Sleef_acosf4_u10(x); }
inline f32x4 atan(f32x4 x)       { return Sleef_atanf4_u10(x); }
inline f32x4 tanh(f32x4 x)       { return Sleef_tanhf4_u10(x); }
inline f32x4 asinh(f32x4 x)      { return Sleef_asinhf4_u10(x); }
inline f32x4 acosh(f32x4 x)      { return Sleef_acoshf4_u10(x); }
inline f32x4 atanh(f32x4 x)      { return Sleef_atanhf4_u10(x); }
inline f32x4 pow(f32x4 x, f32x4 y) { return Sleef_powf4_u10(x, y); }
inline f32x4 log2(f32x4 x)       { return Sleef_log2f4_u10(x); }
inline f32x4 log10(f32x4 x)      { return Sleef_log10f4_u10(x); }
inline f32x4 log1p(f32x4 x)      { return Sleef_log1pf4_u10(x); }
inline f32x4 expm1(f32x4 x)      { return Sleef_expm1f4_u10(x); }
inline f32x4 fmod(f32x4 x, f32x4 y) { return Sleef_fmodf4(x, y); }
inline f32x4 ceil(f32x4 x)       { return f32x4{std::ceil(x.s0), std::ceil(x.s1), std::ceil(x.s2), std::ceil(x.s3)}; }
inline f32x4 round(f32x4 x)      { return f32x4{std::round(x.s0), std::round(x.s1), std::round(x.s2), std::round(x.s3)}; }
inline f32x4 trunc(f32x4 x)      { return f32x4{std::trunc(x.s0), std::trunc(x.s1), std::trunc(x.s2), std::trunc(x.s3)}; }
inline f32x4 min(f32x4 a, f32x4 b) { return f32x4{std::min(a.s0, b.s0), std::min(a.s1, b.s1), std::min(a.s2, b.s2), std::min(a.s3, b.s3)}; }
inline f32x4 max(f32x4 a, f32x4 b) { return f32x4{std::max(a.s0, b.s0), std::max(a.s1, b.s1), std::max(a.s2, b.s2), std::max(a.s3, b.s3)}; }

// --- f64x4 math ---
inline f64x4 sin(f64x4 x)        { return Sleef_sind4_u10(x); }
inline f64x4 cos(f64x4 x)        { return Sleef_cosd4_u10(x); }
inline f64x4 exp(f64x4 x)        { return Sleef_expd4_u10(x); }
inline f64x4 exp2(f64x4 x)       { return Sleef_exp2d4_u10(x); }
inline f64x4 exp10(f64x4 x)      { return Sleef_exp10d4_u10(x); }
inline f64x4 log(f64x4 x)        { return Sleef_logd4_u10(x); }
inline f64x4 sqrt(f64x4 x)       { return Sleef_sqrtd4_u05(x); }
inline f64x4 cbrt(f64x4 x)       { return Sleef_cbrtd4_u10(x); }
inline f64x4 hypot(f64x4 x, f64x4 y) { return Sleef_hypotd4_u05(x, y); }
inline f64x4 atan2(f64x4 y, f64x4 x) { return Sleef_atan2d4_u10(y, x); }
inline f64x4 sinh(f64x4 x)       { return Sleef_sinhd4_u10(x); }
inline f64x4 cosh(f64x4 x)       { return Sleef_coshd4_u10(x); }
inline f64x4 copysign(f64x4 x, f64x4 y) { return Sleef_copysignd4(x, y); }
inline f64x4 abs(f64x4 x)        { return Sleef_fabsd4(x); }
inline f64x4 floor(f64x4 x)      { return Sleef_floord4(x); }
inline f64x4 tan(f64x4 x)        { return Sleef_tand4_u10(x); }
inline f64x4 asin(f64x4 x)       { return Sleef_asind4_u10(x); }
inline f64x4 acos(f64x4 x)       { return Sleef_acosd4_u10(x); }
inline f64x4 atan(f64x4 x)       { return Sleef_atand4_u10(x); }
inline f64x4 tanh(f64x4 x)       { return Sleef_tanhd4_u10(x); }
inline f64x4 asinh(f64x4 x)      { return Sleef_asinhd4_u10(x); }
inline f64x4 acosh(f64x4 x)      { return Sleef_acoshd4_u10(x); }
inline f64x4 atanh(f64x4 x)      { return Sleef_atanhd4_u10(x); }
inline f64x4 pow(f64x4 x, f64x4 y) { return Sleef_powd4_u10(x, y); }
inline f64x4 log2(f64x4 x)       { return Sleef_log2d4_u10(x); }
inline f64x4 log10(f64x4 x)      { return Sleef_log10d4_u10(x); }
inline f64x4 log1p(f64x4 x)      { return Sleef_log1pd4_u10(x); }
inline f64x4 expm1(f64x4 x)      { return Sleef_expm1d4_u10(x); }
inline f64x4 fmod(f64x4 x, f64x4 y) { return Sleef_fmodd4(x, y); }
inline f64x4 ceil(f64x4 x)       { return f64x4{std::ceil(x.s0), std::ceil(x.s1), std::ceil(x.s2), std::ceil(x.s3)}; }
inline f64x4 round(f64x4 x)      { return f64x4{std::round(x.s0), std::round(x.s1), std::round(x.s2), std::round(x.s3)}; }
inline f64x4 trunc(f64x4 x)      { return f64x4{std::trunc(x.s0), std::trunc(x.s1), std::trunc(x.s2), std::trunc(x.s3)}; }
inline f64x4 min(f64x4 a, f64x4 b) { return f64x4{std::min(a.s0, b.s0), std::min(a.s1, b.s1), std::min(a.s2, b.s2), std::min(a.s3, b.s3)}; }
inline f64x4 max(f64x4 a, f64x4 b) { return f64x4{std::max(a.s0, b.s0), std::max(a.s1, b.s1), std::max(a.s2, b.s2), std::max(a.s3, b.s3)}; }

// --- f32x2 math (scalar fallback) ---
inline f32x2 sin(f32x2 x)        { return f32x2{std::sin(x.s0), std::sin(x.s1)}; }
inline f32x2 cos(f32x2 x)        { return f32x2{std::cos(x.s0), std::cos(x.s1)}; }
inline f32x2 exp(f32x2 x)        { return f32x2{std::exp(x.s0), std::exp(x.s1)}; }
inline f32x2 exp2(f32x2 x)       { return f32x2{std::exp2(x.s0), std::exp2(x.s1)}; }
inline f32x2 log(f32x2 x)        { return f32x2{std::log(x.s0), std::log(x.s1)}; }
inline f32x2 sqrt(f32x2 x)       { return f32x2{std::sqrt(x.s0), std::sqrt(x.s1)}; }
inline f32x2 cbrt(f32x2 x)       { return f32x2{std::cbrt(x.s0), std::cbrt(x.s1)}; }
inline f32x2 hypot(f32x2 x, f32x2 y) { return f32x2{std::hypot(x.s0, y.s0), std::hypot(x.s1, y.s1)}; }
inline f32x2 atan2(f32x2 y, f32x2 x) { return f32x2{std::atan2(y.s0, x.s0), std::atan2(y.s1, x.s1)}; }
inline f32x2 sinh(f32x2 x)       { return f32x2{std::sinh(x.s0), std::sinh(x.s1)}; }
inline f32x2 cosh(f32x2 x)       { return f32x2{std::cosh(x.s0), std::cosh(x.s1)}; }
inline f32x2 copysign(f32x2 x, f32x2 y) { return f32x2{std::copysign(x.s0, y.s0), std::copysign(x.s1, y.s1)}; }
inline f32x2 abs(f32x2 x)        { return f32x2{std::fabs(x.s0), std::fabs(x.s1)}; }
inline f32x2 floor(f32x2 x)      { return f32x2{std::floor(x.s0), std::floor(x.s1)}; }
inline f32x2 tan(f32x2 x)        { return f32x2{std::tan(x.s0), std::tan(x.s1)}; }
inline f32x2 asin(f32x2 x)       { return f32x2{std::asin(x.s0), std::asin(x.s1)}; }
inline f32x2 acos(f32x2 x)       { return f32x2{std::acos(x.s0), std::acos(x.s1)}; }
inline f32x2 atan(f32x2 x)       { return f32x2{std::atan(x.s0), std::atan(x.s1)}; }
inline f32x2 tanh(f32x2 x)       { return f32x2{std::tanh(x.s0), std::tanh(x.s1)}; }
inline f32x2 asinh(f32x2 x)      { return f32x2{std::asinh(x.s0), std::asinh(x.s1)}; }
inline f32x2 acosh(f32x2 x)      { return f32x2{std::acosh(x.s0), std::acosh(x.s1)}; }
inline f32x2 atanh(f32x2 x)      { return f32x2{std::atanh(x.s0), std::atanh(x.s1)}; }
inline f32x2 pow(f32x2 x, f32x2 y) { return f32x2{std::pow(x.s0, y.s0), std::pow(x.s1, y.s1)}; }
inline f32x2 log2(f32x2 x)       { return f32x2{std::log2(x.s0), std::log2(x.s1)}; }
inline f32x2 log10(f32x2 x)      { return f32x2{std::log10(x.s0), std::log10(x.s1)}; }
inline f32x2 log1p(f32x2 x)      { return f32x2{std::log1p(x.s0), std::log1p(x.s1)}; }
inline f32x2 exp10(f32x2 x)      { return f32x2{std::exp10(x.s0), std::exp10(x.s1)}; }
inline f32x2 expm1(f32x2 x)      { return f32x2{std::expm1(x.s0), std::expm1(x.s1)}; }
inline f32x2 fmod(f32x2 x, f32x2 y) { return f32x2{std::fmod(x.s0, y.s0), std::fmod(x.s1, y.s1)}; }
inline f32x2 ceil(f32x2 x)       { return f32x2{std::ceil(x.s0), std::ceil(x.s1)}; }
inline f32x2 round(f32x2 x)      { return f32x2{std::round(x.s0), std::round(x.s1)}; }
inline f32x2 trunc(f32x2 x)      { return f32x2{std::trunc(x.s0), std::trunc(x.s1)}; }
inline f32x2 min(f32x2 a, f32x2 b) { return f32x2{std::min(a.s0, b.s0), std::min(a.s1, b.s1)}; }
inline f32x2 max(f32x2 a, f32x2 b) { return f32x2{std::max(a.s0, b.s0), std::max(a.s1, b.s1)}; }

// --- f64x2 math (scalar fallback) ---
inline f64x2 sin(f64x2 x)        { return f64x2{std::sin(x.s0), std::sin(x.s1)}; }
inline f64x2 cos(f64x2 x)        { return f64x2{std::cos(x.s0), std::cos(x.s1)}; }
inline f64x2 exp(f64x2 x)        { return f64x2{std::exp(x.s0), std::exp(x.s1)}; }
inline f64x2 exp2(f64x2 x)       { return f64x2{std::exp2(x.s0), std::exp2(x.s1)}; }
inline f64x2 log(f64x2 x)        { return f64x2{std::log(x.s0), std::log(x.s1)}; }
inline f64x2 sqrt(f64x2 x)       { return f64x2{std::sqrt(x.s0), std::sqrt(x.s1)}; }
inline f64x2 cbrt(f64x2 x)       { return f64x2{std::cbrt(x.s0), std::cbrt(x.s1)}; }
inline f64x2 hypot(f64x2 x, f64x2 y) { return f64x2{std::hypot(x.s0, y.s0), std::hypot(x.s1, y.s1)}; }
inline f64x2 atan2(f64x2 y, f64x2 x) { return f64x2{std::atan2(y.s0, x.s0), std::atan2(y.s1, x.s1)}; }
inline f64x2 sinh(f64x2 x)       { return f64x2{std::sinh(x.s0), std::sinh(x.s1)}; }
inline f64x2 cosh(f64x2 x)       { return f64x2{std::cosh(x.s0), std::cosh(x.s1)}; }
inline f64x2 copysign(f64x2 x, f64x2 y) { return f64x2{std::copysign(x.s0, y.s0), std::copysign(x.s1, y.s1)}; }
inline f64x2 abs(f64x2 x)        { return f64x2{std::fabs(x.s0), std::fabs(x.s1)}; }
inline f64x2 floor(f64x2 x)      { return f64x2{std::floor(x.s0), std::floor(x.s1)}; }
inline f64x2 tan(f64x2 x)        { return f64x2{std::tan(x.s0), std::tan(x.s1)}; }
inline f64x2 asin(f64x2 x)       { return f64x2{std::asin(x.s0), std::asin(x.s1)}; }
inline f64x2 acos(f64x2 x)       { return f64x2{std::acos(x.s0), std::acos(x.s1)}; }
inline f64x2 atan(f64x2 x)       { return f64x2{std::atan(x.s0), std::atan(x.s1)}; }
inline f64x2 tanh(f64x2 x)       { return f64x2{std::tanh(x.s0), std::tanh(x.s1)}; }
inline f64x2 asinh(f64x2 x)      { return f64x2{std::asinh(x.s0), std::asinh(x.s1)}; }
inline f64x2 acosh(f64x2 x)      { return f64x2{std::acosh(x.s0), std::acosh(x.s1)}; }
inline f64x2 atanh(f64x2 x)      { return f64x2{std::atanh(x.s0), std::atanh(x.s1)}; }
inline f64x2 pow(f64x2 x, f64x2 y) { return f64x2{std::pow(x.s0, y.s0), std::pow(x.s1, y.s1)}; }
inline f64x2 log2(f64x2 x)       { return f64x2{std::log2(x.s0), std::log2(x.s1)}; }
inline f64x2 log10(f64x2 x)      { return f64x2{std::log10(x.s0), std::log10(x.s1)}; }
inline f64x2 log1p(f64x2 x)      { return f64x2{std::log1p(x.s0), std::log1p(x.s1)}; }
inline f64x2 exp10(f64x2 x)      { return f64x2{std::exp10(x.s0), std::exp10(x.s1)}; }
inline f64x2 expm1(f64x2 x)      { return f64x2{std::expm1(x.s0), std::expm1(x.s1)}; }
inline f64x2 fmod(f64x2 x, f64x2 y) { return f64x2{std::fmod(x.s0, y.s0), std::fmod(x.s1, y.s1)}; }
inline f64x2 ceil(f64x2 x)       { return f64x2{std::ceil(x.s0), std::ceil(x.s1)}; }
inline f64x2 round(f64x2 x)      { return f64x2{std::round(x.s0), std::round(x.s1)}; }
inline f64x2 trunc(f64x2 x)      { return f64x2{std::trunc(x.s0), std::trunc(x.s1)}; }
inline f64x2 min(f64x2 a, f64x2 b) { return f64x2{std::min(a.s0, b.s0), std::min(a.s1, b.s1)}; }
inline f64x2 max(f64x2 a, f64x2 b) { return f64x2{std::max(a.s0, b.s0), std::max(a.s1, b.s1)}; }


// ============================================================================
// SECTION D — Utility wrappers
// ============================================================================

// --- convert<T>(x): type conversion ---

template <typename To>
struct convert_impl;

template <> struct convert_impl<float> {
    // Scalars
    static float apply(float x)  { return x; }
    static float apply(double x) { return (float)x; }
    static float apply(int x)    { return (float)x; }
    static float apply(long x)   { return (float)x; }
    // Vectors
    static f32x2 apply(f32x2 x) { return x; }
    static f32x2 apply(f64x2 x) { return __builtin_convertvector(x, f32x2); }
    static f32x2 apply(i32x2 x) { return __builtin_convertvector(x, f32x2); }
    static f32x2 apply(i64x2 x) { return __builtin_convertvector(x, f32x2); }
    static f32x4 apply(f32x4 x) { return x; }
    static f32x4 apply(f64x4 x) { return __builtin_convertvector(x, f32x4); }
    static f32x4 apply(i32x4 x) { return __builtin_convertvector(x, f32x4); }
    static f32x4 apply(i64x4 x) { return __builtin_convertvector(x, f32x4); }
    static f32x8 apply(f32x8 x) { return x; }
    static f32x8 apply(f64x8 x) { return __builtin_convertvector(x, f32x8); }
    static f32x8 apply(i32x8 x) { return __builtin_convertvector(x, f32x8); }
    static f32x8 apply(i64x8 x) { return __builtin_convertvector(x, f32x8); }
};
template <> struct convert_impl<double> {
    static double apply(float x)  { return x; }
    static double apply(double x) { return x; }
    static double apply(int x)    { return (double)x; }
    static double apply(long x)   { return (double)x; }
    static f64x2 apply(f32x2 x) { return __builtin_convertvector(x, f64x2); }
    static f64x2 apply(f64x2 x) { return x; }
    static f64x2 apply(i32x2 x) { return __builtin_convertvector(x, f64x2); }
    static f64x2 apply(i64x2 x) { return __builtin_convertvector(x, f64x2); }
    static f64x4 apply(f32x4 x) { return __builtin_convertvector(x, f64x4); }
    static f64x4 apply(f64x4 x) { return x; }
    static f64x4 apply(i32x4 x) { return __builtin_convertvector(x, f64x4); }
    static f64x4 apply(i64x4 x) { return __builtin_convertvector(x, f64x4); }
    static f64x8 apply(f32x8 x) { return __builtin_convertvector(x, f64x8); }
    static f64x8 apply(f64x8 x) { return x; }
    static f64x8 apply(i32x8 x) { return __builtin_convertvector(x, f64x8); }
    static f64x8 apply(i64x8 x) { return __builtin_convertvector(x, f64x8); }
};
template <> struct convert_impl<int> {
    static int apply(float x)  { return (int)x; }
    static int apply(double x) { return (int)x; }
    static int apply(int x)    { return x; }
    static int apply(long x)   { return (int)x; }
    static i32x2 apply(f32x2 x) { return __builtin_convertvector(x, i32x2); }
    static i32x2 apply(f64x2 x) { return __builtin_convertvector(x, i32x2); }
    static i32x2 apply(i32x2 x) { return x; }
    static i32x2 apply(i64x2 x) { return __builtin_convertvector(x, i32x2); }
    static i32x4 apply(f32x4 x) { return __builtin_convertvector(x, i32x4); }
    static i32x4 apply(f64x4 x) { return __builtin_convertvector(x, i32x4); }
    static i32x4 apply(i32x4 x) { return x; }
    static i32x4 apply(i64x4 x) { return __builtin_convertvector(x, i32x4); }
    static i32x8 apply(f32x8 x) { return __builtin_convertvector(x, i32x8); }
    static i32x8 apply(f64x8 x) { return __builtin_convertvector(x, i32x8); }
    static i32x8 apply(i32x8 x) { return x; }
    static i32x8 apply(i64x8 x) { return __builtin_convertvector(x, i32x8); }
};
template <> struct convert_impl<long> {
    static long apply(float x)  { return (long)x; }
    static long apply(double x) { return (long)x; }
    static long apply(int x)    { return (long)x; }
    static long apply(long x)   { return x; }
    static i64x2 apply(f32x2 x) { return __builtin_convertvector(x, i64x2); }
    static i64x2 apply(f64x2 x) { return __builtin_convertvector(x, i64x2); }
    static i64x2 apply(i32x2 x) { return __builtin_convertvector(x, i64x2); }
    static i64x2 apply(i64x2 x) { return x; }
    static i64x4 apply(f32x4 x) { return __builtin_convertvector(x, i64x4); }
    static i64x4 apply(f64x4 x) { return __builtin_convertvector(x, i64x4); }
    static i64x4 apply(i32x4 x) { return __builtin_convertvector(x, i64x4); }
    static i64x4 apply(i64x4 x) { return x; }
    static i64x8 apply(f32x8 x) { return __builtin_convertvector(x, i64x8); }
    static i64x8 apply(f64x8 x) { return __builtin_convertvector(x, i64x8); }
    static i64x8 apply(i32x8 x) { return __builtin_convertvector(x, i64x8); }
    static i64x8 apply(i64x8 x) { return x; }
};

template <typename To, typename From>
auto convert(From x) { return convert_impl<To>::apply(x); }

// --- dot product ---

inline float dot(f32x2 a, f32x2 b) {
    f32x2 c = a * b;
    return c.s0 + c.s1;
}
inline float dot(f32x4 a, f32x4 b) {
    f32x4 c = a * b;
    return c.s0 + c.s1 + c.s2 + c.s3;
}
inline double dot(f64x2 a, f64x2 b) {
    f64x2 c = a * b;
    return c.s0 + c.s1;
}
inline double dot(f64x4 a, f64x4 b) {
    f64x4 c = a * b;
    return c.s0 + c.s1 + c.s2 + c.s3;
}

// --- reductions ---

inline float  reduce_max(f32x2 x) { return x.s0 > x.s1 ? x.s0 : x.s1; }
inline float  reduce_max(f32x4 x) { float a = x.s0 > x.s1 ? x.s0 : x.s1; float b = x.s2 > x.s3 ? x.s2 : x.s3; return a > b ? a : b; }
inline float  reduce_max(f32x8 x) { return reduce_max(f32x4{x.s0>x.s4?x.s0:x.s4, x.s1>x.s5?x.s1:x.s5, x.s2>x.s6?x.s2:x.s6, x.s3>x.s7?x.s3:x.s7}); }
inline double reduce_max(f64x2 x) { return x.s0 > x.s1 ? x.s0 : x.s1; }
inline double reduce_max(f64x4 x) { double a = x.s0 > x.s1 ? x.s0 : x.s1; double b = x.s2 > x.s3 ? x.s2 : x.s3; return a > b ? a : b; }
inline double reduce_max(f64x8 x) { return reduce_max(f64x4{x.s0>x.s4?x.s0:x.s4, x.s1>x.s5?x.s1:x.s5, x.s2>x.s6?x.s2:x.s6, x.s3>x.s7?x.s3:x.s7}); }
inline int    reduce_max(i32x2 x) { return x.s0 > x.s1 ? x.s0 : x.s1; }
inline int    reduce_max(i32x4 x) { int a = x.s0 > x.s1 ? x.s0 : x.s1; int b = x.s2 > x.s3 ? x.s2 : x.s3; return a > b ? a : b; }
inline long   reduce_max(i64x2 x) { return x.s0 > x.s1 ? x.s0 : x.s1; }
inline long   reduce_max(i64x4 x) { long a = x.s0 > x.s1 ? x.s0 : x.s1; long b = x.s2 > x.s3 ? x.s2 : x.s3; return a > b ? a : b; }

inline float  reduce_min(f32x2 x) { return x.s0 < x.s1 ? x.s0 : x.s1; }
inline float  reduce_min(f32x4 x) { float a = x.s0 < x.s1 ? x.s0 : x.s1; float b = x.s2 < x.s3 ? x.s2 : x.s3; return a < b ? a : b; }
inline float  reduce_min(f32x8 x) { return reduce_min(f32x4{x.s0<x.s4?x.s0:x.s4, x.s1<x.s5?x.s1:x.s5, x.s2<x.s6?x.s2:x.s6, x.s3<x.s7?x.s3:x.s7}); }
inline double reduce_min(f64x2 x) { return x.s0 < x.s1 ? x.s0 : x.s1; }
inline double reduce_min(f64x4 x) { double a = x.s0 < x.s1 ? x.s0 : x.s1; double b = x.s2 < x.s3 ? x.s2 : x.s3; return a < b ? a : b; }
inline double reduce_min(f64x8 x) { return reduce_min(f64x4{x.s0<x.s4?x.s0:x.s4, x.s1<x.s5?x.s1:x.s5, x.s2<x.s6?x.s2:x.s6, x.s3<x.s7?x.s3:x.s7}); }
inline int    reduce_min(i32x2 x) { return x.s0 < x.s1 ? x.s0 : x.s1; }
inline int    reduce_min(i32x4 x) { int a = x.s0 < x.s1 ? x.s0 : x.s1; int b = x.s2 < x.s3 ? x.s2 : x.s3; return a < b ? a : b; }
inline long   reduce_min(i64x2 x) { return x.s0 < x.s1 ? x.s0 : x.s1; }
inline long   reduce_min(i64x4 x) { long a = x.s0 < x.s1 ? x.s0 : x.s1; long b = x.s2 < x.s3 ? x.s2 : x.s3; return a < b ? a : b; }

inline float  reduce_add(f32x2 x) { return x.s0 + x.s1; }
inline float  reduce_add(f32x4 x) { return x.s0 + x.s1 + x.s2 + x.s3; }
inline float  reduce_add(f32x8 x) { return x.s0 + x.s1 + x.s2 + x.s3 + x.s4 + x.s5 + x.s6 + x.s7; }
inline double reduce_add(f64x2 x) { return x.s0 + x.s1; }
inline double reduce_add(f64x4 x) { return x.s0 + x.s1 + x.s2 + x.s3; }
inline double reduce_add(f64x8 x) { return x.s0 + x.s1 + x.s2 + x.s3 + x.s4 + x.s5 + x.s6 + x.s7; }
inline int    reduce_add(i32x2 x) { return x.s0 + x.s1; }
inline int    reduce_add(i32x4 x) { return x.s0 + x.s1 + x.s2 + x.s3; }
inline long   reduce_add(i64x2 x) { return x.s0 + x.s1; }
inline long   reduce_add(i64x4 x) { return x.s0 + x.s1 + x.s2 + x.s3; }

// --- element-wise max/min ---

template <typename T>
T max(T a, T b) { return a > b ? a : b; }

template <typename T>
T min(T a, T b) { return a < b ? a : b; }

// Scalar floor (passthrough to std::floor for scalar types used in templates)
inline float  floor(float x)  { return std::floor(x); }
inline double floor(double x) { return std::floor(x); }

} // namespace simd_compat

// Alias so existing `simd::func()` call sites work on non-Apple
namespace simd = simd_compat;

#endif // __APPLE__

#endif // tzpl_simd_hpp
