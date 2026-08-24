-- synthc NON-FLAT (AoS) voicer differential test.
--
-- A voice body containing control flow (pause here) cannot be flattened, so
-- both compilers emit the AoS layout: a VoiceState struct, per-voice loops
-- with `vparams`/`vs` bindings, per-field noteOn resets that preserve
-- init-rate state, per-voice delay init values, and top-of-init RNG seeding
-- (after the voice_state wipe). The generated C++ must byte-match at the
-- production config (rewrites ON, SIMD 4 -- SIMD applies to the shared
-- top-graph loops; the AoS voice loops are scalar).
--
-- The corpus covers: init-rate scalar and multichannel per-voice draws,
-- constant delay inits (seq's init(1, -1)) and non-constant ones (pink's
-- dice), control-driven per-voice event coefficients, and the plain baseline.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkNonFlat(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	let ctx = g importGraph(name, true) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = voicerCppCompare(genCpp(ctx, name, 4), synthdefGenCppFromSexpr(sx, 4, true));
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

fn nfA() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let a = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
		let ph = urand(1, Rate.init);
		let g = gate();
		g pause(fn() S { sinosc(f, ph) * a * 0.1 })
	}) sum(2) outlet
}

fn nfB() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let det = exprand(0.99, 1.01, 2, Rate.init);
		let g = gate();
		g pause(fn() S { (f * det) sinosc sum * 0.1 })
	}) sum(2) outlet
}

fn nfC() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 256.0, warp: ControlWarp.linear });
		let pattern = [1.0, 1.25, 1.5, 2.0];
		let g = gate();
		g pause(fn() S { (2 lfimp seq(pattern * f, 4)) sinosc * 0.1 })
	}) sum(2) outlet
}

fn nfD() S {
	voicer(4, fn() S {
		let g = gate();
		g pause(fn() S { pink() * 0.1 })
	}) sum(2) outlet
}

fn nfE() S {
	voicer(4, fn() S {
		let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
		let dcy = control("decay", ControlSpec { lo: 0.1, hi: 8.0, init: 2.0, warp: ControlWarp.exponential });
		let g = gate();
		let env = g adsr(0.01, 0.05, 0.6, dcy);
		g pause(fn() S { sinosc(f) * 0.1 }) * env
	}) sum(2) outlet
}

fn nfF() S {
	voicer(4, fn() S {
		let g = gate();
		g pause(fn() S { white() * 0.1 })
	}) sum(2) outlet
}

checkNonFlat("nfA", nfA);
checkNonFlat("nfB", nfB);
checkNonFlat("nfC", nfC);
checkNonFlat("nfD", nfD);
checkNonFlat("nfE", nfE);
checkNonFlat("nfF", nfF);

if (`failures == 0) { println("NONFLAT DIFF PASS"); }
else { println("NONFLAT DIFF FAIL (%^)" fmt(`failures)); }
