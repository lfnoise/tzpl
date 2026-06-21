-- synthc SIMD differential test (M5.1).
--
-- Compares synthc's generated C++ at SIMD width 4 against the C++ compiler
-- (synthdefGenCppFromSexpr(sexpr, 4, false)). Exercises the SIMD scaffolding:
-- the width decision, Case A (chans == width, no loop) and Case B (stride loop),
-- and the simple vector forms -- inlet load/splat, constant load/splat, sampleRate
-- splat, unary/binary ops (infix + function form), and the outlet store.
--
-- Audio-rate sources are inlets (the only delay-free audio source); delays/voicers/
-- spectral SIMD land in M5.2-M5.3.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkSimd(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = fullCompare(genCpp(ctx, name, 4), synthdefGenCppFromSexpr(sx, 4, false));
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

-- Case A: chans == width 4 -> a single vector op, no loop.
fn s4() S { (inlet(FLOAT32, 4) sin) * [0.1, 0.2, 0.3, 0.4] |> outlet }
-- Case B: 8 channels -> stride loop (i += 4); scalar const -> splat.
fn s8() S { (inlet(FLOAT32, 8) sin) * 0.1 |> outlet }
-- sampleRate splat into a vector op.
fn srate() S { (inlet(FLOAT32, 4) * fs()) sin |> outlet }
-- function-form binop (max) + a constant-vector multiply.
fn mx() S { (inlet(FLOAT32, 4) max(0.0)) * [0.5, 0.6, 0.7, 0.8] |> outlet }
-- two inlets + a unary (tanh) at 8 channels (Case B).
fn add2() S { inlet(FLOAT32, 8) + (inlet(FLOAT32, 8) tanh) |> outlet }
-- constant-vector multiply then add (load forms).
fn cst() S { (inlet(FLOAT32, 4) * [1.0, 2.0, 3.0, 4.0]) + [10.0, 20.0, 30.0, 40.0] |> outlet }
-- SIMD casts: convert<f64>/convert<f32> around a vector value.
fn cast4() S { ((inlet(FLOAT32, 4) f64) sin) f32 |> outlet }
-- event-rate control splatted into a vector op.
fn ctl() S {
	let a = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	(inlet(FLOAT32, 4) * a) |> outlet
}
-- mono inlet broadcast (chans 1 -> splat of the bare accessor) into a 4ch op.
fn mono4() S { (inlet(FLOAT32, 1) + [0.1, 0.2, 0.3, 0.4]) |> outlet }

-- M5.2: 1-sample (z1) delays vectorize as contiguous loads/stores of the delay
-- row (`*(f32x4*)(&p->dN[cel])`). Case A (4ch) + Case B (8ch).
fn d4() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(1, 0.0);
	let r = d read(1); d write(x * 0.5 + r); (x * 0.5 + r) |> outlet
}
fn d8() S {
	let x = inlet(FLOAT32, 8); let d = delayVar(); d init(1, 0.0);
	let r = d read(1); d write(x + r * 0.9); r |> outlet
}
-- Ring delays (allocSize > 1) gather per lane on read, scatter per lane on write.
-- r4: 4ch Case A; r8: 8ch Case B stride loop; rfb: ring with feedback in the value.
fn r4() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(2, 0.0);
	let r = d read(2); d write(x); r |> outlet
}
fn r8() S {
	let x = inlet(FLOAT32, 8); let d = delayVar(); d init(2, 0.0);
	let r = d read(2); d write(x); r |> outlet
}
fn rfb() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(3, 0.0);
	let r = d read(3); d write(x * 0.5 + r * 0.5); r |> outlet
}

-- Variable-rate (interpolating) reads. A vector offset gives per-lane integer delays
-- and tap gathers; a scalar offset (control, chans 1) caches one `_di` and casts frac.
-- Every kernel: none/linear/cubic/lagrange (tap counts 1/2/4/8) plus sinc (per-lane
-- coefficient table). `dt` is a shared scalar control offset.
let dtSpec = ControlSpec { lo: 0.0, hi: 1.0, init: 0.3, warp: ControlWarp.linear };
fn vc() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.cubic) |> outlet }
fn vl() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.linear) |> outlet }
fn vg() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.lagrange) |> outlet }
fn vs() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.sinc) |> outlet }
fn vn() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.none) |> outlet }
fn sc() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = control("dt", dtSpec); d vread(off, Interpolation.cubic) |> outlet }
fn ss() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = control("dt", dtSpec); d vread(off, Interpolation.sinc) |> outlet }
fn sn() S { let x = inlet(FLOAT32, 8); let d = delayVar(1.0); d write(x); let off = control("dt", dtSpec); d vread(off, Interpolation.none) |> outlet }

checkSimd("s4", s4);
checkSimd("s8", s8);
checkSimd("srate", srate);
checkSimd("mx", mx);
checkSimd("add2", add2);
checkSimd("cst", cst);
checkSimd("cast4", cast4);
checkSimd("ctl", ctl);
checkSimd("mono4", mono4);
checkSimd("d4", d4);
checkSimd("d8", d8);
checkSimd("r4", r4);
checkSimd("r8", r8);
checkSimd("rfb", rfb);
checkSimd("vc", vc);
checkSimd("vl", vl);
checkSimd("vg", vg);
checkSimd("vs", vs);
checkSimd("vn", vn);
checkSimd("sc", sc);
checkSimd("ss", ss);
checkSimd("sn", sn);

if (`failures == 0) {
	println("M5 SIMD DIFF PASS");
} else {
	println("M5 SIMD DIFF FAIL: " $ `failures toString);
}
