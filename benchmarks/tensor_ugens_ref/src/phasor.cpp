//
//  phasor.cpp
//  tensor-ugens
//

#include "phasor.hpp"

void Phasor_run(UGen* u, int n) { phasorRunAcross<Identity, false>(u, n); }
void PhasorPM_run(UGen* u, int n) { phasorRunAcross<Identity, true>(u, n); }
void Phasor_run1(UGen* u) { phasorRun1<Identity, false>(u); }
void PhasorPM_run1(UGen* u) { phasorRun1<Identity, true>(u); }

S phasor(S fm) {
    Output* sources[1] = { fm.o };
    return newPhasorUGen<Phasor_run, Phasor_run1>(sources);
}

S phasor(S fm, S pm) {
    Output* sources[2] = { fm.o, pm.o };
    return newPhasorUGen<PhasorPM_run, PhasorPM_run1>(sources);
}

#define OSC(NAME, WS) \
    S NAME(S fm) { return waveshaperOsc<decltype([](auto x) { return WS(x); })>(fm); } \
    S NAME(S fm, S pm) { return waveshaperOsc<decltype([](auto x) { return WS(x); })>(fm, pm); }

OSC(sinosc, sin1)
OSC(usinosc, usin)
OSC(fsinosc, fsin)
OSC(fsinxosc, fsinx)
OSC(lftri, par)
OSC(lfizigzag, izigzag)
OSC(lfozigzag, ozigzag)

#define PARAM_OSC(NAME, WS) \
    S NAME(S fm, S w) { return paramWaveshaperOsc<decltype([](auto x, auto w) { return WS(x, w); })>(fm, w); } \
    S NAME(S fm, S w, S pm) { return paramWaveshaperOsc<decltype([](auto x, auto w) { return WS(x, w); })>(fm, w, pm); }

PARAM_OSC(lfvarsaw, varsaw)
PARAM_OSC(lfvartri, vartri)
PARAM_OSC(lfupulse, upulse)
PARAM_OSC(lfbpulse, bpulse)
PARAM_OSC(lfzpulse, zpulse)

#define PREV_OSC(NAME, WS) \
    S NAME(S fm, S w) { return paramWaveshaperOsc<decltype([](auto x, auto x1, auto w) { return WS(x, x1, w); })>(fm, w); } \
    S NAME(S fm, S w, S pm) { return paramWaveshaperOsc<decltype([](auto x, auto x1, auto w) { return WS(x, x1, w); })>(fm, w, pm); }

PREV_OSC(lfupulse1, upulse1)
PREV_OSC(lfbpulse1, bpulse1)
PREV_OSC(lfzpulse1, zpulse1)
