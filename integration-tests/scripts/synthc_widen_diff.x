-- Widen-on-conflict differential test (issue #16).
--
-- Mixing f32 and f64 signals used to fail type inference in both compilers
-- ("intersected to empty"): an operation and its operands were unified to one
-- type. Now, when unification has no solution, both compilers find the
-- operands that must widen (f32 + f64 -> f64, floats win over ints), insert
-- explicit casts, and infer again. The casts are new nodes created during
-- analysis, so their serials, placement in the sort, and hash-consing must
-- match between the compilers. This asserts synthc's production output
-- (rewrites ON, SIMD 4) byte-matches the C++ compiler on graphs that take the
-- widening path, and that each one actually compiles under both.

import synthdef.*;
import common_ugens.*;
import instruments.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkWiden(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	let ctx = g importGraph(name, true) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (synthc errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let ref = synthdefGenCppFromSexpr(sx, 4, true);
	if (ref startsWith("error") || ref length == 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (C++ compiler): " $ ref);
		return;
	}
	let v = voicerCppCompare(genCpp(ctx, name, 4), ref);
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

-- c, d: f32 audio (sinosc ends in an f32 cast); y: f64 audio; k: f64 constant.
fn sig() (S, S, S, S) = (3 sinosc, 5 sinosc, 7 lfsaw f64, 1.5 f64);

fn wSubF32(f fn(S, S, S, S) S) GraphFn = fn() S {
	let (c, d, y, k) = sig();
	f(c, d, y, k) f32 * 0.1 |> outlet
};

checkWiden("w_y_minus_c",     wSubF32(fn(c S, d S, y S, k S) S { y - c }));
checkWiden("w_c_minus_y",     wSubF32(fn(c S, d S, y S, k S) S { c - y }));
checkWiden("w_c_times_y",     wSubF32(fn(c S, d S, y S, k S) S { c * y }));
checkWiden("w_y_minus_min",   wSubF32(fn(c S, d S, y S, k S) S { y - min(c, d) }));
checkWiden("w_min_times_y",   wSubF32(fn(c S, d S, y S, k S) S { min(c, d) * y }));
checkWiden("w_k_minus_c",     wSubF32(fn(c S, d S, y S, k S) S { k - c }));
checkWiden("w_c_plus_k",      wSubF32(fn(c S, d S, y S, k S) S { c + k }));
checkWiden("w_chain",         wSubF32(fn(c S, d S, y S, k S) S { y + min(c, d) * k }));
-- the same widened operand used by two operations: one shared cast
checkWiden("w_shared_cast",   wSubF32(fn(c S, d S, y S, k S) S { (y - c) * (y + c) }));
-- an operand already cast by the user: widening reuses that cast node
checkWiden("w_reuse_cast",    wSubF32(fn(c S, d S, y S, k S) S { (y - c) + (c f64) * y }));
-- compare operands widen; the compare's result feeds a select selector
checkWiden("w_compare",       wSubF32(fn(c S, d S, y S, k S) S { select2(c > y, y, c) }));
-- select branches widen
checkWiden("w_select",        wSubF32(fn(c S, d S, y S, k S) S { select2(d > 0.0, y, c) }));

-- feedback through a delay: the f64 accumulator meets an f32 rate
fn wDelay() S {
	let rate = 2 sinosc * 0.001;
	let d = delayVar();
	let pos = d read(1);
	(pos f64 + rate) frac -> d;
	pos f32 * 0.1 |> outlet
}
checkWiden("w_delay", wDelay);

-- issue #10: loop points from f32 controls against the f64 playhead
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
	}) sum(2) outlet
}
checkWiden("w_issue10_neoSmpl", neoSmpl);

if (`failures == 0) { println("SYNTHC WIDEN DIFF ALL PASS"); }
else { println("SYNTHC WIDEN DIFF FAILURES: %^" fmt(`failures)); }
