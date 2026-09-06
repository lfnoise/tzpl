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
//  tzpl_common.hpp
//  audio engine
//
//  Created by James McCartney on 2/2/21.
//

#ifndef tzpl_common_hpp
#define tzpl_common_hpp

#include <cstdint>
#include <cstddef>
#include <complex>
#include <vector>
#include <string>
#include <cassert>
#include "tzpl_simd.hpp"

namespace engine {

#pragma mark BASE TYPES

using c8 = char;

using i8  = char;
using i16 = short;
using i32 = int;
using i64 = std::int64_t;   // NOT `long`: 32-bit on Windows (LLP64)
using i128 = __int128_t;

using u8  = unsigned char ;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = std::uint64_t;
using u128 = __uint128_t;

using f32 = float;
using f64 = double;

using usize = size_t;
using isize = std::ptrdiff_t;  // ssize_t does not exist in the MSVC CRT


}

#endif /* audio_engine_hpp */
