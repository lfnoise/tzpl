//
//  phasor.hpp
//  tensor-ugens
//
//  Phase-accumulating oscillators: run functions and constructors.
//

#ifndef phasor_h
#define phasor_h

#include <cassert>
#include <cstdlib>
#include <type_traits>
#include "elementwise.hpp"
#include "signal.hpp"
#include "math_funs.hpp"

// Phase-accumulating oscillators. Each output channel keeps its phase in
// ((Z*)u->state)[channel]. Every sample, phase = frac(phase + fm), and the
// output is the shaper applied to the new phase (offset by frac(phase + pm) in
// the PM variants), and for a shaper with a parameter, to the sample of the
// parameter's input as well. A shaper of the previous sample's phase too (as
// upulse1's is) keeps it in ((Z*)u->state)[chans + channel]. It starts at -1,
// more than 0.5 from every phase, so the first sample starts a cycle.
//
// The recurrence is vectorized two ways:
//  - across channels (phasorRunAcross, the default): kVecLen channels run at
//    once, through kVecLen x kVecLen blocks transposed so each vector holds one
//    sample of each channel. The phase arithmetic is the same as a scalar loop's,
//    and this works for any recurrence, including ones with no parallel form,
//    such as filters. Channels left over from whole groups of kVecLen run along
//    samples instead.
//  - along samples (phasorRun): a vector of fm values becomes its running sums,
//    and the new phases are frac(phase + running sum), since
//    frac(frac(x) + y) == frac(x + y). Only the last lane carries over. Rounding
//    the running sums lets the phase drift from a scalar loop's, by about 1e-12
//    cycles per second at a constant frequency for double samples.

struct Identity {
	template <class T> T operator()(T x) const { return x; }
};

// ws applied to the lanes of its arguments: as one vector op if ws accepts Vs,
// or a lane at a time if it only accepts Zs.
template <typename Shaper, class... A>
inline V applyShaper(Shaper& ws, A... a) {
	if constexpr (std::is_invocable_v<Shaper&, A...>) {
		return ws(a...);
	} else {
		V y = 0;
		for (int k = 0; k < kVecLen; ++k) y[k] = ws(a[k]...);
		return y;
	}
}

// ws applied to its arguments: directly if ws accepts Zs, or to splats of them
// if it only accepts Vs.
template <typename Shaper, class... A>
inline Z applyShaper1(Shaper& ws, A... a) {
	if constexpr (std::is_invocable_v<Shaper&, A...>) {
		return ws(a...);
	} else {
		return ws(V(a)...)[0];
	}
}

// A phasor UGen's inputs are fm, then NP (0 or 1) parameters, then pm if PM.
// Prev says the shaper takes the previous phase, as ws(x, x1, w), where
// otherwise it takes ws(x, w), or ws(x) with no parameter.
template <typename Shaper, int NP, bool Prev, class T>
inline T shape(Shaper& ws, T x, T x1, T w) {
	if constexpr (std::is_same_v<T, V>) {
		if constexpr (Prev) return applyShaper(ws, x, x1, w);
		else if constexpr (NP) return applyShaper(ws, x, w);
		else return applyShaper(ws, x);
	} else {
		if constexpr (Prev) return applyShaper1(ws, x, x1, w);
		else if constexpr (NP) return applyShaper1(ws, x, w);
		else return applyShaper1(ws, x);
	}
}

// The vectors of channel i's input m, in turn.
struct VecIn {
	const VP* p;
	int inc;
	VecIn() : p(nullptr), inc(0) {}
	VecIn(UGen* u, int m, int i) {
		Wire& w = *u->ins[m].wires[i];
		p = vecInput(w);
		inc = w.stride;
	}
	V next() {
		V x = *p;
		p += inc;
		return x;
	}
};

// Runs channel i of a phasor-based UGen along samples, nv vectors.
template <typename Shaper, bool PM, int NP = 0, bool Prev = false>
void phasorChannel(UGen* u, int i, int nv) {
	Shaper ws;
	Z* state = (Z*)u->state;
	int chans = u->outs[0].chans;
	VecIn fm(u, 0, i), pw, pm;
	if constexpr (NP) pw = VecIn(u, 1, i);
	if constexpr (PM) pm = VecIn(u, 1 + NP, i);
	V* y = (V*)u->outs[0].wires[i].buffer;
	
	Z phase = state[i];
	V last = Prev ? V(state[chans + i]) : V(0);  // the previous vector of phases
	for (int v = 0; v < nv; ++v) {
		V p = frac(phase + prefixSum(fm.next()));
		phase = p[kVecLen - 1];
		if constexpr (PM) p = frac(p + pm.next());
		V w = 0, p1 = 0;
		if constexpr (NP) w = pw.next();
		if constexpr (Prev) {
			static_assert(kVecLen == 4);
			p1 = __builtin_shufflevector(last, p, 3, 4, 5, 6);
			last = p;
		}
		y[v] = shape<Shaper, NP, Prev>(ws, p, p1, w);
	}
	state[i] = phase;
	if constexpr (Prev) state[chans + i] = last[kVecLen - 1];
}

