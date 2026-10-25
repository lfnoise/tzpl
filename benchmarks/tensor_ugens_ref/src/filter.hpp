//
//  filter.hpp
//  tensor-ugens
//
//  Recursive filters: first order filters, the RBJ cookbook biquads, and the
//  lags, decays and resonators of sapf.
//

#ifndef filter_h
#define filter_h

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <bit>
#include "elementwise.hpp"
#include "signal.hpp"
#include "math_funs.hpp"

// Filters run groups of kVecLen channels across channels, as the phasors do:
// kVecLen channels at once, through kVecLen x kVecLen blocks transposed so each
// vector holds one sample of each channel. A last, partial group that doesn't
// run along samples (below) fills its spare lanes with copies of its first
// channel, and drops their outputs.
//
// A last group of few channels, as for a mono or stereo signal, would waste
// most of each vector that way, so its channels run along samples instead, each
// on its own, when there are at most kMaxAlongConst of them for constant
// parameters, or kMaxAlongAudio for audio rate ones:
//  - With constant parameters, a linear filter runs in block state space form:
//    its state after a vector of kVecLen samples, and the vector's outputs,
//    are linear functions of the state before it and the vector's inputs. The
//    matrices of those functions are found by running the filter from each
//    unit state and each unit input, and kept until a parameter changes. A
//    vector's outputs then don't depend on each other, and the state steps two
//    vectors at a time, its chain of dependences about as long per step as a
//    sample's is in the recurrence. For one channel that is 2 to 7 times as
//    fast as running across. It rounds differently from the recurrence, by
//    about 1e-14 relative to the output for double samples and 1e-5 for float,
//    and up to 1e-12 and 2e-4 for very low, sharp resonances.
//  - Otherwise, the filter is designed for kVecLen samples at once, the
//    parameters being along samples already, and the recurrence runs a sample
//    at a time on scalars. The design is most of a filter's cost when the
//    parameters are audio rate, so that is about 3 times as fast.
//
// A filter type F provides:
//  - kNumParams: the number of parameter inputs, which follow the signal input;
//  - kNumState: the number of state vectors per group of kVecLen channels;
//  - Coefs<T>, a struct of T coefficients only, and coefs(const V* params),
//    which designs the filter from one sample of each parameter in each lane;
//  - step(const Coefs<T>&, T* state, T x), which filters one sample, for T of
//    V (a sample of each of kVecLen channels) or Z (one channel's sample);
//  - optionally, init(T* state, T x), which sets the state from the first
//    input sample before the first run. Without it, the state starts at 0;
//  - kNonlinear = true if step isn't a linear function of the state and x,
//    which rules out the state space form;
//  - kMaxAlongConst and kMaxAlongAudio, to override kMaxAlongChannels: lower
//    where running along samples gains less, as where the design is cheap or
//    the recurrence long, so that a run across channels costs no more for 2
//    channels than for 1 while the along run's cost doubles; and 3 where even
//    3 channels run faster along samples than across.
// Filters with audio rate parameters are redesigned every sample. If every
// parameter of a group of channels is a constant, the group is designed once
// per block.
//
// Frequencies are in cycles per sample, as for the phasors. The first order
// and RBJ filters clamp them to [0, kMaxFilterFreq] so that the design can't
// blow up at Nyquist. Gains are in dB. Times are in samples.

const Z kMaxFilterFreq = 0.499;

inline V filterFreq(V freq) { return simd::clamp(freq, V(0), V(kMaxFilterFreq)); }

// 10^(dB/40): the square root of the linear gain, which the cookbook calls A.
inline V shelfGainRoot(V db) { return vmath::exp(db * (M_LN10 / 40.)); }

#pragma mark First order

// y = b0 x + b1 x[-1] - a1 y[-1], in transposed direct form II.
struct FirstOrderFilter {
	static constexpr int kNumState = 1;
	static constexpr int kMaxAlongConst = 3;
	template <class T> struct Coefs { T b0, b1, a1; };
	template <class T> static T step(const Coefs<T>& c, T* s, T x) {
		T y = c.b0 * x + s[0];
		s[0] = c.b1 * x - c.a1 * y;
		return y;
	}
};

// The bilinear transform of 1 / (1 + s/w), prewarped so the -3 dB point is at
// freq. K = tan(pi freq).
struct LPF1 : FirstOrderFilter {
	static constexpr int kNumParams = 1;  // freq
	static Coefs<V> coefs(const V* p) {
		V K = vmath::tanpi(filterFreq(p[0]));
		V inv = 1. / (1. + K);
		return { K * inv, K * inv, (K - 1.) * inv };
	}
};

