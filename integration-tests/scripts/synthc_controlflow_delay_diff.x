-- synthc control-flow + subgraph-local-delay differential test (M3 tail).
--
-- The pull* examples are the real-world stress for control flow: nested `pause`
-- (no-else if_) branches that each contain a `combn` (a runtime-allocated comb
-- delay), multichannel signals inside the branches, and RNG (`dust`) inside a
-- branch. They exercise subgraph-local delays, per-graph hash-consing, per-graph
-- delay-advance, and per-graph RNG state.
--
-- These run in their OWN process (just two synths) on purpose: compiling them
-- back-to-back with the ~8 synths in synthc_controlflow_diff.x trips a latent
-- lang-VM GC premature-free -- an array still live in a register during the deep
-- control-flow codegen recursion is missed by a stack map, freed, and its reused
-- memory later faults in builtin_push_array. Each synth byte-matches the C++
-- compiler standalone; the crash is purely a GC-timing artifact of the heavier
-- combined run, tracked separately from this milestone.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkCF(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let dumpV = cfDumpCompare(ctx dumpToString, synthdefAnalysisDump(g toSynthSexpr(name), false));
	let cppV = fullCompare(genCpp(ctx, name), synthdefGenCppFromSexpr(g toSynthSexpr(name), 0, false));
	if (dumpV == "PASS" && cppV == "PASS") { println("  PASS " $ name); }
	else {
		`failures = `failures + 1;
		println("  FAIL " $ name);
		if (dumpV != "PASS") { println("  [dump] " $ dumpV); }
		if (cppV != "PASS") { println("  [cpp] " $ cppV); }
	}
}

-- nested pauses, comb delay + multichannel in the inner/outer branches
fn pull_nested() S {
	let gate1 = 0.3 fsinosc max0;
	let out = gate1 pause(fn(){
		let gate2 = 2 fsinosc max0;
		let out = gate2 pause(fn(){
			let freq = nnhz(81 + 24 * 0.4 lfsaw + 3 * [8, 7.23] lfsaw);
			0.04 * freq fsinxosc
		});
		out fadein(0.1) combn(0.2, 4)
	});
	out outlet
}
-- two parallel pauses; RNG (dust) in one branch, comb + multichannel in the other
fn pulltwo() S {
	let pullFun1 = fn(){ let amp = 0.2 * dust(4, 2) decay2(0.04, 0.3); amp * 800 fsinxosc };
	let pullFun2 = fn(){ let freq = nnhz(81 + 24 * 0.4 lfsaw + 3 * [8, 7.23] lfsaw); 0.04 * fsinxosc(freq) fadein(0.1) combn(0.2, 4) };
	let gate1 = 2.71828 fsinosc max0;
	let gate2 = 3.14159 fsinosc max0;
	let out = gate1 pause(pullFun1) + gate2 pause(pullFun2);
	out outlet
}

checkCF("pull_nested", pull_nested);
checkCF("pulltwo", pulltwo);

if (`failures == 0) {
	println("M3 CONTROLFLOW DELAY DIFF PASS");
} else {
	println("M3 CONTROLFLOW DELAY DIFF FAIL: " $ `failures toString);
}