template <typename Shaper, bool PM, int NP = 0, bool Prev = false>
void phasorRun(UGen* u, int n) {
	assert(n % kVecLen == 0);
	for (int i = 0; i < u->outs[0].chans; ++i) {
		phasorChannel<Shaper, PM, NP, Prev>(u, i, n / kVecLen);
	}
}

// Reads the next vector of each of kVecLen channels' inputs and transposes
// them, so that x[j] holds sample j of each channel.
inline void nextAcross(VecIn (&in)[kVecLen], V (&x)[kVecLen]) {
	for (int k = 0; k < kVecLen; ++k) x[k] = in[k].next();
	transpose4(x[0], x[1], x[2], x[3]);
}

template <typename Shaper, bool PM, int NP = 0, bool Prev = false>
void phasorRunAcross(UGen* u, int n) {
	Shaper ws;
	assert(n % kVecLen == 0);
	int nv = n / kVecLen;
	int chans = u->outs[0].chans;
	Z* state = (Z*)u->state;
	int i = 0;
	for (; i + kVecLen <= chans; i += kVecLen) {
		VecIn fm[kVecLen], pw[kVecLen], pm[kVecLen];
		V* y[kVecLen];
		for (int k = 0; k < kVecLen; ++k) {
			fm[k] = VecIn(u, 0, i + k);
			if constexpr (NP) pw[k] = VecIn(u, 1, i + k);
			if constexpr (PM) pm[k] = VecIn(u, 1 + NP, i + k);
			y[k] = (V*)u->outs[0].wires[i + k].buffer;
		}
		
		// one lane per channel
		V phase = *(const VP*)&state[i];
		V last = 0;
		if constexpr (Prev) last = *(const VP*)&state[chans + i];
		for (int v = 0; v < nv; ++v) {
			V f[kVecLen], w[kVecLen] = {}, q[kVecLen], out[kVecLen];
			nextAcross(fm, f);
			if constexpr (NP) nextAcross(pw, w);
			if constexpr (PM) nextAcross(pm, q);
			for (int j = 0; j < kVecLen; ++j) {
				phase = frac(phase + f[j]);
				V p = phase;
				if constexpr (PM) p = frac(p + q[j]);
				out[j] = shape<Shaper, NP, Prev>(ws, p, last, w[j]);
				if constexpr (Prev) last = p;
			}
			transpose4(out[0], out[1], out[2], out[3]);
			for (int k = 0; k < kVecLen; ++k) {
				y[k][v] = out[k];
			}
		}
		*(VP*)&state[i] = phase;
		if constexpr (Prev) *(VP*)&state[chans + i] = last;
	}
	for (; i < chans; ++i) {
		phasorChannel<Shaper, PM, NP, Prev>(u, i, nv);
	}
}

// One sample of each channel, for graphs compiled with a block size of 1.
template <typename Shaper, bool PM, int NP = 0, bool Prev = false>
void phasorRun1(UGen* u) {
	Shaper ws;
	Z* state = (Z*)u->state;
	Output& py = u->outs[0];
	for (int i = 0; i < py.chans; ++i) {
		Z phase = frac(state[i] + *u->ins[0].wires[i]->buffer);
		state[i] = phase;
		if constexpr (PM) phase = frac(phase + *u->ins[1 + NP].wires[i]->buffer);
		Z w = 0, p1 = 0;
		if constexpr (NP) w = *u->ins[1].wires[i]->buffer;
		if constexpr (Prev) {
			p1 = state[py.chans + i];
			state[py.chans + i] = phase;
		}
		*py.wires[i].buffer = shape<Shaper, NP, Prev>(ws, phase, p1, w);
	}
}

void Phasor_run(UGen* u, int n);
void PhasorPM_run(UGen* u, int n);
void Phasor_run1(UGen* u);
void PhasorPM_run1(UGen* u);

// Phasor-based oscillators. fm is the phase increment per sample and pm a
// phase offset, both in cycles; the phase starts at 0. Prev UGens keep the
// previous phase of each channel after the phases, starting at -1.
// Each returns S{nullptr} on allocation failure. If only the state can't be
// allocated, the UGen stays in the graph with no run function.
template <UGenRunFun Run, UGenRun1Fun Run1, int N, bool Prev = false>
S newPhasorUGen(Output* (&sources)[N]) {
    UGen* u = newUGen(gGraph, N, sources, 1, nullptr, true);
    if (!u) return S{nullptr};
    int chans = u->outs[0].chans;
    int n = Prev ? 2 * chans : chans;
    u->state = calloc(n, sizeof(Z));
    if (!u->state && chans > 0) return S{nullptr};
    if (Prev) {
        Z* state = (Z*)u->state;
        for (int i = chans; i < n; ++i) state[i] = Z(-1);
    }
    u->run = Run;
    u->run1 = Run1;
    return S{&u->outs[0]};
}