struct HPF1 : FirstOrderFilter {
	static constexpr int kNumParams = 1;  // freq
	static Coefs<V> coefs(const V* p) {
		V K = vmath::tanpi(filterFreq(p[0]));
		V inv = 1. / (1. + K);
		return { inv, -inv, (K - 1.) * inv };
	}
};

// Unit gain, with a phase shift of -90 degrees at freq.
struct APF1 : FirstOrderFilter {
	static constexpr int kNumParams = 1;  // freq
	static Coefs<V> coefs(const V* p) {
		V K = vmath::tanpi(filterFreq(p[0]));
		V a1 = (K - 1.) / (K + 1.);
		return { a1, 1., a1 };
	}
};

// Shelves of gain G = A^2 below (or above) freq, and unity gain past it, from
// (s + w A) / (s + w / A) and its high shelf mirror. The gain at freq is A, half
// the shelf's gain in dB.
struct LSF1 : FirstOrderFilter {
	static constexpr int kNumParams = 2;  // freq, db
	static constexpr int kMaxAlongAudio = 3;
	static Coefs<V> coefs(const V* p) {
		V K = vmath::tanpi(filterFreq(p[0]));
		V A = shelfGainRoot(p[1]);
		V KA = K * A, KoA = K / A;
		V inv = 1. / (1. + KoA);
		return { (1. + KA) * inv, (KA - 1.) * inv, (KoA - 1.) * inv };
	}
};

struct HSF1 : FirstOrderFilter {
	static constexpr int kNumParams = 2;  // freq, db
	static constexpr int kMaxAlongAudio = 3;
	static Coefs<V> coefs(const V* p) {
		V K = vmath::tanpi(filterFreq(p[0]));
		V A = shelfGainRoot(p[1]);
		V G = A * A, KA = K * A;
		V inv = 1. / (1. + KA);
		return { (G + KA) * inv, (KA - G) * inv, (KA - 1.) * inv };
	}
};

#pragma mark RBJ biquads

// y = b0 x + b1 x[-1] + b2 x[-2] - a1 y[-1] - a2 y[-2], in transposed direct
// form II.
struct Biquad {
	static constexpr int kNumState = 2;
	static constexpr int kMaxAlongConst = 3;
	static constexpr int kMaxAlongAudio = 3;
	template <class T> struct Coefs { T b0, b1, b2, a1, a2; };
	template <class T> static T step(const Coefs<T>& c, T* s, T x) {
		T y = c.b0 * x + s[0];
		s[0] = c.b1 * x - c.a1 * y + s[1];
		s[1] = c.b2 * x - c.a2 * y;
		return y;
	}
	// Normalizes a cookbook design so that a0 is 1.
	static Coefs<V> normalize(V b0, V b1, V b2, V a0, V a1, V a2) {
		V inv = 1. / a0;
		return { b0 * inv, b1 * inv, b2 * inv, a1 * inv, a2 * inv };
	}
};

// The cookbook's intermediates for frequency freq and quality q.
struct RBJ {
	V cs, alpha;
	RBJ(V freq, V q) {
		V f2 = 2. * filterFreq(freq);  // w0 / pi
		cs = vmath::cospi(f2);
		alpha = vmath::sinpi(f2) / (2. * q);
	}
};

struct LPF : Biquad {
	static constexpr int kNumParams = 2;  // freq, q
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		V b1 = 1. - r.cs;
		return normalize(.5 * b1, b1, .5 * b1, 1. + r.alpha, -2. * r.cs, 1. - r.alpha);
	}
};

struct HPF : Biquad {
	static constexpr int kNumParams = 2;  // freq, q
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		V b1 = -(1. + r.cs);
		return normalize(-.5 * b1, b1, -.5 * b1, 1. + r.alpha, -2. * r.cs, 1. - r.alpha);
	}
};

// Constant 0 dB peak gain.
struct BPF : Biquad {
	static constexpr int kNumParams = 2;  // freq, q
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		return normalize(r.alpha, 0., -r.alpha, 1. + r.alpha, -2. * r.cs, 1. - r.alpha);
	}
};

struct BRF : Biquad {
	static constexpr int kNumParams = 2;  // freq, q
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		return normalize(1., -2. * r.cs, 1., 1. + r.alpha, -2. * r.cs, 1. - r.alpha);
	}
};

struct APF : Biquad {
	static constexpr int kNumParams = 2;  // freq, q
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		return normalize(1. - r.alpha, -2. * r.cs, 1. + r.alpha, 1. + r.alpha, -2. * r.cs, 1. - r.alpha);
	}
};

struct PEQ : Biquad {
	static constexpr int kNumParams = 3;  // freq, q, db
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		V A = shelfGainRoot(p[2]);
		V aA = r.alpha * A, aoA = r.alpha / A;
		return normalize(1. + aA, -2. * r.cs, 1. - aA, 1. + aoA, -2. * r.cs, 1. - aoA);
	}
};

