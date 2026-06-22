-- synthc buffer + width-2 SIMD differential test (M5.4).
--
-- Validates the SIMD buffer codegen against the C++ compiler: BufFixRead (splat /
-- per-lane gather), BufVarRead (scalar-offset splat / per-lane gather wrapped in a
-- buffer-pointer-caching lambda, all five interp kernels), and BufLength (splat).
-- BufWrite stays SIMD-disqualified (the C++ BufWrite visitor has no SIMD path), so
-- a buffer-write loop stays correct-scalar. Each buffer synth runs at BOTH width 4
-- (4-channel reads) and width 2 (2-channel reads + the existing 1-2 chan corpus),
-- exercising the width-2 decision branch the width-4 oracle never reaches.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkBufSimd(name String, w Int, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " w" $ w toString $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = fullCompare(genCpp(ctx, name, w), synthdefGenCppFromSexpr(sx, w, false));
	if (v == "PASS") { println("  PASS " $ name $ " w" $ w toString); }
	else { `failures = `failures + 1; println("  FAIL " $ name $ " w" $ w toString); println(v); }
}

-- 4-channel reads (SIMD-eligible at width 4): fixed gather + every var-read kernel.
fn b4()  S = bufferVar() read(0, 4, 0) outlet;
fn bv4N() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.none, 4) outlet }
fn bv4L() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.linear, 4) outlet }
fn bv4C() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.cubic, 4) outlet }
fn bv4G() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.lagrange, 4) outlet }
fn bv4S() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.sinc, 4) outlet }
-- bufLength (f64 scalar) splatted into a 4-chan vector index.
fn bl4()  S { let b = bufferVar(); let idx = b length * [0.1, 0.2, 0.3, 0.4]; b vread(idx, Interpolation.none, 4) outlet }
-- a 1-channel fixed read (f64) splatted into a 4-chan vector op. (No constant
-- addend: a polymorphic float literal makes the C++ ORACLE nondeterministic here --
-- it resolves the literal's f32/f64 type in pointer-hash order -- while synthc is
-- deterministic. The bare read still exercises the BufFixRead splat path.)
fn b4s() S { let b = bufferVar(); (inlet(FLOAT32, 4) f64 * b read(0, 1, 0)) outlet }
-- 4-channel buffer write: the BufWrite sink stays the scalar-form store, but its
-- index/value operands are vectors in a SIMD loop (matches the C++, which has no
-- dedicated SIMD BufWrite path).
fn bw4() S { let b = bufferVar(); let sig = inlet(FLOAT32, 4) f64; let idx = (0.1 lfsaw) f64 * 100.0; b write(sig, idx, 0); sig f32 outlet }

-- 1-2 channel corpus at width 2: scalar-1chan splat + 2-channel gather.
fn frd()  S = bufferVar() read(0) * 0.5 |> outlet;
fn frd2() S = bufferVar() read(0, 2, 1) outlet;
fn vrd2() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.cubic, 2) outlet }
fn bwr()  S { let b = bufferVar(); let sig = (0.4 lfsaw) * 0.3; let idx = (0.1 lfsaw) * 100.0; b write(sig, idx); sig outlet }

checkBufSimd("b4", 4, b4);
checkBufSimd("bv4N", 4, bv4N);
checkBufSimd("bv4L", 4, bv4L);
checkBufSimd("bv4C", 4, bv4C);
checkBufSimd("bv4G", 4, bv4G);
checkBufSimd("bv4S", 4, bv4S);
checkBufSimd("bl4", 4, bl4);
checkBufSimd("b4s", 4, b4s);
checkBufSimd("bw4", 4, bw4);
checkBufSimd("frd", 2, frd);
checkBufSimd("frd2", 2, frd2);
checkBufSimd("vrd2", 2, vrd2);
checkBufSimd("bwr", 2, bwr);

if (`failures == 0) { println("M5 BUFFER SIMD DIFF PASS"); }
else { println("M5 BUFFER SIMD DIFF FAIL: " $ `failures toString); }
