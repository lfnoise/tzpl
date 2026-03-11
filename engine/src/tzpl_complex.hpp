//
//  tzpl_complex.hpp
//  audio engine
//
//  Created by James McCartney on 7/17/25.
//

#ifndef tzpl_complex_h
#define tzpl_complex_h

#include "tzpl_common.hpp"

namespace engine {

// Unlike std::complex, this class supports SIMD
// real and imaginary parts.
// Also unlike std::complex, it does not
// handle cases of NaN or infinity.
template <typename T>
struct complex {
	T r, i;
	
	complex() = default;
	complex(T a) : r(a), i(0) {}
	complex(T a, T b) : r(a), i(b) {}
	complex(complex const&) = default;
	
	template <class U>
	complex(complex<U> const& x) : r(x.r), i(x.i) {}
	
	
	T real() const { return r; }
	T imag() const { return i; }
};

using c32   = complex<f32>;
using c32x2 = complex<f32x2>;
using c32x4 = complex<f32x4>;
using c32x8 = complex<f32x8>;

using c64 = complex<f64>;
using c64x2 = complex<f64x2>;
using c64x4 = complex<f64x4>;
using c64x8 = complex<f64x8>;


template <typename T>
bool operator==(complex<T> const& a, complex<T> const& b) {
	return a.r == b.r && a.i == b.i;
}

template <typename T>
bool operator==(T const& a, complex<T> const& b) {
	return a == b.r && T(0) == b.i;
}

template <typename T>
bool operator==(complex<T> const& a, T const& b) {
	return a.r == b && a.i == T(0);
}

template <typename T>
bool operator!=(complex<T> const& a, complex<T> const& b) {
	return !(a == b);
}

template <typename T>
bool operator!=(T const& a, complex<T> const& b) {
	return !(a == b);
}

template <typename T>
bool operator!=(complex<T> const& a, T const& b) {
	return !(a == b);
}

template <typename T>
auto operator-(complex<T> const& a) {
	return complex<T>(-a.r,-a.i);
}

template <typename T>
auto operator+(complex<T> const& a, complex<T> const& b) {
	return complex<T>(a.r+b.r, a.i+b.i);
}

template <typename T>
auto operator+(T const& a, complex<T> const& b) {
	return complex<T>(a+b.r, b.i);
}

template <typename T>
auto operator+(complex<T> const& a, T const& b) {
	return complex<T>(a.r+b, a.i);
}

template <typename T>
auto operator-(complex<T> const& a, complex<T> const& b) {
	return complex<T>(a.r-b.r, a.i-b.i);
}

template <typename T>
auto operator-(T const& a, complex<T> const& b) {
	return complex<T>(a-b.r, -b.i);
}

template <typename T>
auto operator-(complex<T> const& a, T const& b) {
	return complex<T>(a.r-b, a.i);
}

template <typename T>
auto operator*(complex<T> const& a, complex<T> const& b) {
	return complex<T>(a.r*b.r-a.i*b.i, a.r*b.i+a.i*b.r);
}

template <typename T>
auto operator*(T const& a, complex<T> const& b) {
	return complex<T>(a*b.r, a*b.i);
}

template <typename T>
auto operator*(complex<T> const& a, T const& b) {
	return complex<T>(a.r*b, a.i*b);
}

template <typename T>
auto reciprocal(complex<T> const& a) {
	auto denom = T(1) / norm(a);
	return complex<T>(
		 a.r * denom,
		-a.i * denom);
}

template <typename T>
auto operator/(complex<T> const& a, complex<T> const& b) {
	auto denom = T(1) / norm(b);
	return complex<T>(
		(a.r*b.r + a.i*b.i) * denom,
		(a.i*b.r - a.r*b.i) * denom);
}

template <typename T>
auto operator/(T const& a, complex<T> const& b) {
	return a * reciprocal(b);
}

template <typename T>
auto operator/(complex<T> const& a, T const& b) {
	return a * (T(1)/b);
}

template <typename T>
auto conj(complex<T> const& a) {
	return complex<T>(a.r,-a.i);
}

template <typename T>
auto pose(complex<T> const& a) {
	return complex<T>(a.i,a.r);
}


template <typename T>
auto norm(complex<T> const& a) {
	return a.r*a.r + a.i*a.i;
}

template <typename T>
auto abs(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		return std::hypot(a.r,a.i);
	} else {
		return simd::hypot(a.r,a.i);
	}
}