struct LSF : Biquad {
	static constexpr int kNumParams = 3;  // freq, q, db
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		V A = shelfGainRoot(p[2]);
		V Ap1 = A + 1., Am1 = A - 1.;
		V k = 2. * vmath::sqrt(A) * r.alpha;
		return normalize(A * (Ap1 - Am1 * r.cs + k),
		                 2. * A * (Am1 - Ap1 * r.cs),
		                 A * (Ap1 - Am1 * r.cs - k),
		                 Ap1 + Am1 * r.cs + k,
		                 -2. * (Am1 + Ap1 * r.cs),
		                 Ap1 + Am1 * r.cs - k);
	}
};

struct HSF : Biquad {
	static constexpr int kNumParams = 3;  // freq, q, db
	static Coefs<V> coefs(const V* p) {
		RBJ r(p[0], p[1]);
		V A = shelfGainRoot(p[2]);
		V Ap1 = A + 1., Am1 = A - 1.;
		V k = 2. * vmath::sqrt(A) * r.alpha;
		return normalize(A * (Ap1 + Am1 * r.cs + k),
		                 -2. * A * (Am1 + Ap1 * r.cs),
		                 A * (Ap1 + Am1 * r.cs - k),
		                 Ap1 - Am1 * r.cs + k,
		                 2. * (Am1 - Ap1 * r.cs),
		                 Ap1 - Am1 * r.cs - k);
	}
};

#pragma mark sapf lags, decays and resonators

// exp(x): the (2,2) Pade approximant of exp(u), u = x/4,
// (1 + u/2 + u^2/12) / (1 - u/2 + u^2/12), squared twice. In terms of x that is
// (a + b) / (a - b) with a = 1 + x^2/192 and b = x/8, which the compiler can't
// find itself, since it may not reassociate the sums. Since R(-u) = 1/R(u), the
// relative error is about the same for x and -x while it is small: below
// 5.5e-6 for |x| <= 1 and 1e-10 for |x| <= 0.1, growing to 5e-2 at |x| = 6.
// For x <= 0 the absolute error is below 1.3e-4. Past |x| = 13.8 the
// approximant turns back toward 1. x is clamped at -13.8, but not at 13.8,
// since the filters only pass x <= 0 and the second clamp would cost them up to
// 13%, so x must be below 13.8. With audio rate parameters it costs about half
// as much as vmath::exp, though about twice as much as sapf's 1 + x.
inline V fastExp(V x) {
	x = simd::max(x, V(-13.8));
	V a = 1. + x * x * (1. / 192.);
	V b = x * .125;
	V e = (a + b) / (a - b);
	e *= e;
	return e * e;
}

// The coefficient of a one pole filter that decays by 60 dB in time samples.
inline V decayCoef(V time) { return fastExp(log001 / time); }

// Order one pole lowpass filters in series, all of time lagTime: the output
// approaches the input by 60 dB in lagTime samples per pole. The poles start at
// the first input sample.
template <int Order>
struct Lag {
	static constexpr int kNumParams = 1;  // lagTime
	static constexpr int kNumState = Order;
	static constexpr int kMaxAlongConst = 4 - Order;
	static constexpr int kMaxAlongAudio = Order < 2 ? 3 : 1;
	template <class T> struct Coefs { T b1; };
	static Coefs<V> coefs(const V* p) { return { decayCoef(p[0]) }; }
	template <class T> static void init(T* s, T x) {
		for (int m = 0; m < Order; ++m) s[m] = x;
	}
	template <class T> static T step(const Coefs<T>& c, T* s, T x) {
		for (int m = 0; m < Order; ++m) x = s[m] = x + c.b1 * (s[m] - x);
		return x;
	}
};

// Lag, with a time of riseTime while a pole's input is above its output and of
// fallTime otherwise.
template <int Order>
struct LagUD {
	static constexpr int kNumParams = 2;  // riseTime, fallTime
	static constexpr int kNumState = Order;
	static constexpr bool kNonlinear = true;  // the time depends on the signal
	static constexpr int kMaxAlongConst = 1;
	template <class T> struct Coefs { T b1r, b1f; };
	static Coefs<V> coefs(const V* p) { return { decayCoef(p[0]), decayCoef(p[1]) }; }
	template <class T> static void init(T* s, T x) {
		for (int m = 0; m < Order; ++m) s[m] = x;
	}
	template <class T> static T step(const Coefs<T>& c, T* s, T x) {
		for (int m = 0; m < Order; ++m) {
			T b1 = vmath::select(x > s[m], c.b1r, c.b1f);
			x = s[m] = x + b1 * (s[m] - x);
		}
		return x;
	}
};

