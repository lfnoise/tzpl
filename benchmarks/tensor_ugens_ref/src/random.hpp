//
//  random.hpp
//  tensor-ugens
//
//  Created by James McCartney on 10/19/26.
//

#ifndef random_h
#define random_h

#include "types.hpp"

#pragma mark RANDOM NUMBERS

template <int SimdWidth>
struct rand_in_type {};

template <> struct rand_in_type<1> { using type = u64; };
template <> struct rand_in_type<2> { using type = u64x2; };
template <> struct rand_in_type<4> { using type = u64x4; };
template <> struct rand_in_type<8> { using type = u64x8; };

template <int SimdWidth>
struct rand_out_type {};

template <> struct rand_out_type<1> { using type = f64; };
template <> struct rand_out_type<2> { using type = f64x2; };
template <> struct rand_out_type<4> { using type = f64x4; };
template <> struct rand_out_type<8> { using type = f64x8; };

template <int SimdWidth = 1>
struct RandState
{
	using T = typename rand_in_type<SimdWidth>::type;
	T s0, s1;
};

template <int SimdWidth>
void arc4seedrand(RandState<SimdWidth>& r) {
	arc4random_buf(&r, sizeof(r));
}

inline constexpr u64 khash(u64 key) // splitmix64
{
    u64 z = key + 0x9E3779B97F4A7C15;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
    return z ^ (z >> 31);
}

template <int SimdWidth>
void seedrand(RandState<SimdWidth>& r, u64 seed) {
	r.s0 = khash(seed);
	r.s1 = khash(r.s0 + seed);
}

template <class T>
T rotl(const T x, int k) {
	return (x << k) | (x >> (64 - k));
}

template <int SimdWidth>
auto xoroshiro128plusplus(RandState<SimdWidth>& r) {
	using T = typename rand_in_type<SimdWidth>::type;
	const T s0 = r.s0;
	T s1 = r.s1;
	const T result = rotl(s0 + s1, 17) + s0;

	s1 ^= s0;
	r.s0 = rotl(s0, 49) ^ s1 ^ (s1 << 21); // a, b
	r.s1 = rotl(s1, 28); // c

	return result;
}

template <int SimdWidth>
inline auto urand(RandState<SimdWidth>& r) {
	using T = typename rand_in_type<SimdWidth>::type;
	using U = typename rand_out_type<SimdWidth>::type;
	union { T i; U f; } u;
	u.i = T(0x3FF0000000000000LL) | (xoroshiro128plusplus(r) >> 12);
	return u.f - U(1.);
}


template <int SimdWidth>
inline auto birand(RandState<SimdWidth>& r) {
	using T = typename rand_in_type<SimdWidth>::type;
	using U = typename rand_out_type<SimdWidth>::type;
	union { T i; U f; } u;
	u.i = T(0x4000000000000000LL) | (xoroshiro128plusplus(r) >> 12);
	return u.f - U(3.);
}

template <int SimdWidth>
inline auto urandf(RandState<SimdWidth>& r) {
	return cast_f32(urand(r));
}

template <int SimdWidth>
inline auto birandf(RandState<SimdWidth>& r) {
	return cast_f32(birand(r));
}

template <typename T, int N>
struct simd_urand {
    static_assert(is_supported_simd_type_v<T>);
    static_assert(isPowerOfTwo(N));
    using elem_t = T;
    using word_t = typename traits<T>::word_t;
    using scalar_t = typename traits<word_t>::scalar_t;
    
    static constexpr int elems = N;
    static constexpr int width = traits<word_t>::width;
    static constexpr int chans = N * width;
    static constexpr int bits = traits<word_t>::bits;

    mutable RandState<width> state_;

    simd_urand() {
		arc4seedrand(state_);
	}
    simd_urand(u64 seed) {
		seedrand(state_, seed);
	}

	elem_t operator[](i64 i) const {
		if constexpr (bits == 32) {
			return urandf(state_);
		} else {
			return urand(state_);
		}
    }
};

template <int Chans, typename ScalarT>
auto urand() {
    static_assert(is_supported_scalar_type_v<ScalarT>);
    static_assert(isPowerOfTwo(Chans));
    static constexpr int width = std::min(8, Chans);
    static constexpr int elems = Chans / width;
    using scalar_t = ScalarT;
    using word_t = typename simd::Vector<scalar_t, width>::type;
    using elem_t = word_t;
    return simd_vec(simd_urand<elem_t, elems>());
}

template <int Chans, typename ScalarT>
auto urand(u64 seed) {
    static_assert(is_supported_scalar_type_v<ScalarT>);
    static_assert(isPowerOfTwo(Chans));
    static constexpr int width = std::min(8, Chans);
    static constexpr int elems = Chans / width;
    using scalar_t = ScalarT;
    using word_t = typename simd::Vector<scalar_t, width>::type;
    using elem_t = word_t;
    return simd_vec(simd_urand<elem_t, elems>(seed));
}

template <typename T, int N>
struct simd_birand {
    static_assert(is_supported_simd_type_v<T>);
    static_assert(isPowerOfTwo(N));
    using elem_t = T;
    using word_t = typename traits<T>::word_t;
    using scalar_t = typename traits<word_t>::scalar_t;
    
    static constexpr int elems = N;
    static constexpr int width = traits<word_t>::width;
    static constexpr int chans = N * width;
    static constexpr int bits = traits<word_t>::bits;

    mutable RandState<width> state_;

    simd_birand() {
		arc4seedrand(state_);
	}
    simd_birand(u64 seed) {
		seedrand(state_, seed);
	}

	elem_t operator[](i64 i) const {
		if constexpr (bits == 32) {
			return birandf(state_);
		} else {
			return birand(state_);
		}
    }
};

template <int Chans, typename ScalarT>
auto birand() {
    static_assert(is_supported_scalar_type_v<ScalarT>);
    static_assert(isPowerOfTwo(Chans));
    static constexpr int width = std::min(8, Chans);
    static constexpr int elems = Chans / width;
    using scalar_t = ScalarT;
    using word_t = typename simd::Vector<scalar_t, width>::type;
    using elem_t = word_t;
    return simd_vec(simd_birand<elem_t, elems>());
}

template <int Chans, typename ScalarT>
auto birand(u64 seed) {
    static_assert(is_supported_scalar_type_v<ScalarT>);
    static_assert(isPowerOfTwo(Chans));
    static constexpr int width = std::min(8, Chans);
    static constexpr int elems = Chans / width;
    using scalar_t = ScalarT;
    using word_t = typename simd::Vector<scalar_t, width>::type;
    using elem_t = word_t;
    return simd_vec(simd_birand<elem_t, elems>(seed));
}









#endif /* random_h */
