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
//  builtins_math.cpp -- Math, type conversion, string, and range builtins
//

#include "builtins_internal.hpp"
#include <cmath>
#include <complex>
#include <bit>
#include <algorithm>

namespace ts {

// ============================================================================
// Float -> Float unary functions (using <cmath>)
// ============================================================================

#define DEFINE_FLOAT_UNARY(fname, cppfun) \
    static void builtin_##fname##_float(VM& vm, u16 dst, u16, u16 argBase) { \
        vm.reg(dst).f = cppfun(vm.reg(argBase).f); \
    }

DEFINE_FLOAT_UNARY(sqrt, std::sqrt)
DEFINE_FLOAT_UNARY(cbrt, std::cbrt)
DEFINE_FLOAT_UNARY(floor, std::floor)
DEFINE_FLOAT_UNARY(ceil, std::ceil)
DEFINE_FLOAT_UNARY(round, std::round)
DEFINE_FLOAT_UNARY(trunc, std::trunc)
DEFINE_FLOAT_UNARY(log, std::log)
DEFINE_FLOAT_UNARY(log2, std::log2)
DEFINE_FLOAT_UNARY(log10, std::log10)
DEFINE_FLOAT_UNARY(log1p, std::log1p)
DEFINE_FLOAT_UNARY(exp, std::exp)
DEFINE_FLOAT_UNARY(exp2, std::exp2)
DEFINE_FLOAT_UNARY(expm1, std::expm1)
DEFINE_FLOAT_UNARY(sin, std::sin)
DEFINE_FLOAT_UNARY(cos, std::cos)
DEFINE_FLOAT_UNARY(tan, std::tan)
DEFINE_FLOAT_UNARY(asin, std::asin)
DEFINE_FLOAT_UNARY(acos, std::acos)
DEFINE_FLOAT_UNARY(atan, std::atan)
DEFINE_FLOAT_UNARY(sinh, std::sinh)
DEFINE_FLOAT_UNARY(cosh, std::cosh)
DEFINE_FLOAT_UNARY(tanh, std::tanh)
DEFINE_FLOAT_UNARY(asinh, std::asinh)
DEFINE_FLOAT_UNARY(acosh, std::acosh)
DEFINE_FLOAT_UNARY(atanh, std::atanh)
DEFINE_FLOAT_UNARY(erf, std::erf)
DEFINE_FLOAT_UNARY(erfc, std::erfc)
DEFINE_FLOAT_UNARY(tgamma, std::tgamma)
DEFINE_FLOAT_UNARY(lgamma, std::lgamma)

#undef DEFINE_FLOAT_UNARY

// ============================================================================
// Type conversion functions: toInt, toFloat, toFraction, toComplex
// ============================================================================

// --- toInt ---
static void builtin_toInt_int(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = vm.reg(argBase).i;
}

static void builtin_toInt_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = (i64)vm.reg(argBase).f;
}

static void builtin_toInt_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* fr = static_cast<Fraction*>(vm.reg(argBase).o);
    vm.reg(dst).i = (i64)(f64)fr->r;
}

// --- toFloat ---
static void builtin_toFloat_int(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).f = (f64)vm.reg(argBase).i;
}

static void builtin_toFloat_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).f = vm.reg(argBase).f;
}

static void builtin_toFloat_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* fr = static_cast<Fraction*>(vm.reg(argBase).o);
    vm.reg(dst).f = (f64)fr->r;
}

static void builtin_toFloat_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).f = z->x.real();
}

// --- toFraction ---
static void builtin_toFraction_int(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).o = new Fraction(r64(vm.reg(argBase).i));
}

static void builtin_toFraction_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).o = vm.reg(argBase).o;
}

// --- toComplex ---
static void builtin_toComplex_int(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).o = new Complex(x64((f64)vm.reg(argBase).i, 0.0));
}

static void builtin_toComplex_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).o = new Complex(x64(vm.reg(argBase).f, 0.0));
}

static void builtin_toComplex_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* fr = static_cast<Fraction*>(vm.reg(argBase).o);
    vm.reg(dst).o = new Complex(x64((f64)fr->r, 0.0));
}

static void builtin_toComplex_complex(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).o = vm.reg(argBase).o;
}

// frac(x) = x - floor(x)
static void builtin_frac_float(VM& vm, u16 dst, u16, u16 argBase) {
    f64 x = vm.reg(argBase).f;
    vm.reg(dst).f = x - std::floor(x);
}

// abs(Float)
static void builtin_abs_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).f = std::fabs(vm.reg(argBase).f);
}

// sinpi, cospi, tanpi
static void builtin_sinpi_float(VM& vm, u16 dst, u16, u16 argBase) {
#if defined(__APPLE__)
    vm.reg(dst).f = __sinpi(vm.reg(argBase).f);
#else
    vm.reg(dst).f = std::sin(vm.reg(argBase).f * M_PI);
#endif
}

static void builtin_cospi_float(VM& vm, u16 dst, u16, u16 argBase) {
#if defined(__APPLE__)
    vm.reg(dst).f = __cospi(vm.reg(argBase).f);
#else
    vm.reg(dst).f = std::cos(vm.reg(argBase).f * M_PI);
#endif
}

static void builtin_tanpi_float(VM& vm, u16 dst, u16, u16 argBase) {
#if defined(__APPLE__)
    vm.reg(dst).f = __tanpi(vm.reg(argBase).f);
#else
    vm.reg(dst).f = std::tan(vm.reg(argBase).f * M_PI);
#endif
}

