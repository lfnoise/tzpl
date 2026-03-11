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
//  synthdef_types2.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/26/24.
//

#pragma once

#include "synthdef_types.hpp"
#include <ranges>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <vector>
#include <numeric>
#include <variant>
#include <format>
#include <print>
#include <functional>

namespace synthdef {

namespace stdr = std::ranges;
namespace stdv = std::views;

using std::string;
using std::vector;
using std::optional;
using std::variant;
using std::unordered_set;
using std::unordered_map;

#define FMT std::format

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

// explicit deduction guide (not needed as of C++20)
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

inline bool isPowerOfTwo(usize x) {
    return x && !(x & (x - 1));
}

} // namespace synthdef
