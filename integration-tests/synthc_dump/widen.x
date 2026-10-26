-- Graphs from the retired synthc_widen_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import instruments.*;

fn sig() (S, S, S, S) = (3 sinosc, 5 sinosc, 7 lfsaw f64, 1.5 f64);
fn wSubF32(f fn(S, S, S, S) S) GraphFn = fn() S {
	let (c, d, y, k) = sig();
	f(c, d, y, k) f32 * 0.1 |> outlet
};
fn wDelay() S {
	let rate = 2 sinosc * 0.001;
	let d = delayVar();
	let pos = d read(1);
	(pos f64 + rate) frac -> d;
	pos f32 * 0.1 |> outlet
}
fn loopPhasorX(len S, rate S, sustain S, lpa S, lpb S) S {
	let d = delayVar();
	let pos = d read(1);
	let next = pos + rate;
	let a = (min(lpa, lpb) * len) round;
	let b = max(lpa, lpb) * len;
	let wrap = (sustain > 0.0) * (next >= b);
	select2(wrap, next - (b - a), next) -> d;
	pos
}
fn neoSmpl() S {
	let bank = sampleBankVar();
	voicer(4, fn() S {
		let pitch = notePitch();
		let vel = noteVel();
		let lpa = control("lpa", ControlSpec { 0.0, 1.0, 0.0, ControlWarp.linear });
		let lpb = control("lpb", ControlSpec { 0.0, 1.0, 1.0, ControlWarp.linear });
		let h = bank lookup(pitch, vel);
		let pos = loopPhasorX(h length, bankRate(h, pitch), gate(), lpa, lpb);
		h bankRead(pos) * vel velAmp
	}) sum(0) outlet
}

dumpCorpus([
	("w_y_minus_c", wSubF32(fn(c S, d S, y S, k S) S { y - c })),
	("w_c_minus_y", wSubF32(fn(c S, d S, y S, k S) S { c - y })),
	("w_c_times_y", wSubF32(fn(c S, d S, y S, k S) S { c * y })),
	("w_y_minus_min", wSubF32(fn(c S, d S, y S, k S) S { y - min(c, d) })),
	("w_min_times_y", wSubF32(fn(c S, d S, y S, k S) S { min(c, d) * y })),
	("w_k_minus_c", wSubF32(fn(c S, d S, y S, k S) S { k - c })),
	("w_c_plus_k", wSubF32(fn(c S, d S, y S, k S) S { c + k })),
	("w_chain", wSubF32(fn(c S, d S, y S, k S) S { y + min(c, d) * k })),
	("w_shared_cast", wSubF32(fn(c S, d S, y S, k S) S { (y - c) * (y + c) })),
	("w_reuse_cast", wSubF32(fn(c S, d S, y S, k S) S { (y - c) + (c f64) * y })),
	("w_compare", wSubF32(fn(c S, d S, y S, k S) S { select2(c > y, y, c) })),
	("w_select", wSubF32(fn(c S, d S, y S, k S) S { select2(d > 0.0, y, c) })),
	("w_delay", wDelay),
	("w_issue10_neoSmpl", neoSmpl),
]);
