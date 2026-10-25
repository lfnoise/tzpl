//
//  noise.cpp
//  tensor-ugens
//

#include "noise.hpp"
#include <bit>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits>
#include <type_traits>
#include "elementwise.hpp"
#include "random.hpp"

// Channels are generated in groups of kVecLen. Each group has kVecLen
// xoroshiro128++ generators (random.hpp's RandState<kVecLen>), each running
// kVecLen independent streams, one per lane.
//
// Noises run along samples: a draw from generator k fills the next kVecLen
// samples of channel k. Each step draws once for each channel, from different
// generators: the draws don't depend on each other, so they overlap, where
// draws from one generator would each wait for the last. Violet noise's
// difference, gray noise's running xor and brown noise's walk have parallel
// forms, and pink noise's dice are rolled a lane at a time.
//
// Brown, pink and blue noise also run across channels, as the filters do, for
// full groups of kVecLen channels: generator j's draw is sample j of
// the step, lane k belonging to channel k of the group, and the kVecLen x
// kVecLen block of output is transposed into the channels' buffers. That takes
// less work per sample than along samples, where pink noise needs a die for
// each lane and brown noise a fold. A partial group runs along samples, as
// it would drop draws for its spare lanes across channels. Low frequency noise
// runs as described at LFNoise.
//
// run1, for graphs compiled with a block size of 1, makes one sample of each
// channel of a group from one draw of the group's first generator, lane k for
// channel k. All of these keep each channel's state in the same place, lane k
// of the group's, so each continues from the state the others leave.

// A draw: 64 random bits per lane, whatever the sample type.
typedef u64x4 U;

// Unsigned integers the size of Z, and vectors of them the size of V.
typedef std::conditional_t<sizeof(Z) == 8, uint64_t, uint32_t> ZBits;
typedef ZBits VBits __attribute__((ext_vector_type(kVecLen)));
static_assert(sizeof(VBits) == sizeof(V));

