//
//  builtins.cpp
//  static-lang-3
//
//  Built-in math function implementations and registration
//

#include "builtins.hpp"
#include "compiler.hpp"
#include "value.hpp"
#include "opcodes.hpp"
#include <cmath>
#include <complex>
#include <bit>
#include <algorithm>
#include <numeric>

namespace ts {

// ============================================================================
// Helper: register one builtin function overload
// ============================================================================

static void registerOne(Compiler& compiler,
    std::unordered_map<std::string, std::vector<FuncInfo>>& functions,
    const std::string& name, Type* returnType,
    std::vector<Type*> paramTypes, CFun cfun,
    bool pure = true, bool rtSafe = true)
{
    u32 idx = compiler.addGlobal(true);
    auto* prim = new Primitive(compiler.voidType());
    prim->cfun_ = cfun;
    prim->pure_ = pure;
    prim->rtSafe_ = rtSafe;
    compiler.global(idx).o = prim;

    FuncInfo info;
    info.returnType = returnType;
    info.paramTypes = std::move(paramTypes);
    info.globalIndex = idx;
    info.bodyChecked = true;
    info.isBuiltin = true;
    info.rtSafe = rtSafe;
    functions[name].push_back(info);
}

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
            arr->v.push_back(elem);
        }
    } else {
        size_t start = 0;
        size_t pos;
        while ((pos = str.find(d, start)) != VMString::npos) {
            auto* elem = new StringObj();
            elem->s = str.substr(start, pos - start);
            registerNewObj(elem);
            arr->v.push_back(elem);
            start = pos + d.size();
        }
        auto* elem = new StringObj();
        elem->s = str.substr(start);
        registerNewObj(elem);
        arr->v.push_back(elem);
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
            arr->v.push_back(new Fraction(i));
        }
    } else if (step < r64(0)) {
        for (r64 i = start; i >= end; i += step) {
            arr->v.push_back(new Fraction(i));
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
// Synchronous call helpers for higher-order builtins
// ============================================================================

// Sentinel opcode: returns to C++ caller
static void op_sync_return(VM&, Code*) { return; }
static Code syncReturnCode(op_sync_return);

// Call a Callable with one arg. Arg at reg(sb). Result in reg(sb).
static void callOneArg(VM& vm, Callable* fn, u16 sb) {
    if (fn->cfun_) {
        fn->cfun_(vm, sb, 1, sb);
    } else {
        auto* lam = static_cast<Lambda*>(fn);
        CodeBlock* cb = lam->codeBlock_;
        u32 callBase = vm.baseReg() + sb;
        for (u16 i = 0; i < lam->numFreeVars_; i++)
            vm.reg(sb + cb->numArgs + i) = lam->freeVars_[i];
        vm.pushFrame(&syncReturnCode, cb, callBase, cb->numRegs, sb);
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
}

// Call a Callable with two args. Args at reg(sb), reg(sb+1). Result in reg(sb).
static void callTwoArgs(VM& vm, Callable* fn, u16 sb) {
    if (fn->cfun_) {
        fn->cfun_(vm, sb, 2, sb);
    } else {
        auto* lam = static_cast<Lambda*>(fn);
        CodeBlock* cb = lam->codeBlock_;
        u32 callBase = vm.baseReg() + sb;
        for (u16 i = 0; i < lam->numFreeVars_; i++)
            vm.reg(sb + cb->numArgs + i) = lam->freeVars_[i];
        vm.pushFrame(&syncReturnCode, cb, callBase, cb->numRegs, sb);
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
}

// ============================================================================
// Array helpers
// ============================================================================

static Word getArrayElem(VM& vm, Obj* a, Type* et, size_t i) {
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) return Word(static_cast<PodArray<i64>*>(a)->v[i]);
    if (et == vm.floatType()) return Word(static_cast<PodArray<f64>*>(a)->v[i]);
    return Word(static_cast<ObjArray*>(a)->v[i]);
}

static size_t getArraySize(VM& vm, Obj* a, Type* et) {
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) return static_cast<PodArray<i64>*>(a)->v.size();
    if (et == vm.floatType()) return static_cast<PodArray<f64>*>(a)->v.size();
    return static_cast<ObjArray*>(a)->v.size();
}

static Obj* makeEmptyArray(ArrayType* at) {
    Type* et = at->elemType_;
    if (et == gCurrentVM->intType() || et == gCurrentVM->boolType() || et == gCurrentVM->symbolType()) return new PodArray<i64>(at);
    if (et == gCurrentVM->floatType()) return new PodArray<f64>(at);
    return new ObjArray(at);
}

static void arrayPush(VM& vm, Obj* a, Type* et, Word v) {
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) static_cast<PodArray<i64>*>(a)->v.push_back(v.i);
    else if (et == vm.floatType()) static_cast<PodArray<f64>*>(a)->v.push_back(v.f);
    else static_cast<ObjArray*>(a)->v.push_back(v.o);
}

// Apply a transform to an array generically
template<typename F>
static void txArray(VM& vm, u16 dst, Obj* src, ArrayType* at, F&& f) {
    Type* et = at->elemType_;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* s = static_cast<PodArray<i64>*>(src);
        auto* r = new PodArray<i64>(at); f(s->v, r->v); vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* s = static_cast<PodArray<f64>*>(src);
        auto* r = new PodArray<f64>(at); f(s->v, r->v); vm.reg(dst).o = r;
    } else {
        auto* s = static_cast<ObjArray*>(src);
        auto* r = new ObjArray(at); f(s->v, r->v); vm.reg(dst).o = r;
    }
}

// ============================================================================
// Simple array builtins: reverse, push, pop, muss, sort
// ============================================================================

static void builtin_reverse_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [](auto& sv, auto& rv) {
        size_t n = sv.size(); rv.resize(n);
        for (size_t i = 0; i < n; i++) rv[i] = sv[n-1-i];
    });
}

static void builtin_push_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* s = static_cast<PodArray<i64>*>(src);
        auto* r = new PodArray<i64>(at); r->v = s->v;
        r->v.push_back(vm.reg(ab+1).i); vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* s = static_cast<PodArray<f64>*>(src);
        auto* r = new PodArray<f64>(at); r->v = s->v;
        r->v.push_back(vm.reg(ab+1).f); vm.reg(dst).o = r;
    } else {
        auto* s = static_cast<ObjArray*>(src);
        auto* r = new ObjArray(at); r->v = s->v;
        r->v.push_back(vm.reg(ab+1).o); vm.reg(dst).o = r;
    }
}

static void builtin_pop_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [](auto& sv, auto& rv) {
        if (!sv.empty()) { rv.resize(sv.size()-1);
            for (size_t i = 0; i < rv.size(); i++) rv[i] = sv[i]; }
    });
}

static void builtin_muss_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    auto& rng = vm.rng();
    txArray(vm, dst, src, at, [&rng](auto& sv, auto& rv) {
        rv = sv;
        for (size_t i = rv.size(); i > 1; i--) {
            size_t j = rng.next() % i;
            std::swap(rv[i-1], rv[j]);
        }
    });
}

// ============================================================================
// Random number generation builtins
// ============================================================================

// urand() -> Float  uniform [0.0, 1.0)
static void builtin_urand(VM& vm, u16 dst, u16, u16) {
    // Map a 64-bit integer to [0.0, 1.0) by using the upper 53 bits
    u64 r = vm.rng().next();
    vm.reg(dst).f = (r >> 11) * (1.0 / (1ULL << 53));
}

// brand() -> Float  bipolar [-1.0, 1.0)
static void builtin_brand(VM& vm, u16 dst, u16, u16) {
    u64 r = vm.rng().next();
    vm.reg(dst).f = (r >> 11) * (2.0 / (1ULL << 53)) - 1.0;
}

// irand(lo, hi) -> Int  uniform integer [lo, hi]
static void builtin_irand(VM& vm, u16 dst, u16, u16 ab) {
    i64 lo = vm.reg(ab).i;
    i64 hi = vm.reg(ab + 1).i;
    if (lo > hi) std::swap(lo, hi);
    u64 range = (u64)(hi - lo) + 1;
    // Unbiased: reject values that would cause modulo bias
    u64 limit = (UINT64_MAX / range) * range;
    u64 r;
    do { r = vm.rng().next(); } while (r >= limit);
    vm.reg(dst).i = lo + (i64)(r % range);
}

// xrand(lo, hi) -> Float  exponentially distributed [lo, hi)
// Equal to lo * pow(hi/lo, urand())
static void builtin_xrand(VM& vm, u16 dst, u16, u16 ab) {
    f64 lo = vm.reg(ab).f;
    f64 hi = vm.reg(ab + 1).f;
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    vm.reg(dst).f = lo * std::pow(hi / lo, u);
}

// pick(Array[T]) -> T  choose a random element
static void builtin_pick_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    size_t n = getArraySize(vm, arr, at->elemType_);
    size_t idx = vm.rng().next() % n;
    vm.reg(dst) = getArrayElem(vm, arr, at->elemType_, idx);
}

// urands() -> List<Float>: infinite lazy list of uniform [0, 1) floats
static void builtin_urands_list(VM& vm, u16 dst, u16, u16) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = new ListNode(lt);
    auto* gen = new UrandsListGen(vm.typeType());
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// urands(n) -> Array<Float>: array of n uniform [0, 1) floats
static void builtin_urands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        r->v[i] = (x >> 11) * (1.0 / (1ULL << 53));
    }
    vm.reg(dst).o = r;
}

// brands() -> List<Float>: infinite lazy list of bipolar [-1, 1) floats
static void builtin_brands_list(VM& vm, u16 dst, u16, u16) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = new ListNode(lt);
    auto* gen = new BrandsListGen(vm.typeType());
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// brands(n) -> Array<Float>: array of n bipolar [-1, 1) floats
static void builtin_brands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        r->v[i] = (x >> 11) * (2.0 / (1ULL << 53)) - 1.0;
    }
    vm.reg(dst).o = r;
}

// irands(lo, hi) -> List<Int>: infinite lazy list of uniform random ints [lo, hi]
static void builtin_irands_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* lt = vm.listType(vm.intType());
    auto* node = new ListNode(lt);
    auto* gen = new IrandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).i;
    gen->hi_ = vm.reg(ab+1).i;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// irands(n, lo, hi) -> Array<Int>: array of n uniform random ints [lo, hi]
static void builtin_irands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    i64 lo = vm.reg(ab+1).i, hi = vm.reg(ab+2).i;
    if (lo > hi) std::swap(lo, hi);
    if (n < 0) n = 0;
    u64 range = (u64)(hi - lo) + 1;
    u64 limit = (UINT64_MAX / range) * range;
    auto* r = new PodArray<i64>(vm.arrayType(vm.intType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x;
        do { x = vm.rng().next(); } while (x >= limit);
        r->v[i] = lo + (i64)(x % range);
    }
    vm.reg(dst).o = r;
}

// xrands(lo, hi) -> List<Float>: infinite lazy list of exponentially distributed floats [lo, hi)
static void builtin_xrands_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = new ListNode(lt);
    auto* gen = new XrandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).f;
    gen->hi_ = vm.reg(ab+1).f;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// xrands(n, lo, hi) -> Array<Float>: array of n exponentially distributed floats [lo, hi)
static void builtin_xrands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    f64 lo = vm.reg(ab+1).f, hi = vm.reg(ab+2).f;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        f64 u = (x >> 11) * (1.0 / (1ULL << 53));
        r->v[i] = lo * std::pow(hi / lo, u);
    }
    vm.reg(dst).o = r;
}

// rand(lo, hi) -> Float: uniform random float in [lo, hi)
static void builtin_rand(VM& vm, u16 dst, u16, u16 ab) {
    f64 lo = vm.reg(ab).f, hi = vm.reg(ab+1).f;
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    vm.reg(dst).f = lo + u * (hi - lo);
}

// rands(lo, hi) -> List<Float>: infinite lazy list of uniform floats [lo, hi)
static void builtin_rands_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* lt = vm.listType(vm.floatType());
    auto* node = new ListNode(lt);
    auto* gen = new RandsListGen(vm.typeType());
    gen->lo_ = vm.reg(ab).f;
    gen->hi_ = vm.reg(ab+1).f;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// rands(n, lo, hi) -> Array<Float>: array of n uniform floats [lo, hi)
static void builtin_rands_array(VM& vm, u16 dst, u16, u16 ab) {
    i64 n = vm.reg(ab).i;
    f64 lo = vm.reg(ab+1).f, hi = vm.reg(ab+2).f;
    if (n < 0) n = 0;
    auto* r = new PodArray<f64>(vm.arrayType(vm.floatType()));
    r->v.resize((size_t)n);
    for (i64 i = 0; i < n; i++) {
        u64 x = vm.rng().next();
        f64 u = (x >> 11) * (1.0 / (1ULL << 53));
        r->v[i] = lo + u * (hi - lo);
    }
    vm.reg(dst).o = r;
}

// picks(Array<T>) -> List<T>: infinite lazy list of random picks
static void builtin_picks_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(arr->type_);
    auto* lt = vm.listType(at->elemType_);
    auto* node = new ListNode(lt);
    auto* gen = new PicksListGen(vm.typeType());
    gen->array_ = arr;
    gen->elemType_ = at->elemType_;
    gen->listType_ = lt;
    node->generator_ = gen;
    vm.reg(dst).o = node;
}

