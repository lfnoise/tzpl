-- Graphs from the retired synthc_nonflat_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import instruments.*;

fn nfA() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let a = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let ph = urand(1, Rate.init);
		let g = gate();
		g pause(fn() S { sinosc(f, ph) * a * 0.1 })
	}) reshape([-1, 2]) sum(0) outlet
}
fn nfB() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let det = exprand(0.99, 1.01, 2, Rate.init);
		let g = gate();
		g pause(fn() S { (f * det) sinosc sum * 0.1 })
	}) reshape([-1, 2]) sum(0) outlet
}
fn nfC() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 256.0, warp: ControlWarp.linear });
		let pattern = [1.0, 1.25, 1.5, 2.0];
		let g = gate();
		g pause(fn() S { (2 lfimp seq(pattern * f, 4)) sinosc * 0.1 })
	}) reshape([-1, 2]) sum(0) outlet
}
fn nfD() S {
	voicer(4, fn() S {
		let g = gate();
		g pause(fn() S { pink() * 0.1 })
	}) reshape([-1, 2]) sum(0) outlet
}
fn nfE() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let dcy = control("decay", ControlSpec { lo: 0.1, hi: 8.0, init: 2.0, warp: ControlWarp.exponential });
		let g = gate();
		let env = g adsr(0.01, 0.05, 0.6, dcy);
		g pause(fn() S { sinosc(f) * 0.1 }) * env
	}) reshape([-1, 2]) sum(0) outlet
}
fn nfF() S {
	voicer(4, fn() S {
		let g = gate();
		g pause(fn() S { white() * 0.1 })
	}) reshape([-1, 2]) sum(0) outlet
}
fn nfBank() S {
	let bank = sampleBankVar();
	voicer(4, fn() S {
		let pitch = notePitch();
		let vel = noteVel();
		let g = gate();
		let h = bank lookup(pitch, vel);
		g pause(fn() S {
			let pos = h loopPhasor(bankRate(h, pitch), 0.0);
			h bankRead(pos) * vel velAmp
		})
	}) reshape([-1, 2]) sum(0) outlet
}
fn evRingFlat() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let c = control("c", ControlSpec { lo: 0.5, hi: 2.0, init: 1.0, warp: ControlWarp.linear });
		let d = delayVar();
		d init(1, 0.0);
		let r1 = d read(1);
		let r2 = d read(2);
		let acc = (f * c) * 0.001 + r1 * 0.4 + r2 * 0.2;
		d write(acc);
		sinosc(440.0 + acc) * gate() * 0.1
	}) reshape([-1, 2]) sum(0) outlet
}
fn evRingAoS() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let c = control("c", ControlSpec { lo: 0.5, hi: 2.0, init: 1.0, warp: ControlWarp.linear });
		let g = gate();
		let d = delayVar();
		d init(1, 0.0);
		let r1 = d read(1);
		let r2 = d read(2);
		let acc = (f * c) * 0.001 + r1 * 0.4 + r2 * 0.2;
		d write(acc);
		g pause(fn() S { sinosc(440.0 + acc) * 0.1 })
	}) reshape([-1, 2]) sum(0) outlet
}

dumpCorpus([
	("nfA", nfA),
	("nfB", nfB),
	("nfC", nfC),
	("nfD", nfD),
	("nfE", nfE),
	("nfF", nfF),
	("nfBank", nfBank),
	("evRingFlat", evRingFlat),
	("evRingAoS", evRingAoS),
]);