template <typename T>
auto arg(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		return std::atan2(a.i,a.r);
	} else {
		return simd::atan2(a.i,a.r);
	}
}

template <typename T>
auto log(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::log(abs(a)), arg(a));
	} else {
		return complex<T>(simd::log(abs(a)), arg(a));
	}
}

template <typename T>
auto log2(complex<T> const& a) {
	static constexpr T recip_log_2 = T(1.44269504088896340736);
	return log(a) * recip_log_2;
}

template <typename T>
auto log10(complex<T> const& a) {
	static constexpr T recip_log_10 = T(0.43429448190325182765);
	return log(a) * recip_log_10;
}

template <typename T>
auto polar(T rho, T theta) {
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(rho * std::cos(theta),
						  rho * std::sin(theta));
	} else {
		return complex<T>(rho * simd::cos(theta),
						  rho * simd::sin(theta));
	}
}

template <typename T>
auto polar(T theta) {
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::cos(theta),
						  std::sin(theta));
	} else {
		return complex<T>(simd::cos(theta),
						  simd::sin(theta));
	}
}

template <typename T>
auto sqrt(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		return polar(std::sqrt(abs(a)), arg(a) * T(.5));
	} else {
		return polar(simd::sqrt(abs(a)), arg(a) * T(.5));
	}
}

template <typename T>
auto cbrt(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		return polar(std::cbrt(abs(a)), arg(a) * T(1./3.));
	} else {
		return polar(simd::cbrt(abs(a)), arg(a) * T(1./3.));
	}
}

template <typename T>
auto exp(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		T e = std::exp(a.r);
		return complex<T>(e * std::cos(a.i), e * std::sin(a.i));
	} else {
		T e = simd::exp(a.r);
		return complex<T>(e * simd::cos(a.i), e * simd::sin(a.i));
	}
}

template <typename T>
auto pow(complex<T> const& a, complex<T> const& b) {
	return exp(b * log(a));
}


template <typename T>
auto pow(T a, complex<T> const& b) {
	if constexpr (std::is_floating_point_v<T>) {
		return exp(b * std::log(a));
	} else {
		return exp(b * simd::log(a));
	}
}

template <typename T>
auto pow(complex<T> const& a, T b) {
	return exp(b * log(a));
}


template <typename T>
auto exp2(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		T e = std::exp2(a.r);
		return complex<T>(e * std::cos(a.i), e * std::sin(a.i));
	} else {
		T e = simd::exp2(a.r);
		return complex<T>(e * simd::cos(a.i), e * simd::sin(a.i));
	}
}

template <typename T>
auto exp10(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		T e = std::pow(T(10), a.r);
		return complex<T>(e * std::cos(a.i), e * std::sin(a.i));
	} else {
		T e = simd::exp10(a.r);
		return complex<T>(e * simd::cos(a.i), e * simd::sin(a.i));
	}
}

template <typename T>
auto sq(complex<T> const& a) {
	return a*a;
}

template <typename T>
auto asinh(complex<T> const& a) {
	auto z = log(a + sqrt(sq(a) + T(1)));
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::copysign(z.r, a.r),
					      std::copysign(z.i, a.i));
	} else {
		return complex<T>(simd::copysign(z.r, a.r),
					      simd::copysign(z.i, a.i));
	}
}

