//
//  synthdef_types.hpp
//  synthdef-compiler
//

#pragma once
#include <cstdint>
#include <complex>
#include <simd/simd.h>
#include <cassert>
#include <string>

namespace synthdef {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using x32 = std::complex<f32>;
using x64 = std::complex<f64>;

using usize = std::size_t;
using isize = std::ptrdiff_t;

#pragma mark SIMD TYPES

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

// modulo function. C++'s % operator is remainder, not modulo.
template <typename T, typename U>
    requires std::is_integral_v<T> && std::is_integral_v<U>
constexpr auto mod(T aa, U bb) {
    using C = std::common_type_t<std::make_signed_t<T>, std::make_signed_t<U>>;
    auto a = static_cast<C>(aa);
    auto b = static_cast<C>(bb);
    auto r = a % b;
    return r < 0 ? r + b : r;
    // in my tests, this is faster than:
    // return (a % b + b) % b;
    // when optimization is turned on.
}

template <usize DstSize, usize SrcSize>
constexpr auto umod(usize a) {
    if constexpr (DstSize == 1) { return 0; }
    else if constexpr (SrcSize <= DstSize) { return a; }
    else { return a % DstSize; }
}

} // namespace synthdef 
