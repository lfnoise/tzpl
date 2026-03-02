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
