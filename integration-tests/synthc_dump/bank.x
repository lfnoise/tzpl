-- Graphs from the retired synthc_bank_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

let fullSpec = ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear };
let velSpec = ControlSpec { lo: 0.0, hi: 127.0, init: 100.0, warp: ControlWarp.linear };
let gateSpec = ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear };

fn bankVoicer() S {
	let bank = sampleBankVar();
	voicer(8, fn() S {
		let pitch = noteParam("pitch", fullSpec);
		let vel = noteParam("vel", velSpec);
		let gate = noteParam("gate", gateSpec);
		let h = bank lookup(pitch, vel);
		let idx = (pitch - h rootKey) f64 * (h sampleRate) * (h length);
		h vread(idx, Interpolation.cubic) f32 * gate
	}) sum(1) outlet
}
fn bankVoicer2() S {
	let bank = sampleBankVar();
	voicer(4, fn() S {
		let pitch = noteParam("pitch", fullSpec);
		let vel = noteParam("vel", velSpec);
		let gate = noteParam("gate", gateSpec);
		let h = bank lookup(pitch, vel);
		let a = h read(0) f32;
		let b = h vread((0.3 lfsaw) f64 * (h length), Interpolation.linear) f32;
		(a + b) * gate
	}) sum(1) outlet
}
fn bankLoop() S {
	let bank = sampleBankVar();
	voicer(8, fn() S {
		let pitch = noteParam("pitch", fullSpec);
		let vel = noteParam("vel", velSpec);
		let gate = noteParam("gate", gateSpec);
		let h = bank lookup(pitch, vel);
		let rate = ((pitch f64 - (h rootKey) f64) / 12.0) exp2 * (h sampleRate) * (T() f64);
		h vread(loopPhasor(h, rate, gate), Interpolation.cubic) f32 * gate
	}) sum(1) outlet
}
fn bankTop() S {
	let bank = sampleBankVar();
	let h = bank lookup(60, 100);
	let idx = (0.2 lfsaw) f64 * (h length);
	h vread(idx, Interpolation.cubic) f32 outlet
}
fn resetRand() S {
	voicer(8, fn() S {
		let gate = noteParam("gate", gateSpec);
		urand(1, Rate.reset) f32 * gate
	}) sum(1) outlet
}

dumpCorpus([
	("bankVoicer", bankVoicer),
	("bankVoicer2", bankVoicer2),
	("bankLoop", bankLoop),
	("bankTop", bankTop),
	("resetRand", resetRand),
	("bankVoicerProd", bankVoicer),
	("bankVoicer2Prod", bankVoicer2),
	("bankLoopProd", bankLoop),
	("bankTopProd", bankTop),
	("resetRandProd", resetRand),
]);
