-- synthc analysis differential test.
--
-- Runs the Tzopilotl-hosted analysis pipeline (synthc) and compares its dump
-- against the C++ compiler's dump (synthdefAnalysisDump, rewrites off) for the
-- same graph. The C++ dump is the ground truth.
--
-- What is checked per synth:
--   * SORTED EXPRS section -- exact match (index, serial, inputs, in/consumer
--     counts, rate, cut, type, chans, str). Validates shape inference, type
--     inference, default-type resolution, delay reader rates, and graph cuts.
--   * TREES section -- match with antecedent SETS normalized (the C++ dump
--     prints antecedents in hash-map order; synthc uses insertion order).
--   * For audio-only synths (no event-rate controls, so no iso-groups), the
--     FULL dump including INIT/EVENT/AUDIO loops matches exactly.
--
-- Iso-group-driven EVENT-loop grouping is deferred to M3, so control-bearing
-- synths are only checked through SORTED + TREES.

import synthdef.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn _expect(label String, verdict String) Void {
	if (verdict == "PASS") {
		println("  PASS " $ label);
	} else {
		`failures = `failures + 1;
		println("  FAIL " $ label);
		println(verdict);
	}
}

fn checkSortedAndTrees(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefAnalysisDump(g toSynthSexpr(name), false);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (import/analysis errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let mine = ctx dumpToString;
	_expect(name $ " SORTED", m0Compare(mine, theirs));
	_expect(name $ " TREES", treesCompare(mine, theirs));
}

fn checkFull(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefAnalysisDump(g toSynthSexpr(name), false);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (import/analysis errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let mine = ctx dumpToString;
	_expect(name $ " FULL", fullCompare(mine, theirs));
}

-- Codegen byte-match: the generated C++ must equal the C++ compiler's output
-- (rewrites off). Used for audio-only synths (no iso-groups).
fn checkCodegen(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefGenCppFromSexpr(g toSynthSexpr(name), 0, false);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " codegen (analysis errors)");
		return;
	}
	let mine = genCpp(ctx, name);
	_expect(name $ " CODEGEN", fullCompare(mine, theirs));
}

-- Control + delay synth: exercises controls (event rate, iso-groups),
-- hash-consing, ops, and a delay with init/read/write. SORTED + TREES + CODEGEN
-- checked (the full dump has benign hash-order antecedent differences; the
-- generated source byte-matches).
fn ctrlDelay() S {
	let freq = control("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
	let osc = (fs() / freq) sin;
	let vib = (fs() / freq) cos;
	let d = delayVar();
	d init(1, 0.0);
	let r = d read(1);
	let mixed = osc * 0.5 + vib * 0.5 + r;
	d write(mixed);
	mixed outlet
}
checkSortedAndTrees("ctrl_delay", ctrlDelay);
checkCodegen("ctrl_delay", ctrlDelay);

-- Two controls -> two iso-groups; exercises the activation machinery.
fn twoCtrl() S {
	let f = control("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
	let a = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	((fs() / f) sin * a) outlet
}
checkSortedAndTrees("two_ctrl", twoCtrl);
checkCodegen("two_ctrl", twoCtrl);

-- Variable (signal-rate) delay read with cubic interpolation: exercises the
-- runtime-sized delay (calloc/_mask/_wrpos via genDelayAlloc) and the
-- tzpl_delay_cubic kernel, with a control driving the delay time.
fn vdelayCtrl() S {
	let d = delayVar(0.05);
	d init(1, 0.0);
	let dt = control("dt", ControlSpec { lo: 0.0, hi: 0.05, init: 0.01, warp: ControlWarp.linear });
	let inp = (fs() / 440.0) sin;
	let r = d vread(dt, Interpolation.cubic);
	d write(inp * 0.5 + r * 0.9);
	r outlet
}
checkSortedAndTrees("vdelay_ctrl", vdelayCtrl);
checkCodegen("vdelay_ctrl", vdelayCtrl);

-- Audio-only integrator: no controls -> no iso-groups -> FULL dump matches.
fn integrator() S {
	let osc = (fs() / 440.0) sin;
	let d = delayVar();
	d init(1, 0.0);
	let r = d read(1);
	let mixed = osc * 0.5 + r;
	d write(mixed);
	mixed outlet
}
checkFull("integrator", integrator);

-- Audio-only, multichannel (2-channel constant broadcast). Avoids constant-
-- on-constant folding (synthc/fold.x is not yet implemented).
fn stereo() S {
	let b = (fs() / [110.0, 110.5]) sin;
	(b * 0.3) outlet
}
checkFull("stereo", stereo);

-- Constant folding: arithmetic on constant subexpressions, a cast on a
-- constant vector, and broadcast folding all match the C++ (which folds at
-- graph construction).
fn folded() S {
	let freq = 440.0 * 2.0;               -- -> 880.0
	let amp = [0.5, 0.25] f32;            -- cast-on-constant fold
	let osc = (fs() / freq) sin;
	(osc * amp) outlet
}
checkFull("folded", folded);

-- Codegen byte-match against the C++ generator (rewrites off) for the audio-
-- only synths above plus an all-init-rate one.
checkCodegen("integrator", integrator);
checkCodegen("stereo", stereo);
checkCodegen("folded", folded);
checkCodegen("initrate", fn() S { ((fs() / 440.0) sin * 0.2) outlet });

if (`failures == 0) {
	println("M1 PASS");
} else {
	println("M1 FAIL (" $ `failures toString $ " failures)");
}
