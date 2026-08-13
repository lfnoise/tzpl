-- synthc sample-bank differential test.
--
-- Byte-matches synthc's generated C++ against the C++ compiler (rewrites off)
-- for the sample bank family: the reset-rate lookup latch (noteOn + init
-- emission), fixed/var reads through the per-voice resolved buffer pointer,
-- the rootKey/sampleRate/length accessors, the per-lookup resolver, the
-- swapSampleBank function + exported symbol, and loadSampleBankDefs. Covers
-- a voicer sampler (flat voice mode, per-voice slots), a non-voicer bank
-- (scalar slots, init-time latch), and reset-rate per-note random (the
-- Rate.reset machinery without banks).

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkBank(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefGenCppFromSexpr(g toSynthSexpr(name), 0, false);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = fullCompare(genCpp(ctx, name), theirs);
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

let fullSpec = ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear };
let velSpec = ControlSpec { lo: 0.0, hi: 127.0, init: 100.0, warp: ControlWarp.linear };
let gateSpec = ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear };

-- voicer sampler: lookup from note params, vread at a rate derived from the
-- resolved sample's rootKey/sampleRate/length
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

-- voicer sampler with a fixed read and interpolation variants
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

-- non-voicer bank: constant lookup, latched once at init
fn bankTop() S {
	let bank = sampleBankVar();
	let h = bank lookup(60, 100);
	let idx = (0.2 lfsaw) f64 * (h length);
	h vread(idx, Interpolation.cubic) f32 outlet
}

-- reset-rate per-note random (the Rate.reset machinery without banks).
-- urand rather than frand: the C++ oracle's s-expr parser knows URand.
fn resetRand() S {
	voicer(8, fn() S {
		let gate = noteParam("gate", gateSpec);
		urand(1, Rate.reset) f32 * gate
	}) sum(1) outlet
}

-- Production config: rewrites ON + SIMD width 4 (what defSynthX emits).
-- Bank loops stay scalar (bank kinds disqualify SIMD on both sides), but the
-- surrounding voicer audio loops vectorize.
fn checkBankProd(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefGenCppFromSexpr(g toSynthSexpr(name), 4, true);
	let ctx = g importGraph(name, true) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = fullCompare(genCpp(ctx, name, 4), theirs);
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

checkBank("bankVoicer", bankVoicer);
checkBank("bankVoicer2", bankVoicer2);
checkBank("bankTop", bankTop);
checkBank("resetRand", resetRand);
checkBankProd("bankVoicerProd", bankVoicer);
checkBankProd("bankVoicer2Prod", bankVoicer2);
checkBankProd("bankTopProd", bankTop);
checkBankProd("resetRandProd", resetRand);

if (`failures == 0) {
	println("BANK DIFF PASS");
} else {
	println("BANK DIFF FAIL: " $ `failures toString);
}