// exp10(x)
static void builtin_exp10_float(VM& vm, u16 dst, u16, u16 argBase) {
#if defined(__APPLE__)
    vm.reg(dst).f = __exp10(vm.reg(argBase).f);
#else
    vm.reg(dst).f = std::pow(10.0, vm.reg(argBase).f);
#endif
}

// ============================================================================
// Float, Float -> Float binary functions
// ============================================================================

#define DEFINE_FLOAT_BINARY(fname, cppfun) \
    static void builtin_##fname##_float(VM& vm, u16 dst, u16, u16 argBase) { \
        vm.reg(dst).f = cppfun(vm.reg(argBase).f, vm.reg(argBase + 1).f); \
    }

DEFINE_FLOAT_BINARY(pow, std::pow)
DEFINE_FLOAT_BINARY(atan2, std::atan2)
DEFINE_FLOAT_BINARY(hypot, std::hypot)
DEFINE_FLOAT_BINARY(copysign, std::copysign)
DEFINE_FLOAT_BINARY(nextafter, std::nextafter)
DEFINE_FLOAT_BINARY(remainder, std::remainder)

#undef DEFINE_FLOAT_BINARY

// min/max for Float
static void builtin_min_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).f = std::fmin(vm.reg(argBase).f, vm.reg(argBase + 1).f);
}

static void builtin_max_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).f = std::fmax(vm.reg(argBase).f, vm.reg(argBase + 1).f);
}

// clamp for Float
static void builtin_clamp_float(VM& vm, u16 dst, u16, u16 argBase) {
    f64 x = vm.reg(argBase).f;
    f64 lo = vm.reg(argBase + 1).f;
    f64 hi = vm.reg(argBase + 2).f;
    vm.reg(dst).f = std::fmin(std::fmax(x, lo), hi);
}

// cmp for Float
static void builtin_cmp_float(VM& vm, u16 dst, u16, u16 argBase) {
    f64 a = vm.reg(argBase).f, b = vm.reg(argBase + 1).f;
    vm.reg(dst).i = (a > b) - (a < b);
}

// sign for Float
static void builtin_sign_float(VM& vm, u16 dst, u16, u16 argBase) {
    f64 x = vm.reg(argBase).f;
    vm.reg(dst).i = (x > 0.0) - (x < 0.0);
}

// ============================================================================
// Float -> Bool predicates
// ============================================================================

static void builtin_isNan_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = std::isnan(vm.reg(argBase).f) ? 1 : 0;
}

static void builtin_isInf_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = std::isinf(vm.reg(argBase).f) ? 1 : 0;
}

static void builtin_isFinite_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = std::isfinite(vm.reg(argBase).f) ? 1 : 0;
}

static void builtin_isNormal_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = std::isnormal(vm.reg(argBase).f) ? 1 : 0;
}

// ============================================================================
// Integer functions
// ============================================================================

// abs(Int)
static void builtin_abs_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 x = vm.reg(argBase).i;
    vm.reg(dst).i = x < 0 ? -x : x;
}

// sign(Int)
static void builtin_sign_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 x = vm.reg(argBase).i;
    vm.reg(dst).i = (x > 0) - (x < 0);
}

// min(Int, Int)
static void builtin_min_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 a = vm.reg(argBase).i, b = vm.reg(argBase + 1).i;
    vm.reg(dst).i = a < b ? a : b;
}

// max(Int, Int)
static void builtin_max_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 a = vm.reg(argBase).i, b = vm.reg(argBase + 1).i;
    vm.reg(dst).i = a > b ? a : b;
}

// clamp(Int, Int, Int)
static void builtin_clamp_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 x = vm.reg(argBase).i;
    i64 lo = vm.reg(argBase + 1).i;
    i64 hi = vm.reg(argBase + 2).i;
    vm.reg(dst).i = x < lo ? lo : (x > hi ? hi : x);
}

// cmp(Int, Int)
static void builtin_cmp_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 a = vm.reg(argBase).i, b = vm.reg(argBase + 1).i;
    vm.reg(dst).i = (a > b) - (a < b);
}

// gcd(Int, Int) -> Int
static void builtin_gcd_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 a = vm.reg(argBase).i, b = vm.reg(argBase + 1).i;
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { i64 t = b; b = a % b; a = t; }
    vm.reg(dst).i = a;
}

// lcm(Int, Int) -> Int
static void builtin_lcm_int(VM& vm, u16 dst, u16, u16 argBase) {
    i64 a = vm.reg(argBase).i, b = vm.reg(argBase + 1).i;
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    if (a == 0 || b == 0) { vm.reg(dst).i = 0; return; }
    // Compute gcd first, then lcm = a / gcd * b to avoid overflow
    i64 ga = a, gb = b;
    while (gb) { i64 t = gb; gb = ga % gb; ga = t; }
    vm.reg(dst).i = a / ga * b;
}

// ============================================================================
// Integer bit manipulation functions
// ============================================================================

static void builtin_clz_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = x == 0 ? 64 : std::countl_zero(x);
}

static void builtin_clo_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = std::countl_one(x);
}

static void builtin_ctz_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = x == 0 ? 64 : std::countr_zero(x);
}

static void builtin_cto_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = std::countr_one(x);
}

static void builtin_popCount_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = std::popcount(x);
}

static void builtin_rotl_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    int s = (int)vm.reg(argBase + 1).i;
    vm.reg(dst).i = (i64)std::rotl(x, s);
}