// An integrator whose output decays by 60 dB in decayTime samples: an impulse
// becomes an exponential decay.
struct Decay {
	static constexpr int kNumParams = 1;  // decayTime
	static constexpr int kNumState = 1;
	static constexpr int kMaxAlongAudio = 3;
	template <class T> struct Coefs { T b1; };
	static Coefs<V> coefs(const V* p) { return { decayCoef(p[0]) }; }
	template <class T> static T step(const Coefs<T>& c, T* s, T x) { return s[0] = x + c.b1 * s[0]; }
};

// The difference of two decays, so an impulse becomes an envelope that rises
// in about attackTime samples and falls in decayTime.
struct Decay2 {
	static constexpr int kNumParams = 2;  // attackTime, decayTime
	static constexpr int kNumState = 2;
	static constexpr int kMaxAlongAudio = 3;
	template <class T> struct Coefs { T b1a, b1b; };
	static Coefs<V> coefs(const V* p) { return { decayCoef(p[0]), decayCoef(p[1]) }; }
	template <class T> static T step(const Coefs<T>& c, T* s, T x) {
		s[0] = x + c.b1a * s[0];
		s[1] = x + c.b1b * s[1];
		return s[1] - s[0];
	}
};

// Removes DC: a differentiator followed by a leaky integrator. leak is the
// integrator's coefficient.
struct LeakDC {
	static constexpr int kNumParams = 1;  // leak
	static constexpr int kNumState = 2;   // x[-1], y[-1]
	static constexpr int kMaxAlongConst = 1;
	static constexpr int kMaxAlongAudio = 0;  // no design to save
	template <class T> struct Coefs { T leak; };
	static Coefs<V> coefs(const V* p) { return { p[0] }; }
	template <class T> static T step(const Coefs<T>& c, T* s, T x) {
		T y = x - s[0] + c.leak * s[1];
		s[0] = x;
		return s[1] = y;
	}
};

// y = x + leak y[-1].
struct Leaky {
	static constexpr int kNumParams = 1;  // leak
	static constexpr int kNumState = 1;
	static constexpr int kMaxAlongAudio = 0;  // no design to save
	template <class T> struct Coefs { T leak; };
	static Coefs<V> coefs(const V* p) { return { p[0] }; }
	template <class T> static T step(const Coefs<T>& c, T* s, T x) { return s[0] = x + c.leak * s[0]; }
};

// Two pole resonators with zeros at DC and Nyquist, and poles at radius R and
// angle 2 pi freq: .5 (1 - z^-2) / (1 - 2 R cos(w0) z^-1 + R^2 z^-2), in
// transposed direct form II.
struct Resonator {
	template <class T> struct Coefs { T a1, a2; };
	static Coefs<V> poles(V freq, V R) {
		return { -2. * R * vmath::cospi(2. * freq), R * R };
	}
	template <class T> static T section(const Coefs<T>& c, T* s, T x) {
		T y = T(.5) * x + s[0];
		s[0] = s[1] - c.a1 * y;
		s[1] = T(-.5) * x - c.a2 * y;
		return y;
	}
};

// A resonator of bandwidth rq * freq.
struct Resonz : Resonator {
	static constexpr int kNumParams = 2;  // freq, rq
	static constexpr int kNumState = 2;
	static constexpr int kMaxAlongConst = 3;
	static Coefs<V> coefs(const V* p) {
		return poles(p[0], fastExp(-M_PI * p[0] * p[1]));
	}
	template <class T> static T step(const Coefs<T>& c, T* s, T x) { return section(c, s, x); }
};

// A resonator whose impulse response decays by 60 dB in ringTime samples.
struct Ringz : Resonator {
	static constexpr int kNumParams = 2;  // freq, ringTime
	static constexpr int kNumState = 2;
	static constexpr int kMaxAlongConst = 3;
	static constexpr int kMaxAlongAudio = 3;
	static Coefs<V> coefs(const V* p) {
		return poles(p[0], decayCoef(p[1]));
	}
	template <class T> static T step(const Coefs<T>& c, T* s, T x) { return section(c, s, x); }
};

// The difference of two ringz with the decay times attackTime and decayTime,
// so an impulse becomes a sine burst with an attack and a decay. In transposed
// form the two can't share their input history, but they need no more state
// than direct form I would with it shared.
struct Formlet : Resonator {
	static constexpr int kNumParams = 3;  // freq, attackTime, decayTime
	static constexpr int kNumState = 4;   // two sections
	static constexpr int kMaxAlongAudio = 3;
	template <class T> struct Coefs { Resonator::Coefs<T> a, b; };
	static Coefs<V> coefs(const V* p) {
		V cs = -2. * vmath::cospi(2. * p[0]);
		V Ra = decayCoef(p[1]);
		V Rb = decayCoef(p[2]);
		return { { cs * Ra, Ra * Ra }, { cs * Rb, Rb * Rb } };
	}
	template <class T> static T step(const Coefs<T>& c, T* s, T x) {
		return section(c.b, s + 2, x) - section(c.a, s, x);
	}
};