// picks(Array<T>, Int) -> Array<T>: array of n random picks
static void builtin_picks_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* arr = vm.reg(ab).o;
    i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    size_t len = getArraySize(vm, arr, et);
    if (n < 0) n = 0;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* src = static_cast<PodArray<i64>*>(arr);
        auto* r = new PodArray<i64>(at);
        r->v.resize((size_t)n);
        for (i64 i = 0; i < n; i++) r->v[i] = src->v[vm.rng().next() % len];
        vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* src = static_cast<PodArray<f64>*>(arr);
        auto* r = new PodArray<f64>(at);
        r->v.resize((size_t)n);
        for (i64 i = 0; i < n; i++) r->v[i] = src->v[vm.rng().next() % len];
        vm.reg(dst).o = r;
    } else {
        auto* src = static_cast<ObjArray*>(arr);
        auto* r = new ObjArray(at);
        r->v.resize((size_t)n);
        for (i64 i = 0; i < n; i++) r->v[i] = src->v[vm.rng().next() % len];
        vm.reg(dst).o = r;
    }
}

static void builtin_sort_int_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<PodArray<i64>*>(vm.reg(ab).o);
    auto* r = new PodArray<i64>(static_cast<ArrayType*>(s->type_));
    r->v = s->v; std::sort(r->v.begin(), r->v.end()); vm.reg(dst).o = r;
}
static void builtin_sort_float_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<PodArray<f64>*>(vm.reg(ab).o);
    auto* r = new PodArray<f64>(static_cast<ArrayType*>(s->type_));
    r->v = s->v; std::sort(r->v.begin(), r->v.end()); vm.reg(dst).o = r;
}
static void builtin_sort_string_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<ObjArray*>(vm.reg(ab).o);
    auto* r = new ObjArray(static_cast<ArrayType*>(s->type_)); r->v = s->v;
    std::sort(r->v.begin(), r->v.end(), [](Obj* a, Obj* b) {
        return static_cast<StringObj*>(a)->s < static_cast<StringObj*>(b)->s;
    });
    vm.reg(dst).o = r;
}

// sort: (Array[T], (T,T)->Bool) -> Array[T]
static void builtin_sort_by_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    u16 sb = vm.currentCodeBlock()->numRegs;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        vm.reg(sb) = getArrayElem(vm, src, et, a);
        vm.reg(sb+1) = getArrayElem(vm, src, et, b);
        callTwoArgs(vm, fn, sb);
        return vm.reg(sb).i != 0;
    });
    auto* result = makeEmptyArray(at);
    for (size_t i = 0; i < n; i++)
        arrayPush(vm, result, et, getArrayElem(vm, src, et, idx[i]));
    vm.reg(dst).o = result;
}

// grade: (Array[T], (T,T)->Bool) -> Array[Int]
static void builtin_grade_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    u16 sb = vm.currentCodeBlock()->numRegs;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        vm.reg(sb) = getArrayElem(vm, src, et, a);
        vm.reg(sb+1) = getArrayElem(vm, src, et, b);
        callTwoArgs(vm, fn, sb);
        return vm.reg(sb).i != 0;
    });
    auto* result = new PodArray<i64>(vm.arrayType(vm.intType()));
    result->v.resize(n);
    for (size_t i = 0; i < n; i++) result->v[i] = (i64)idx[i];
    vm.reg(dst).o = result;
}

// ============================================================================
// Array: take, drop, stride, stutter, cat, join, flatten
// ============================================================================

static void builtin_take_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        size_t take = n < 0 ? 0 : ((size_t)n > sv.size() ? sv.size() : (size_t)n);
        rv.resize(take);
        for (size_t i = 0; i < take; i++) rv[i] = sv[i];
    });
}

static void builtin_drop_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        size_t drop = n < 0 ? 0 : ((size_t)n > sv.size() ? sv.size() : (size_t)n);
        rv.resize(sv.size() - drop);
        for (size_t i = 0; i < rv.size(); i++) rv[i] = sv[drop+i];
    });
}

static void builtin_stride_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        if (n <= 0) return;
        for (size_t i = 0; i < sv.size(); i += (size_t)n) rv.push_back(sv[i]);
    });
}

static void builtin_stutter_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o; i64 n = vm.reg(ab+1).i;
    auto* at = static_cast<ArrayType*>(src->type_);
    txArray(vm, dst, src, at, [n](auto& sv, auto& rv) {
        if (n <= 0) return;
        for (size_t i = 0; i < sv.size(); i++)
            for (i64 j = 0; j < n; j++) rv.push_back(sv[i]);
    });
}

static void builtin_cat_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = vm.reg(ab).o; auto* b = vm.reg(ab+1).o;
    auto* at = static_cast<ArrayType*>(a->type_);
    Type* et = at->elemType_;
    if (et == vm.intType() || et == vm.boolType() || et == vm.symbolType()) {
        auto* r = new PodArray<i64>(at);
        r->v = static_cast<PodArray<i64>*>(a)->v;
        auto& bv = static_cast<PodArray<i64>*>(b)->v;
        for (auto& x : bv) r->v.push_back(x); vm.reg(dst).o = r;
    } else if (et == vm.floatType()) {
        auto* r = new PodArray<f64>(at);
        r->v = static_cast<PodArray<f64>*>(a)->v;
        auto& bv = static_cast<PodArray<f64>*>(b)->v;
        for (auto& x : bv) r->v.push_back(x); vm.reg(dst).o = r;
    } else {
        auto* r = new ObjArray(at);
        r->v = static_cast<ObjArray*>(a)->v;
        auto& bv = static_cast<ObjArray*>(b)->v;
        for (auto& x : bv) r->v.push_back(x); vm.reg(dst).o = r;
    }
}

static void builtin_join_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* outerType = static_cast<ArrayType*>(src->type_);
    auto* innerType = dynamic_cast<ArrayType*>(outerType->elemType_);
    if (!innerType) { vm.reg(dst).o = src; return; }
    auto* outer = static_cast<ObjArray*>(src);
    auto* result = makeEmptyArray(innerType);
    Type* et = innerType->elemType_;
    for (size_t i = 0; i < outer->v.size(); i++) {
        if (!outer->v[i]) continue;
        size_t n = getArraySize(vm, outer->v[i], et);
        for (size_t j = 0; j < n; j++) arrayPush(vm, result, et, getArrayElem(vm, outer->v[i], et, j));
    }
    vm.reg(dst).o = result;
}

// flatten = join for now (one level)
// Recursively collect leaf elements from nested arrays into result.
static void flattenArrayInto(VM& vm, Obj* arr, Obj* result, Type* leafType) {
    auto* at = static_cast<ArrayType*>(arr->type_);
    Type* et = at->elemType_;
    if (dynamic_cast<ArrayType*>(et)) {
        // Elements are sub-arrays — recurse
        auto* oa = static_cast<ObjArray*>(arr);
        for (size_t i = 0; i < oa->v.size(); i++) {
            if (oa->v[i]) flattenArrayInto(vm, oa->v[i], result, leafType);
        }
    } else {
        // Leaf level — copy elements into result
        size_t n = getArraySize(vm, arr, et);
        for (size_t i = 0; i < n; i++) {
            arrayPush(vm, result, leafType, getArrayElem(vm, arr, et, i));
        }
    }
}

static void builtin_flatten_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* at = static_cast<ArrayType*>(src->type_);
    // Find innermost non-Array element type
    Type* leafType = at->elemType_;
    int depth = 0;
    while (auto* inner = dynamic_cast<ArrayType*>(leafType)) {
        leafType = inner->elemType_; depth++;
    }
    (void)depth;
    // If element type was not an array, nothing to flatten
    if (leafType == at->elemType_) { vm.reg(dst).o = src; return; }
    auto* resultType = vm.arrayType(leafType);
    auto* result = makeEmptyArray(resultType);
    vm.reg(dst).o = result;  // root against GC
    flattenArrayInto(vm, src, result, leafType);
}

// ============================================================================
// Array HOF: map, filter, fold, scan, fold1, scan1, find, takeWhile, dropWhile
// ============================================================================

static void builtin_map_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* srcType = static_cast<ArrayType*>(src->type_);
    Type* srcET = srcType->elemType_;
    size_t n = getArraySize(vm, src, srcET);
    // Determine result type from fn's type
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* resET = fnType->returnType_;
    auto* resAT = vm.arrayType(resET);
    auto* result = makeEmptyArray(resAT);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = getArrayElem(vm, src, srcET, i);
        callOneArg(vm, fn, sb);
        arrayPush(vm, result, resET, vm.reg(sb));
    }
    vm.reg(dst).o = result;
}

static void builtin_filter_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        Word elem = getArrayElem(vm, src, et, i);
        vm.reg(sb) = elem;
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) arrayPush(vm, result, et, elem);
    }
    vm.reg(dst).o = result;
}

static void builtin_fold_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
    }
    vm.reg(dst) = acc;
}

static void builtin_scan_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* accET = fnType->returnType_;
    auto* resAT = vm.arrayType(accET);
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(resAT);
    arrayPush(vm, result, accET, acc);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
        arrayPush(vm, result, accET, acc);
    }
    vm.reg(dst).o = result;
}

static void builtin_fold1_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    if (n == 0) { vm.reg(dst).i = 0; return; }
    Word acc = getArrayElem(vm, src, et, 0);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 1; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
    }
    vm.reg(dst) = acc;
}

static void builtin_scan1_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    if (n == 0) { vm.reg(dst).o = result; return; }
    Word acc = getArrayElem(vm, src, et, 0);
    arrayPush(vm, result, et, acc);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 1; i < n; i++) {
        vm.reg(sb) = acc;
        vm.reg(sb+1) = getArrayElem(vm, src, et, i);
        callTwoArgs(vm, fn, sb);
        acc = vm.reg(sb);
        arrayPush(vm, result, et, acc);
    }
    vm.reg(dst).o = result;
}

static void builtin_find_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        vm.reg(sb) = getArrayElem(vm, src, et, i);
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) { vm.reg(dst).i = (i64)i; return; }
    }
    vm.reg(dst).i = -1;
}

static void builtin_takeWhile_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (size_t i = 0; i < n; i++) {
        Word elem = getArrayElem(vm, src, et, i);
        vm.reg(sb) = elem;
        callOneArg(vm, fn, sb);
        if (!vm.reg(sb).i) break;
        arrayPush(vm, result, et, elem);
    }
    vm.reg(dst).o = result;
}

static void builtin_dropWhile_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto* result = makeEmptyArray(at);
    u16 sb = vm.currentCodeBlock()->numRegs;
    bool dropping = true;
    for (size_t i = 0; i < n; i++) {
        Word elem = getArrayElem(vm, src, et, i);
        if (dropping) {
            vm.reg(sb) = elem;
            callOneArg(vm, fn, sb);
            if (vm.reg(sb).i) continue;
            dropping = false;
        }
        arrayPush(vm, result, et, elem);
    }
    vm.reg(dst).o = result;
}

// ============================================================================
// Array: zip, enumerate
// ============================================================================

static void builtin_zip_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = vm.reg(ab).o; auto* b = vm.reg(ab+1).o;
    auto* atA = static_cast<ArrayType*>(a->type_);
    auto* atB = static_cast<ArrayType*>(b->type_);
    Type* etA = atA->elemType_; Type* etB = atB->elemType_;
    size_t na = getArraySize(vm, a, etA), nb = getArraySize(vm, b, etB);
    size_t n = na < nb ? na : nb;
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(etA); fields.push_back(etB);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    auto* result = new ObjArray(resAT);
    for (size_t i = 0; i < n; i++) {
        auto* tup = Tuple::create(tt, 2);
        tup->v[0] = getArrayElem(vm, a, etA, i);
        tup->v[1] = getArrayElem(vm, b, etB, i);
        result->v.push_back(tup);
    }
    vm.reg(dst).o = result;
}

static void builtin_enumerate_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    Type* et = at->elemType_;
    size_t n = getArraySize(vm, src, et);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(vm.intType()); fields.push_back(et);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    auto* result = new ObjArray(resAT);
    for (size_t i = 0; i < n; i++) {
        auto* tup = Tuple::create(tt, 2);
        tup->v[0] = Word((i64)i);
        tup->v[1] = getArrayElem(vm, src, et, i);
        result->v.push_back(tup);
    }
    vm.reg(dst).o = result;
}

// ============================================================================
// List generator generate() implementations
// ============================================================================