static void builtin_rotr_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    int s = (int)vm.reg(argBase + 1).i;
    vm.reg(dst).i = (i64)std::rotr(x, s);
}

static void builtin_bitCeil_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = (i64)std::bit_ceil(x);
}

static void builtin_bitFloor_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = (i64)std::bit_floor(x);
}

static void builtin_bitWidth_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = (i64)std::bit_width(x);
}

static void builtin_hasSingleBit_int(VM& vm, u16 dst, u16, u16 argBase) {
    u64 x = (u64)vm.reg(argBase).i;
    vm.reg(dst).i = std::has_single_bit(x) ? 1 : 0;
}

// ============================================================================
// Fraction functions
// ============================================================================

// abs(Fraction)
static void builtin_abs_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* fr = static_cast<Fraction*>(vm.reg(argBase).o);
    auto* result = new Fraction(fr->r.abs());
    vm.reg(dst).o = result;
}

// min(Fraction, Fraction)
static void builtin_min_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* a = static_cast<Fraction*>(vm.reg(argBase).o);
    auto* b = static_cast<Fraction*>(vm.reg(argBase + 1).o);
    vm.reg(dst).o = a->r < b->r ? vm.reg(argBase).o : vm.reg(argBase + 1).o;
}

// max(Fraction, Fraction)
static void builtin_max_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* a = static_cast<Fraction*>(vm.reg(argBase).o);
    auto* b = static_cast<Fraction*>(vm.reg(argBase + 1).o);
    vm.reg(dst).o = a->r > b->r ? vm.reg(argBase).o : vm.reg(argBase + 1).o;
}

// clamp(Fraction, Fraction, Fraction)
static void builtin_clamp_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* x = static_cast<Fraction*>(vm.reg(argBase).o);
    auto* lo = static_cast<Fraction*>(vm.reg(argBase + 1).o);
    auto* hi = static_cast<Fraction*>(vm.reg(argBase + 2).o);
    if (x->r < lo->r)
        vm.reg(dst).o = vm.reg(argBase + 1).o;
    else if (x->r > hi->r)
        vm.reg(dst).o = vm.reg(argBase + 2).o;
    else
        vm.reg(dst).o = vm.reg(argBase).o;
}

// cmp(Fraction, Fraction)
static void builtin_cmp_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* a = static_cast<Fraction*>(vm.reg(argBase).o);
    auto* b = static_cast<Fraction*>(vm.reg(argBase + 1).o);
    vm.reg(dst).i = (a->r > b->r) - (a->r < b->r);
}

// sign(Fraction)
static void builtin_sign_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* fr = static_cast<Fraction*>(vm.reg(argBase).o);
    i64 n = fr->r.numer();
    vm.reg(dst).i = (n > 0) - (n < 0);
}

// numer(Fraction) -> Int
static void builtin_numer_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* fr = static_cast<Fraction*>(vm.reg(argBase).o);
    vm.reg(dst).i = fr->r.numer();
}

// denom(Fraction) -> Int
static void builtin_denom_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* fr = static_cast<Fraction*>(vm.reg(argBase).o);
    vm.reg(dst).i = fr->r.denom();
}

// ============================================================================
// Complex functions
// ============================================================================

// abs(Complex) -> Float
static void builtin_abs_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).f = std::abs(z->x);
}

// sqrt(Complex) -> Complex
static void builtin_sqrt_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).o = new Complex(std::sqrt(z->x));
}

// real(Complex) -> Float
static void builtin_real_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).f = z->x.real();
}

// imag(Complex) -> Float
static void builtin_imag_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).f = z->x.imag();
}

// arg(Complex) -> Float
static void builtin_arg_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).f = std::arg(z->x);
}

// norm(Complex) -> Float
static void builtin_norm_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).f = std::norm(z->x);
}

// conj(Complex) -> Complex
static void builtin_conj_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* z = static_cast<Complex*>(vm.reg(argBase).o);
    vm.reg(dst).o = new Complex(std::conj(z->x));
}

// polar(Float, Float) -> Complex
static void builtin_polar_float(VM& vm, u16 dst, u16, u16 argBase) {
    f64 r = vm.reg(argBase).f;
    f64 theta = vm.reg(argBase + 1).f;
    vm.reg(dst).o = new Complex(std::polar(r, theta));
}

// Complex unary functions that return Complex
#define DEFINE_COMPLEX_UNARY(fname, cppfun) \
    static void builtin_##fname##_complex(VM& vm, u16 dst, u16, u16 argBase) { \
        auto* z = static_cast<Complex*>(vm.reg(argBase).o); \
        vm.reg(dst).o = new Complex(cppfun(z->x)); \
    }

DEFINE_COMPLEX_UNARY(log, std::log)
DEFINE_COMPLEX_UNARY(exp, std::exp)
DEFINE_COMPLEX_UNARY(sin, std::sin)
DEFINE_COMPLEX_UNARY(cos, std::cos)
DEFINE_COMPLEX_UNARY(tan, std::tan)
DEFINE_COMPLEX_UNARY(asin, std::asin)
DEFINE_COMPLEX_UNARY(acos, std::acos)
DEFINE_COMPLEX_UNARY(atan, std::atan)
DEFINE_COMPLEX_UNARY(sinh, std::sinh)
DEFINE_COMPLEX_UNARY(cosh, std::cosh)
DEFINE_COMPLEX_UNARY(tanh, std::tanh)
DEFINE_COMPLEX_UNARY(asinh, std::asinh)
DEFINE_COMPLEX_UNARY(acosh, std::acosh)
DEFINE_COMPLEX_UNARY(atanh, std::atanh)