#pragma mark Running

// Channels of a last group that run along samples, at most, by default.
const int kMaxAlongChannels = 2;

template <typename F>
constexpr bool kHasInit = requires(V* s, V x) { F::init(s, x); };

template <typename F>
constexpr bool kLinear = !requires { requires F::kNonlinear; };

// The most channels of a last group that run along samples, if its parameters
// are all constant, or if not.
template <typename F>
constexpr int maxAlong(bool constParams) {
	int c = kMaxAlongChannels, a = kMaxAlongChannels;
	if constexpr (requires { F::kMaxAlongConst; }) c = F::kMaxAlongConst;
	if constexpr (requires { F::kMaxAlongAudio; }) a = F::kMaxAlongAudio;
	return constParams ? c : a;
}

template <typename F, typename T>
using FilterCoefs = typename F::template Coefs<T>;

// Lane k of each coefficient of c.
template <typename F>
FilterCoefs<F, Z> laneCoefs(const FilterCoefs<F, V>& c, int k) {
	constexpr int n = sizeof(c) / sizeof(V);
	static_assert(sizeof(FilterCoefs<F, V>) == n * sizeof(V));
	static_assert(sizeof(FilterCoefs<F, Z>) == n * sizeof(Z));
	std::array<V, n> cv = std::bit_cast<std::array<V, n>>(c);
	std::array<Z, n> cz;
	for (int i = 0; i < n; ++i) cz[i] = cv[i][k];
	return std::bit_cast<FilterCoefs<F, Z>>(cz);
}

// A linear filter with constant coefficients, kVecLen samples at a time. For
// a vector x of inputs and the state s before it, the outputs are
// sum over q of ys[q] s[q], plus sum over j of yx[j] x[j]; and the state after
// it is s'[m] = sum over q of ss[m][q] s[q], plus the sum of sx[m] * x's lanes.
// ss2 and sx2 likewise take the state across two vectors, so that the chain of
// dependences from one state to the next is as long for two vectors as it is
// for one. Each vector's outputs are found from the state before it.
template <typename F>
struct BlockFilter {
	static constexpr int S = F::kNumState;
	V ys[S], yx[kVecLen], sx[S], sx2[S][2];
	Z ss[S][S], ss2[S][S];

	BlockFilter() = default;

	// The columns of ys, yx, ss and sx are the filter's responses over kVecLen
	// samples to a unit state, or to a unit input.
	explicit BlockFilter(const FilterCoefs<F, Z>& c) {
		for (int q = 0; q < S; ++q) {
			Z s[S] = {};
			s[q] = 1;
			for (int k = 0; k < kVecLen; ++k) ys[q][k] = F::step(c, s, Z(0));
			for (int m = 0; m < S; ++m) ss[m][q] = s[m];
		}
		for (int j = 0; j < kVecLen; ++j) {
			Z s[S] = {};
			for (int k = 0; k < kVecLen; ++k) yx[j][k] = F::step(c, s, Z(k == j));
			for (int m = 0; m < S; ++m) sx[m][j] = s[m];
		}
		// s'' = ss (ss s + sx . x0) + sx . x1
		for (int m = 0; m < S; ++m) {
			for (int q = 0; q < S; ++q) {
				Z a = 0;
				for (int r = 0; r < S; ++r) a += ss[m][r] * ss[r][q];
				ss2[m][q] = a;
			}
			V a = 0;
			for (int r = 0; r < S; ++r) a += ss[m][r] * sx[r];
			sx2[m][0] = a;
			sx2[m][1] = sx[m];
		}
	}

	V out(const Z* s, V x) const {
		V y = yx[0] * x[0];
		for (int j = 1; j < kVecLen; ++j) y += yx[j] * x[j];
		for (int q = 0; q < S; ++q) y += ys[q] * s[q];
		return y;
	}

	// The input terms are summed first, since they don't depend on the state.
	void next(Z* s, V x) const {
		Z n[S];
		for (int m = 0; m < S; ++m) {
			Z a = simd::reduce_add(sx[m] * x);
			for (int q = 0; q < S; ++q) a += ss[m][q] * s[q];
			n[m] = a;
		}
		for (int m = 0; m < S; ++m) s[m] = n[m];
	}