void TakeListGen::generate(VM& vm, ListNode* owner) {
    if (remaining_ <= 0 || !source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);
    owner->head_ = source_->head_;
    if (remaining_ <= 1 || !source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    source_ = source_->tail_; remaining_--;
    vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void DropListGen::generate(VM& vm, ListNode* owner) {
    ListNode* cur = source_;
    i64 rem = remaining_;
    while (rem > 0 && cur) {
        if (cur->generator_) cur->force(vm);
        cur = cur->tail_; rem--;
    }
    if (!cur) { owner->tail_ = nullptr; return; }
    if (cur->generator_) cur->force(vm);
    owner->head_ = cur->head_;
    owner->tail_ = cur->tail_;
}

void StrideListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);
    owner->head_ = source_->head_;
    ListNode* cur = source_;
    for (i64 i = 0; i < stride_ && cur; i++) {
        cur = cur->tail_;
        if (cur && cur->generator_) cur->force(vm);
    }
    if (!cur) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    source_ = cur;
    vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void StutterListGen::generate(VM& vm, ListNode* owner) {
    if (currentRepeat_ > 0) {
        owner->head_ = currentValue_;
        currentRepeat_--;
        if (currentRepeat_ > 0 || source_) {
            auto* tail = new ListNode(listType_);
            tail->generator_ = this; owner->tail_ = tail;
        } else { owner->tail_ = nullptr; }
        return;
    }
    if (!source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);
    owner->head_ = source_->head_;
    i64 repsLeft = repeatCount_ - 1;
    if (repsLeft > 0 || source_->tail_) {
        auto* tail = new ListNode(listType_);
        currentRepeat_ = repsLeft; currentValue_ = source_->head_;
        source_ = source_->tail_;
        if (valueIsObj_ && currentValue_.o) vm.gc().writeBarrier(currentValue_.o);
        if (source_) vm.gc().writeBarrier(source_);
        tail->generator_ = this; owner->tail_ = tail;
    } else { owner->tail_ = nullptr; }
}

void CatListGen::generate(VM& vm, ListNode* owner) {
    if (!inSecond_) {
        if (!first_) { inSecond_ = true; }
        else {
            if (first_->generator_) first_->force(vm);
            owner->head_ = first_->head_;
            auto* tail = new ListNode(listType_);
            first_ = first_->tail_;
            if (first_) vm.gc().writeBarrier(first_);
            tail->generator_ = this; owner->tail_ = tail;
            return;
        }
    }
    if (!second_) { owner->tail_ = nullptr; return; }
    if (second_->generator_) second_->force(vm);
    owner->head_ = second_->head_;
    owner->tail_ = second_->tail_;
}

void UrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (1.0 / (1ULL << 53));
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void BrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (2.0 / (1ULL << 53)) - 1.0;
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void IrandsListGen::generate(VM& vm, ListNode* owner) {
    i64 lo = lo_, hi = hi_;
    if (lo > hi) std::swap(lo, hi);
    u64 range = (u64)(hi - lo) + 1;
    u64 limit = (UINT64_MAX / range) * range;
    u64 r;
    do { r = vm.rng().next(); } while (r >= limit);
    owner->head_.i = lo + (i64)(r % range);
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void XrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ * std::pow(hi_ / lo_, u);
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void RandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ + u * (hi_ - lo_);
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void PicksListGen::generate(VM& vm, ListNode* owner) {
    size_t n = getArraySize(vm, array_, elemType_);
    size_t idx = vm.rng().next() % n;
    owner->head_ = getArrayElem(vm, array_, elemType_, idx);
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void CycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) current_ = head_;
    if (!current_) { owner->tail_ = nullptr; return; }
    if (current_->generator_) current_->force(vm);
    owner->head_ = current_->head_;
    auto* tail = new ListNode(listType_);
    current_ = current_->tail_ ? current_->tail_ : head_;
    vm.gc().writeBarrier(current_);
    tail->generator_ = this; owner->tail_ = tail;
}

void NCycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) {
        if (remaining_ <= 0) { owner->tail_ = nullptr; return; }
        current_ = head_; remaining_--;
    }
    if (!current_) { owner->tail_ = nullptr; return; }
    if (current_->generator_) current_->force(vm);
    owner->head_ = current_->head_;
    ListNode* next = current_->tail_;
    if (!next && remaining_ > 0) { next = head_; remaining_--; }
    if (!next) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    current_ = next;
    vm.gc().writeBarrier(current_);
    tail->generator_ = this; owner->tail_ = tail;
}

void HangListGen::generate(VM& vm, ListNode* owner) {
    if (hasLast_ && !source_) {
        owner->head_ = lastValue_;
        auto* tail = new ListNode(listType_);
        tail->generator_ = this; owner->tail_ = tail;
        return;
    }
    if (!source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);
    owner->head_ = source_->head_;
    auto* tail = new ListNode(listType_);
    lastValue_ = source_->head_; hasLast_ = true;
    source_ = source_->tail_;
    if (valueIsObj_ && lastValue_.o) vm.gc().writeBarrier(lastValue_.o);
    if (source_) vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void MapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = source_->head_;
    callOneArg(vm, fn_, sb);
    owner->head_ = vm.reg(sb);
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(resultListType_);
    source_ = source_->tail_;
    vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void AutoMapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);

    u16 sb = vm.currentCodeBlock()->numRegs;
    u16 argc = info_->argc;

    // Place arguments into scratch regs sb..sb+argc-1
    for (u16 i = 0; i < argc; ++i) {
        if (i == info_->listArgIndex) {
            // List element — possibly with type promotion
            Word elem = source_->head_;
            if (info_->listElemType != info_->listParamType) {
                // Runtime promotion: Int->Float, Int->Fraction, etc.
                if (info_->listParamType == vm.floatType() &&
                    (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())) {
                    elem.f = (f64)elem.i;
                } else if (info_->listParamType == vm.fractionType() &&
                           (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())) {
                    auto* frac = new Fraction(r64(elem.i));
                    elem.o = frac;
                } else if (info_->listParamType == vm.floatType() &&
                           info_->listElemType == vm.fractionType()) {
                    auto* frac = static_cast<Fraction*>(elem.o);
                    elem.f = (f64)frac->r;
                } else if (info_->listParamType == vm.complexType()) {
                    f64 re = 0.0;
                    if (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())
                        re = (f64)elem.i;
                    else if (info_->listElemType == vm.floatType())
                        re = elem.f;
                    else if (info_->listElemType == vm.fractionType())
                        re = (f64)(static_cast<Fraction*>(elem.o)->r);
                    auto* cx = new Complex(x64(re, 0.0));
                    elem.o = cx;
                }
            }
            vm.reg(sb + i) = elem;
        } else {
            // Broadcast arg — find which broadcast slot this is
            u16 bIdx = 0;
            for (u16 b = 0; b < numBroadcast_; ++b) {
                if (info_->broadcastArgs[b].argIndex == i) {
                    bIdx = b;
                    break;
                }
            }
            auto& ba = info_->broadcastArgs[bIdx];
            if (ba.isArray) {
                // Extract element from parallel array at current index
                Word elem;
                Type* et = ba.elemType;
                if (et == vm.intType() || et == vm.boolType()) {
                    auto* arr = static_cast<PodArray<i64>*>(broadcastVals_[bIdx].o);
                    if (arrayIndex_ >= (u16)arr->v.size()) { owner->tail_ = nullptr; return; }
                    elem.i = arr->v[arrayIndex_];
                } else if (et == vm.floatType()) {
                    auto* arr = static_cast<PodArray<f64>*>(broadcastVals_[bIdx].o);
                    if (arrayIndex_ >= (u16)arr->v.size()) { owner->tail_ = nullptr; return; }
                    elem.f = arr->v[arrayIndex_];
                } else {
                    auto* arr = static_cast<ObjArray*>(broadcastVals_[bIdx].o);
                    if (arrayIndex_ >= (u16)arr->v.size()) { owner->tail_ = nullptr; return; }
                    elem.o = arr->v[arrayIndex_];
                }
                // Runtime type promotion if needed
                Type* paramType = ba.dstType;
                if (paramType && et != paramType) {
                    if (paramType == vm.floatType() &&
                        (et == vm.intType() || et == vm.boolType())) {
                        elem.f = (f64)elem.i;
                    } else if (paramType == vm.fractionType() &&
                               (et == vm.intType() || et == vm.boolType())) {
                        auto* frac = new Fraction(r64(elem.i));
                        elem.o = frac;
                    }
                }
                vm.reg(sb + i) = elem;
            } else {
                vm.reg(sb + i) = broadcastVals_[bIdx];
            }
        }
    }

    // Call the function
    if (info_->isBuiltin) {
        auto* prim = static_cast<Primitive*>(vm.global(info_->funcGlobalIndex).o);
        prim->cfun_(vm, sb, argc, sb);
    } else {
        auto* cb = static_cast<CodeBlock*>(vm.global(info_->funcGlobalIndex).p);
        u32 callBase = vm.baseReg() + sb;
        vm.pushFrame(&syncReturnCode, cb, callBase, cb->numRegs, sb);
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
    owner->head_ = vm.reg(sb);

    // Create lazy tail
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(info_->resultListType);
    source_ = source_->tail_;
    vm.gc().writeBarrier(source_);
    arrayIndex_++;
    tail->generator_ = this;
    owner->tail_ = tail;
}

void FilterListGen::generate(VM& vm, ListNode* owner) {
    ListNode* cur = source_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    while (cur) {
        if (cur->generator_) cur->force(vm);
        vm.reg(sb) = cur->head_;
        callOneArg(vm, fn_, sb);
        if (vm.reg(sb).i) {
            owner->head_ = cur->head_;
            ListNode* rest = cur->tail_;
            if (!rest) { owner->tail_ = nullptr; return; }
            auto* tail = new ListNode(listType_);
            source_ = rest;
            vm.gc().writeBarrier(source_);
            tail->generator_ = this; owner->tail_ = tail;
            return;
        }
        cur = cur->tail_;
    }
    owner->tail_ = nullptr;
}

void PredicateListGen::generate(VM& vm, ListNode* owner) {
    u16 sb = vm.currentCodeBlock()->numRegs;
    if (mode_ == TakeWhile) {
        // source_ is guaranteed to have passed the predicate (checked before
        // creating this generator).  Copy its head into owner.
        if (!source_) { owner->head_.i = 0; owner->tail_ = nullptr; return; }
        if (source_->generator_) source_->force(vm);
        owner->head_ = source_->head_;
        // Check if the NEXT source element passes the predicate
        ListNode* next = source_->tail_;
        if (!next) { owner->tail_ = nullptr; return; }
        if (next->generator_) next->force(vm);
        vm.reg(sb) = next->head_;
        callOneArg(vm, fn_, sb);
        if (!vm.reg(sb).i) { owner->tail_ = nullptr; return; }
        // Next element passes — create lazy tail
        auto* tail = new ListNode(listType_);
        source_ = next;
        vm.gc().writeBarrier(source_);
        tail->generator_ = this; owner->tail_ = tail;
    } else { // DropWhile
        ListNode* cur = source_;
        if (dropping_) {
            while (cur) {
                if (cur->generator_) cur->force(vm);
                vm.reg(sb) = cur->head_;
                callOneArg(vm, fn_, sb);
                if (!vm.reg(sb).i) break;
                cur = cur->tail_;
            }
        }
        if (!cur) { owner->tail_ = nullptr; return; }
        if (cur->generator_) cur->force(vm);
        owner->head_ = cur->head_;
        owner->tail_ = cur->tail_;
    }
}