namespace {

typedef RandState<kVecLen> Rand;

void seed(Rand (&r)[kVecLen]) {
	for (int k = 0; k < kVecLen; ++k) arc4seedrand(r[k]);
}

// Uniform in [base, 2 base), for base a power of 2: the top bits of each lane
// of a draw as the mantissa of a Z with base's exponent.
V uniform(U draw, Z base) {
	constexpr int kMantissaBits = std::numeric_limits<Z>::digits - 1;
	VBits mantissa = __builtin_convertvector(draw >> (64 - kMantissaBits), VBits);
	return std::bit_cast<V>(mantissa | std::bit_cast<ZBits>(base));
}

// Uniform in [0, 1).
V unipolar(Rand& r) { return uniform(xoroshiro128plusplus(r), 1) - 1; }

// Uniform in [-1, 1).
V bipolar(Rand& r) { return uniform(xoroshiro128plusplus(r), 2) - 3; }

// Uniform in [-1/16, 1/16).
V step16(Rand& r) { return uniform(xoroshiro128plusplus(r), .125) - .1875; }

// A noise type N provides State, the state of a group of kVecLen channels,
// whose first member is its generators r; init(State&), which seeds them and
// sets the rest of the state; next(State&, int k, Rand& r, V in), which
// returns the next kVecLen samples of channel k from generator r (in is the
// next vector of the channel's input, for a noise with kInputs = 1: density
// for dust, freq for lfnoise; a noise with kInputs = 2 takes V a, V b); and
// run1(State&, Group1&), which generates one sample of each channel. Brown, pink and blue noise also provide runAcross(State&,
// Group&, int nv), which generates nv vectors of each channel across channels.

// The arguments of a noise's runAcross: y[k] is channel k of the group's
// output, or NULL past the last channel, and for a noise with inputs, in[k] is
// its first input, stepping by inc[k] vectors (the first channel's input past the
// last). Groups of at least kMinAcross channels run across.
struct Group {
	V* y[kVecLen];
	const VP* in[kVecLen];
	int inc[kVecLen];
};

// The arguments of a noise's run1: y[k] is channel k of the group's output,
// and in[k] and in2[k] its first and second inputs, for a noise with them, or
// NULL past the last channel.
struct Group1 {
	Z* y[kVecLen];
	const Z* in[kVecLen];
	const Z* in2[kVecLen];
};

// Writes lane k of out to channel k of the group.
void store1(Group1& g, V out) {
	for (int k = 0; k < kVecLen; ++k) {
		if (g.y[k]) *g.y[k] = out[k];
	}
}

// Writes the kVecLen sample vectors out (out[j] holds sample j of each channel)
// to vector v of the group's channels.
void storeAcross(Group& g, int v, V (&out)[kVecLen]) {
	transpose4(out[0], out[1], out[2], out[3]);
	for (int k = 0; k < kVecLen; ++k) {
		if (g.y[k]) g.y[k][v] = out[k];
	}
}

struct White {
	struct State { Rand r[kVecLen]; };
	static void init(State& s) { seed(s.r); }
	static V next(State&, int, Rand& r, V) { return bipolar(r); }
	static void run1(State& s, Group1& g) { store1(g, bipolar(s.r[0])); }
};

// x[n] - x[n-1] for x white in [-.5, .5). Starting x[-1] at a random value
// makes the first sample like the rest.
struct Violet {
	struct State { Rand r[kVecLen]; Z prev[kVecLen]; };
	static void init(State& s) {
		seed(s.r);
		for (int k = 0; k < kVecLen; ++k) s.prev[k] = unipolar(s.r[k])[0] - Z(.5);
	}
	static V next(State& s, int k, Rand& r, V) {
		V x = unipolar(r) - .5;
		V y = x - __builtin_shufflevector(V(s.prev[k]), x, 0, 4, 5, 6);
		s.prev[k] = x[kVecLen - 1];
		return y;
	}
	static void run1(State& s, Group1& g) {
		V x = unipolar(s.r[0]) - .5;
		V prev = 0;
		for (int k = 0; k < kVecLen; ++k) {
			prev[k] = s.prev[k];
			s.prev[k] = x[k];
		}
		store1(g, x - prev);
	}
};

// Each sample flips a random bit of a 32 bit counter, and the counter is the
// output. A vector's counters are the previous counter xor the running xor of
// its flips. The counter starts at a random value.
struct Gray {
	struct State { Rand r[kVecLen]; uint64_t counter[kVecLen]; };
	// The low 32 bits of c as signed counters, scaled to [-1, 1).
	static V value(U c) {
		simd_long4 signedCounter = (simd_long4)(c << 32) >> 32;
		return __builtin_convertvector(signedCounter, V) * (1. / 2147483648.);
	}
	static void init(State& s) {
		seed(s.r);
		for (int k = 0; k < kVecLen; ++k) s.counter[k] = xoroshiro128plusplus(s.r[k])[0] & 0xFFFFFFFF;
	}
	static V next(State& s, int k, Rand& r, V) {
		U zero = 0;
		U flips = U(1) << (xoroshiro128plusplus(r) & 31);
		flips ^= __builtin_shufflevector(flips, zero, 4, 0, 1, 2);
		flips ^= __builtin_shufflevector(flips, zero, 4, 5, 0, 1);
		U c = s.counter[k] ^ flips;
		s.counter[k] = c[kVecLen - 1];
		return value(c);
	}
	static void run1(State& s, Group1& g) {
		U c = U(1) << (xoroshiro128plusplus(s.r[0]) & 31);
		for (int k = 0; k < kVecLen; ++k) {
			c[k] ^= s.counter[k];
			s.counter[k] = c[k];
		}
		store1(g, value(c));
	}
};

// A random walk reflected at -1 and 1, starting at a random point. Lane k of z
// is channel k's position.
//
// Across channels, each step is reflected as it is taken. Along samples, the
// steps' running sums make a free walk, which is folded into [-1, 1] like a
// triangle wave. Where the folded walk runs backward, it takes each step
// negated; since the steps are symmetric, that is a reflected walk all the
// same.
struct Brown {
	struct State { Rand r[kVecLen]; V z; };
	static void init(State& s) {
		seed(s.r);
		s.z = bipolar(s.r[0]);
	}
	// The walk's next point, from generator r.
	static V step(V z, Rand& r) {
		z += step16(r);
		z = vmath::select(z > 1., 2. - z, z);
		return vmath::select(z < -1., -2. - z, z);
	}
	// u folded into [-1, 1] with period 4, so that fold(u) == u in [-1, 1].
	static V fold(V u) {
		V w = (u + 1.) * .25;
		return 1. - simd::abs(4. * (w - vmath::floor(w)) - 2.);
	}
	static V next(State& s, int k, Rand& r, V) {
		V z = fold(s.z[k] + prefixSum(step16(r)));
		s.z[k] = z[kVecLen - 1];
		return z;
	}
	static void runAcross(State& s, Group& g, int nv) {
		V z = s.z;
		for (int v = 0; v < nv; ++v) {
			V out[kVecLen];
			for (int j = 0; j < kVecLen; ++j) out[j] = z = step(z, s.r[j]);
			storeAcross(g, v, out);
		}
		s.z = z;
	}
	static void run1(State& s, Group1& g) { store1(g, s.z = step(s.z, s.r[0])); }
};

// Voss-McCartney pink noise, with Magnus Jonsson's way of choosing which die to
// roll: die k is rolled when the draw's lowest set bit is bit k, so on average
// every 2^(k+1) samples (die 15 also takes the draws with bits 0..14 clear).
// The dice and a white value each sample are 48 bit integers, uniform in units
// of 2^48. Their sum S, centered and divided by 8, is the output, (S - 8.5) / 8:
// within [-1.0625, 1.0625), with a standard deviation of 0.149. It exceeds 1
// with a probability of 4.3e-20 per sample. The dice start rolled, so the
// noise starts out stationary. Lane k of total and of each die is channel k's.
struct Pink {
	static constexpr int kNumDice = 16;
	struct State { Rand r[kVecLen]; U total; uint64_t dice[kNumDice][kVecLen]; };
	static void init(State& s) {
		seed(s.r);
		s.total = 0;
		for (int d = 0; d < kNumDice; ++d) {
			U roll = xoroshiro128plusplus(s.r[0]) >> 16;
			for (int k = 0; k < kVecLen; ++k) s.dice[d][k] = roll[k];
			s.total += roll;
		}
	}
	// Pink noise from sums of dice and white values, both in units of 2^48.
	static V value(U sum) {
		return __builtin_convertvector(sum, V) * (1. / (8. * 281474976710656.)) - 8.5 / 8.;
	}
	// The next sample of each lane's channel, from generator j.
	static V nextAcross(State& s, int j) {
		U draw = xoroshiro128plusplus(s.r[j]);
		U roll = draw >> 16;
		U change = 0;
		for (int k = 0; k < kVecLen; ++k) {
			int d = __builtin_ctz((uint32_t)draw[k] | 0x8000u);
			change[k] = roll[k] - s.dice[d][k];
			s.dice[d][k] = roll[k];
		}
		s.total += change;
		return value(s.total + (xoroshiro128plusplus(s.r[j]) >> 16));
	}
	// The next kVecLen samples of channel k, a die rolled for each lane.
	static V next(State& s, int k, Rand& r, V) {
		U draw = xoroshiro128plusplus(r);
		U totals = 0;
		uint64_t total = s.total[k];
		for (int j = 0; j < kVecLen; ++j) {
			int d = __builtin_ctz((uint32_t)draw[j] | 0x8000u);
			uint64_t roll = draw[j] >> 16;
			total += roll - s.dice[d][k];
			s.dice[d][k] = roll;
			totals[j] = total;
		}
		s.total[k] = total;
		return value(totals + (xoroshiro128plusplus(r) >> 16));
	}
	static void runAcross(State& s, Group& g, int nv) {
		for (int v = 0; v < nv; ++v) {
			V out[kVecLen];
			for (int j = 0; j < kVecLen; ++j) out[j] = nextAcross(s, j);
			storeAcross(g, v, out);
		}
	}
	static void run1(State& s, Group1& g) { store1(g, nextAcross(s, 0)); }
};

// x[n] - x[n-1] for x 4 times pink noise. x[-1] is a pink sample, not 0, so the
// output doesn't start with a jump. Lane k of prev is channel k's.
struct Blue {
	struct State : Pink::State { V prev; };
	static void init(State& s) {
		Pink::init(s);
		s.prev = 4. * Pink::nextAcross(s, 0);
	}
	static V next(State& s, int k, Rand& r, V d) {
		V x = 4. * Pink::next(s, k, r, d);
		V y = x - __builtin_shufflevector(V(s.prev[k]), x, 0, 4, 5, 6);
		s.prev[k] = x[kVecLen - 1];
		return y;
	}
	static void runAcross(State& s, Group& g, int nv) {
		V prev = s.prev;
		for (int v = 0; v < nv; ++v) {
			V out[kVecLen];
			for (int j = 0; j < kVecLen; ++j) {
				V x = 4. * Pink::nextAcross(s, j);
				out[j] = x - prev;
				prev = x;
			}
			storeAcross(g, v, out);
		}
		s.prev = prev;
	}
	static void run1(State& s, Group1& g) {
		V x = 4. * Pink::nextAcross(s, 0);
		store1(g, x - s.prev);
		s.prev = x;
	}
};

template <bool Bipolar>
struct DustT {
	static constexpr int kInputs = 1;  // density
	struct State { Rand r[kVecLen]; };
	static void init(State& s) { seed(s.r); }
	// An impulse where z < d.
	static V impulses(V z, V d) {
		V a = z / d;
		if constexpr (Bipolar) a = 2. * a - 1.;
		return vmath::select(z < d, a, V(0.));
	}
	static V next(State&, int, Rand& r, V d) { return impulses(unipolar(r), d); }
	static void run1(State& s, Group1& g) {
		V d = 1.;  // spare lanes, whose output is dropped
		for (int k = 0; k < kVecLen; ++k) {
			if (g.in[k]) d[k] = *g.in[k];
		}
		store1(g, impulses(unipolar(s.r[0]), d));
	}
};

// A value drawn afresh each sample, as sapf's coinz, randz, rand2z, irandz,
// irand2z and xrandz: F::value(u, a, b) maps u, uniform in [0, 1), to a sample
// of the distribution its inputs a and b (b unused for one input) describe.
template <typename F>
struct Draw {
	static constexpr int kInputs = F::kInputs;
	struct State { Rand r[kVecLen]; };
	static void init(State& s) { seed(s.r); }
	static V next(State&, int, Rand& r, V a, V b = 0) { return F::value(unipolar(r), a, b); }
	static void run1(State& s, Group1& g) {
		V a = 1, b = 1;  // spare lanes, whose output is dropped
		for (int k = 0; k < kVecLen; ++k) {
			if (g.in[k]) a[k] = *g.in[k];
			if (g.in2[k]) b[k] = *g.in2[k];
		}
		store1(g, F::value(unipolar(s.r[0]), a, b));
	}
};

// 1 with probability p, else 0.
struct Coin {
	static constexpr int kInputs = 1;
	static V value(V u, V p, V) { return vmath::truth(u < p); }
};

// Uniform in [lo, hi), for lo and hi in either order.
struct Uniform {
	static constexpr int kInputs = 2;
	static V value(V u, V a, V b) {
		V lo = simd::min(a, b);
		return lo + (simd::max(a, b) - lo) * u;
	}
};

// Uniform in [-a, a).
struct Uniform2 {
	static constexpr int kInputs = 1;
	static V value(V u, V a, V) { return (a + a) * u - a; }
};

// An integer from trunc(lo) to trunc(hi), each equally likely, for lo and hi
// in either order. The min keeps a product that rounds up to n below it.
struct IUniform {
	static constexpr int kInputs = 2;
	static V value(V u, V a, V b) {
		a = vmath::trunc(a);
		b = vmath::trunc(b);
		V lo = simd::min(a, b), hi = simd::max(a, b);
		return simd::min(lo + vmath::floor((hi - lo + 1) * u), hi);
	}
};

// floor of a uniform value in [-a, a + 1): for an integer a, an integer from
// -a to a, each equally likely. The min, with the largest floor of the range,
// keeps a product that rounds up to 2 a + 1 below it.
struct IUniform2 {
	static constexpr int kInputs = 1;
	static V value(V u, V a, V) {
		V top = simd::max(vmath::ceil(a), vmath::floor(-a));
		return simd::min(vmath::floor((a + a + 1) * u - a), top);
	}
};

// Log-uniform in [lo, hi], for lo and hi in either order and of
// the same sign: lo (hi / lo)^u.
struct XUniform {
	static constexpr int kInputs = 2;
	static V value(V u, V a, V b) {
		V lo = simd::min(a, b);
		return lo * vmath::exp2(vmath::log2(simd::max(a, b) / lo) * u);
	}
};

// Random values at freq values per sample on average, as sapf's lfnoise0,
// lfnoise1 and lfnoise3: held (Points = 1), joined by lines (2), or joined by
// a cubic Hermite spline through the last 4 (4), which reach the output a
// cycle of the phase after they are drawn. The phase starts at 1, so the first
// sample draws a value, and the history starts with Points - 1 values already
// drawn. Lane k of the phase, the last Points values y and the coefficients c
// of the curve between them is channel k's.
//
// Unlike sapf, a negative freq stops the phase, rather than running it back
// past where the curve was fit. A phase that passes several whole cycles in one
// sample draws only the last Points values, the rest never being output; the
// values are independent, so that changes nothing but the cost.
//
// A group of 3 or 4 channels runs across channels, a sample of each at once,
// a spare lane copying the first channel's freq. A sample with a new cycle in
// any lane updates the values and curves of those lanes with selects, from a
// draw whose lane k is channel k's.
//
// A group of 1 or 2 channels runs along samples, each channel on its own for a
// whole block, its state in registers. A vector's phases are the running sums
// of freq, and while a vector has no new cycle, its outputs are the curve at
// those phases. A vector with one runs a sample at a time. New values are
// drawn kVecLen at a time from the channel's generator, and the ones left at
// the end of a block are dropped.
template <int Points>
struct LFNoise {
	static constexpr int kInputs = 1;  // freq
	static constexpr int kMinAcross = kVecLen - 1;
	struct State { Rand r[kVecLen]; V phase; V y[Points]; V c[Points]; };
	// One channel's state, lane k of the group's, held in registers.
	struct Chan {
		Z x;
		Z y[Points], c[Points];
		Chan(const State& s, int k) : x(s.phase[k]) {
			for (int i = 0; i < Points; ++i) {
				y[i] = s.y[i][k];
				c[i] = s.c[i][k];
			}
		}
		void store(State& s, int k) const {
			s.phase[k] = x;
			for (int i = 0; i < Points; ++i) {
				s.y[i][k] = y[i];
				s.c[i][k] = c[i];
			}
		}
	};
	// Values in [-1, 1), kVecLen per draw from r.
	struct Pool {
		Rand& r;
		V v;
		int n = 0;
		explicit Pool(Rand& r) : r(r) {}
		Z next() {
			if (n == 0) {
				v = bipolar(r);
				n = kVecLen;
			}
			return v[--n];
		}
	};
	static void init(State& s) {
		seed(s.r);
		s.phase = 1;
		Pool pool(s.r[0]);
		for (int k = 0; k < kVecLen; ++k) {
			Chan ch(s, k);
			for (int i = 0; i + 1 < Points; ++i) push(ch, pool.next());
			fit(ch.y, ch.c);
			ch.store(s, k);
		}
	}
	// A value drawn as v, in [-1, 1).
	template <class T>
	static T value(T v) {
		if constexpr (Points == 4) return T(Z(.8)) * v;  // the spline overshoots by up to 1.25
		else return v;
	}
	// The coefficients c of the curve through values y, for T Z or V.
	template <class T>
	static void fit(const T* y, T* c) {
		if constexpr (Points == 1) {
			c[0] = y[0];
		} else if constexpr (Points == 2) {
			c[0] = y[0];
			c[1] = y[1] - y[0];
		} else {
			c[0] = y[1];
			c[1] = T(Z(.5)) * (y[2] - y[0]);
			c[2] = y[0] - T(Z(2.5)) * y[1] + T(Z(2)) * y[2] - T(Z(.5)) * y[3];
			c[3] = T(Z(1.5)) * (y[1] - y[2]) + T(Z(.5)) * (y[3] - y[0]);
		}
	}
	// Adds v to the channel's values. The curve needs refitting after.
	static void push(Chan& ch, Z v) {
		for (int i = 0; i + 1 < Points; ++i) ch.y[i] = ch.y[i + 1];
		ch.y[Points - 1] = value(v);
	}
	// The curve with coefficients c at phase x, for C and T each Z or V.
	template <class C, class T>
	static T eval(const C* c, T x) {
		if constexpr (Points == 1) return T(c[0]);
		else if constexpr (Points == 2) return c[0] + c[1] * x;
		else return ((c[3] * x + c[2]) * x + c[1]) * x + c[0];
	}
	// Brings the channel's phase, at least 1, back into [0, 1), with a new value
	// for each cycle passed, up to Points of them.
	static void wrap(Chan& ch, Pool& pool) {
		Z n = std::floor(ch.x);
		ch.x -= n;
		int m = n < Points ? int(n) : Points;
		for (int i = 0; i < m; ++i) push(ch, pool.next());
		if (m) fit(ch.y, ch.c);
	}
	// A vector with a new cycle in it, a sample at a time.
	[[gnu::noinline]] static V slow(Chan& ch, Pool& pool, V freq) {
		V out = 0;
		for (int j = 0; j < kVecLen; ++j) {
			if (ch.x >= 1) wrap(ch, pool);
			out[j] = eval(ch.c, ch.x);
			ch.x += freq[j];
		}
		return out;
	}
	// Runs channel k for nv vectors, from freq input in into y.
	static void runAlong(State& s, int k, Rand& r, const VP* in, int inc, V* y, int nv) {
		Chan ch(s, k);
		Pool pool(r);
		for (int v = 0; v < nv; ++v) {
			V freq = simd::max(V(*in), V(0));
			in += inc;
			V sums = prefixSum(freq);
			// The phase at each sample, before adding its freq.
			V p = ch.x + __builtin_shufflevector(sums, V(0), 4, 0, 1, 2);
			if (p[kVecLen - 1] < 1) {
				y[v] = eval(ch.c, p);
				ch.x += sums[kVecLen - 1];
			} else {
				y[v] = slow(ch, pool, freq);
			}
		}
		ch.store(s, k);
	}
	// Brings each lane's phase x back into [0, 1), with a new value for each
	// cycle passed, up to Points of them, drawn from r.
	[[gnu::noinline]] static void wrapAcross(V& x, V* y, V* c, Rand& r) {
		V n = vmath::floor(x);
		x -= n;
		for (int i = 0; i < Points; ++i) {
			VMask more = n > Z(i);
			if (!simd::any(more)) break;
			V v = value(bipolar(r));
			for (int m = 0; m + 1 < Points; ++m) y[m] = vmath::select(more, y[m + 1], y[m]);
			y[Points - 1] = vmath::select(more, v, y[Points - 1]);
		}
		fit(y, c);
	}
	// A full group across channels: lane k of each vector is channel k.
	static void runAcross(State& s, Group& g, int nv) {
		V x = s.phase, y[Points], c[Points];
		for (int i = 0; i < Points; ++i) {
			y[i] = s.y[i];
			c[i] = s.c[i];
		}
		for (int v = 0; v < nv; ++v) {
			V freq[kVecLen];
			for (int k = 0; k < kVecLen; ++k) {
				freq[k] = *g.in[k];
				g.in[k] += g.inc[k];
			}
			transpose4(freq[0], freq[1], freq[2], freq[3]);
			V out[kVecLen];
			for (int j = 0; j < kVecLen; ++j) {
				if (simd::any(x >= 1)) wrapAcross(x, y, c, s.r[j]);
				out[j] = eval(c, x);
				x += simd::max(freq[j], V(0));
			}
			storeAcross(g, v, out);
		}
		s.phase = x;
		for (int i = 0; i < Points; ++i) {
			s.y[i] = y[i];
			s.c[i] = c[i];
		}
	}
	static void run1(State& s, Group1& g) {
		Pool pool(s.r[0]);
		V out = 0;
		for (int k = 0; k < kVecLen; ++k) {
			Z freq = g.in[k] ? std::max(*g.in[k], Z(0)) : 0;
			Chan ch(s, k);
			if (ch.x >= 1) wrap(ch, pool);
			out[k] = eval(ch.c, ch.x);
			ch.x += freq;
			ch.store(s, k);
		}
		store1(g, out);
	}
};

// The number of inputs of N, 0 to 2.
template <typename N>
constexpr int numInputs() {
	if constexpr (requires { N::kInputs; }) return N::kInputs;
	else return 0;
}

template <typename N>
constexpr bool kHasAcross = requires(typename N::State& s, Group& g) { N::runAcross(s, g, 1); };

// The fewest channels of a group that run across, if N can.
template <typename N>
constexpr int minAcross() {
	if constexpr (requires { N::kMinAcross; }) return N::kMinAcross;
	else return kVecLen;
}

template <typename N>
constexpr bool kHasRunAlong = requires(typename N::State& s, Rand& r, const VP* in, V* y) {
	N::runAlong(s, 0, r, in, 1, y, 1);
};

// Runs a group of L channels, starting at channel i, along samples. Each
// channel k has its own generator, k, so that the draws of a step don't
// depend on each other. A group of at most kVecLen / 2 channels has more
// generators than channels, and channel k alternates between generators k and
// k + kVecLen / 2, so that each draw needn't wait for the last either.
template <typename N, int L>
void noiseAlong(UGen* u, typename N::State& s, int i, int nv) {
	constexpr int M = numInputs<N>();
	constexpr int alt = L <= kVecLen / 2 ? kVecLen / 2 : 0;
	V* y[L];
	const VP* in[M + 1][L];
	int inc[M + 1][L];
	for (int k = 0; k < L; ++k) {
		y[k] = (V*)u->outs[0].wires[i + k].buffer;
		for (int m = 0; m < M; ++m) {
			Wire& w = *u->ins[m].wires[i + k];
			in[m][k] = vecInput(w);
			inc[m][k] = w.stride;
		}
	}
	for (int v = 0; v < nv; ++v) {
		int shift = alt && (v & 1) ? alt : 0;
		for (int k = 0; k < L; ++k) {
			V x[M + 1] = {};
			for (int m = 0; m < M; ++m) {
				x[m] = *in[m][k];
				in[m][k] += inc[m][k];
			}
			if constexpr (M == 2) y[k][v] = N::next(s, k, s.r[k + shift], x[0], x[1]);
			else y[k][v] = N::next(s, k, s.r[k + shift], x[0]);
		}
	}
}

template <typename N>
void Noise_run(UGen* u, int n) {
	assert(n % kVecLen == 0);
	int nv = n / kVecLen;
	int chans = u->outs[0].chans;
	typename N::State* state = (typename N::State*)u->state;
	for (int i = 0; i < chans; i += kVecLen, ++state) {
		int lanes = std::min(kVecLen, chans - i);
		if constexpr (kHasAcross<N>) {
			// Along samples, a partial group draws a generator per channel and
			// vector, where across channels it would draw kVecLen.
			if (lanes >= minAcross<N>()) {
				Group g;
				for (int k = 0; k < kVecLen; ++k) {
					g.y[k] = k < lanes ? (V*)u->outs[0].wires[i + k].buffer : nullptr;
					if constexpr (numInputs<N>() > 0) {
						Wire& w = *u->ins[0].wires[k < lanes ? i + k : i];
						g.in[k] = vecInput(w);
						g.inc[k] = w.stride;
					}
				}
				N::runAcross(*state, g, nv);
				continue;
			}
		}
		if constexpr (kHasRunAlong<N>) {
			// A channel at a time, a block at a time, so that its state stays in
			// registers.
			for (int k = 0; k < lanes; ++k) {
				Wire& w = *u->ins[0].wires[i + k];
				N::runAlong(*state, k, state->r[k], vecInput(w), w.stride, (V*)u->outs[0].wires[i + k].buffer, nv);
			}
		} else {
			switch (lanes) {
				case 1: noiseAlong<N, 1>(u, *state, i, nv); break;
				case 2: noiseAlong<N, 2>(u, *state, i, nv); break;
				case 3: noiseAlong<N, 3>(u, *state, i, nv); break;
				default: noiseAlong<N, 4>(u, *state, i, nv); break;
			}
		}
	}
}

template <typename N>
void Noise_run1(UGen* u) {
	int chans = u->outs[0].chans;
	typename N::State* state = (typename N::State*)u->state;
	for (int i = 0; i < chans; i += kVecLen, ++state) {
		int lanes = std::min(kVecLen, chans - i);
		Group1 g;
		for (int k = 0; k < kVecLen; ++k) {
			g.y[k] = k < lanes ? u->outs[0].wires[i + k].buffer : nullptr;
			g.in[k] = u->numIn > 0 && k < lanes ? u->ins[0].wires[i + k]->buffer : nullptr;
			g.in2[k] = u->numIn > 1 && k < lanes ? u->ins[1].wires[i + k]->buffer : nullptr;
		}
		N::run1(*state, g);
	}
}

// Creates a noise UGen of shape, or for a noise with inputs, of the broadcast
// of shape and the inputs' shapes, with the inputs broadcast to it. Those run
// in place on their first input, since they read each vector of the inputs
// before writing that vector.
// Returns S{nullptr} on allocation failure. If only the state can't be
// allocated, the UGen stays in the graph with no run function.
template <typename N>
S newNoise(Shape shape, Output* input = nullptr, Output* input2 = nullptr) {
	constexpr int numIn = numInputs<N>();
	Output* inputs[2] = {input, input2};
	for (int m = 0; m < numIn; ++m) shape = Shape_broadcast(&shape, &inputs[m]->shape);
	UGen* u = newUGen(gGraph, numIn, inputs, 1, &shape, numIn > 0);
	if (!u) return S{nullptr};
	for (int m = 0; m < numIn; ++m) {
		u->ins[m].shape = shape;
		if (!mapInputWires(&u->ins[m], &shape)) return S{nullptr};
	}

	typedef typename N::State State;
	int groups = (u->outs[0].chans + kVecLen - 1) / kVecLen;
	if (groups > 0) {
		State* state = (State*)aligned_alloc(alignof(State), groups * sizeof(State));
		if (!state) return S{nullptr};
		memset(state, 0, groups * sizeof(State));
		for (int i = 0; i < groups; ++i) N::init(state[i]);
		u->state = state;
	}
	u->run = Noise_run<N>;
	u->run1 = Noise_run1<N>;
	return S{&u->outs[0]};
}

} // namespace