	void next2(Z* s, V x0, V x1) const {
		Z n[S];
		for (int m = 0; m < S; ++m) {
			Z a = simd::reduce_add(sx2[m][0] * x0 + sx2[m][1] * x1);
			for (int q = 0; q < S; ++q) a += ss2[m][q] * s[q];
			n[m] = a;
		}
		for (int m = 0; m < S; ++m) s[m] = n[m];
	}

	// Filters nv vectors of in, stepping by inc, into y.
	void run(Z* s, const VP* in, int inc, V* y, int nv) const {
		int v = 0;
		for (; v + 2 <= nv; v += 2) {
			// Both inputs are read before either output is written, for in place.
			V x0 = in[0], x1 = in[inc];
			in += 2 * inc;
			Z mid[S];
			for (int m = 0; m < S; ++m) mid[m] = s[m];
			next(mid, x0);
			y[v] = out(s, x0);
			y[v + 1] = out(mid, x1);
			next2(s, x0, x1);
		}
		if (v < nv) {
			V x = *in;
			y[v] = out(s, x);
			next(s, x);
		}
	}
};

// The design of a channel running along samples with constant parameters, for
// the parameter values params: a BlockFilter, or for a nonlinear filter, its
// coefficients. It is kept in the state from block to block, and redone if a
// parameter changes.
template <typename F>
struct AlongDesign {
	std::conditional_t<kLinear<F>, BlockFilter<F>, FilterCoefs<F, Z>> design;
	Z params[F::kNumParams];
	bool ready;
};

// Runs channel ch along samples for nv vectors. Its state is lane k of the
// group's state vectors, so it is laid out as for a run across channels, or a
// run1. ad is its design.
template <typename F>
void filterAlong(UGen* u, int ch, VP* state, int k, AlongDesign<F>& ad, bool first, int nv) {
	constexpr int P = F::kNumParams;
	constexpr int S = F::kNumState;
	const VP* in[1 + P];
	int inc[1 + P];
	bool constParams = true;
	for (int m = 0; m <= P; ++m) {
		Wire& w = *u->ins[m].wires[ch];
		in[m] = vecInput(w);
		inc[m] = w.stride;
		if (m > 0 && w.stride) constParams = false;
	}
	V* y = (V*)u->outs[0].wires[ch].buffer;

	Z s[S];
	for (int m = 0; m < S; ++m) s[m] = state[m][k];
	if constexpr (kHasInit<F>) {
		if (first) F::init(s, (*in[0])[0]);
	}
	if (constParams) {
		bool same = ad.ready;
		for (int m = 0; m < P; ++m) same = same && ad.params[m] == (*in[1 + m])[0];
		if (!same) {
			V p[P];
			for (int m = 0; m < P; ++m) p[m] = *in[1 + m];
			FilterCoefs<F, Z> c = laneCoefs<F>(F::coefs(p), 0);
			if constexpr (kLinear<F>) ad.design = BlockFilter<F>(c);
			else ad.design = c;
			for (int m = 0; m < P; ++m) ad.params[m] = p[m][0];
			ad.ready = true;
		}
		if constexpr (kLinear<F>) {
			ad.design.run(s, in[0], inc[0], y, nv);
		} else {
			for (int v = 0; v < nv; ++v) {
				V x = *in[0], out = 0;
				in[0] += inc[0];
				for (int j = 0; j < kVecLen; ++j) out[j] = F::step(ad.design, s, x[j]);
				y[v] = out;
			}
		}
	} else {
		for (int v = 0; v < nv; ++v) {
			V x = *in[0], p[P], out = 0;
			in[0] += inc[0];
			for (int m = 0; m < P; ++m) {
				p[m] = *in[1 + m];
				in[1 + m] += inc[1 + m];
			}
			FilterCoefs<F, V> c = F::coefs(p);  // lane j for sample j
			for (int j = 0; j < kVecLen; ++j) out[j] = F::step(laneCoefs<F>(c, j), s, x[j]);
			y[v] = out;
		}
	}
	for (int m = 0; m < S; ++m) state[m][k] = s[m];
}

// Filter_run's state: F::kNumState vectors per group of kVecLen channels, lane
// k of each belonging to channel k of the group, then, if F has init, one more
// vector whose first lane is nonzero once the state has been initialized, then
// an AlongDesign for each channel of a last group that runs along samples.
template <typename F>
int filterStateVectors(int chans) {
	int groups = (chans + kVecLen - 1) / kVecLen;
	return groups * F::kNumState + (kHasInit<F> ? 1 : 0);
}

// The number of channels of the last group if it may run along samples, else 0.
template <typename F>
int alongChannels(int chans) {
	int lanes = chans % kVecLen;
	return lanes <= std::max(maxAlong<F>(true), maxAlong<F>(false)) ? lanes : 0;
}

