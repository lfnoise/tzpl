//
//  tzpl_hash.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_hash_h
#define tzpl_hash_h

#include "tzpl_common.hpp"

namespace engine {

//=============================================================================================
#pragma mark HASH FUNCTION

const u64 kHashStart = 0x9E3779B97F4A7C15; // = (golden ratio - 1)*2^64 = (.618033988...)*2^64

#define mix(h) ({                    \
            (h) ^= (h) >> 23;        \
            (h) *= 0x2127599bf4325c37ULL;    \
            (h) ^= (h) >> 47; })

inline u64 hash64(u64 n, const void *buf, u64 seed) {
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

inline u64 hash64(const char* s, u64 seed) {
    return hash64(strlen(s), s, seed);
}

}

#endif /* tzpl_hash_h */
