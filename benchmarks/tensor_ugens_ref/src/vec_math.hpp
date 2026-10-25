//
//  vec_math.hpp
//  tensor-ugens
//
//  Elementwise primitives for both samples (Z) and SIMD vectors of samples (V).
//  Ops written in terms of these run a sample at a time or a vector at a time
//  from one definition.
//

#ifndef vec_math_h
#define vec_math_h

#include <cmath>
#include <algorithm>
#include <type_traits>
#include <simd/simd.h>

// The sample type, double or float. Everything else follows from it.
typedef double Z;
static_assert(std::is_same_v<Z, double> || std::is_same_v<Z, float>);

// A vector of kVecLen samples. Vectorized run functions step through buffers a
// vector at a time, so the block size must be a multiple of kVecLen. VMask is
// the result of comparing Vs: -1 in true lanes, 0 in false. VP is V with only
// Z's alignment, for reading Wire::splat.
const int kVecLen = 4;
typedef simd::Vector<Z, kVecLen>::type V;
typedef simd::Vector<Z, kVecLen>::packed_t VP;
typedef decltype(V() < V()) VMask;

static_assert(kVecLen == 4, "transpose4 and prefixSum assume 4 lanes");

// Transposes the 4 x 4 matrix whose rows are r0..r3.
inline void transpose4(V& r0, V& r1, V& r2, V& r3) {
	V t0 = __builtin_shufflevector(r0, r1, 0, 4, 2, 6);
	V t1 = __builtin_shufflevector(r0, r1, 1, 5, 3, 7);
	V t2 = __builtin_shufflevector(r2, r3, 0, 4, 2, 6);
	V t3 = __builtin_shufflevector(r2, r3, 1, 5, 3, 7);
	r0 = __builtin_shufflevector(t0, t2, 0, 1, 4, 5);
	r1 = __builtin_shufflevector(t1, t3, 0, 1, 4, 5);
	r2 = __builtin_shufflevector(t0, t2, 2, 3, 6, 7);
	r3 = __builtin_shufflevector(t1, t3, 2, 3, 6, 7);
}

// Running sums of x's lanes: (x0, x0+x1, x0+x1+x2, x0+x1+x2+x3).
inline V prefixSum(V x) {
	V z = 0;
	x += __builtin_shufflevector(x, z, 4, 0, 1, 2);
	x += __builtin_shufflevector(x, z, 4, 5, 0, 1);
	return x;
}

namespace vmath {

// A value as itself, or a comparison's result as 1 for true and 0 for false.
inline Z truth(Z x) { return x; }
inline Z truth(bool x) { return x; }
inline V truth(V x) { return x; }
inline V truth(VMask mask) { return __builtin_convertvector(-mask, V); }

// c ? a : b, lane by lane for vectors. Both a and b are evaluated. c is a
// comparison's result, or a value that is true if nonzero.
inline Z select(bool c, Z a, Z b) { return c ? a : b; }
inline V select(VMask c, V a, V b) { return simd_select(b, a, c); }
inline Z select(Z c, Z a, Z b) { return c != 0 ? a : b; }
inline V select(V c, V a, V b) { return simd_select(b, a, c != 0); }

#define VMATH_UNARY(NAME) \
inline Z NAME(Z x) { return std::NAME(x); } \
inline V NAME(V x) { return simd::NAME(x); }

// For functions <simd/simd.h> lacks: the scalar function on each lane.
#define VMATH_UNARY_LANES(NAME) \
inline Z NAME(Z x) { return std::NAME(x); } \
inline V NAME(V x) { \
    V y = 0; \
    for (int i = 0; i < kVecLen; ++i) y[i] = std::NAME(x[i]); \
    return y; \
}

#define VMATH_BINARY(NAME) \
inline Z NAME(Z x, Z y) { return std::NAME(x, y); } \
inline V NAME(V x, V y) { return simd::NAME(x, y); }

VMATH_UNARY(sin) VMATH_UNARY(cos) VMATH_UNARY(tan)
VMATH_UNARY(asin) VMATH_UNARY(acos) VMATH_UNARY(atan)
VMATH_UNARY(sinh) VMATH_UNARY(cosh) VMATH_UNARY(tanh)
VMATH_UNARY(asinh) VMATH_UNARY(acosh) VMATH_UNARY(atanh)
VMATH_UNARY(exp) VMATH_UNARY(exp2) VMATH_UNARY(expm1)
VMATH_UNARY(log) VMATH_UNARY(log10) VMATH_UNARY(log2) VMATH_UNARY(log1p) VMATH_UNARY_LANES(logb)
VMATH_UNARY(sqrt) VMATH_UNARY(cbrt)
VMATH_UNARY(erf) VMATH_UNARY(erfc) VMATH_UNARY(tgamma) VMATH_UNARY(lgamma)
VMATH_UNARY(ceil) VMATH_UNARY(floor) VMATH_UNARY(trunc) VMATH_UNARY(round)
VMATH_UNARY_LANES(nearbyint) VMATH_UNARY(rint)
VMATH_UNARY(abs) VMATH_UNARY(fabs)

VMATH_BINARY(pow) VMATH_BINARY(atan2) VMATH_BINARY(hypot)
VMATH_BINARY(fmod) VMATH_BINARY(remainder)
VMATH_BINARY(fmin) VMATH_BINARY(fmax) VMATH_BINARY(fdim)
VMATH_BINARY(copysign) VMATH_BINARY(nextafter)

inline Z min(Z a, Z b) { return std::min(a, b); }
inline V min(V a, V b) { return simd::min(a, b); }
inline Z max(Z a, Z b) { return std::max(a, b); }
inline V max(V a, V b) { return simd::max(a, b); }

// sin, cos and tan of pi * x
inline Z sinpi(Z x) { return simd::sinpi(x); }
inline V sinpi(V x) { return simd::sinpi(x); }
inline Z cospi(Z x) { return simd::cospi(x); }
inline V cospi(V x) { return simd::cospi(x); }
inline Z tanpi(Z x) { return simd::tanpi(x); }
inline V tanpi(V x) { return simd::tanpi(x); }

#undef VMATH_UNARY
#undef VMATH_UNARY_LANES
#undef VMATH_BINARY

} // namespace vmath

#endif /* vec_math_h */