template <typename F>
size_t filterStateBytes(int chans) {
	size_t bytes = filterStateVectors<F>(chans) * sizeof(V) + alongChannels<F>(chans) * sizeof(AlongDesign<F>);
	return (bytes + sizeof(V) - 1) / sizeof(V) * sizeof(V);  // for aligned_alloc
}

// True if every parameter of channels i to i + lanes - 1 is a constant.
template <typename F>
bool constParamChannels(UGen* u, int i, int lanes) {
	for (int m = 1; m <= F::kNumParams; ++m)
		for (int k = 0; k < lanes; ++k)
			if (u->ins[m].wires[i + k]->stride) return false;
	return true;
}

template <typename F>
AlongDesign<F>* alongDesigns(UGen* u) {
	return (AlongDesign<F>*)((V*)u->state + filterStateVectors<F>(u->outs[0].chans));
}

// Runs one group of kVecLen channels for nv vectors. ins[m][k] and inc[m][k]
// are input m of lane k, where input 0 is the signal; y[k] is lane k's output,
// or NULL for a spare lane.
template <typename F, bool ConstParams>
void filterGroup(const VP* (&ins)[1 + F::kNumParams][kVecLen],
                 const int (&inc)[1 + F::kNumParams][kVecLen],
                 V* const (&y)[kVecLen], V* s, int nv) {
	constexpr int P = F::kNumParams;
	FilterCoefs<F, V> c;
	if constexpr (ConstParams) {
		// A constant's splat holds its value in every lane, so lane 0 of each
		// lane's splat makes one vector of the group's parameters.
		V p[P];
		for (int m = 0; m < P; ++m)
			for (int k = 0; k < kVecLen; ++k) p[m][k] = (*ins[1 + m][k])[0];
		c = F::coefs(p);
	}
	const VP* in[1 + P][kVecLen];
	std::copy(&ins[0][0], &ins[0][0] + (1 + P) * kVecLen, &in[0][0]);

	for (int v = 0; v < nv; ++v) {
		// After transposing, x[j] (and p[m][j]) hold sample j of each channel.
		V x[kVecLen], out[kVecLen];
		for (int k = 0; k < kVecLen; ++k) {
			x[k] = *in[0][k];
			in[0][k] += inc[0][k];
		}
		transpose4(x[0], x[1], x[2], x[3]);
		V p[P][kVecLen];
		if constexpr (!ConstParams) {
			for (int m = 0; m < P; ++m) {
				for (int k = 0; k < kVecLen; ++k) {
					p[m][k] = *in[1 + m][k];
					in[1 + m][k] += inc[1 + m][k];
				}
				transpose4(p[m][0], p[m][1], p[m][2], p[m][3]);
			}
		}
		for (int j = 0; j < kVecLen; ++j) {
			if constexpr (!ConstParams) {
				V pj[P];
				for (int m = 0; m < P; ++m) pj[m] = p[m][j];
				c = F::coefs(pj);
			}
			out[j] = F::step(c, s, x[j]);
		}
		transpose4(out[0], out[1], out[2], out[3]);
		for (int k = 0; k < kVecLen; ++k) {
			if (y[k]) y[k][v] = out[k];
		}
	}
}

// True on the first run (or run1) of a filter with init, after which it is
// false.
template <typename F>
bool filterFirstRun(VP* state, int chans) {
	if constexpr (kHasInit<F>) {
		Z& initialized = ((Z*)&state[filterStateVectors<F>(chans) - 1])[0];
		bool first = !initialized;
		initialized = 1;
		return first;
	}
	return false;
}

template <typename F>
void Filter_run(UGen* u, int n) {
	constexpr int P = F::kNumParams;
	assert(n % kVecLen == 0);
	int nv = n / kVecLen;
	int chans = u->outs[0].chans;
	VP* state = (VP*)u->state;
	bool first = filterFirstRun<F>(state, chans);
	for (int i = 0; i < chans; i += kVecLen, state += F::kNumState) {
		int lanes = std::min(kVecLen, chans - i);
		if (lanes < kVecLen && lanes <= maxAlong<F>(constParamChannels<F>(u, i, lanes))) {
			AlongDesign<F>* ad = alongDesigns<F>(u);
			for (int k = 0; k < lanes; ++k) filterAlong<F>(u, i + k, state, k, ad[k], first, nv);
			continue;
		}
		const VP* ins[1 + P][kVecLen];
		int inc[1 + P][kVecLen];
		V* y[kVecLen];
		bool constParams = true;
		for (int k = 0; k < kVecLen; ++k) {
			int ch = k < lanes ? i + k : i;
			for (int m = 0; m <= P; ++m) {
				Wire& w = *u->ins[m].wires[ch];
				ins[m][k] = vecInput(w);
				inc[m][k] = w.stride;
				if (m > 0 && w.stride) constParams = false;
			}
			y[k] = k < lanes ? (V*)u->outs[0].wires[ch].buffer : nullptr;
		}

		V s[F::kNumState];
		for (int m = 0; m < F::kNumState; ++m) s[m] = state[m];
		if constexpr (kHasInit<F>) {
			if (first) {
				V x0 = 0;
				for (int k = 0; k < kVecLen; ++k) x0[k] = (*ins[0][k])[0];
				F::init(s, x0);
			}
		}
		if (constParams) {
			filterGroup<F, true>(ins, inc, y, s, nv);
		} else {
			filterGroup<F, false>(ins, inc, y, s, nv);
		}
		for (int m = 0; m < F::kNumState; ++m) state[m] = s[m];
	}
}

