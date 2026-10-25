//
//  filter.cpp
//  tensor-ugens
//

#include "filter.hpp"

template <typename F, typename... Ss>
static S filter(S in, Ss... params) {
	static_assert(sizeof...(Ss) == F::kNumParams);
	Output* sources[1 + F::kNumParams] = { in.o, params.o... };
	return newFilter<F>(sources);
}

S lpf1(S in, S freq) { return filter<LPF1>(in, freq); }
S hpf1(S in, S freq) { return filter<HPF1>(in, freq); }
S apf1(S in, S freq) { return filter<APF1>(in, freq); }
S lsf1(S in, S freq, S db) { return filter<LSF1>(in, freq, db); }
S hsf1(S in, S freq, S db) { return filter<HSF1>(in, freq, db); }

S lpf(S in, S freq, S q) { return filter<LPF>(in, freq, q); }
S hpf(S in, S freq, S q) { return filter<HPF>(in, freq, q); }
S bpf(S in, S freq, S q) { return filter<BPF>(in, freq, q); }
S brf(S in, S freq, S q) { return filter<BRF>(in, freq, q); }
S apf(S in, S freq, S q) { return filter<APF>(in, freq, q); }
S peq(S in, S freq, S q, S db) { return filter<PEQ>(in, freq, q, db); }
S lsf(S in, S freq, S q, S db) { return filter<LSF>(in, freq, q, db); }
S hsf(S in, S freq, S q, S db) { return filter<HSF>(in, freq, q, db); }

S lag(S in, S lagTime) { return filter<Lag<1>>(in, lagTime); }
S lag2(S in, S lagTime) { return filter<Lag<2>>(in, lagTime); }
S lag3(S in, S lagTime) { return filter<Lag<3>>(in, lagTime); }
S lagud(S in, S riseTime, S fallTime) { return filter<LagUD<1>>(in, riseTime, fallTime); }
S lagud2(S in, S riseTime, S fallTime) { return filter<LagUD<2>>(in, riseTime, fallTime); }
S lagud3(S in, S riseTime, S fallTime) { return filter<LagUD<3>>(in, riseTime, fallTime); }
S decay(S in, S decayTime) { return filter<Decay>(in, decayTime); }
S decay2(S in, S attackTime, S decayTime) { return filter<Decay2>(in, attackTime, decayTime); }
S leakdc(S in, S leak) { return filter<LeakDC>(in, leak); }
S leaky(S in, S leak) { return filter<Leaky>(in, leak); }
S resonz(S in, S freq, S rq) { return filter<Resonz>(in, freq, rq); }
S ringz(S in, S freq, S ringTime) { return filter<Ringz>(in, freq, ringTime); }
S formlet(S in, S freq, S attackTime, S decayTime) { return filter<Formlet>(in, freq, attackTime, decayTime); }
