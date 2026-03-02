#pragma once
#include "synthdef_types.hpp"

namespace synthdef {

    const u64 kHashStart = 0x9E3779B97F4A7C15; // = (golden ratio - 1)*2^64 = (.618033988...)*2^64

    // splitmix64
    inline u64 hash64(u64 x, u64 seed = kHashStart) {
        x += seed;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
        x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
        return x ^ (x >> 31);
    }
    
    u64 hash64(size_t n, const void *buf, u64 seed = kHashStart);
    u64 hash64(const char* s, u64 seed = kHashStart);


    inline u64 fnv1a(usize len, u8* bytes, u64 seed = kHashStart) {
        u64 hash = seed;
        for (usize i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= 0x100000001b3;
        }
        return hash;
    }

    // hash_combine takes multiple hash values and combines them into a single hash value.
    inline u64 hash_combine(u64 seed) {
        return seed;
    }
    template<typename... Args>
    inline u64 hash_combine(u64 seed, u64 value, Args... args) {
        return hash_combine(hash64(value, seed), args...);
    }
    template<typename... Args>
    inline u64 hash_combine(u64 seed, usize value, Args... args) {
        return hash_combine(hash64(value, seed), args...);
    }
    template<typename... Args>
    inline u64 hash_combine(u64 seed, f64 value, Args... args) {
        return hash_combine(hash64(value, seed), args...);
    }
    
//    inline u64 newseed() {
//        u64 seed;
//        arc4random_buf(&seed, sizeof(seed));
//        return seed;
//    }
//
//    inline u64 default_seed(u64 seed) {
//        if (seed == 0) { seed = newseed(); }
//        return seed;
//    }

     
}