// One sample of each channel, for graphs compiled with a block size of 1. Each
// group of kVecLen channels is one vector, lane k for channel k, as in
// Filter_run's state, so it needs no transposes.
template <typename F>
void Filter_run1(UGen* u) {
	constexpr int P = F::kNumParams;
	int chans = u->outs[0].chans;
	VP* state = (VP*)u->state;
	bool first = filterFirstRun<F>(state, chans);
	for (int i = 0; i < chans; i += kVecLen, state += F::kNumState) {
		int lanes = std::min(kVecLen, chans - i);
		V x = 0, p[P] = {};
		for (int k = 0; k < kVecLen; ++k) {
			int ch = k < lanes ? i + k : i;
			x[k] = *u->ins[0].wires[ch]->buffer;
			for (int m = 0; m < P; ++m) p[m][k] = *u->ins[1 + m].wires[ch]->buffer;
		}

		V s[F::kNumState];
		for (int m = 0; m < F::kNumState; ++m) s[m] = state[m];
		if constexpr (kHasInit<F>) {
			if (first) F::init(s, x);
		}
		V y = F::step(F::coefs(p), s, x);
		for (int m = 0; m < F::kNumState; ++m) state[m] = s[m];
		for (int k = 0; k < lanes; ++k) *u->outs[0].wires[i + k].buffer = y[k];
	}
}

// Creates a filter UGen on sources: the signal, then F's parameters, all
// broadcast to a common shape. It runs in place on the signal, since channel j
// reads its inputs at each vector before writing it. Returns S{nullptr} on
// allocation failure. If only the state can't be allocated, the UGen stays in
// the graph with no run function.
template <typename F>
S newFilter(Output* (&sources)[1 + F::kNumParams]) {
	UGen* u = newUGen(gGraph, 1 + F::kNumParams, sources, 1, nullptr, true);
	if (!u) return S{nullptr};
	size_t bytes = filterStateBytes<F>(u->outs[0].chans);
	if (bytes > 0) {
		u->state = aligned_alloc(alignof(V), bytes);
		if (!u->state) return S{nullptr};
		memset(u->state, 0, bytes);
	}
	u->run = Filter_run<F>;
	u->run1 = Filter_run1<F>;
	return S{&u->outs[0]};
}

// First order filters. freq is the cutoff (-3 dB, or the -90 degree phase
// point of apf1, or the midpoint of the shelves in dB).
S lpf1(S in, S freq);
S hpf1(S in, S freq);
S apf1(S in, S freq);
S lsf1(S in, S freq, S db);
S hsf1(S in, S freq, S db);

// RBJ cookbook biquads. q is the cookbook's Q; for the shelves, a q of
// 1/sqrt(2) is the steepest slope without overshoot.
S lpf(S in, S freq, S q);
S hpf(S in, S freq, S q);
S bpf(S in, S freq, S q);
S brf(S in, S freq, S q);
S apf(S in, S freq, S q);
S peq(S in, S freq, S q, S db);
S lsf(S in, S freq, S q, S db);
S hsf(S in, S freq, S q, S db);

// sapf's lags, decays and resonators. Times are 60 dB decay times in samples.
// leak is the coefficient of leakdc's and leaky's integrators.
S lag(S in, S lagTime);
S lag2(S in, S lagTime);
S lag3(S in, S lagTime);
S lagud(S in, S riseTime, S fallTime);
S lagud2(S in, S riseTime, S fallTime);
S lagud3(S in, S riseTime, S fallTime);
S decay(S in, S decayTime);
S decay2(S in, S attackTime, S decayTime);
S leakdc(S in, S leak = 0.995);
S leaky(S in, S leak = 0.995);
S resonz(S in, S freq, S rq);
S ringz(S in, S freq, S ringTime);
S formlet(S in, S freq, S attackTime, S decayTime);

#endif /* filter_h */