template <typename WS, bool PM, int NP, bool Prev, int N>
S newWaveshaperOsc(Output* (&sources)[N]) {
    static_assert(N == 1 + NP + PM);
    return newPhasorUGen<phasorRunAcross<WS, PM, NP, Prev>, phasorRun1<WS, PM, NP, Prev>, N, Prev>(sources);
}

S phasor(S fm);
S phasor(S fm, S pm);

// An oscillator of WS, a function object type such as
// decltype([](auto x){ return sin1(x); }), applied to the phase.
template <typename WS>
S waveshaperOsc(S fm) {
    Output* sources[1] = { fm.o };
    return newWaveshaperOsc<WS, false, 0, false>(sources);
}

template <typename WS>
S waveshaperOsc(S fm, S pm) {
    Output* sources[2] = { fm.o, pm.o };
    return newWaveshaperOsc<WS, true, 0, false>(sources);
}

// An oscillator of WS applied to the phase and a parameter w, as ws(x, w), or
// to the phase, the previous sample's phase and w, as ws(x, x1, w), if WS
// takes 3 arguments.
template <typename WS>
S paramWaveshaperOsc(S fm, S w) {
    Output* sources[2] = { fm.o, w.o };
    return newWaveshaperOsc<WS, false, 1, std::is_invocable_v<WS&, Z, Z, Z>>(sources);
}

template <typename WS>
S paramWaveshaperOsc(S fm, S w, S pm) {
    Output* sources[3] = { fm.o, w.o, pm.o };
    return newWaveshaperOsc<WS, true, 1, std::is_invocable_v<WS&, Z, Z, Z>>(sources);
}

// Oscillators of the waveshapers of math_funs.hpp, each with or without pm:
//  sinosc     sin1:    sine in [-1, 1].
//  usinosc    usin:    sine in [0, 1].
//  fsinosc    fsin:    a fast approximation of a sine, from two parabolas.
//  fsinxosc   fsinx:   a closer fast approximation.
//  lftri      par:     (2 x - 1)^2, parabolas in [0, 1].
//  lfizigzag  izigzag: and lfozigzag, ozigzag: Rubicon-style zigzags.
S sinosc(S fm);
S sinosc(S fm, S pm);
S usinosc(S fm);
S usinosc(S fm, S pm);
S fsinosc(S fm);
S fsinosc(S fm, S pm);
S fsinxosc(S fm);
S fsinxosc(S fm, S pm);
S lftri(S fm);
S lftri(S fm, S pm);
S lfizigzag(S fm);
S lfizigzag(S fm, S pm);
S lfozigzag(S fm);
S lfozigzag(S fm, S pm);

// Oscillators with a width w in (0, 1), each with or without pm:
//  lfvarsaw   varsaw:  from 1 down to -1 over the first w of a cycle and back up
//                      over the rest: a triangle at w = .5, a saw near 0 or 1.
//  lfvartri   vartri:  varsaw scaled into [0, 1].
//  lfupulse   upulse:  1 for the first w of each cycle, else 0.
//  lfbpulse   bpulse:  1 for the first w of each cycle, else -1.
//  lfzpulse   zpulse:  1 - w for the first w of each cycle, else -w: mean 0.
//  lfupulse1, lfbpulse1, lfzpulse1: the pulses, but each cycle has a sample
//      of each level however narrow the pulse. If w < .5, the first sample of
//      each cycle is high, so the first sample of all is too. If not, the last
//      sample of each cycle is low, predicted from the last phase increment:
//      exact for a constant fm, but with a changing fm the low sample can be
//      missed, or come a sample early.
// (varsaw and vartri, as ops of a phase and a width, are ops.def's.)
S lfvarsaw(S fm, S w);
S lfvarsaw(S fm, S w, S pm);
S lfvartri(S fm, S w);
S lfvartri(S fm, S w, S pm);
S lfupulse(S fm, S w);
S lfupulse(S fm, S w, S pm);
S lfbpulse(S fm, S w);
S lfbpulse(S fm, S w, S pm);
S lfzpulse(S fm, S w);
S lfzpulse(S fm, S w, S pm);
S lfupulse1(S fm, S w);
S lfupulse1(S fm, S w, S pm);
S lfbpulse1(S fm, S w);
S lfbpulse1(S fm, S w, S pm);
S lfzpulse1(S fm, S w);
S lfzpulse1(S fm, S w, S pm);

#endif /* phasor_h */