#undef DEFINE_COMPLEX_UNARY

// pow(Complex, Complex) -> Complex
static void builtin_pow_complex(VM& vm, u16 dst, u16, u16 argBase) {
    auto* a = static_cast<Complex*>(vm.reg(argBase).o);
    auto* b = static_cast<Complex*>(vm.reg(argBase + 1).o);
    vm.reg(dst).o = new Complex(std::pow(a->x, b->x));
}

// ============================================================================
// String functions
// ============================================================================

// length(String) -> Int
static void builtin_length_string(VM& vm, u16 dst, u16, u16 argBase) {
    auto* s = static_cast<StringObj*>(vm.reg(argBase).o);
    vm.reg(dst).i = (i64)s->s.size();
}

// min(String, String) -> String
static void builtin_min_string(VM& vm, u16 dst, u16, u16 argBase) {
    auto* a = static_cast<StringObj*>(vm.reg(argBase).o);
    auto* b = static_cast<StringObj*>(vm.reg(argBase + 1).o);
    vm.reg(dst).o = a->s <= b->s ? vm.reg(argBase).o : vm.reg(argBase + 1).o;
}

// max(String, String) -> String
static void builtin_max_string(VM& vm, u16 dst, u16, u16 argBase) {
    auto* a = static_cast<StringObj*>(vm.reg(argBase).o);
    auto* b = static_cast<StringObj*>(vm.reg(argBase + 1).o);
    vm.reg(dst).o = a->s >= b->s ? vm.reg(argBase).o : vm.reg(argBase + 1).o;
}

// cmp(String, String) -> Int
static void builtin_cmp_string(VM& vm, u16 dst, u16, u16 argBase) {
    auto* a = static_cast<StringObj*>(vm.reg(argBase).o);
    auto* b = static_cast<StringObj*>(vm.reg(argBase + 1).o);
    int c = a->s.compare(b->s);
    vm.reg(dst).i = (c > 0) - (c < 0);
}

// substring(String, Int, Int) -> String
static void builtin_substring_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    i64 start = vm.reg(ab + 1).i;
    i64 len = vm.reg(ab + 2).i;
    auto* result = new StringObj();
    result->s = s->s.substr((size_t)start, (size_t)len);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// contains(String, String) -> Bool
static void builtin_contains_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* sub = static_cast<StringObj*>(vm.reg(ab + 1).o);
    vm.reg(dst).i = s->s.find(sub->s) != VMString::npos ? 1 : 0;
}

// startsWith(String, String) -> Bool
static void builtin_startsWith_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* prefix = static_cast<StringObj*>(vm.reg(ab + 1).o);
    vm.reg(dst).i = (s->s.size() >= prefix->s.size() &&
                     s->s.compare(0, prefix->s.size(), prefix->s) == 0) ? 1 : 0;
}

// endsWith(String, String) -> Bool
static void builtin_endsWith_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* suffix = static_cast<StringObj*>(vm.reg(ab + 1).o);
    vm.reg(dst).i = (s->s.size() >= suffix->s.size() &&
                     s->s.compare(s->s.size() - suffix->s.size(), suffix->s.size(), suffix->s) == 0) ? 1 : 0;
}

// split(String, String) -> Array[String]
static void builtin_split_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* delim = static_cast<StringObj*>(vm.reg(ab + 1).o);
    auto* arrType = vm.arrayType(vm.stringType());
    auto* arr = new ObjArray(arrType);
    const VMString& str = s->s;
    const VMString& d = delim->s;
    if (d.empty()) {
        // Split into individual bytes
        for (size_t i = 0; i < str.size(); ++i) {
            auto* elem = new StringObj();
            elem->s = VMString(1, str[i], str.get_allocator());
            registerNewObj(elem);
            arr->push(elem);
        }
    } else {
        size_t start = 0;
        size_t pos;
        while ((pos = str.find(d, start)) != VMString::npos) {
            auto* elem = new StringObj();
            elem->s = str.substr(start, pos - start);
            registerNewObj(elem);
            arr->push(elem);
            start = pos + d.size();
        }
        auto* elem = new StringObj();
        elem->s = str.substr(start);
        registerNewObj(elem);
        arr->push(elem);
    }
    vm.reg(dst).o = arr;
}

// trim(String) -> String
static void builtin_trim_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    const VMString& str = s->s;
    size_t start = 0;
    while (start < str.size() && std::isspace((unsigned char)str[start])) ++start;
    size_t end = str.size();
    while (end > start && std::isspace((unsigned char)str[end - 1])) --end;
    auto* result = new StringObj();
    result->s = str.substr(start, end - start);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// toUpper(String) -> String
static void builtin_toUpper_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* result = new StringObj();
    result->s = s->s;
    for (auto& c : result->s) c = (char)std::toupper((unsigned char)c);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// toLower(String) -> String
static void builtin_toLower_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* result = new StringObj();
    result->s = s->s;
    for (auto& c : result->s) c = (char)std::tolower((unsigned char)c);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// reverse(String) -> String  (UTF-8 aware)
