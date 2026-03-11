//
//  tzpl_random.hpp
//  audio engine
//
//  Created by James McCartney on 5/31/22.
//

#ifndef tzpl_random_h
#define tzpl_random_h

#include "tzpl_common.hpp"
#ifndef __APPLE__
#include <sys/random.h>
#endif

namespace engine {

template <int Chans>
struct rand_in_type {};

template <> struct rand_in_type<1> { using type = u64; };
template <> struct rand_in_type<2> { using type = u64x2; };
template <> struct rand_in_type<4> { using type = u64x4; };
template <> struct rand_in_type<8> { using type = u64x8; };

template <int Chans>
struct rand_out_type {};

template <> struct rand_out_type<1> { using type = f64; };
template <> struct rand_out_type<2> { using type = f64x2; };
template <> struct rand_out_type<4> { using type = f64x4; };
template <> struct rand_out_type<8> { using type = f64x8; };

template <int Chans = 1>
struct RandState
{
	using T = typename rand_in_type<Chans>::type;
	T s0, s1;
};

template <class T>
T rotl(const T x, int k) {
	return (x << k) | (x >> (64 - k));
}

template <int Chans>
auto xoroshiro128plusplus(RandState<Chans>& r) {
	using T = typename rand_in_type<Chans>::type;
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
	using T = typename rand_in_type<Chans>::type;
	using U = typename rand_out_type<Chans>::type;
	union { T i; U f; } u;
	u.i = 0x3FF0000000000000LL | (xoroshiro128plusplus(r) >> 12);
	return u.f - 1.;
}


template <int Chans>
inline auto birand(RandState<Chans>& r) {
	using T = typename rand_in_type<Chans>::type;
	using U = typename rand_out_type<Chans>::type;
	union { T i; U f; } u;
	u.i = 0x4000000000000000LL | (xoroshiro128plusplus(r) >> 12);
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

template <int Chans>
void arc4seedrand(RandState<Chans>& r) {
#ifdef __APPLE__
	arc4random_buf(&r, sizeof(r));
#else
	getrandom(&r, sizeof(r), 0);
#endif
}

}

#endif /* tzpl_random_h */
