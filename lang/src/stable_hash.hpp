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
//  stable_hash.hpp
//
//  Hash functions for language values that are identical on every platform.
//
//  Map and Set iteration order (and therefore printed output) depends on the
//  key hash. std::hash is implementation-defined and differs between libc++,
//  libstdc++, and the MSVC STL, which is why the golden test suite used to
//  need per-OS override files. Everything user-visible hashes through these
//  instead. Ints hash to themselves (what libc++ and libstdc++ both do, so
//  small-int map order stays readable); floats hash to their bit pattern
//  with +-0.0 folded to 0 (libc++'s rule, so existing macOS output is
//  preserved); strings use FNV-1a with a splitmix64 finalizer.
//

#ifndef stable_hash_hpp
#define stable_hash_hpp

#include "base_types.hpp"
#include <cstring>
#include <string_view>

namespace ts {

inline u64 stableMix64(u64 x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

inline size_t stableHashInt(i64 v) { return (size_t)(u64)v; }

inline size_t stableHashFloat(f64 v) {
    if (v == 0.0) return 0;  // +0.0 and -0.0 compare equal, so they must hash equal
    u64 bits;
    std::memcpy(&bits, &v, sizeof bits);
    return (size_t)bits;
}

inline size_t stableHashBytes(void const* data, size_t len) {
    u64 h = 0xcbf29ce484222325ull;
    auto const* p = static_cast<unsigned char const*>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return (size_t)stableMix64(h ^ (u64)len);
}

inline size_t stableHashString(std::string_view s) {
    return stableHashBytes(s.data(), s.size());
}

// Pointer identity. Not stable across runs by nature; spelled out so the
// choice (the address itself, as libc++ does) is the same on every platform.
inline size_t stableHashPtr(void const* p) { return (size_t)(uintptr_t)p; }

} // namespace ts

#endif // stable_hash_hpp