static void builtin_reverse_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* result = new StringObj();
    const auto& src = s->s;
    size_t len = src.size();
    result->s.resize(len);
    // Walk forward collecting UTF-8 codepoint boundaries, copy in reverse order
    size_t out = 0;
    size_t i = len;
    while (i > 0) {
        size_t start = i - 1;
        while (start > 0 && ((unsigned char)src[start] & 0xC0) == 0x80) --start;
        size_t cpLen = i - start;
        for (size_t j = 0; j < cpLen; ++j) result->s[out + j] = src[start + j];
        out += cpLen;
        i = start;
    }
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// replace(String, String, String) -> String
static void builtin_replace_string(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    auto* from = static_cast<StringObj*>(vm.reg(ab + 1).o);
    auto* to = static_cast<StringObj*>(vm.reg(ab + 2).o);
    auto* result = new StringObj();
    result->s = s->s;
    if (!from->s.empty()) {
        size_t pos = 0;
        while ((pos = result->s.find(from->s, pos)) != VMString::npos) {
            result->s.replace(pos, from->s.size(), to->s);
            pos += to->s.size();
        }
    }
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// ============================================================================
// Range functions
// ============================================================================

// toArray(Range[Int]) -> Array[Int]
static void builtin_toArray_range_int(VM& vm, u16 dst, u16, u16 argBase) {
    auto* range = static_cast<RangeObj*>(vm.reg(argBase).o);
    auto* rangeType = static_cast<RangeType*>(range->type_);
    auto* arrayType = vm.arrayType(rangeType->elemType_);

    i64 start = range->start_.i;
    i64 step = range->step_.i;
    i64 end = range->end_.i;

    auto* arr = new PodArray<i64>(arrayType);
    if (step > 0) {
        for (i64 i = start; i <= end; i += step) {
            arr->v.push_back(i);
        }
    } else if (step < 0) {
        for (i64 i = start; i >= end; i += step) {
            arr->v.push_back(i);
        }
    }
    vm.reg(dst).o = arr;
}

// toList(Range[Int]) -> List[Int]  (lazy)
static void builtin_toList_range_int(VM& vm, u16 dst, u16, u16 argBase) {
    auto* range = static_cast<RangeObj*>(vm.reg(argBase).o);
    auto* rangeType = static_cast<RangeType*>(range->type_);
    auto* listType = vm.listType(rangeType->elemType_);

    i64 start = range->start_.i;
    i64 step = range->step_.i;
    i64 end = range->end_.i;

    // Check for empty range (direction mismatch)
    if (!range->isInfinite_) {
        if (step == 0 ||
            (step > 0 && start > end) ||
            (step < 0 && start < end)) {
            vm.reg(dst).o = nullptr;  // nil list
            return;
        }
    }

    // Create a lazy list node with a RangeListGen
    auto* node = new ListNode(listType);
    auto* gen = new RangeListGen(vm.typeType());
    gen->current_ = start;
    gen->end_ = end;
    gen->step_ = step;
    gen->isInfinite_ = range->isInfinite_;
    gen->listType_ = listType;
    node->generator_ = gen;
    reinterpret_cast<GCObj*>(gen)->retain();
    vm.reg(dst).o = node;
}

// length(Range[Int]) -> Int
static void builtin_length_range_int(VM& vm, u16 dst, u16, u16 argBase) {
    auto* range = static_cast<RangeObj*>(vm.reg(argBase).o);
    i64 start = range->start_.i;
    i64 step = range->step_.i;
    i64 end = range->end_.i;

    if (range->isInfinite_) {
        vm.reg(dst).i = -1;  // sentinel for infinite
        return;
    }

    if (step == 0) {
        vm.reg(dst).i = 0;
        return;
    }

    i64 diff = end - start;
    if ((step > 0 && diff < 0) || (step < 0 && diff > 0)) {
        vm.reg(dst).i = 0;  // empty range (direction mismatch)
    } else {
        vm.reg(dst).i = diff / step + 1;
    }
}

// toArray(Range[Fraction]) -> Array[Fraction]
static void builtin_toArray_range_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* range = static_cast<RangeObj*>(vm.reg(argBase).o);
    auto* rangeType = static_cast<RangeType*>(range->type_);
    auto* arrayType = vm.arrayType(rangeType->elemType_);

    r64 start = static_cast<Fraction*>(range->start_.o)->r;
    r64 step = static_cast<Fraction*>(range->step_.o)->r;
    r64 end = range->isInfinite_ ? r64(0) : static_cast<Fraction*>(range->end_.o)->r;

    auto* arr = new ObjArray(arrayType);
    if (step > r64(0)) {
        for (r64 i = start; i <= end; i += step) {
            arr->push(new Fraction(i));
        }
    } else if (step < r64(0)) {
        for (r64 i = start; i >= end; i += step) {
            arr->push(new Fraction(i));
        }
    }
    vm.reg(dst).o = arr;
}

// toList(Range[Fraction]) -> List[Fraction]  (lazy)
static void builtin_toList_range_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* range = static_cast<RangeObj*>(vm.reg(argBase).o);
    auto* rangeType = static_cast<RangeType*>(range->type_);
    auto* listType = vm.listType(rangeType->elemType_);

    auto* startFrac = static_cast<Fraction*>(range->start_.o);
    auto* stepFrac = static_cast<Fraction*>(range->step_.o);
    auto* endFrac = range->isInfinite_ ? nullptr : static_cast<Fraction*>(range->end_.o);

    // Check for empty range (direction mismatch)
    if (!range->isInfinite_) {
        r64 stp = stepFrac->r;
        r64 s = startFrac->r;
        r64 e = endFrac->r;
        if (stp == r64(0) ||
            (stp > r64(0) && s > e) ||
            (stp < r64(0) && s < e)) {
            vm.reg(dst).o = nullptr;  // nil list
            return;
        }
    }

    // Create a lazy list node with a FractionRangeListGen
    auto* node = new ListNode(listType);
    auto* gen = new FractionRangeListGen(vm.typeType());
    gen->current_ = startFrac;
    gen->end_ = endFrac;
    gen->step_ = stepFrac;
    gen->isInfinite_ = range->isInfinite_;
    gen->listType_ = listType;
    node->generator_ = gen;
    // Retain Obj* fields stored in the generator
    if (gen->current_) gen->current_->retain();
    if (gen->end_) gen->end_->retain();
    if (gen->step_) gen->step_->retain();
    reinterpret_cast<GCObj*>(gen)->retain();
    vm.reg(dst).o = node;
}