void ScanListGen::generate(VM& vm, ListNode* owner) {
    owner->head_ = accumulator_;
    if (!source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = accumulator_;
    vm.reg(sb+1) = source_->head_;
    callTwoArgs(vm, fn_, sb);
    Word newAcc = vm.reg(sb);
    auto* tail = new ListNode(resultListType_);
    accumulator_ = newAcc; source_ = source_->tail_;
    if (accIsObj_ && accumulator_.o) vm.gc().writeBarrier(accumulator_.o);
    if (source_) vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void IterListGen::generate(VM& vm, ListNode* owner) {
    owner->head_ = current_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = current_;
    callOneArg(vm, fn_, sb);
    auto* tail = new ListNode(listType_);
    current_ = vm.reg(sb);
    if (valueIsObj_ && current_.o) vm.gc().writeBarrier(current_.o);
    tail->generator_ = this; owner->tail_ = tail;
}

void ZipListGen::generate(VM& vm, ListNode* owner) {
    if (!left_ || !right_) { owner->tail_ = nullptr; return; }
    if (left_->generator_) left_->force(vm);
    if (right_->generator_) right_->force(vm);
    auto* tup = Tuple::create(tupleType_, 2);
    tup->v[0] = left_->head_; tup->v[1] = right_->head_;
    owner->head_.o = tup;
    if (!left_->tail_ || !right_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(resultListType_);
    left_ = left_->tail_; right_ = right_->tail_;
    if (left_) vm.gc().writeBarrier(left_);
    if (right_) vm.gc().writeBarrier(right_);
    tail->generator_ = this; owner->tail_ = tail;
}

void EnumerateListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    if (source_->generator_) source_->force(vm);
    auto* tup = Tuple::create(tupleType_, 2);
    tup->v[0] = Word(index_); tup->v[1] = source_->head_;
    owner->head_.o = tup;
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(resultListType_);
    source_ = source_->tail_; index_++;
    if (source_) vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void JoinListGen::generate(VM& vm, ListNode* owner) {
    // Advance inner list; if exhausted, move to next outer element
    while (!inner_) {
        if (!outer_) { owner->head_.i = 0; owner->tail_ = nullptr; return; }
        if (outer_->generator_) outer_->force(vm);
        inner_ = static_cast<ListNode*>(outer_->head_.o);
        outer_ = outer_->tail_;
    }
    if (inner_->generator_) inner_->force(vm);
    owner->head_ = inner_->head_;
    // Check if there's more data before creating a tail
    ListNode* nextInner = inner_->tail_;
    ListNode* nextOuter = outer_;
    // Peek ahead: is there any more data?
    if (!nextInner) {
        // Current inner list exhausted; check if any outer elements remain
        ListNode* o = nextOuter;
        ListNode* foundInner = nullptr;
        while (o) {
            if (o->generator_) o->force(vm);
            foundInner = static_cast<ListNode*>(o->head_.o);
            o = o->tail_;
            if (foundInner) { nextOuter = o; nextInner = foundInner; break; }
        }
        if (!foundInner) { owner->tail_ = nullptr; return; }
    }
    auto* tail = new ListNode(resultListType_);
    outer_ = nextOuter; inner_ = nextInner;
    if (outer_) vm.gc().writeBarrier(outer_);
    if (inner_) vm.gc().writeBarrier(inner_);
    tail->generator_ = this; owner->tail_ = tail;
}

void ArrayToListGen::generate(VM& vm, ListNode* owner) {
    size_t sz = getArraySize(vm, array_, elemType_);
    if (index_ >= sz) { owner->tail_ = nullptr; return; }
    owner->head_ = getArrayElem(vm, array_, elemType_, index_);
    index_++;
    if (index_ >= sz) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    tail->generator_ = this; owner->tail_ = tail;
}

// Synchronously resume a coroutine from C++ code.
// Returns the Option<T> enum (some(value) or none).
// Uses a static halt instruction as the return PC so that when
// yield/done tail-calls it, control returns to the caller.
// Synchronously resume a coroutine from C++ code.
// Returns the yielded value directly (no Enum wrapper).
// Caller must check coro->state_ to determine if a value was yielded
// (Suspended) or if the coroutine finished (Done).
static Word syncResumeCoroutine(VM& vm, CoroutineObj* coro) {
    auto* coroType = static_cast<CoroutineType*>(coro->type_);

    // Save current VM state (yield/done will restore these)
    u32 savedBaseReg = vm.baseReg();
    u32 savedFrameCount = vm.frameCount();
    auto* savedCoroFrame = vm.currentCoroFrame();
    auto* savedCoroutine = vm.currentCoroutine();
    Word savedReg0 = vm.reg(0);

    // Static halt instruction - yield/done will tail-call this to return here
    static Code haltCode(op_halt);

    // Set up caller context for yield/done to restore
    coro->callerReturnPC_ = &haltCode;
    coro->callerResultReg_ = 0;
    coro->callerBaseReg_ = savedBaseReg;
    coro->callerFrameCount_ = savedFrameCount;
    coro->callerCoroFrame_ = savedCoroFrame;
    coro->callerCoroutine_ = savedCoroutine;

    // Activate coroutine
    vm.setCurrentCoroutine(coro);
    coro->state_ = CoroutineObj::Running;

    u32 newBase = vm.baseReg() + vm.currentFrameNumRegs();

    Code* entry;
    if (!coro->topFrame_) {
        // Created state
        CodeBlock* callee = coro->entryBlock_;
        auto* frame = CoroutineFrame::create(coroType, callee, callee->numRegs);
        vm.setCurrentCoroFrame(frame);

        for (u16 i = 0; i < coro->numArgs_; ++i) {
            vm.regsBase()[newBase + i] = coro->args_[i];
        }

        vm.pushFrame(nullptr, callee, newBase, callee->numRegs, 0);

        if (!callee->defaultEntryOffsets.empty()) {
            u16 idx = coro->numArgs_ - callee->minArity;
            entry = callee->code.data() + callee->defaultEntryOffsets[idx];
        } else {
            entry = callee->code.data();
        }
    } else {
        // Suspended state
        auto* frame = coro->topFrame_;
        vm.setCurrentCoroFrame(frame);
        vm.gc().writeBarrier(frame);

        for (u16 i = 0; i < frame->numRegs_; ++i) {
            vm.regsBase()[newBase + i] = frame->regs_[i];
        }

        vm.pushFrame(nullptr, frame->codeBlock_, newBase, frame->numRegs_, 0);

        entry = coro->resumePC_;
    }

    // Enter dispatch loop - returns when yield/done tail-calls op_halt
    entry->op(vm, entry);

    // VM state has been restored by yield/done.
    // Yielded value (or 0 if done) is in reg(0).
    Word result = vm.reg(0);

    // Restore reg(0)
    vm.reg(0) = savedReg0;

    return result;
}

// CoroutineListGen::generate - emit the buffered head value and peek ahead
// for the tail. The bufferedValue_ was set by the previous generate() call
// or by builtin_toList_coroutine.
void CoroutineListGen::generate(VM& vm, ListNode* owner) {
    // Use the pre-fetched value as this node's head
    owner->head_ = bufferedValue_;

    // Peek ahead: resume the coroutine to see if there's a next value
    if (coro_->state_ == CoroutineObj::Done) {
        owner->tail_ = nullptr;
        return;
    }

    Word peek = syncResumeCoroutine(vm, coro_);

    if (coro_->state_ != CoroutineObj::Done) {
        // Yielded nextValue - buffer it and create a lazy tail
        bufferedValue_ = peek;
        auto* tail = new ListNode(listType_);
        tail->generator_ = this;
        owner->tail_ = tail;
    } else {
        // Done - no more values
        owner->tail_ = nullptr;
    }
}

// ============================================================================
// List builtins (create generators)
// ============================================================================

// toList(Array[T]) -> List[T]  (lazy)
static void builtin_toList_array(VM& vm, u16 dst, u16, u16 argBase) {
    auto* arr = vm.reg(argBase).o;
    auto* arrType = static_cast<ArrayType*>(arr->type_);
    Type* elemType = arrType->elemType_;
    auto* listType = vm.listType(elemType);
    size_t sz = getArraySize(vm, arr, elemType);
    if (sz == 0) { vm.reg(dst).o = nullptr; return; }
    auto* node = new ListNode(listType);
    auto* gen = new ArrayToListGen(vm.typeType());
    gen->array_ = arr; gen->index_ = 0;
    gen->elemType_ = elemType; gen->listType_ = listType;
    node->generator_ = gen; vm.reg(dst).o = node;
}

// toList(Coroutine[T]) -> List[T]  (lazy)
static void builtin_toList_coroutine(VM& vm, u16 dst, u16, u16 argBase) {
    auto* coro = static_cast<CoroutineObj*>(vm.reg(argBase).o);
    auto* coroType = static_cast<CoroutineType*>(coro->type_);
    Type* elemType = coroType->yieldType_;
    auto* listType = vm.listType(elemType);

    if (coro->state_ == CoroutineObj::Done) {
        vm.reg(dst).o = nullptr;
        return;
    }

    // Eagerly resume once to get the first value (and handle empty coroutines)
    Word first = syncResumeCoroutine(vm, coro);

    if (coro->state_ == CoroutineObj::Done) {
        // Coroutine yielded nothing
        vm.reg(dst).o = nullptr;
        return;
    }

    // Create the first list node with the first value set directly
    auto* node = new ListNode(listType);
    node->head_ = first;

    // Peek ahead for the second value
    if (coro->state_ == CoroutineObj::Done) {
        node->tail_ = nullptr;
    } else {
        Word second = syncResumeCoroutine(vm, coro);
        if (coro->state_ == CoroutineObj::Done) {
            // Only one value
            node->tail_ = nullptr;
        } else {
            // Buffer the second value in the generator
            auto* gen = new CoroutineListGen(vm.typeType());
            gen->coro_ = coro;
            gen->listType_ = listType;
            gen->bufferedValue_ = second;
            gen->valueIsObj_ = elemType->isObjType();
            auto* tail = new ListNode(listType);
            tail->generator_ = gen;
            node->tail_ = tail;
        }
    }

    vm.reg(dst).o = node;
}

// collect(List[T], Int) -> Array[T]  — collect at most n elements
static void builtin_collect(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    Type* elemType = lt->elemType_;
    auto* arrType = vm.arrayType(elemType);
    auto* arr = makeEmptyArray(arrType);
    ListNode* cur = src;
    for (i64 i = 0; i < n && cur; i++) {
        if (cur->generator_) cur->force(vm);
        arrayPush(vm, arr, elemType, cur->head_);
        cur = cur->tail_;
    }
    vm.reg(dst).o = arr;
}

static void builtin_take_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    if (n <= 0 || !src) { vm.reg(dst).o = nullptr; return; }
    auto* node = new ListNode(lt);
    auto* gen = new TakeListGen(vm.typeType());
    gen->source_ = src; gen->remaining_ = n; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_drop_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    if (n <= 0) { vm.reg(dst).o = src; return; }
    // Eagerly skip n elements
    ListNode* cur = src;
    i64 rem = n;
    while (rem > 0 && cur) {
        if (cur->generator_) cur->force(vm);
        cur = cur->tail_; rem--;
    }
    vm.reg(dst).o = cur;  // nullptr if list is shorter than n
}

static void builtin_stride_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    if (n <= 0 || !src) { vm.reg(dst).o = nullptr; return; }
    auto* node = new ListNode(lt);
    auto* gen = new StrideListGen(vm.typeType());
    gen->source_ = src; gen->stride_ = n; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_stutter_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    if (n <= 0 || !src) { vm.reg(dst).o = nullptr; return; }
    auto* node = new ListNode(lt);
    auto* gen = new StutterListGen(vm.typeType());
    gen->source_ = src; gen->repeatCount_ = n; gen->currentRepeat_ = 0;
    gen->valueIsObj_ = lt->elemType_->isObjType(); gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_cat_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<ListNode*>(vm.reg(ab).o);
    auto* b = static_cast<ListNode*>(vm.reg(ab+1).o);
    if (!a) { vm.reg(dst).o = b; return; }
    if (!b) { vm.reg(dst).o = a; return; }
    auto* lt = static_cast<ListType*>(a->type_);
    auto* node = new ListNode(lt);
    auto* gen = new CatListGen(vm.typeType());
    gen->first_ = a; gen->second_ = b; gen->inSecond_ = false; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_cyc_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new CycleListGen(vm.typeType());
    gen->current_ = src; gen->head_ = src; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_ncyc_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    if (!src || n <= 0) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new NCycleListGen(vm.typeType());
    gen->current_ = src; gen->head_ = src; gen->remaining_ = n - 1; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_hang_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new HangListGen(vm.typeType());
    gen->source_ = src; gen->hasLast_ = false;
    gen->valueIsObj_ = lt->elemType_->isObjType(); gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_map_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* resET = fnType->returnType_;
    auto* resLT = vm.listType(resET);
    auto* node = new ListNode(resLT);
    auto* gen = new MapListGen(vm.typeType());
    gen->source_ = src; gen->fn_ = fn;
    gen->resultElemType_ = resET; gen->resultListType_ = resLT;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_filter_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    // Eagerly find the first element that passes the predicate
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src;
    while (cur) {
        if (cur->generator_) cur->force(vm);
        vm.reg(sb) = cur->head_;
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) break;
        cur = cur->tail_;
    }
    if (!cur) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    // Build the first node eagerly with the found element
    auto* node = new ListNode(lt);
    node->head_ = cur->head_;
    ListNode* rest = cur->tail_;
    if (!rest) { node->tail_ = nullptr; }
    else {
        auto* tail = new ListNode(lt);
        auto* gen = new FilterListGen(vm.typeType());
        gen->source_ = rest; gen->fn_ = fn; gen->listType_ = lt;
        tail->generator_ = gen; node->tail_ = tail;
    }
    vm.reg(dst).o = node;
}

static void builtin_fold_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src;
    while (cur) {
        if (cur->generator_) cur->force(vm);
        vm.reg(sb) = acc; vm.reg(sb+1) = cur->head_;
        callTwoArgs(vm, fn, sb); acc = vm.reg(sb);
        cur = cur->tail_;
    }
    vm.reg(dst) = acc;
}

static void builtin_scan_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* accET = fnType->returnType_;
    auto* resLT = vm.listType(accET);
    auto* node = new ListNode(resLT);
    auto* gen = new ScanListGen(vm.typeType());
    gen->source_ = src; gen->fn_ = fn; gen->accumulator_ = acc;
    gen->accIsObj_ = accET->isObjType();
    gen->accElemType_ = accET; gen->resultListType_ = resLT;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_fold1_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).i = 0; return; }
    if (src->generator_) src->force(vm);
    Word acc = src->head_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src->tail_;
    while (cur) {
        if (cur->generator_) cur->force(vm);
        vm.reg(sb) = acc; vm.reg(sb+1) = cur->head_;
        callTwoArgs(vm, fn, sb); acc = vm.reg(sb);
        cur = cur->tail_;
    }
    vm.reg(dst) = acc;
}

static void builtin_scan1_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    if (src->generator_) src->force(vm);
    auto* lt = static_cast<ListType*>(src->type_);
    Type* et = lt->elemType_;
    auto* node = new ListNode(lt);
    auto* gen = new ScanListGen(vm.typeType());
    gen->source_ = src->tail_; gen->fn_ = fn; gen->accumulator_ = src->head_;
    gen->accIsObj_ = et->isObjType();
    gen->accElemType_ = et; gen->resultListType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_iter(VM& vm, u16 dst, u16, u16 ab) {
    Word init = vm.reg(ab);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* et = fnType->returnType_;
    auto* lt = vm.listType(et);
    auto* node = new ListNode(lt);
    auto* gen = new IterListGen(vm.typeType());
    gen->current_ = init; gen->fn_ = fn;
    gen->valueIsObj_ = et->isObjType(); gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_find_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    u16 sb = vm.currentCodeBlock()->numRegs;
    i64 idx = 0;
    ListNode* cur = src;
    while (cur) {
        if (cur->generator_) cur->force(vm);
        vm.reg(sb) = cur->head_;
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) { vm.reg(dst).i = idx; return; }
        cur = cur->tail_; idx++;
    }
    vm.reg(dst).i = -1;
}

