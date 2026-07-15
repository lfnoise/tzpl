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
//  types.hpp
//  lispish
//
//  Created by James McCartney on 11/19/25.
//

#ifndef base_types_hpp
#define base_types_hpp

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <bit>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <complex>
#include <stdexcept>
#include <variant>
#include <filesystem>
#include <ranges>
#include <span>
#include <algorithm>
#include <cassert>
#include <print>
#include <format>
#include <charconv>
#include <cstdlib>
#include <xlocale.h>

#pragma mark BASE TYPES

using c8 = char;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = long;  // matches engine convention (long == 64-bit on macOS ARM64)
using i128 = __int128_t;

using u8 = uint8_t ;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = unsigned long;  // matches engine convention
using u128 = __uint128_t;

using f32 = float;
using f64 = double;
using f80 = long double;  // long double is 80 bits, but stored in 128 bits with Clang.

using x32 = std::complex<f32> ;
using x64 = std::complex<f64> ;

using usize = size_t;
using isize = ptrdiff_t;

// Parse a double from a C string: the inverse of formatFloat below.
// strtod pinned to the C locale so a host that calls setlocale() (GUI
// frameworks do) cannot change the decimal separator, preserving the
// bit-identical print->parse round-trip guarantee. macOS strtod (gdtoa)
// is correctly rounded, so parseFloatC(formatFloat(v)) == v bit-for-bit.
// (FP std::from_chars needs a macOS 26 deployment target; revisit then.)
inline f64 parseFloatC(char const* s, char** end) {
    static locale_t cLocale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    return strtod_l(s, end, cLocale);
}

// Format a double to its shortest round-trip representation.
// Always includes a decimal point (e.g., "1.0" not "1").
// Writes a null-terminated string to buf. Returns the length (excluding null).
inline size_t formatFloat(f64 value, char* buf, size_t bufsize) {
    auto [ptr, ec] = std::to_chars(buf, buf + bufsize - 1, value);
    size_t len = static_cast<size_t>(ptr - buf);

    // Append ".0" if the result has no decimal point or exponent
    // and is not a special value (nan, inf)
    bool needsDotZero = true;
    for (size_t i = 0; i < len; ++i) {
        char c = buf[i];
        if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'i') {
            needsDotZero = false;
            break;
        }
    }
    if (needsDotZero && len + 2 < bufsize) {
        buf[len] = '.';
        buf[len + 1] = '0';
        len += 2;
    }

    buf[len] = '\0';
    return len;
}

#endif /* base_types_hpp */
