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
//  synthdef_random.hpp
//  tzpl
//
//  Created by James McCartney on 8/25/24.
//

#ifndef synthdef_random_hpp
#define synthdef_random_hpp

#include "synthdef_types.hpp"
#include "tzpl_simd.hpp"
#ifndef __APPLE__
#include <sys/random.h>
#endif

namespace synthdef {

template <int Chans>
struct u64xN {};

template <> struct u64xN<1> { using type = u64; };
template <> struct u64xN<2> { using type = u64x2; };
template <> struct u64xN<4> { using type = u64x4; };
template <> struct u64xN<8> { using type = u64x8; };

template <int Chans>
struct f64xN {};

template <> struct f64xN<1> { using type = f64; };
template <> struct f64xN<2> { using type = f64x2; };
template <> struct f64xN<4> { using type = f64x4; };
template <> struct f64xN<8> { using type = f64x8; };

template <int Chans = 1>
struct RandState
{
	using T = typename u64xN<Chans>::type;
	T s0, s1;
};

using RandState1 = RandState<1>;
using RandState2 = RandState<2>;
using RandState4 = RandState<4>;
using RandState8 = RandState<8>;

template <class T>
T rotl(const T x, int k) {
	return (x << k) | (x >> (64 - k));
}

// xoroshiro128plusplus
template <int Chans>
auto rand64(RandState<Chans>& r) {
	using T = typename u64xN<Chans>::type;
	const T s0 = r.s0;
	T s1 = r.s1;
	const T result = rotl(s0 + s1, 17) + s0;

	s1 ^= s0;
	r.s0 = rotl(s0, 49) ^ s1 ^ (s1 << 21); // a, b
	r.s1 = rotl(s1, 28); // c

	return result;
}

template <int Chans>
inline auto urand(RandState<Chans>& r) {
	using T = typename u64xN<Chans>::type;
	using U = typename f64xN<Chans>::type;
	union { T i; U f; } u;
	u.i = 0x3FF0000000000000LL | (rand64(r) >> 12);
	return u.f - 1.;
}


template <int Chans>
inline auto birand(RandState<Chans>& r) {
	using T = typename u64xN<Chans>::type;
	using U = typename f64xN<Chans>::type;
	union { T i; U f; } u;
	u.i = 0x4000000000000000LL | (rand64(r) >> 12);
	return u.f - 3.;
}

template <int Chans>
inline auto urandf(RandState<Chans>& r) {
	return cast_f32(urand(r));
}

template <int Chans>
inline auto birandf(RandState<Chans>& r) {
	return cast_f32(birand(r));
}

template <>
inline auto urandf(RandState<1>& r) {
	return f32(urand(r));
}

template <>
inline auto birandf(RandState<1>& r) {
	return f32(birand(r));
}

template <int Chans>
void arc4seedrand(RandState<Chans>& r) {
#ifdef __APPLE__
	arc4random_buf(&r, sizeof(r));
#else
	getrandom(&r, sizeof(r), 0);
#endif
}

} // namespace synthdef

#endif /* synthdef_random_hpp */