static void builtin_takeWhile_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    // Eagerly check the first element before creating the result node
    if (src->generator_) src->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = src->head_;
    callOneArg(vm, fn, sb);
    if (!vm.reg(sb).i) { vm.reg(dst).o = nullptr; return; }
    // First element passes — create a node with a generator that will copy
    // src->head_ and then check-ahead for the next element
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new PredicateListGen(vm.typeType());
    gen->mode_ = PredicateListGen::TakeWhile;
    gen->source_ = src; gen->fn_ = fn; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_dropWhile_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new PredicateListGen(vm.typeType());
    gen->mode_ = PredicateListGen::DropWhile; gen->dropping_ = true;
    gen->source_ = src; gen->fn_ = fn; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_zip_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<ListNode*>(vm.reg(ab).o);
    auto* b = static_cast<ListNode*>(vm.reg(ab+1).o);
    if (!a || !b) { vm.reg(dst).o = nullptr; return; }
    auto* ltA = static_cast<ListType*>(a->type_);
    auto* ltB = static_cast<ListType*>(b->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(ltA->elemType_); fields.push_back(ltB->elemType_);
    auto* tt = vm.tupleType(fields);
    auto* resLT = vm.listType(tt);
    auto* node = new ListNode(resLT);
    auto* gen = new ZipListGen(vm.typeType());
    gen->left_ = a; gen->right_ = b;
    gen->leftElemType_ = ltA->elemType_; gen->rightElemType_ = ltB->elemType_;
    gen->resultListType_ = resLT; gen->tupleType_ = tt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_enumerate_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(vm.intType()); fields.push_back(lt->elemType_);
    auto* tt = vm.tupleType(fields);
    auto* resLT = vm.listType(tt);
    auto* node = new ListNode(resLT);
    auto* gen = new EnumerateListGen(vm.typeType());
    gen->source_ = src; gen->index_ = 0;
    gen->elemType_ = lt->elemType_; gen->resultListType_ = resLT; gen->tupleType_ = tt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_join_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* outerLT = static_cast<ListType*>(src->type_);
    auto* innerLT = dynamic_cast<ListType*>(outerLT->elemType_);
    if (!innerLT) { vm.reg(dst).o = src; return; }
    // Eagerly find the first non-empty inner list
    ListNode* outer = src;
    ListNode* inner = nullptr;
    while (outer) {
        if (outer->generator_) outer->force(vm);
        inner = static_cast<ListNode*>(outer->head_.o);
        outer = outer->tail_;
        if (inner) break;
    }
    if (!inner) { vm.reg(dst).o = nullptr; return; }
    auto* node = new ListNode(innerLT);
    auto* gen = new JoinListGen(vm.typeType());
    gen->outer_ = outer; gen->inner_ = inner; gen->resultListType_ = innerLT;
    node->generator_ = gen; vm.reg(dst).o = node;
}

static void builtin_flatten_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    // Count List nesting depth
    int depth = 0;
    Type* cur = lt->elemType_;
    while (auto* inner = dynamic_cast<ListType*>(cur)) {
        depth++; cur = inner->elemType_;
    }
    if (depth == 0) { vm.reg(dst).o = src; return; }
    // Apply join lazily `depth` times
    ListNode* result = src;
    for (int i = 0; i < depth; i++) {
        auto* outerLT = static_cast<ListType*>(result->type_);
        auto* innerLT = static_cast<ListType*>(outerLT->elemType_);
        // Eagerly find first non-empty inner list
        ListNode* outer = result;
        ListNode* inner = nullptr;
        while (outer) {
            if (outer->generator_) outer->force(vm);
            inner = static_cast<ListNode*>(outer->head_.o);
            outer = outer->tail_;
            if (inner) break;
        }
        if (!inner) { vm.reg(dst).o = nullptr; return; }
        auto* node = new ListNode(innerLT);
        auto* gen = new JoinListGen(vm.typeType());
        gen->outer_ = outer; gen->inner_ = inner; gen->resultListType_ = innerLT;
        node->generator_ = gen;
        result = node;
    }
    vm.reg(dst).o = result;
}

// ============================================================================
// Template resolvers
// ============================================================================

// Helper to register a template builtin
static void registerTemplate(Compiler& compiler,
    std::unordered_map<std::string, std::vector<FuncInfo>>& functions,
    const std::string& name, BuiltinTemplateResolver resolver,
    bool rtSafe = true)
{
    FuncInfo info{};
    info.isTemplate = true;
    info.builtinTemplate = resolver;
    info.bodyChecked = true;
    info.isBuiltin = true;
    info.rtSafe = rtSafe;
    functions[name].push_back(info);
}

// --- Resolvers for Array[T] -> Array[T] unary ops ---
#define RESOLVE_ARRAY_UNARY(fname, cfun) \
static bool resolve_##fname(Compiler& compiler, const std::vector<Type*>& args, \
    std::vector<Type*>& pt, Type*& rt, CFun& cf) { \
    if (args.size() != 1) return false; \
    auto* at = dynamic_cast<ArrayType*>(args[0]); \
    if (!at) return false; \
    pt = {at}; rt = at; cf = cfun; return true; \
}

RESOLVE_ARRAY_UNARY(reverse_a, builtin_reverse_array)
RESOLVE_ARRAY_UNARY(pop_a, builtin_pop_array)
RESOLVE_ARRAY_UNARY(muss_a, builtin_muss_array)
#undef RESOLVE_ARRAY_UNARY

// pick: Array[T] -> T  (choose a random element)
static bool resolve_pick(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at) return false;
    pt = {at}; rt = at->elemType_; cf = builtin_pick_array; return true;
}

// picks: Array<T> -> List<T>  or  (Array<T>, Int) -> Array<T>
static bool resolve_picks(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() == 1) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at) return false;
        pt = {at}; rt = compiler.listType(at->elemType_); cf = builtin_picks_list; return true;
    }
    if (args.size() == 2) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at || args[1] != compiler.intType()) return false;
        pt = {at, compiler.intType()}; rt = at; cf = builtin_picks_array; return true;
    }
    return false;
}

// sort: (Array[Int|Float|String]) or (Array[T], (T,T)->Bool)
static bool resolve_sort(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() == 1) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at) return false;
        Type* et = at->elemType_;
        if (et == compiler.intType()) cf = builtin_sort_int_array;
        else if (et == compiler.floatType()) cf = builtin_sort_float_array;
        else if (et == compiler.stringType()) cf = builtin_sort_string_array;
        else return false;
        pt = {at}; rt = at; return true;
    }
    if (args.size() == 2) {
        auto* at = dynamic_cast<ArrayType*>(args[0]);
        if (!at) return false;
        auto* ft = dynamic_cast<FunctionType*>(args[1]);
        if (!ft || ft->argTypes_.size() != 2) return false;
        Type* et = at->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et) return false;
        if (ft->returnType_ != compiler.boolType()) return false;
        pt = {at, args[1]}; rt = at; cf = builtin_sort_by_array; return true;
    }
    return false;
}

// grade: (Array[T], (T,T)->Bool) -> Array[Int]
static bool resolve_grade(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    Type* et = at->elemType_;
    if (ft->argTypes_[0] != et || ft->argTypes_[1] != et) return false;
    if (ft->returnType_ != compiler.boolType()) return false;
    pt = {at, args[1]}; rt = compiler.arrayType(compiler.intType());
    cf = builtin_grade_array; return true;
}

// push: Array[T], T -> Array[T]
static bool resolve_push(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at || args[1] != at->elemType_) return false;
    pt = {at, at->elemType_}; rt = at; cf = builtin_push_array; return true;
}

// --- Array[T], Int -> Array[T] ---
#define RESOLVE_ARRAY_INT(fname, cfun_a, cfun_l) \
static bool resolve_##fname(Compiler& compiler, const std::vector<Type*>& args, \
    std::vector<Type*>& pt, Type*& rt, CFun& cf) { \
    if (args.size() != 2 || args[1] != compiler.intType()) return false; \
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) { \
        pt = {at, compiler.intType()}; rt = at; cf = cfun_a; return true; \
    } \
    if (auto* lt = dynamic_cast<ListType*>(args[0])) { \
        pt = {lt, compiler.intType()}; rt = lt; cf = cfun_l; return true; \
    } \
    return false; \
}

RESOLVE_ARRAY_INT(take, builtin_take_array, builtin_take_list)
RESOLVE_ARRAY_INT(drop, builtin_drop_array, builtin_drop_list)
RESOLVE_ARRAY_INT(stride, builtin_stride_array, builtin_stride_list)
RESOLVE_ARRAY_INT(stutter, builtin_stutter_array, builtin_stutter_list)
#undef RESOLVE_ARRAY_INT

// cat: (Array[T], Array[T]) -> Array[T]  or  (List[T], List[T]) -> List[T]
static bool resolve_cat(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (args[1] != at) return false;
        pt = {at, at}; rt = at; cf = builtin_cat_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (args[1] != lt) return false;
        pt = {lt, lt}; rt = lt; cf = builtin_cat_list; return true;
    }
    return false;
}

// join/flatten: Array[Array[T]] -> Array[T]  or  List[List[T]] -> List[T]
static bool resolve_join(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (dynamic_cast<ArrayType*>(at->elemType_)) {
            pt = {at}; rt = compiler.arrayType(static_cast<ArrayType*>(at->elemType_)->elemType_);
            cf = builtin_join_array; return true;
        }
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (dynamic_cast<ListType*>(lt->elemType_)) {
            pt = {lt}; rt = static_cast<ListType*>(lt->elemType_);
            cf = builtin_join_list; return true;
        }
    }
    return false;
}

static bool resolve_flatten(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    // Array: unwrap all Array layers to find leaf type
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Type* leaf = at->elemType_;
        bool nested = false;
        while (auto* inner = dynamic_cast<ArrayType*>(leaf)) {
            leaf = inner->elemType_; nested = true;
        }
        if (!nested) return false;
        pt = {at}; rt = compiler.arrayType(leaf);
        cf = builtin_flatten_array; return true;
    }
    // List: unwrap all List layers to find leaf type
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Type* leaf = lt->elemType_;
        bool nested = false;
        while (auto* inner = dynamic_cast<ListType*>(leaf)) {
            leaf = inner->elemType_; nested = true;
        }
        if (!nested) return false;
        pt = {lt}; rt = compiler.listType(leaf);
        cf = builtin_flatten_list; return true;
    }
    return false;
}

// length: Array[T] -> Int
static void builtin_length_array(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = vm.reg(ab).o;
    auto* at = static_cast<ArrayType*>(src->type_);
    vm.reg(dst).i = (i64)getArraySize(vm, src, at->elemType_);
}

// length: List[T] -> Int  (WARNING: forces entire list; infinite lists will hang)
static void builtin_length_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    i64 count = 0;
    while (node) {
        if (node->generator_) node->force(vm);
        ++count;
        node = node->tail_;
    }
    vm.reg(dst).i = count;
}

// head: List[T] -> T  (returns first element; undefined on nil list)
static void builtin_head_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    if (node->generator_) node->force(vm);
    vm.reg(dst) = node->head_;
}

// tail: List[T] -> List[T]  (returns rest of list; undefined on nil list)
static void builtin_tail_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    if (node->generator_) node->force(vm);
    vm.reg(dst).o = node->tail_;
}

// cons: (T, List[T]) -> List[T]  (prepend element to list)
// Per-value-type variants needed because nil lists have no runtime type info.
#define CONS_LIST_VALUETYPE(suffix, typeGetter) \
static void builtin_cons_list_##suffix(VM& vm, u16 dst, u16, u16 ab) { \
    Word elem = vm.reg(ab); \
    auto* tail = static_cast<ListNode*>(vm.reg(ab+1).o); \
    auto* lt = tail ? static_cast<ListType*>(tail->type_) \
                    : vm.listType(vm.typeGetter()); \
    auto* node = new ListNode(lt); \
    node->head_ = elem; \
    node->tail_ = tail; \
    vm.reg(dst).o = node; \
}
CONS_LIST_VALUETYPE(int,    intType)
CONS_LIST_VALUETYPE(float,  floatType)
CONS_LIST_VALUETYPE(bool,   boolType)
CONS_LIST_VALUETYPE(symbol, symbolType)
#undef CONS_LIST_VALUETYPE

// cons for Obj element types — derive ListType from the element's type
static void builtin_cons_list_obj(VM& vm, u16 dst, u16, u16 ab) {
    Word elem = vm.reg(ab);
    auto* tail = static_cast<ListNode*>(vm.reg(ab+1).o);
    auto* lt = tail ? static_cast<ListType*>(tail->type_)
                    : vm.listType(elem.o->type_);
    auto* node = new ListNode(lt);
    node->head_ = elem;
    node->tail_ = tail;
    vm.reg(dst).o = node;
}

// isNil: List[T] -> Bool
static void builtin_isNil_list(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (vm.reg(ab).o == nullptr) ? 1 : 0;
}

// notNil: List[T] -> Bool
static void builtin_notNil_list(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (vm.reg(ab).o != nullptr) ? 1 : 0;
}

// ============================================================================
// Map builtins
// ============================================================================

// length: [K:V] -> Int
static void builtin_length_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    vm.reg(dst).i = (i64)map->entries_.size();
}

// get: [K:V], K -> Option<V>
static void builtin_get_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    Word key = vm.reg(ab + 1);
    auto* mt = static_cast<MapType*>(map->type_);
    auto* optType = vm.optionType(mt->valueType_);
    auto* e = new Enum(optType);
    auto it = map->entries_.find(key);
    if (it != map->entries_.end()) {
        e->which_ = 0;  // some
        e->word_ = it->second;
    } else {
        e->which_ = 1;  // none
        e->word_.i = 0;
    }
    vm.reg(dst).o = e;
}

// get: [K:V], K, V -> V  (with default)
static void builtin_get_map_default(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    Word key = vm.reg(ab + 1);
    auto it = map->entries_.find(key);
    if (it != map->entries_.end()) {
        vm.reg(dst) = it->second;
    } else {
        vm.reg(dst) = vm.reg(ab + 2);
    }
}

// put: [K:V], K, V -> [K:V]  (return new map with entry added/updated)
static void builtin_put_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(src->type_);
    auto* result = new MapObj(mt);
    result->entries_ = src->entries_;
    result->entries_[vm.reg(ab + 1)] = vm.reg(ab + 2);
    vm.reg(dst).o = result;
}

// remove: [K:V], K -> [K:V]
static void builtin_remove_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(src->type_);
    auto* result = new MapObj(mt);
    result->entries_ = src->entries_;
    result->entries_.erase(vm.reg(ab + 1));
    vm.reg(dst).o = result;
}

// contains: [K:V], K -> Bool
static void builtin_contains_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    vm.reg(dst).i = map->entries_.count(vm.reg(ab + 1)) ? 1 : 0;
}

