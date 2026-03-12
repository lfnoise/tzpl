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
//  synthdef_types.hpp
//  synthdef-compiler
//

#pragma once
#include <cstdint>
#include <complex>
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
