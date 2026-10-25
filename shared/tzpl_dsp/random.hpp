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
//  tzpl_dsp/random.hpp
//  xoroshiro128++ generators, 1 or 4 lanes wide, and uniform values of each
//  element type (tensor-ugens random.hpp and noise.cpp; ARCHITECTURE.md §6.4
//  and §12).
//
//  RandState<4> runs 4 independent streams, one per lane, as u64x4: one draw
//  is 4 lanes of 64 random bits. Each lane is seeded separately (splitmix64
//  of the seed and the lane), so lanes are independent streams, and a group of
//  4 channels can keep 4 such generators (randGroupSeed), as tensor-ugens does:
//  along samples, a draw from generator k fills the next 4 samples of channel
//  k; across channels, generator j's draw is sample j of the 4 channels.
//
//  Uniform floats take their mantissa from the top bits of each 64-bit lane,
//  with the widths from the element type: 52 bits for f64, 23 for f32. A
//  float in [base, 2 base) is built from the bits and the exponent of base, and
//  shifted: [0, 1) for uniform, [-1, 1) for bipolar.
//
//  No system calls: seeding is from a caller-supplied seed only, so these are
//  safe on the audio thread.
//

#ifndef tzpl_dsp_random_hpp
#define tzpl_dsp_random_hpp

#include <limits>

#include "vec_types.hpp"

namespace tzpl::dsp {

// splitmix64: a well-mixed 64-bit value of key, for seeding.
inline constexpr u64 splitmix64(u64 key) {
    u64 z = key + 0x9E3779B97F4A7C15ul;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ul;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBul;
    return z ^ (z >> 31);
}

template <int W> struct rand_word;
template <> struct rand_word<1> { using type = u64; };
template <> struct rand_word<4> { using type = u64x4; };

// W independent xoroshiro128++ streams.
template <int W = 4>
struct RandState {
    using U = typename rand_word<W>::type;
    U s0, s1;
};

// Seeds lane k from seed and stream + k: each (seed, stream) pair gives an
// independent stream. A state of all zeros would stick at zero; splitmix64
// makes that a probability-2^-128 event, and it is corrected anyway.
template <int W>
inline void randSeed(RandState<W>& r, u64 seed, u64 stream = 0) {
    for (int k = 0; k < W; ++k) {
        u64 a = splitmix64(seed ^ splitmix64(stream + u64(k)));
        u64 b = splitmix64(a + seed);
        if ((a | b) == 0) b = 1;
        if constexpr (W == 1) {
            r.s0 = a;
            r.s1 = b;
        } else {
            r.s0[k] = a;
            r.s1[k] = b;
        }
    }
}

// Seeds the 4 generators of channel group `group`: 16 independent streams.
inline void randGroupSeed(RandState<4> (&r)[kVecLen], u64 seed, u64 group = 0) {
    for (int g = 0; g < kVecLen; ++g) randSeed(r[g], seed, (group * kVecLen + u64(g)) * kVecLen);
}

template <class U> inline U rotl(U x, int k) { return (x << k) | (x >> (64 - k)); }

// 64 random bits per lane.
template <int W>
inline typename RandState<W>::U randBits(RandState<W>& r) {
    using U = typename RandState<W>::U;
    U const s0 = r.s0;
    U s1 = r.s1;
    U const result = rotl(s0 + s1, 17) + s0;
    s1 ^= s0;
    r.s0 = rotl(s0, 49) ^ s1 ^ (s1 << 21);
    r.s1 = rotl(s1, 28);
    return result;
}

namespace detail {

// A float of type T (f32, f64 or their vectors) in [base, 2 base), from the top
// mantissa bits of each lane of draw. base must be a power of two.
template <class T, class U>
inline T uniformFrom(U draw, elem_t<T> base) {
    using E = elem_t<T>;
    using B = bits_t<T>;
    constexpr int kMant = std::numeric_limits<E>::digits - 1;  // 52 or 23
    using EB = elem_t<B>;
    EB baseBits = __builtin_bit_cast(EB, base);
    if constexpr (sizeof(E) == 8) {
        B m = bitcast<B>(draw >> (64 - kMant));
        return bitcast<T>(m | B(baseBits));
    } else {
        U top = draw >> (64 - kMant);
        B m = convert<B>(top);
        return bitcast<T>(m | B(baseBits));
    }
}

}  // namespace detail

// Uniform in [0, 1), of element type elem_t<T>, one lane per stream. T is a
// scalar for RandState<1>, a 4-lane vector for RandState<4>.
//  - f32, f64: the top 23 / 52 bits as the mantissa.
//  - i32, i64: the top 32 / all 64 bits, as uniform integers over the type.
template <class T, int W>
inline T uniform(RandState<W>& r) {
    static_assert(lanes_v<T> == W);
    using E = elem_t<T>;
    auto draw = randBits(r);
    if constexpr (std::is_floating_point_v<E>) {
        return detail::uniformFrom<T>(draw, E(1)) - T(1);
    } else if constexpr (sizeof(E) == 8) {
        return bitcast<T>(draw);
    } else {
        return bitcast<T>(convert<bits_t<T>>(draw >> 32));
    }
}

// Uniform in [-1, 1), from [2, 4) - 3.
template <class T, int W>
inline T bipolar(RandState<W>& r) {
    static_assert(lanes_v<T> == W);
    static_assert(is_float_v<T>);
    using E = elem_t<T>;
    return detail::uniformFrom<T>(randBits(r), E(2)) - T(3);
}

}  // namespace tzpl::dsp

#endif  // tzpl_dsp_random_hpp