// keys: [K:V] -> Array[K]
static void builtin_keys_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    Type* keyType = mt->keyType_;
    auto* arrType = vm.arrayType(keyType);

    if (keyType == vm.intType() || keyType == vm.boolType() || keyType == vm.symbolType()) {
        auto* arr = new PodArray<i64>(arrType);
        for (auto& [k, v] : map->entries_) arr->v.push_back(k.i);
        vm.reg(dst).o = arr;
    } else if (keyType == vm.floatType()) {
        auto* arr = new PodArray<f64>(arrType);
        for (auto& [k, v] : map->entries_) arr->v.push_back(k.f);
        vm.reg(dst).o = arr;
    } else {
        auto* arr = new ObjArray(arrType);
        for (auto& [k, v] : map->entries_) arr->v.push_back(k.o);
        vm.reg(dst).o = arr;
    }
}

// values: [K:V] -> Array[V]
static void builtin_values_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    Type* valType = mt->valueType_;
    auto* arrType = vm.arrayType(valType);

    if (valType == vm.intType() || valType == vm.boolType() || valType == vm.symbolType()) {
        auto* arr = new PodArray<i64>(arrType);
        for (auto& [k, v] : map->entries_) arr->v.push_back(v.i);
        vm.reg(dst).o = arr;
    } else if (valType == vm.floatType()) {
        auto* arr = new PodArray<f64>(arrType);
        for (auto& [k, v] : map->entries_) arr->v.push_back(v.f);
        vm.reg(dst).o = arr;
    } else {
        auto* arr = new ObjArray(arrType);
        for (auto& [k, v] : map->entries_) arr->v.push_back(v.o);
        vm.reg(dst).o = arr;
    }
}

// merge: [K:V], [K:V] -> [K:V]
static void builtin_merge_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<MapObj*>(vm.reg(ab).o);
    auto* b = static_cast<MapObj*>(vm.reg(ab + 1).o);
    auto* mt = static_cast<MapType*>(a->type_);
    auto* result = new MapObj(mt);
    result->entries_ = a->entries_;
    for (auto& [k, v] : b->entries_) {
        result->entries_[k] = v;
    }
    vm.reg(dst).o = result;
}

// Forward declarations for set builtins used in shared resolvers
static void builtin_remove_set(VM& vm, u16 dst, u16, u16 ab);
static void builtin_contains_set(VM& vm, u16 dst, u16, u16 ab);

// Map resolvers
static bool resolve_get_map(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() == 2) {
        auto* mt = dynamic_cast<MapType*>(args[0]);
        if (!mt) return false;
        if (args[1] != mt->keyType_) return false;
        pt = {mt, mt->keyType_}; rt = compiler.optionType(mt->valueType_); cf = builtin_get_map; return true;
    }
    if (args.size() == 3) {
        auto* mt = dynamic_cast<MapType*>(args[0]);
        if (!mt) return false;
        if (args[1] != mt->keyType_) return false;
        if (args[2] != mt->valueType_) return false;
        pt = {mt, mt->keyType_, mt->valueType_}; rt = mt->valueType_; cf = builtin_get_map_default; return true;
    }
    return false;
}

static bool resolve_put(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 3) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    if (args[1] != mt->keyType_ || args[2] != mt->valueType_) return false;
    pt = {mt, mt->keyType_, mt->valueType_}; rt = mt; cf = builtin_put_map; return true;
}

static bool resolve_remove(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* mt = dynamic_cast<MapType*>(args[0])) {
        if (args[1] != mt->keyType_) return false;
        pt = {mt, mt->keyType_}; rt = mt; cf = builtin_remove_map; return true;
    }
    if (auto* st = dynamic_cast<SetType*>(args[0])) {
        if (args[1] != st->elemType_) return false;
        pt = {st, st->elemType_}; rt = st; cf = builtin_remove_set; return true;
    }
    return false;
}

static bool resolve_contains(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* mt = dynamic_cast<MapType*>(args[0])) {
        if (args[1] != mt->keyType_) return false;
        pt = {mt, mt->keyType_}; rt = compiler.boolType(); cf = builtin_contains_map; return true;
    }
    if (auto* st = dynamic_cast<SetType*>(args[0])) {
        if (args[1] != st->elemType_) return false;
        pt = {st, st->elemType_}; rt = compiler.boolType(); cf = builtin_contains_set; return true;
    }
    return false;
}

static bool resolve_keys(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    pt = {mt}; rt = compiler.arrayType(mt->keyType_); cf = builtin_keys_map; return true;
}

static bool resolve_values(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    pt = {mt}; rt = compiler.arrayType(mt->valueType_); cf = builtin_values_map; return true;
}

static bool resolve_merge(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    if (args[1] != mt) return false;
    pt = {mt, mt}; rt = mt; cf = builtin_merge_map; return true;
}

// pairs: [K:V] -> Array[(K, V)]
static void builtin_pairs_map(VM& vm, u16 dst, u16, u16 ab) {
    auto* map = static_cast<MapObj*>(vm.reg(ab).o);
    auto* mt = static_cast<MapType*>(map->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc};
    fields.push_back(mt->keyType_);
    fields.push_back(mt->valueType_);
    auto* tt = vm.tupleType(fields);
    auto* resAT = vm.arrayType(tt);
    auto* result = new ObjArray(resAT);
    for (auto& [k, v] : map->entries_) {
        auto* tup = Tuple::create(tt, 2);
        tup->v[0] = k;
        tup->v[1] = v;
        result->v.push_back(tup);
    }
    vm.reg(dst).o = result;
}

static bool resolve_pairs(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* mt = dynamic_cast<MapType*>(args[0]);
    if (!mt) return false;
    auto alloc = rt::STLAllocator<Type*>{nullptr};
    Vec<Type*> fields{alloc};
    fields.push_back(mt->keyType_);
    fields.push_back(mt->valueType_);
    auto* tt = compiler.tupleType(fields);
    pt = {mt}; rt = compiler.arrayType(tt); cf = builtin_pairs_map; return true;
}

// ============================================================================
// Option builtins
// ============================================================================

static bool isOptionType(Type* t) {
    auto* et = dynamic_cast<EnumType*>(t);
    if (!et) return false;
    auto s = et->name_->str();
    return s.substr(0, 7) == "Option<";
}

// unwrap: Option<T> -> T
static void builtin_unwrap(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    if (e->which_ != 0) {
        fprintf(vm.printOutput(), "Error: unwrap called on none\n");
        vm.setHalted(true);
        return;
    }
    auto* et = static_cast<EnumType*>(e->type_);
    if (et->cases_[0].type->isObjType()) {
        vm.reg(dst).o = e->word_.o;
    } else {
        vm.reg(dst) = e->word_;
    }
}

// unwrapOr: Option<T>, T -> T
static void builtin_unwrapOr(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    if (e->which_ == 0) {
        auto* et = static_cast<EnumType*>(e->type_);
        if (et->cases_[0].type->isObjType()) {
            vm.reg(dst).o = e->word_.o;
        } else {
            vm.reg(dst) = e->word_;
        }
    } else {
        vm.reg(dst) = vm.reg(ab + 1);
    }
}

// isSome: Option<T> -> Bool
static void builtin_isSome(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    vm.reg(dst).i = (e->which_ == 0) ? 1 : 0;
}

// isNone: Option<T> -> Bool
static void builtin_isNone(VM& vm, u16 dst, u16, u16 ab) {
    auto* e = static_cast<Enum*>(vm.reg(ab).o);
    vm.reg(dst).i = (e->which_ == 1) ? 1 : 0;
}

// Option resolvers
static bool resolve_unwrap(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = et->cases_[0].type; cf = builtin_unwrap; return true;
}

static bool resolve_unwrapOr(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    Type* innerType = et->cases_[0].type;
    if (args[1] != innerType) return false;
    pt = {et, innerType}; rt = innerType; cf = builtin_unwrapOr; return true;
}

static bool resolve_isSome(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = compiler.boolType(); cf = builtin_isSome; return true;
}

static bool resolve_isNone(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* et = dynamic_cast<EnumType*>(args[0]);
    if (!et || !isOptionType(et)) return false;
    pt = {et}; rt = compiler.boolType(); cf = builtin_isNone; return true;
}

// next: Coroutine<T> -> Option<T>  (coroutine resume — handled by op_coro_resume in codegen)
static bool resolve_next(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* ct = dynamic_cast<CoroutineType*>(args[0]);
    if (!ct) return false;
    pt = {ct};
    rt = compiler.optionType(ct->yieldType_);
    cf = nullptr;  // codegen handles this specially via op_coro_resume
    return true;
}

// yield: T -> Void  (coroutine yield — handled by op_yield in codegen)
static bool resolve_yield(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    pt = {args[0]};
    rt = compiler.voidType();
    cf = nullptr;  // codegen handles this specially via op_yield
    return true;
}

// yieldAll: Coroutine<T> -> Void  (drain inner coroutine, yielding each value)
static bool resolve_yieldAll(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* ct = dynamic_cast<CoroutineType*>(args[0]);
    if (!ct) return false;
    pt = {ct};
    rt = compiler.voidType();
    cf = nullptr;  // codegen handles this specially
    return true;
}

// ============================================================================
// Set builtins
// ============================================================================

// length: Set<T> -> Int
static void builtin_length_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    vm.reg(dst).i = (i64)set->entries_.size();
}

// add: Set<T>, T -> Set<T>
static void builtin_add_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(src->type_);
    auto* result = new SetObj(st);
    result->entries_ = src->entries_;
    result->entries_.insert(vm.reg(ab + 1));
    vm.reg(dst).o = result;
}

// remove: Set<T>, T -> Set<T>
static void builtin_remove_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(src->type_);
    auto* result = new SetObj(st);
    result->entries_ = src->entries_;
    result->entries_.erase(vm.reg(ab + 1));
    vm.reg(dst).o = result;
}

// contains: Set<T>, T -> Bool
static void builtin_contains_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    vm.reg(dst).i = set->entries_.count(vm.reg(ab + 1)) ? 1 : 0;
}

// union: Set<T>, Set<T> -> Set<T>
static void builtin_union_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    result->entries_ = a->entries_;
    for (auto& elem : b->entries_) result->entries_.insert(elem);
    vm.reg(dst).o = result;
}

// intersection: Set<T>, Set<T> -> Set<T>
static void builtin_intersection_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    for (auto& elem : a->entries_) {
        if (b->entries_.count(elem)) result->entries_.insert(elem);
    }
    vm.reg(dst).o = result;
}

// difference: Set<T>, Set<T> -> Set<T>
static void builtin_difference_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<SetObj*>(vm.reg(ab).o);
    auto* b = static_cast<SetObj*>(vm.reg(ab + 1).o);
    auto* st = static_cast<SetType*>(a->type_);
    auto* result = new SetObj(st);
    for (auto& elem : a->entries_) {
        if (!b->entries_.count(elem)) result->entries_.insert(elem);
    }
    vm.reg(dst).o = result;
}

// toArray: Set<T> -> Array[T]
static void builtin_toArray_set(VM& vm, u16 dst, u16, u16 ab) {
    auto* set = static_cast<SetObj*>(vm.reg(ab).o);
    auto* st = static_cast<SetType*>(set->type_);
    Type* elemType = st->elemType_;
    auto* arrType = vm.arrayType(elemType);

    if (elemType == vm.intType() || elemType == vm.boolType() || elemType == vm.symbolType()) {
        auto* arr = new PodArray<i64>(arrType);
        for (auto& elem : set->entries_) arr->v.push_back(elem.i);
        vm.reg(dst).o = arr;
    } else if (elemType == vm.floatType()) {
        auto* arr = new PodArray<f64>(arrType);
        for (auto& elem : set->entries_) arr->v.push_back(elem.f);
        vm.reg(dst).o = arr;
    } else {
        auto* arr = new ObjArray(arrType);
        for (auto& elem : set->entries_) arr->v.push_back(elem.o);
        vm.reg(dst).o = arr;
    }
}

// Set resolvers
static bool resolve_add_set(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st) return false;
    if (args[1] != st->elemType_) return false;
    pt = {st, st->elemType_}; rt = st; cf = builtin_add_set; return true;
}

static bool resolve_union(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st || args[1] != st) return false;
    pt = {st, st}; rt = st; cf = builtin_union_set; return true;
}

static bool resolve_intersection(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st || args[1] != st) return false;
    pt = {st, st}; rt = st; cf = builtin_intersection_set; return true;
}

static bool resolve_difference(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st || args[1] != st) return false;
    pt = {st, st}; rt = st; cf = builtin_difference_set; return true;
}

static bool resolve_toArray_set(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* st = dynamic_cast<SetType*>(args[0]);
    if (!st) return false;
    pt = {st}; rt = compiler.arrayType(st->elemType_); cf = builtin_toArray_set; return true;
}

// toList: Array[T] -> List[T]
static bool resolve_toList_array(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* at = dynamic_cast<ArrayType*>(args[0]);
    if (!at) return false;
    pt = {at}; rt = compiler.listType(at->elemType_); cf = builtin_toList_array; return true;
}

// toList: Coroutine[T] -> List[T]
static bool resolve_toList_coroutine(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* ct = dynamic_cast<CoroutineType*>(args[0]);
    if (!ct) return false;
    pt = {ct}; rt = compiler.listType(ct->yieldType_); cf = builtin_toList_coroutine; return true;
}

// collect: (List[T], Int) -> Array[T]
static bool resolve_collect(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2 || args[1] != compiler.intType()) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt, compiler.intType()}; rt = compiler.arrayType(lt->elemType_); cf = builtin_collect; return true;
}

