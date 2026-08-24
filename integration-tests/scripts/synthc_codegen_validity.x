-- Codegen validity regressions: graph shapes where BOTH compilers once
-- emitted the SAME invalid C++ (so the byte-diff suites passed while the
-- defs were uncompilable). Each shape is checked two ways: byte parity
-- between the compilers, and an actual clang compile + load on both the
-- C++ path (defSynthChecked) and the synthc path (defSynthXChecked).
--
-- Shapes covered:
--  * simd mixed-width binop: the rewriter's reciprocal constant (x / fs()
--    -> x * (1/fs())) is created f32 against f64 operands after inference,
--    and the SIMD init/reset loop emitted (f32x4)(1.0f) / f64x4. Fixed by
--    widening each binop operand to the common type under SIMD.
--  * join with a scalar-constant input: [sig, 0.25 asSignal] join
--    referenced the constant by its var name (p->cN), which is never
--    declared for scalar constants. Fixed by emitting the literal in the
--    put/join emitters.

import synthdef.*;
import common_ugens.*;
import std.result.*;
import audio_engine.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.compile.*;
import synthc.difftest.*;

let fails = &0;

-- samplebank-style voicer: an f64 rate divided by (fs() f64) at init/reset
-- rate; 4 voices so the per-voice init/reset loops run at SIMD width 4.
fn fsdivVoicer() S {
	voicer(4, fn() {
		let g = gate();
		let pitch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
		let rate = ((pitch f64 - 60.0) / 12.0) exp2 * 44100.0 / (fs() f64);
		let d = delayVar();
		let pos = d read(1);
		(pos + rate) -> d;
		(pos * 0.0001) f32 sin * g * 0.2
	}) sum outlet
}

-- join with a scalar constant channel (and a scalar-constant put index/value).
fn joinConst() S {
	let sig = inlet(FLOAT32, 1) sin;
	[sig, 0.25 asSignal] join |> outlet
}
fn putConst() S {
	let sig = [220.0, 330.0] lfsaw;
	sig put(1 asSignal, 0.5 asSignal) |> outlet
}

fn checkParity(name String, synthFn GraphFn, rewrites Bool, w Int) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefGenCppFromSexpr(g toSynthSexpr(name), w, rewrites);
	let ctx = g importGraph(name, rewrites) analyzeM1;
	if (ctx.errors length > 0) {
		fails <- *fails + 1;
		println("  FAIL " $ name $ " (synthc errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = fullCompare(genCpp(ctx, name, w), theirs);
	if (v == "PASS") { println("  PASS parity " $ name); }
	else { fails <- *fails + 1; println("  FAIL parity " $ name); println(v); }
}

async fn checkCompiles(name String, synthFn GraphFn) Void {
	match (synthFn defSynthChecked(name $ "_cpp") await) {
		ok(_):    println("  PASS compile (C++)    " $ name);
		err(msg): { fails <- *fails + 1; println("  FAIL compile (C++)    " $ name $ ": " $ msg); }
	}
	match (synthFn defSynthXChecked(name $ "_x") await) {
		ok(_):    println("  PASS compile (synthc) " $ name);
		err(msg): { fails <- *fails + 1; println("  FAIL compile (synthc) " $ name $ ": " $ msg); }
	}
}

checkParity("fsdivVoicer", fsdivVoicer, true, 4);
checkParity("fsdivVoicer_norw", fsdivVoicer, false, 4);
checkParity("joinConst", joinConst, true, 4);
checkParity("joinConst_norw", joinConst, false, 0);
checkParity("putConst", putConst, true, 4);
checkParity("putConst_norw", putConst, false, 0);
checkCompiles("fsdivVoicer", fsdivVoicer) await;
checkCompiles("joinConst", joinConst) await;
checkCompiles("putConst", putConst) await;
println(*fails == 0 ? "CODEGEN VALIDITY PASS" : "CODEGEN VALIDITY FAIL: %^" fmt(*fails));