// length(Range[Fraction]) -> Int
static void builtin_length_range_fraction(VM& vm, u16 dst, u16, u16 argBase) {
    auto* range = static_cast<RangeObj*>(vm.reg(argBase).o);

    if (range->isInfinite_) {
        vm.reg(dst).i = -1;  // sentinel for infinite
        return;
    }

    r64 start = static_cast<Fraction*>(range->start_.o)->r;
    r64 step = static_cast<Fraction*>(range->step_.o)->r;
    r64 end = static_cast<Fraction*>(range->end_.o)->r;

    if (step == r64(0)) {
        vm.reg(dst).i = 0;
        return;
    }

    r64 diff = end - start;
    if ((step > r64(0) && diff < r64(0)) || (step < r64(0) && diff > r64(0))) {
        vm.reg(dst).i = 0;  // empty range (direction mismatch)
    } else {
        // For rational ranges: length = floor(diff / step) + 1
        r64 q = diff / step;
        vm.reg(dst).i = q.numer() / q.denom() + 1;
    }
}

// ============================================================================
// Registration
// ============================================================================

void registerMathBuiltins(Compiler& compiler, FuncMap& functions)
{
    Type* Int = compiler.intType();
    Type* Float = compiler.floatType();
    Type* Bool = compiler.boolType();
    Type* Str = compiler.stringType();
    Type* Frac = compiler.fractionType();
    Type* Cmplx = compiler.complexType();

    // --- abs ---
    registerOne(compiler, functions, "abs", Int,   {Int},   builtin_abs_int);
    registerOne(compiler, functions, "abs", Float, {Float}, builtin_abs_float);
    registerOne(compiler, functions, "abs", Frac,  {Frac},  builtin_abs_fraction);
    registerOne(compiler, functions, "abs", Float, {Cmplx}, builtin_abs_complex);

    // --- min ---
    registerOne(compiler, functions, "min", Int,   {Int, Int},     builtin_min_int);
    registerOne(compiler, functions, "min", Float, {Float, Float}, builtin_min_float);
    registerOne(compiler, functions, "min", Frac,  {Frac, Frac},   builtin_min_fraction);
    registerOne(compiler, functions, "min", Str,   {Str, Str},     builtin_min_string);

    // --- max ---
    registerOne(compiler, functions, "max", Int,   {Int, Int},     builtin_max_int);
    registerOne(compiler, functions, "max", Float, {Float, Float}, builtin_max_float);
    registerOne(compiler, functions, "max", Frac,  {Frac, Frac},   builtin_max_fraction);
    registerOne(compiler, functions, "max", Str,   {Str, Str},     builtin_max_string);

    // --- clamp ---
    registerOne(compiler, functions, "clamp", Int,   {Int, Int, Int},       builtin_clamp_int);
    registerOne(compiler, functions, "clamp", Float, {Float, Float, Float}, builtin_clamp_float);
    registerOne(compiler, functions, "clamp", Frac,  {Frac, Frac, Frac},    builtin_clamp_fraction);

    // --- gcd, lcm ---
    registerOne(compiler, functions, "gcd", Int, {Int, Int}, builtin_gcd_int);
    registerOne(compiler, functions, "lcm", Int, {Int, Int}, builtin_lcm_int);

    // --- cmp ---
    registerOne(compiler, functions, "cmp", Int, {Int, Int},     builtin_cmp_int);
    registerOne(compiler, functions, "cmp", Int, {Float, Float}, builtin_cmp_float);
    registerOne(compiler, functions, "cmp", Int, {Frac, Frac},   builtin_cmp_fraction);
    registerOne(compiler, functions, "cmp", Int, {Str, Str},     builtin_cmp_string);

    // --- sign ---
    registerOne(compiler, functions, "sign", Int, {Int},   builtin_sign_int);
    registerOne(compiler, functions, "sign", Int, {Float}, builtin_sign_float);
    registerOne(compiler, functions, "sign", Int, {Frac},  builtin_sign_fraction);

    // --- sqrt ---
    registerOne(compiler, functions, "sqrt", Float, {Float}, builtin_sqrt_float);
    registerOne(compiler, functions, "sqrt", Cmplx, {Cmplx}, builtin_sqrt_complex);

    // --- cbrt ---
    registerOne(compiler, functions, "cbrt", Float, {Float}, builtin_cbrt_float);

    // --- floor, ceil, round, trunc ---
    registerOne(compiler, functions, "floor", Float, {Float}, builtin_floor_float);
    registerOne(compiler, functions, "ceil",  Float, {Float}, builtin_ceil_float);
    registerOne(compiler, functions, "round", Float, {Float}, builtin_round_float);
    registerOne(compiler, functions, "trunc", Float, {Float}, builtin_trunc_float);

    // --- frac ---
    registerOne(compiler, functions, "frac", Float, {Float}, builtin_frac_float);

    // --- log family ---
    registerOne(compiler, functions, "log",   Float, {Float}, builtin_log_float);
    registerOne(compiler, functions, "log",   Cmplx, {Cmplx}, builtin_log_complex);
    registerOne(compiler, functions, "log2",  Float, {Float}, builtin_log2_float);
    registerOne(compiler, functions, "log10", Float, {Float}, builtin_log10_float);
    registerOne(compiler, functions, "log1p", Float, {Float}, builtin_log1p_float);

    // --- exp family ---
    registerOne(compiler, functions, "exp",   Float, {Float}, builtin_exp_float);
    registerOne(compiler, functions, "exp",   Cmplx, {Cmplx}, builtin_exp_complex);
    registerOne(compiler, functions, "exp2",  Float, {Float}, builtin_exp2_float);
    registerOne(compiler, functions, "expm1", Float, {Float}, builtin_expm1_float);
    registerOne(compiler, functions, "exp10", Float, {Float}, builtin_exp10_float);

    // --- trig ---
    registerOne(compiler, functions, "sin", Float, {Float}, builtin_sin_float);
    registerOne(compiler, functions, "sin", Cmplx, {Cmplx}, builtin_sin_complex);
    registerOne(compiler, functions, "cos", Float, {Float}, builtin_cos_float);
    registerOne(compiler, functions, "cos", Cmplx, {Cmplx}, builtin_cos_complex);
    registerOne(compiler, functions, "tan", Float, {Float}, builtin_tan_float);
    registerOne(compiler, functions, "tan", Cmplx, {Cmplx}, builtin_tan_complex);

    // --- inverse trig ---
    registerOne(compiler, functions, "asin", Float, {Float}, builtin_asin_float);
    registerOne(compiler, functions, "asin", Cmplx, {Cmplx}, builtin_asin_complex);
    registerOne(compiler, functions, "acos", Float, {Float}, builtin_acos_float);
    registerOne(compiler, functions, "acos", Cmplx, {Cmplx}, builtin_acos_complex);
    registerOne(compiler, functions, "atan", Float, {Float}, builtin_atan_float);
    registerOne(compiler, functions, "atan", Cmplx, {Cmplx}, builtin_atan_complex);

    // --- hyperbolic ---
    registerOne(compiler, functions, "sinh", Float, {Float}, builtin_sinh_float);
    registerOne(compiler, functions, "sinh", Cmplx, {Cmplx}, builtin_sinh_complex);
    registerOne(compiler, functions, "cosh", Float, {Float}, builtin_cosh_float);
    registerOne(compiler, functions, "cosh", Cmplx, {Cmplx}, builtin_cosh_complex);
    registerOne(compiler, functions, "tanh", Float, {Float}, builtin_tanh_float);
    registerOne(compiler, functions, "tanh", Cmplx, {Cmplx}, builtin_tanh_complex);

    // --- inverse hyperbolic ---
    registerOne(compiler, functions, "asinh", Float, {Float}, builtin_asinh_float);
    registerOne(compiler, functions, "asinh", Cmplx, {Cmplx}, builtin_asinh_complex);
    registerOne(compiler, functions, "acosh", Float, {Float}, builtin_acosh_float);
    registerOne(compiler, functions, "acosh", Cmplx, {Cmplx}, builtin_acosh_complex);
    registerOne(compiler, functions, "atanh", Float, {Float}, builtin_atanh_float);
    registerOne(compiler, functions, "atanh", Cmplx, {Cmplx}, builtin_atanh_complex);

    // --- sinpi, cospi, tanpi ---
    registerOne(compiler, functions, "sinpi", Float, {Float}, builtin_sinpi_float);
    registerOne(compiler, functions, "cospi", Float, {Float}, builtin_cospi_float);
    registerOne(compiler, functions, "tanpi", Float, {Float}, builtin_tanpi_float);

    // --- error functions ---
    registerOne(compiler, functions, "erf",  Float, {Float}, builtin_erf_float);
    registerOne(compiler, functions, "erfc", Float, {Float}, builtin_erfc_float);

    // --- gamma ---
    registerOne(compiler, functions, "tgamma", Float, {Float}, builtin_tgamma_float);
    registerOne(compiler, functions, "lgamma", Float, {Float}, builtin_lgamma_float);

    // --- two-arg float ---
    registerOne(compiler, functions, "copysign",  Float, {Float, Float}, builtin_copysign_float);
    registerOne(compiler, functions, "nextafter", Float, {Float, Float}, builtin_nextafter_float);
    registerOne(compiler, functions, "pow",       Float, {Float, Float}, builtin_pow_float);
    registerOne(compiler, functions, "pow",       Cmplx, {Cmplx, Cmplx}, builtin_pow_complex);
    registerOne(compiler, functions, "atan2",     Float, {Float, Float}, builtin_atan2_float);
    registerOne(compiler, functions, "hypot",     Float, {Float, Float}, builtin_hypot_float);
    registerOne(compiler, functions, "remainder", Float, {Float, Float}, builtin_remainder_float);

    // --- float predicates ---
    registerOne(compiler, functions, "isNan",    Bool, {Float}, builtin_isNan_float);
    registerOne(compiler, functions, "isInf",    Bool, {Float}, builtin_isInf_float);
    registerOne(compiler, functions, "isFinite", Bool, {Float}, builtin_isFinite_float);
    registerOne(compiler, functions, "isNormal", Bool, {Float}, builtin_isNormal_float);

    // --- integer bit ops ---
    registerOne(compiler, functions, "clz", Int, {Int}, builtin_clz_int);
    registerOne(compiler, functions, "clo", Int, {Int}, builtin_clo_int);
    registerOne(compiler, functions, "ctz", Int, {Int}, builtin_ctz_int);
    registerOne(compiler, functions, "cto", Int, {Int}, builtin_cto_int);
    registerOne(compiler, functions, "popCount", Int, {Int}, builtin_popCount_int);
    registerOne(compiler, functions, "rotl",  Int, {Int, Int}, builtin_rotl_int);
    registerOne(compiler, functions, "rotr",  Int, {Int, Int}, builtin_rotr_int);
    registerOne(compiler, functions, "bitCeil",  Int, {Int}, builtin_bitCeil_int);
    registerOne(compiler, functions, "bitFloor", Int, {Int}, builtin_bitFloor_int);
    registerOne(compiler, functions, "bitWidth", Int, {Int}, builtin_bitWidth_int);
    registerOne(compiler, functions, "hasSingleBit", Bool, {Int}, builtin_hasSingleBit_int);

    // --- Range functions ---
    Type* RangeInt = compiler.rangeType(Int);
    ArrayType* ArrayInt = compiler.arrayType(Int);
    registerOne(compiler, functions, "toArray", ArrayInt, {RangeInt}, builtin_toArray_range_int);
    ListType* ListInt = compiler.listType(Int);
    registerOne(compiler, functions, "toList",  ListInt,  {RangeInt}, builtin_toList_range_int);
    registerOne(compiler, functions, "length",  Int,      {RangeInt}, builtin_length_range_int);

    Type* RangeFrac = compiler.rangeType(Frac);
    ArrayType* ArrayFrac = compiler.arrayType(Frac);
    registerOne(compiler, functions, "toArray", ArrayFrac, {RangeFrac}, builtin_toArray_range_fraction);
    ListType* ListFrac = compiler.listType(Frac);
    registerOne(compiler, functions, "toList",  ListFrac,  {RangeFrac}, builtin_toList_range_fraction);
    registerOne(compiler, functions, "length",  Int,       {RangeFrac}, builtin_length_range_fraction);

    // --- String functions ---
    registerOne(compiler, functions, "length",     Int,      {Str},           builtin_length_string);
    registerOne(compiler, functions, "substring",  Str,      {Str, Int, Int}, builtin_substring_string);
    registerOne(compiler, functions, "contains",   Bool,     {Str, Str},      builtin_contains_string);
    registerOne(compiler, functions, "startsWith", Bool,     {Str, Str},      builtin_startsWith_string);
    registerOne(compiler, functions, "endsWith",   Bool,     {Str, Str},      builtin_endsWith_string);
    ArrayType* ArrayStr = compiler.arrayType(Str);
    registerOne(compiler, functions, "split",      ArrayStr, {Str, Str},      builtin_split_string);
    registerOne(compiler, functions, "trim",       Str,      {Str},           builtin_trim_string);
    registerOne(compiler, functions, "reverse",    Str,      {Str},           builtin_reverse_string);
    registerOne(compiler, functions, "toUpper",    Str,      {Str},           builtin_toUpper_string);
    registerOne(compiler, functions, "toLower",    Str,      {Str},           builtin_toLower_string);
    registerOne(compiler, functions, "replace",    Str,      {Str, Str, Str}, builtin_replace_string);

    // --- fraction accessors ---
    registerOne(compiler, functions, "numer", Int, {Frac}, builtin_numer_fraction);
    registerOne(compiler, functions, "denom", Int, {Frac}, builtin_denom_fraction);

    // --- complex accessors ---
    registerOne(compiler, functions, "real", Float, {Cmplx}, builtin_real_complex);
    registerOne(compiler, functions, "imag", Float, {Cmplx}, builtin_imag_complex);
    registerOne(compiler, functions, "arg",  Float, {Cmplx}, builtin_arg_complex);
    registerOne(compiler, functions, "norm", Float, {Cmplx}, builtin_norm_complex);
    registerOne(compiler, functions, "conj", Cmplx, {Cmplx}, builtin_conj_complex);
    registerOne(compiler, functions, "polar", Cmplx, {Float, Float}, builtin_polar_float);

    // --- type conversions ---
    registerOne(compiler, functions, "toInt", Int, {Int},   builtin_toInt_int);
    registerOne(compiler, functions, "toInt", Int, {Float}, builtin_toInt_float);
    registerOne(compiler, functions, "toInt", Int, {Frac},  builtin_toInt_fraction);

    registerOne(compiler, functions, "toFloat", Float, {Int},   builtin_toFloat_int);
    registerOne(compiler, functions, "toFloat", Float, {Float}, builtin_toFloat_float);
    registerOne(compiler, functions, "toFloat", Float, {Frac},  builtin_toFloat_fraction);
    registerOne(compiler, functions, "toFloat", Float, {Cmplx}, builtin_toFloat_complex);

    registerOne(compiler, functions, "toFraction", Frac, {Int},  builtin_toFraction_int);
    registerOne(compiler, functions, "toFraction", Frac, {Frac}, builtin_toFraction_fraction);

    registerOne(compiler, functions, "toComplex", Cmplx, {Int},   builtin_toComplex_int);
    registerOne(compiler, functions, "toComplex", Cmplx, {Float}, builtin_toComplex_float);
    registerOne(compiler, functions, "toComplex", Cmplx, {Frac},  builtin_toComplex_fraction);
    registerOne(compiler, functions, "toComplex", Cmplx, {Cmplx}, builtin_toComplex_complex);
}

} // namespace ts