// length resolver: Array[T] -> Int  or  List[T] -> Int  or  Map[K,V] -> Int  or  Set<T> -> Int
static bool resolve_length(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        pt = {at}; rt = compiler.intType(); cf = builtin_length_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        pt = {lt}; rt = compiler.intType(); cf = builtin_length_list; return true;
    }
    if (auto* mt = dynamic_cast<MapType*>(args[0])) {
        pt = {mt}; rt = compiler.intType(); cf = builtin_length_map; return true;
    }
    if (auto* st = dynamic_cast<SetType*>(args[0])) {
        pt = {st}; rt = compiler.intType(); cf = builtin_length_set; return true;
    }
    return false;
}

// --- ordinal (enum case index) ---

static void builtin_ordinal_enum(VM& vm, u16 dst, u16, u16 argBase) {
    auto* e = static_cast<Enum*>(vm.reg(argBase).o);
    vm.reg(dst).i = e->which_;
}

static bool resolve_ordinal(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* et = dynamic_cast<EnumType*>(args[0])) {
        pt = {et}; rt = compiler.intType(); cf = builtin_ordinal_enum; return true;
    }
    return false;
}

// --- tag (enum case name as Symbol) ---

static void builtin_tag_enum(VM& vm, u16 dst, u16, u16 argBase) {
    auto* e = static_cast<Enum*>(vm.reg(argBase).o);
    auto* et = static_cast<EnumType*>(e->type_);
    vm.reg(dst).s = const_cast<Symbol*>(et->cases_[e->which_].name);
}

static bool resolve_tag(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    if (auto* et = dynamic_cast<EnumType*>(args[0])) {
        pt = {et}; rt = compiler.symbolType(); cf = builtin_tag_enum; return true;
    }
    return false;
}

// --- HOF resolvers ---

// map: (Array[T], (T)->U) -> Array[U]  or  (List[T], (T)->U) -> List[U]
static bool resolve_map(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[0] != at->elemType_) return false;
        pt = {at, args[1]}; rt = compiler.arrayType(ft->returnType_);
        cf = builtin_map_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[0] != lt->elemType_) return false;
        pt = {lt, args[1]}; rt = compiler.listType(ft->returnType_);
        cf = builtin_map_list; return true;
    }
    return false;
}

// filter: (Array[T], (T)->Bool) -> Array[T]  or  (List[T], (T)->Bool) -> List[T]
static bool resolve_filter(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1 || ft->returnType_ != compiler.boolType()) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[0] != at->elemType_) return false;
        pt = {at, args[1]}; rt = at; cf = builtin_filter_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[0] != lt->elemType_) return false;
        pt = {lt, args[1]}; rt = lt; cf = builtin_filter_list; return true;
    }
    return false;
}

// fold: (Array[T], U, (U,T)->U) -> U  or  (List[T], U, (U,T)->U) -> U
static bool resolve_fold(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 3) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[2]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (ft->argTypes_[0] != args[1] || ft->returnType_ != args[1]) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[1] != at->elemType_) return false;
        pt = {at, args[1], args[2]}; rt = args[1]; cf = builtin_fold_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[1] != lt->elemType_) return false;
        pt = {lt, args[1], args[2]}; rt = args[1]; cf = builtin_fold_list; return true;
    }
    return false;
}

// scan: (Array[T], U, (U,T)->U) -> Array[U]  or  (List[T], U, (U,T)->U) -> List[U]
static bool resolve_scan(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 3) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[2]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (ft->argTypes_[0] != args[1] || ft->returnType_ != args[1]) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[1] != at->elemType_) return false;
        pt = {at, args[1], args[2]}; rt = compiler.arrayType(args[1]);
        cf = builtin_scan_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[1] != lt->elemType_) return false;
        pt = {lt, args[1], args[2]}; rt = compiler.listType(args[1]);
        cf = builtin_scan_list; return true;
    }
    return false;
}

// fold1: (Array[T], (T,T)->T) -> T  or  (List[T], (T,T)->T) -> T
static bool resolve_fold1(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Type* et = at->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {at, args[1]}; rt = et; cf = builtin_fold1_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Type* et = lt->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {lt, args[1]}; rt = et; cf = builtin_fold1_list; return true;
    }
    return false;
}

// scan1: (Array[T], (T,T)->T) -> Array[T]  or  (List[T], (T,T)->T) -> List[T]
static bool resolve_scan1(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 2) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Type* et = at->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {at, args[1]}; rt = at; cf = builtin_scan1_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Type* et = lt->elemType_;
        if (ft->argTypes_[0] != et || ft->argTypes_[1] != et || ft->returnType_ != et) return false;
        pt = {lt, args[1]}; rt = lt; cf = builtin_scan1_list; return true;
    }
    return false;
}

// iter: (T, (T)->T) -> List[T]
static bool resolve_iter(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1) return false;
    Type* t = args[0];
    if (ft->argTypes_[0] != t || ft->returnType_ != t) return false;
    pt = {t, args[1]};
    rt = compiler.listType(t);
    cf = builtin_iter;
    return true;
}

// find: (Array[T], (T)->Bool) -> Int  or  (List[T], (T)->Bool) -> Int
static bool resolve_find(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[1]);
    if (!ft || ft->argTypes_.size() != 1 || ft->returnType_ != compiler.boolType()) return false;
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        if (ft->argTypes_[0] != at->elemType_) return false;
        pt = {at, args[1]}; rt = compiler.intType(); cf = builtin_find_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        if (ft->argTypes_[0] != lt->elemType_) return false;
        pt = {lt, args[1]}; rt = compiler.intType(); cf = builtin_find_list; return true;
    }
    return false;
}

// takeWhile/dropWhile: (Array[T], (T)->Bool) -> Array[T]  or  (List[T], (T)->Bool) -> List[T]
#define RESOLVE_PREDICATE_OP(fname, cfun_a, cfun_l) \
static bool resolve_##fname(Compiler& compiler, const std::vector<Type*>& args, \
    std::vector<Type*>& pt, Type*& rt, CFun& cf) { \
    if (args.size() != 2) return false; \
    auto* ft = dynamic_cast<FunctionType*>(args[1]); \
    if (!ft || ft->argTypes_.size() != 1 || ft->returnType_ != compiler.boolType()) return false; \
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) { \
        if (ft->argTypes_[0] != at->elemType_) return false; \
        pt = {at, args[1]}; rt = at; cf = cfun_a; return true; \
    } \
    if (auto* lt = dynamic_cast<ListType*>(args[0])) { \
        if (ft->argTypes_[0] != lt->elemType_) return false; \
        pt = {lt, args[1]}; rt = lt; cf = cfun_l; return true; \
    } \
    return false; \
}
RESOLVE_PREDICATE_OP(takeWhile, builtin_takeWhile_array, builtin_takeWhile_list)
RESOLVE_PREDICATE_OP(dropWhile, builtin_dropWhile_array, builtin_dropWhile_list)
#undef RESOLVE_PREDICATE_OP