S white(Shape shape) { return newNoise<White>(shape); }
S pink(Shape shape) { return newNoise<Pink>(shape); }
S blue(Shape shape) { return newNoise<Blue>(shape); }
S violet(Shape shape) { return newNoise<Violet>(shape); }
S gray(Shape shape) { return newNoise<Gray>(shape); }
S brown(Shape shape) { return newNoise<Brown>(shape); }
S dust(S density, Shape shape) { return newNoise<DustT<false>>(shape, density.o); }
S dust2(S density, Shape shape) { return newNoise<DustT<true>>(shape, density.o); }
S lfnoise0(S freq, Shape shape) { return newNoise<LFNoise<1>>(shape, freq.o); }
S lfnoise1(S freq, Shape shape) { return newNoise<LFNoise<2>>(shape, freq.o); }
S lfnoise3(S freq, Shape shape) { return newNoise<LFNoise<4>>(shape, freq.o); }
S coin(S p, Shape shape) { return newNoise<Draw<Coin>>(shape, p.o); }
S rand(S lo, S hi, Shape shape) { return newNoise<Draw<Uniform>>(shape, lo.o, hi.o); }
S rand2(S a, Shape shape) { return newNoise<Draw<Uniform2>>(shape, a.o); }
S irand(S lo, S hi, Shape shape) { return newNoise<Draw<IUniform>>(shape, lo.o, hi.o); }
S irand2(S a, Shape shape) { return newNoise<Draw<IUniform2>>(shape, a.o); }
S xrand(S lo, S hi, Shape shape) { return newNoise<Draw<XUniform>>(shape, lo.o, hi.o); }