template <typename T>
auto acosh(complex<T> const& a) {
	auto z = log(a + sqrt(sq(a) - T(1)));
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::copysign(z.r, T(0)),
					      std::copysign(z.i, a.i));
	} else {
		return complex<T>(simd::copysign(z.r, T(0)),
					      simd::copysign(z.i, a.i));
	}
}

template <typename T>
auto atanh(complex<T> const& a) {
	auto z = log((T(1) + a) / (T(1) - a)) * T(.5);
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::copysign(z.r, T(0)),
					      std::copysign(z.i, a.i));
	} else {
		return complex<T>(simd::copysign(z.r, T(0)),
					      simd::copysign(z.i, a.i));
	}
}

template <typename T>
auto sinh(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::sinh(a.i)*std::cos(a.i),
					      std::cosh(a.r)*std::sin(a.i));
	} else {
		return complex<T>(simd::sinh(a.i)*simd::cos(a.i),
					      simd::cosh(a.r)*simd::sin(a.i));
	}
}

template <typename T>
auto cosh(complex<T> const& a) {
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::cosh(a.i)*std::cos(a.i),
					      std::sinh(a.r)*std::sin(a.i));
	} else {
		return complex<T>(simd::cosh(a.i)*simd::cos(a.i),
					      simd::sinh(a.r)*simd::sin(a.i));
	}
}
template <typename T>
auto tanh(complex<T> const& a) {
	T twoR = T(2) * a.r;
	T twoI = T(2) * a.i;
	if constexpr (std::is_floating_point_v<T>) {
		T d = std::cosh(twoR) + std::cos(twoI);
		T twoSinhR = std::sinh(twoR);
		return complex<T>(twoSinhR/d, std::sin(twoI)/d);
	} else {
		T d = simd::cosh(twoR) + simd::cos(twoI);
		T twoSinhR = simd::sinh(twoR);
		return complex<T>(twoSinhR/d, simd::sin(twoI)/d);
	}
}

template <typename T>
auto asin(complex<T> const& a) {
	auto z = asinh(complex<T>(-a.i, a.r));
	return complex<T>(z.i, -z.r);
}

template <typename T>
auto acos(complex<T> const& a) {
	auto z = log(a + sqrt(a*a - T(1)));
	if constexpr (std::is_floating_point_v<T>) {
		return complex<T>(std::abs(z.i),
					  -std::copysign(z.r,a.i));
	} else {
		return complex<T>(simd::abs(z.i),
					  -simd::copysign(z.r,a.i));
	}
}

template <typename T>
auto atan(complex<T> const& a) {
	auto z = atanh(complex<T>(-a.i,a.r));
	return complex<T>(z.i,-z.r);
}

template <typename T>
auto sin(complex<T> const& a) {
	auto z = sinh(complex<T>(-a.i,a.r));
	return complex<T>(z.i,-z.r);
}

template <typename T>
auto cos(complex<T> const& a) {
	return cosh(complex<T>(-a.i,a.r));
}

template <typename T>
auto tan(complex<T> const& a) {
	auto z = tanh(complex<T>(-a.i,a.r));
	return complex<T>(z.i,-z.r);
}

#pragma mark SIMD SPLAT

inline auto splat2(i32   x) { return i32x2{x, x}; }
inline auto splat2(i64   x) { return i64x2{x, x}; }
inline auto splat2(f32   x) { return f32x2{x, x}; }
inline auto splat2(f64   x) { return f64x2{x, x}; }

inline auto splat4(i32   x) { return i32x4{x, x, x, x}; }
inline auto splat4(i64   x) { return i64x4{x, x, x, x}; }
inline auto splat4(f32   x) { return f32x4{x, x, x, x}; }
inline auto splat4(f64   x) { return f64x4{x, x, x, x}; }

