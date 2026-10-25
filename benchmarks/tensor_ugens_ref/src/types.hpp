//
//  types.hpp
//  tensor-ugens
//
//  Created by James McCartney on 10/19/26.
//

#ifndef types_h
#define types_h

#include <simd/simd.h>

using u32 = unsigned int;
using u64 = unsigned long;

using i32 = int;
using i64 = long;

using f32 = float;
using f64 = double;

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

template <typename T>
struct traits {};

template<>
struct traits<i32> {
    using scalar_t = i32;
    using word_t = i32;
    using elem_t = word_t;
    static constexpr int width = 1;
    static constexpr int bits = 32;
};

template<>
struct traits<i32x2> {
    using scalar_t = i32;
    using word_t = i32x2;
    using elem_t = word_t;
    static constexpr int width = 2;
    static constexpr int bits = 32;
};

template<>
struct traits<i32x4> {
    using scalar_t = i32;
    using word_t = i32x4;
    using elem_t = word_t;
    static constexpr int width = 4;
    static constexpr int bits = 32;
};

template<>
struct traits<i32x8> {
    using scalar_t = i32;
    using word_t = i32x8;
    using elem_t = word_t;
    static constexpr int width = 8;
    static constexpr int bits = 32;
};

template<>
struct traits<i64> {
    using scalar_t = i64;
    using word_t = i64;
    using elem_t = word_t;
    static constexpr int width = 1;
    static constexpr int bits = 64;
};

template<>
struct traits<i64x2> {
    using scalar_t = i64;
    using word_t = i64x2;
    using elem_t = word_t;
    static constexpr int width = 2;
    static constexpr int bits = 64;
};

template<>
struct traits<i64x4> {
    using scalar_t = i64;
    using word_t = i64x4;
    using elem_t = word_t;
    static constexpr int width = 4;
    static constexpr int bits = 64;
};

template<>
struct traits<i64x8> {
    using scalar_t = i64;
    using word_t = i64x8;
    using elem_t = word_t;
    static constexpr int width = 8;
    static constexpr int bits = 64;
};


template<>
struct traits<f32> {
    using scalar_t = f32;
    using word_t = f32;
    using elem_t = word_t;
    static constexpr int width = 1;
    static constexpr int bits = 32;
};

template<>
struct traits<f32x2> {
    using scalar_t = f32;
    using word_t = f32x2;
    using elem_t = word_t;
    static constexpr int width = 2;
    static constexpr int bits = 32;
};

template<>
struct traits<f32x4> {
    using scalar_t = f32;
    using word_t = f32x4;
    using elem_t = word_t;
    static constexpr int width = 4;
    static constexpr int bits = 32;
};

template<>
struct traits<f32x8> {
    using scalar_t = f32;
    using word_t = f32x8;
    using elem_t = word_t;
    static constexpr int width = 8;
    static constexpr int bits = 32;
};

template<>
struct traits<f64> {
    using scalar_t = f64;
    using word_t = f64;
    using elem_t = word_t;
    static constexpr int width = 1;
    static constexpr int bits = 64;
};

template<>
struct traits<f64x2> {
    using scalar_t = f64;
    using word_t = f64x2;
    using elem_t = word_t;
    static constexpr int width = 2;
    static constexpr int bits = 64;
};

template<>
struct traits<f64x4> {
    using scalar_t = f64;
    using word_t = f64x4;
    using elem_t = word_t;
    static constexpr int width = 4;
    static constexpr int bits = 64;
};

template<>
struct traits<f64x8> {
    using scalar_t = f64;
    using word_t = f64x8;
    using elem_t = word_t;
    static constexpr int width = 8;
    static constexpr int bits = 64;
};

static u64 clz( u64 x )
{
	if (x == 0) return 64;
	return __builtin_clzll(x);
}

inline u64 log2ceil(u64 x)
{
	return 64 - clz(x - 1);
}

// next power of two greater than or equal to x
inline u64 nextPowerOfTwo(u64 x)
{
	return u64(1) << log2ceil(x);
}


template <typename I>
constexpr bool isPowerOfTwo(I x) {
    static_assert(std::is_integral_v<I>);
	return (x & (x-1)) == 0;
}

template <typename T>
struct is_supported_scalar_type {
    using scalar_t = typename traits<T>::scalar_t;
    static constexpr bool value =
           std::is_same_v<scalar_t, i32> || std::is_same_v<scalar_t, i64>
        || std::is_same_v<scalar_t, f32> || std::is_same_v<scalar_t, f64>;
};

template <typename T>
constexpr bool is_supported_scalar_type_v = is_supported_scalar_type<T>::value;


template <int N>
struct is_supported_simd_width {
    static constexpr bool value =
        N == 1 || N == 2 || N == 4 || N == 8;
};

template <int N>
constexpr bool is_supported_simd_width_v = is_supported_simd_width<N>::value;


template <typename T>
struct is_supported_simd_type {
    static constexpr int width = traits<T>::width;
	static constexpr bool value =
        is_supported_scalar_type_v<T> && is_supported_simd_width_v<width>;
};

template <typename T>
constexpr bool is_supported_simd_type_v = is_supported_simd_type<T>::value;

#endif /* types_h */
