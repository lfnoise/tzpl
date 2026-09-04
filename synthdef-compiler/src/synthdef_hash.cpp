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
//  synthdef_hash.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/11/24.
//

#include "synthdef_hash.hpp"
#include <cstring>

namespace synthdef {

// Compression function for Merkle-Damgard construction.
// This function is generated using the framework provided.
#define mix(h) ({                    \
            (h) ^= (h) >> 23;        \
            (h) *= 0x2127599bf4325c37ULL;    \
            (h) ^= (h) >> 47; })

u64 hash64(size_t n, const void *buf, u64 seed)
{
    const u64    m = 0x880355f21e6d1965ULL;
    const u8 *pos = (const u8 *)buf;
    const u8 *end = pos + n;
    u64 h = seed ^ (n * m);
    u64 v;

    while (end-pos >= 8) {
        memcpy(&v, pos, 8);
        pos += 8;
        h ^= mix(v);
        h *= m;
    }

    v = 0;

    switch (n & 7) {
    case 7: v ^= (u64)pos[6] << 48;
    case 6: v ^= (u64)pos[5] << 40;
    case 5: v ^= (u64)pos[4] << 32;
    case 4: v ^= (u64)pos[3] << 24;
    case 3: v ^= (u64)pos[2] << 16;
    case 2: v ^= (u64)pos[1] << 8;
    case 1: v ^= (u64)pos[0];
        h ^= mix(v);
        h *= m;
    }

    return mix(h);
}

u64 hash64(const char* s, u64 seed)
{
    return hash64(strlen(s), s, seed);
}



} // namespace synthdef