inline auto splat8(i32   x) { return i32x8{x, x, x, x, x, x, x, x}; }
inline auto splat8(i64   x) { return i64x8{x, x, x, x, x, x, x, x}; }
inline auto splat8(f32   x) { return f32x8{x, x, x, x, x, x, x, x}; }
inline auto splat8(f64   x) { return f64x8{x, x, x, x, x, x, x, x}; }

inline auto splat4(i32x2 x) { return x.xyxy; }
inline auto splat4(i64x2 x) { return x.xyxy; }
inline auto splat4(f32x2 x) { return x.xyxy; }
inline auto splat4(f64x2 x) { return x.xyxy; }

inline auto splat8(i32x2 x) { return x.xyxyxyxy; }
inline auto splat8(i64x2 x) { return x.xyxyxyxy; }
inline auto splat8(f32x2 x) { return x.xyxyxyxy; }
inline auto splat8(f64x2 x) { return x.xyxyxyxy; }

inline auto splat8(i32x4 x) { return x.xyzwxyzw; }
inline auto splat8(i64x4 x) { return x.xyzwxyzw; }
inline auto splat8(f32x4 x) { return x.xyzwxyzw; }
inline auto splat8(f64x4 x) { return x.xyzwxyzw; }

inline auto splat2(c32   x) { return c32x2(splat2(x.r), splat2(x.i)); }
inline auto splat2(c64   x) { return c64x2(splat2(x.r), splat2(x.i)); }

inline auto splat4(c32   x) { return c32x4(splat4(x.r), splat4(x.i)); }
inline auto splat4(c64   x) { return c64x4(splat4(x.r), splat4(x.i)); }

inline auto splat8(c32   x) { return c32x8(splat8(x.r), splat8(x.i)); }
inline auto splat8(c64   x) { return c64x8(splat8(x.r), splat8(x.i)); }

inline auto splat4(c32x2 x) { return c32x4(splat4(x.r), splat4(x.i)); }
inline auto splat4(c64x2 x) { return c64x4(splat4(x.r), splat4(x.i)); }

inline auto splat8(c32x2 x) { return c32x8(splat8(x.r), splat8(x.i)); }
inline auto splat8(c64x2 x) { return c64x8(splat8(x.r), splat8(x.i)); }

inline auto splat8(c32x4 x) { return c32x8(splat8(x.r), splat8(x.i)); }
inline auto splat8(c64x4 x) { return c64x8(splat8(x.r), splat8(x.i)); }


#pragma mark SIMD CASTS

template <typename T>
auto cast_i32(T x) { return simd::convert<i32>(x); }

template <typename T>
auto cast_i64(T x) { return simd::convert<i64>(x); }

template <typename T>
auto cast_f32(T x) { return simd::convert<f32>(x); }

template <typename T>
auto cast_f64(T x) { return simd::convert<f64>(x); }


inline auto cast_i32(bool  x) { return i32(x); }
inline auto cast_i64(bool  x) { return i64(x); }
inline auto cast_f32(bool  x) { return f32(x); }
inline auto cast_f64(bool  x) { return f64(x); }

inline auto cast_f32(c64   x) { return c32  (cast_f32(x.r), cast_f32(x.i)); }
inline auto cast_f32(c64x2 x) { return c32x2(cast_f32(x.r), cast_f32(x.i)); }
inline auto cast_f32(c64x4 x) { return c32x4(cast_f32(x.r), cast_f32(x.i)); }
inline auto cast_f32(c64x8 x) { return c32x8(cast_f32(x.r), cast_f32(x.i)); }

inline auto cast_f64(c32   x) { return c64  (cast_f64(x.r), cast_f64(x.i)); }
inline auto cast_f64(c32x2 x) { return c64x2(cast_f64(x.r), cast_f64(x.i)); }
inline auto cast_f64(c32x4 x) { return c64x4(cast_f64(x.r), cast_f64(x.i)); }
inline auto cast_f64(c32x8 x) { return c64x8(cast_f64(x.r), cast_f64(x.i)); }



}

#endif /* tzpl_complex_h */
