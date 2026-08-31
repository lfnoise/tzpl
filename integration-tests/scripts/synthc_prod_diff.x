-- synthc production-config differential test (M5.5 switchover).
--
-- The switchover makes synthc (defSynthX) the production compiler. Its production
-- config is rewrites-ON + SIMD width 4 -- the SAME settings the C++ compiler uses by
-- default. This test asserts synthc's production output byte-matches the C++ compiler
-- at THAT config (synthdefGenCppFromSexpr(sexpr, 4, true)) across the real instrument
-- corpus + a voicer/spectral/delay/buffer mix. (The other SIMD diff suites compare
-- rewrites-OFF; this is the only one at the true production config -- rewrites + SIMD
-- together -- which is what defSynthX actually emits.)

import synthdef.*;
import common_ugens.*;
import filters.*;
import instrument_synthdefs.*;
import instruments.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkProd(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name);
	-- production config: rewrites ON, SIMD width 4 (matches defSynthX + the C++ default).
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

-- the real production instrument corpus (the synths defSynthX now compiles).
checkProd("organ", organ);
checkProd("fmBell", fmBell);
checkProd("fmBrass", fmBrass);
checkProd("sawLead", sawLead);
checkProd("pwmPad", pwmPad);
checkProd("pluck", pluck);
checkProd("modalBell", modalBell);
checkProd("kick", kick);
checkProd("snare", snare);
checkProd("subBass", subBass);

-- the instruments.x library defs (voicers whose event-rate expressions mix
-- node-level controls with per-voice values -- wtLead's control-driven adsr
-- coefficients caught two C++-side bugs the corpus above cannot: the
-- flat-voice auto-enter for event loops and the voicer-vs-scalar struct
-- declaration order).
checkProd("lib_wtLead", wtLead());
checkProd("lib_ksPluck", ksPluck());
-- non-power-of-two voice count: the flat SIMD phi copy loop must index the
-- voice-expanded temp directly (a `& (voices-1)` wrap mask scrambled/skipped
-- voices for 12 -- both compilers agreed, so only a non-pow2 case covers it)
checkProd("lib_ksPluck12", ksPluck(12));
checkProd("lib_resonBank", resonBank());
checkProd("lib_smpPerc", smpPerc());
checkProd("lib_smpLoopTail", smpLoopTail());
checkProd("lib_smpLoopEnv", smpLoopEnv());

-- a non-voicer audio synth + a delay synth exercising rewrites + SIMD together.
fn arith() S { (inlet(FLOAT32, 4) * 0.5 + 0.5) sin * [0.1, 0.2, 0.3, 0.4] |> outlet }
fn echo() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(2, 0.0);
	let r = d read(2); d write(x + r * 0.5); (x + r) |> outlet
}
checkProd("arith", arith);
checkProd("echo", echo);

-- eventToAudio: the promoted trigger (audio-rate one-sample impulse via an
-- audio z1) beside the pure event-rate trigger (held; its materialized z1
-- read inherits the writer's sources -- the delay-read activation fix), plus
-- the constant passthrough (eventToAudio(1.0) folds to the constant).
fn e2aTrig() S {
	let c = control("trig", ControlSpec { lo: 0.0, hi: 10.0, init: 0.0, warp: ControlWarp.linear });
	let imp = (c eventToAudio) tr * eventToAudio(1.0);
	let held = (c tr) * (1.0 + 1.0e-30 * white());
	[imp, held] join outlet
}
checkProd("e2a_trig", e2aTrig);

-- category tags: the loadTags emission must byte-match between the two
-- compilers (tags flow to C++ via the (Tags ...) sexpr clause, to synthc via
-- the importGraph tags argument).
fn checkTagged(name String, synthFn GraphFn, tags [String]) Void {
	let g = makeGraph(synthFn);
	let sx = g toSynthSexpr(name, tags);
	let ctx = g importGraph(name, true, tags) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = voicerCppCompare(genCpp(ctx, name, 4), synthdefGenCppFromSexpr(sx, 4, true));
	if (v == "PASS") { println("  PASS " $ name $ " (tagged)"); }
	else { `failures = `failures + 1; println("  FAIL " $ name $ " (tagged)"); println(v); }
}
checkTagged("arith_tagged", arith, ["test", "demo"]);

if (`failures == 0) { println("M5 PROD DIFF PASS"); }
else { println("M5 PROD DIFF FAIL: " $ `failures toString); }