// zip: (Array[T], Array[U]) -> Array[(T,U)]  or  (List[T], List[U]) -> List[(T,U)]
static bool resolve_zip(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (auto* atA = dynamic_cast<ArrayType*>(args[0])) {
        auto* atB = dynamic_cast<ArrayType*>(args[1]);
        if (!atB) return false;
        auto alloc = rt::STLAllocator<Type*>{nullptr};
        Vec<Type*> fields{alloc}; fields.push_back(atA->elemType_); fields.push_back(atB->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {atA, atB}; rt = compiler.arrayType(tt); cf = builtin_zip_array; return true;
    }
    if (auto* ltA = dynamic_cast<ListType*>(args[0])) {
        auto* ltB = dynamic_cast<ListType*>(args[1]);
        if (!ltB) return false;
        auto alloc = rt::STLAllocator<Type*>{nullptr};
        Vec<Type*> fields{alloc}; fields.push_back(ltA->elemType_); fields.push_back(ltB->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {ltA, ltB}; rt = compiler.listType(tt); cf = builtin_zip_list; return true;
    }
    return false;
}

// enumerate: Array[T] -> Array[(Int,T)]  or  List[T] -> List[(Int,T)]
static bool resolve_enumerate(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto alloc = rt::STLAllocator<Type*>{nullptr};
    if (auto* at = dynamic_cast<ArrayType*>(args[0])) {
        Vec<Type*> fields{alloc}; fields.push_back(compiler.intType()); fields.push_back(at->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {at}; rt = compiler.arrayType(tt); cf = builtin_enumerate_array; return true;
    }
    if (auto* lt = dynamic_cast<ListType*>(args[0])) {
        Vec<Type*> fields{alloc}; fields.push_back(compiler.intType()); fields.push_back(lt->elemType_);
        auto* tt = compiler.tupleType(fields);
        pt = {lt}; rt = compiler.listType(tt); cf = builtin_enumerate_list; return true;
    }
    return false;
}

// cyc: List[T] -> List[T]
static bool resolve_cyc(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt; cf = builtin_cyc_list; return true;
}

// ncyc: List[T], Int -> List[T]
static bool resolve_ncyc(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2 || args[1] != compiler.intType()) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt, compiler.intType()}; rt = lt; cf = builtin_ncyc_list; return true;
}

// hang: List[T] -> List[T]
static bool resolve_hang(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt; cf = builtin_hang_list; return true;
}

// head: List[T] -> T
static bool resolve_head(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt->elemType_; cf = builtin_head_list; return true;
}

// tail: List[T] -> List[T]
static bool resolve_tail(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = lt; cf = builtin_tail_list; return true;
}

// cons: (T, List[T]) -> List[T]
static bool resolve_cons(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* lt = dynamic_cast<ListType*>(args[1]);
    if (!lt) return false;
    if (args[0] != lt->elemType_) return false;
    pt = {lt->elemType_, lt}; rt = lt;
    // Dispatch to per-type variant so cons works even when the list is nil
    if (lt->elemType_ == compiler.intType())         cf = builtin_cons_list_int;
    else if (lt->elemType_ == compiler.floatType())   cf = builtin_cons_list_float;
    else if (lt->elemType_ == compiler.boolType())    cf = builtin_cons_list_bool;
    else if (lt->elemType_ == compiler.symbolType())  cf = builtin_cons_list_symbol;
    else                                              cf = builtin_cons_list_obj;
    return true;
}

// isNil: List[T] -> Bool
static bool resolve_isNil(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = compiler.boolType(); cf = builtin_isNil_list; return true;
}

// notNil: List[T] -> Bool
static bool resolve_notNil(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* lt = dynamic_cast<ListType*>(args[0]);
    if (!lt) return false;
    pt = {lt}; rt = compiler.boolType(); cf = builtin_notNil_list; return true;
}

// --- toString builtins ---

static void builtin_toString_bool(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = rt::vmstr(vm.reg(argBase).i ? "true" : "false");
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_int(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = rt::fmt("{}", vm.reg(argBase).i);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_float(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = rt::fmtFloat(vm.reg(argBase).f);
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_symbol(VM& vm, u16 dst, u16, u16 argBase) {
    auto* result = new StringObj();
    result->s = rt::vmstr(":");
    result->s += vm.reg(argBase).s->str();
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static void builtin_toString_obj(VM& vm, u16 dst, u16, u16 argBase) {
    Obj* obj = vm.reg(argBase).o;
    auto* result = new StringObj();
    result->s = obj ? obj->str() : rt::vmstr("nil");
    registerNewObj(result);
    vm.reg(dst).o = result;
}

static bool resolve_toString(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    Type* t = args[0];
    rt = compiler.stringType();
    pt = {t};
    if (t == compiler.boolType()) {
        cf = builtin_toString_bool; return true;
    }
    if (t == compiler.intType()) {
        cf = builtin_toString_int; return true;
    }
    if (t == compiler.floatType()) {
        cf = builtin_toString_float; return true;
    }
    if (t == compiler.symbolType()) {
        cf = builtin_toString_symbol; return true;
    }
    if (t->isObjType()) {
        cf = builtin_toString_obj; return true;
    }
    return false;
}

// --- fmt builtin ---

static void builtin_fmt(VM& vm, u16 dst, u16, u16 argBase) {
    Obj* fmtObj = vm.reg(argBase).o;
    Obj* tupObj = vm.reg(argBase + 1).o;
    auto* fmtStr = static_cast<StringObj*>(fmtObj);
    auto* tup = static_cast<Tuple*>(tupObj);
    auto* tt = static_cast<TupleType*>(tup->type_);

    const VMString& fmt = fmtStr->s;
    u32 numFields = tup->numFields_;
    u32 nextPos = 0; // next positional index for %^

    VMString result = rt::vmstr("");
    size_t len = fmt.size();
    for (size_t i = 0; i < len; ++i) {
        if (fmt[i] == '%' && i + 1 < len) {
            char next = fmt[i + 1];
            if (next == '%') {
                result += '%';
                ++i;
            } else if (next == '^') {
                if (nextPos < numFields) {
                    result += wordToString(tup->v[nextPos], tt->fields_[nextPos]);
                    ++nextPos;
                }
                ++i;
            } else if (next >= '0' && next <= '9') {
                u32 idx = (u32)(next - '0');
                if (idx < numFields) {
                    result += wordToString(tup->v[idx], tt->fields_[idx]);
                }
                ++i;
            } else {
                result += fmt[i];
            }
        } else {
            result += fmt[i];
        }
    }

    auto* resultObj = new StringObj();
    resultObj->s = std::move(result);
    registerNewObj(resultObj);
    vm.reg(dst).o = resultObj;
}

static bool resolve_fmt(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.empty() || args[0] != compiler.stringType()) return false;

    // Variadic form: fmt(String, v1, v2, ...) — build TupleType from args[1..]
    // Also handles fmt(String) with zero variadic args
    TypeVec fields(rt::STLAllocator<Type*>(nullptr));
    for (size_t i = 1; i < args.size(); ++i)
        fields.push_back(args[i]);
    auto* tt = compiler.tupleType(fields);
    pt = {compiler.stringType(), tt};
    rt = compiler.stringType();
    cf = builtin_fmt;
    return true;
}

// --- print/println builtins ---

static void printArgs(VM& vm, u16 argc, u16 argBase) {
    // The Primitive's type_ field stores the TupleType describing each arg's type
    // argc args are in consecutive registers starting at argBase
    auto* prim = static_cast<Primitive*>(vm.currentPrimitive());
    auto* tt = static_cast<TupleType*>(prim->type_);
    FILE* out = vm.printOutput();

    for (u16 i = 0; i < argc; ++i) {
        if (i > 0) std::fputc(' ', out);
        VMString s = wordToString(vm.reg(argBase + i), tt->fields_[i]);
        std::fprintf(out, "%.*s", (int)s.size(), s.data());
    }
}

static void builtin_print(VM& vm, u16 dst, u16 argc, u16 argBase) {
    printArgs(vm, argc, argBase);
    vm.reg(dst).i = 0;
}

static void builtin_println(VM& vm, u16 dst, u16 argc, u16 argBase) {
    printArgs(vm, argc, argBase);
    FILE* out = vm.printOutput();
    std::fputc('\n', out);
    std::fflush(out);
    vm.reg(dst).i = 0;
}

static bool resolve_print(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    pt = args;
    rt = compiler.voidType();
    cf = builtin_print;
    return true;
}

static bool resolve_println(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    pt = args;
    rt = compiler.voidType();
    cf = builtin_println;
    return true;
}

// --- hash builtins ---

static void builtin_hash_int(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = (i64)std::hash<i64>{}(vm.reg(argBase).i);
}

static void builtin_hash_float(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = (i64)std::hash<f64>{}(vm.reg(argBase).f);
}

static void builtin_hash_symbol(VM& vm, u16 dst, u16, u16 argBase) {
    vm.reg(dst).i = (i64)std::hash<const void*>{}(vm.reg(argBase).s);
}

static void builtin_hash_obj(VM& vm, u16 dst, u16, u16 argBase) {
    Obj* obj = vm.reg(argBase).o;
    Type* type = obj->type_;
    WordHash hasher{type};
    vm.reg(dst).i = (i64)hasher(vm.reg(argBase));
}

static bool resolve_hash(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    Type* t = args[0];
    rt = compiler.intType();
    pt = {t};
    if (t == compiler.intType() || t == compiler.boolType()) {
        cf = builtin_hash_int; return true;
    }
    if (t == compiler.floatType()) {
        cf = builtin_hash_float; return true;
    }
    if (t == compiler.symbolType()) {
        cf = builtin_hash_symbol; return true;
    }
    if (t->isObjType()) {
        cf = builtin_hash_obj; return true;
    }
    return false;
}

// ============================================================================
// any() and toAnyArray() builtins
// ============================================================================

// any(x) — wrap a single value of any type into Any
static void builtin_any_single(VM& vm, u16 dst, u16, u16 argBase) {
    // The wrapped type is encoded in the primitive's function type
    auto* prim = vm.currentPrimitive();
    auto* ft = static_cast<FunctionType*>(prim->type_);
    Type* wrappedType = ft->argTypes_[0];
    auto* any = new AnyObj(vm.anyType());
    any->value_ = vm.reg(argBase);
    any->wrappedType_ = wrappedType;
    any->isObjType_ = wrappedType->isObjType();
    vm.reg(dst).o = any;
}

static bool resolve_any_single(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    pt = {args[0]};
    rt = compiler.anyType();
    cf = builtin_any_single;
    return true;
}

// any(x, y, ...) — wrap multiple values into Array<Any>
static void builtin_any_variadic(VM& vm, u16 dst, u16, u16 argBase) {
    // Args are packed into a tuple by the compiler
    auto* tuple = static_cast<Tuple*>(vm.reg(argBase).o);
    auto* tupleType = static_cast<TupleType*>(tuple->type_);
    auto* anyArrayType = vm.arrayType(vm.anyType());
    auto* arr = new ObjArray(anyArrayType);
    size_t n = tuple->numFields_;
    arr->v.resize(n);
    for (size_t i = 0; i < n; ++i) {
        auto* any = new AnyObj(vm.anyType());
        any->value_ = tuple->v[i];
        any->wrappedType_ = tupleType->fields_[i];
        any->isObjType_ = tupleType->fields_[i]->isObjType();
        arr->v[i] = any;
    }
    vm.reg(dst).o = arr;
}

static bool resolve_any_variadic(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() < 2) return false;
    // Pack all args into a TupleType so variadic detection triggers tuple packing
    TypeVec fields(rt::STLAllocator<Type*>(nullptr));
    for (auto* a : args)
        fields.push_back(a);
    auto* tt = compiler.tupleType(fields);
    pt = {tt};
    rt = compiler.arrayType(compiler.anyType());
    cf = builtin_any_variadic;
    return true;
}

// toAnyArray(tuple) — convert a tuple to Array<Any>
static void builtin_toAnyArray(VM& vm, u16 dst, u16, u16 argBase) {
    auto* tuple = static_cast<Tuple*>(vm.reg(argBase).o);
    auto* tupleType = static_cast<TupleType*>(tuple->type_);
    auto* anyArrayType = vm.arrayType(vm.anyType());
    auto* arr = new ObjArray(anyArrayType);
    size_t n = tuple->numFields_;
    arr->v.resize(n);
    for (size_t i = 0; i < n; ++i) {
        auto* any = new AnyObj(vm.anyType());
        any->value_ = tuple->v[i];
        any->wrappedType_ = tupleType->fields_[i];
        any->isObjType_ = tupleType->fields_[i]->isObjType();
        arr->v[i] = any;
    }
    vm.reg(dst).o = arr;
}

static bool resolve_toAnyArray(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* tt = dynamic_cast<TupleType*>(args[0]);
    if (!tt) return false;
    pt = {tt};
    rt = compiler.arrayType(compiler.anyType());
    cf = builtin_toAnyArray;
    return true;
}

// ============================================================================
// Registration
// ============================================================================

void registerBuiltinFunctions(Compiler& compiler,
    std::unordered_map<std::string, std::vector<FuncInfo>>& functions)
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

    // --- random number generation (impure — must not be constant-folded) ---
    registerOne(compiler, functions, "urand", Float, {}, builtin_urand, /*pure=*/false);
    registerOne(compiler, functions, "brand", Float, {}, builtin_brand, /*pure=*/false);
    registerOne(compiler, functions, "irand", Int,   {Int, Int},     builtin_irand, /*pure=*/false);
    registerOne(compiler, functions, "rand",  Float, {Float, Float}, builtin_rand, /*pure=*/false);
    registerOne(compiler, functions, "xrand", Float, {Float, Float}, builtin_xrand, /*pure=*/false);
    registerTemplate(compiler, functions, "pick", resolve_pick);
    registerTemplate(compiler, functions, "picks", resolve_picks);

    auto* FloatList = compiler.listType(Float);
    auto* IntList   = compiler.listType(Int);
    auto* FloatArr  = compiler.arrayType(Float);
    auto* IntArr    = compiler.arrayType(Int);
    registerOne(compiler, functions, "urands", FloatList, {}, builtin_urands_list, /*pure=*/false);
    registerOne(compiler, functions, "urands", FloatArr, {Int}, builtin_urands_array, /*pure=*/false);
    registerOne(compiler, functions, "brands", FloatList, {}, builtin_brands_list, /*pure=*/false);
    registerOne(compiler, functions, "brands", FloatArr, {Int}, builtin_brands_array, /*pure=*/false);
    registerOne(compiler, functions, "irands", IntList, {Int, Int}, builtin_irands_list, /*pure=*/false);
    registerOne(compiler, functions, "irands", IntArr, {Int, Int, Int}, builtin_irands_array, /*pure=*/false);
    registerOne(compiler, functions, "rands", FloatList, {Float, Float}, builtin_rands_list, /*pure=*/false);
    registerOne(compiler, functions, "rands", FloatArr, {Int, Float, Float}, builtin_rands_array, /*pure=*/false);
    registerOne(compiler, functions, "xrands", FloatList, {Float, Float}, builtin_xrands_list, /*pure=*/false);
    registerOne(compiler, functions, "xrands", FloatArr, {Int, Float, Float}, builtin_xrands_array, /*pure=*/false);

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

    registerTemplate(compiler, functions, "toList",    resolve_toList_array);
    registerTemplate(compiler, functions, "toList",    resolve_toList_coroutine);
    registerTemplate(compiler, functions, "collect",   resolve_collect);

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

    // --- Collection builtins (template-resolved) ---
    registerTemplate(compiler, functions, "reverse",   resolve_reverse_a);
    registerTemplate(compiler, functions, "push",      resolve_push);
    registerTemplate(compiler, functions, "pop",       resolve_pop_a);
    registerTemplate(compiler, functions, "muss",      resolve_muss_a);
    registerTemplate(compiler, functions, "sort",      resolve_sort);
    registerTemplate(compiler, functions, "grade",     resolve_grade);
    registerTemplate(compiler, functions, "take",      resolve_take);
    registerTemplate(compiler, functions, "drop",      resolve_drop);
    registerTemplate(compiler, functions, "stride",    resolve_stride);
    registerTemplate(compiler, functions, "stutter",   resolve_stutter);
    registerTemplate(compiler, functions, "cat",       resolve_cat);
    registerTemplate(compiler, functions, "join",      resolve_join);
    registerTemplate(compiler, functions, "flatten",   resolve_flatten);
    registerTemplate(compiler, functions, "map",       resolve_map);
    registerTemplate(compiler, functions, "filter",    resolve_filter);
    registerTemplate(compiler, functions, "fold",      resolve_fold);
    registerTemplate(compiler, functions, "scan",      resolve_scan);
    registerTemplate(compiler, functions, "fold1",     resolve_fold1);
    registerTemplate(compiler, functions, "scan1",     resolve_scan1);
    registerTemplate(compiler, functions, "find",      resolve_find);
    registerTemplate(compiler, functions, "iter",      resolve_iter);
    registerTemplate(compiler, functions, "takeWhile", resolve_takeWhile);
    registerTemplate(compiler, functions, "dropWhile", resolve_dropWhile);
    registerTemplate(compiler, functions, "zip",       resolve_zip);
    registerTemplate(compiler, functions, "enumerate", resolve_enumerate);
    registerTemplate(compiler, functions, "cyc",       resolve_cyc);
    registerTemplate(compiler, functions, "ncyc",      resolve_ncyc);
    registerTemplate(compiler, functions, "hang",      resolve_hang);
    registerTemplate(compiler, functions, "head",      resolve_head);
    registerTemplate(compiler, functions, "tail",      resolve_tail);
    registerTemplate(compiler, functions, "cons",      resolve_cons);
    registerTemplate(compiler, functions, "isNil",     resolve_isNil);
    registerTemplate(compiler, functions, "notNil",    resolve_notNil);
    registerTemplate(compiler, functions, "length",    resolve_length);
    registerTemplate(compiler, functions, "ordinal",   resolve_ordinal);
    registerTemplate(compiler, functions, "tag",       resolve_tag);

    // --- Map builtins ---
    registerTemplate(compiler, functions, "get",          resolve_get_map);
    registerTemplate(compiler, functions, "put",          resolve_put);
    registerTemplate(compiler, functions, "remove",       resolve_remove);
    registerTemplate(compiler, functions, "contains",     resolve_contains);
    registerTemplate(compiler, functions, "keys",         resolve_keys);
    registerTemplate(compiler, functions, "values",       resolve_values);
    registerTemplate(compiler, functions, "pairs",        resolve_pairs);
    registerTemplate(compiler, functions, "merge",        resolve_merge);

    // --- Option builtins ---
    registerTemplate(compiler, functions, "unwrap",       resolve_unwrap);
    registerTemplate(compiler, functions, "unwrapOr",     resolve_unwrapOr);
    registerTemplate(compiler, functions, "isSome",       resolve_isSome);
    registerTemplate(compiler, functions, "isNone",       resolve_isNone);

    // --- Coroutine builtins ---
    registerTemplate(compiler, functions, "next",         resolve_next);
    registerTemplate(compiler, functions, "yield",        resolve_yield);
    registerTemplate(compiler, functions, "yieldAll",     resolve_yieldAll);

    // --- Set builtins ---
    registerTemplate(compiler, functions, "add",          resolve_add_set);
    registerTemplate(compiler, functions, "union",        resolve_union);
    registerTemplate(compiler, functions, "intersection", resolve_intersection);
    registerTemplate(compiler, functions, "difference",   resolve_difference);
    registerTemplate(compiler, functions, "toArray",      resolve_toArray_set);

    // --- hash builtin ---
    registerTemplate(compiler, functions, "hash",         resolve_hash);

    // --- toString builtin ---
    registerTemplate(compiler, functions, "toString",     resolve_toString);

    // --- fmt builtin ---
    registerTemplate(compiler, functions, "fmt",          resolve_fmt);

    // --- print/println builtins (not RT-safe: they write to stdout) ---
    registerTemplate(compiler, functions, "print",        resolve_print,   /*rtSafe=*/false);
    registerTemplate(compiler, functions, "println",      resolve_println, /*rtSafe=*/false);

    // --- Any builtins ---
    registerTemplate(compiler, functions, "any",          resolve_any_single);
    registerTemplate(compiler, functions, "any",          resolve_any_variadic);
    registerTemplate(compiler, functions, "toAnyArray",   resolve_toAnyArray);
}

} // namespace ts
