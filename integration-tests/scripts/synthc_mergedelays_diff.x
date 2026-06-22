-- synthc mergeDelays differential test.
--
-- synthc does Faust-style delay-line sharing: it keys delay allocation on the
-- WRITTEN SIGNAL (+ graph + init + max-delay bound), so two delays fed the same
-- hash-consed signal with the same init collapse to one ring buffer with multiple
-- read taps (sized to the max offset). This diverges from the C++ compiler, which
-- keys the merge on the buffer object and so never merges (a no-op).
--
-- This test pins both halves:
--  * NON-merge cases (distinct signals, distinct inits, feedback) must still
--    byte-match the C++ generator -- the share must NOT over-fire.
--  * MERGE cases must actually collapse in synthc (fewer delay buffers). Their
--    behavioural equivalence to the C++ two-buffer build is checked by the
--    Tier-3 render A/B (run_synthc_render_ab.sh, the `merge` case): a merged synth
--    renders bit-identically to its unmerged C++ build.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

-- A synth whose delays must NOT merge: synthc's output still byte-matches the C++.
fn checkNoMerge(name String, synthFn GraphFn) Void {
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
	if (v == "PASS") { println("  PASS (no-merge) " $ name); }
	else { `failures = `failures + 1; println("  FAIL (no-merge) " $ name); println(v); }
}

-- A synth whose delays SHOULD merge: assert synthc collapses to `expectDelays`
-- buffers (fewer than the naive count). Behaviour is checked by the render A/B.
fn checkMerge(name String, expectDelays Int, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	if (ctx.delays length == expectDelays) { println("  PASS (merge) " $ name $ " -> " $ expectDelays toString $ " delays"); }
	else {
		`failures = `failures + 1;
		println("  FAIL (merge) " $ name $ ": expected " $ expectDelays toString $ " delays, got " $ ctx.delays length toString);
	}
}

-- == NON-merge: must stay separate AND byte-match the C++ ==

-- Single feedback delay read at two fixed offsets from one buffer.
fn fb2() S {
	let drive = (fs() / 330.0) sin * 0.001;
	let y = delayVar(); y init(1, 0.0);
	let o = y read(1) * 0.7 + y read(2) * 0.2 + drive;
	y write(o); o outlet
}
-- Two delays fed DIFFERENT signals: distinct written-signal nodes, no merge.
fn diffsig() S {
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.0);
	a write((0.5 lfsaw) * 0.3);
	b write((0.5 lfsaw) * 0.4);   -- different gain -> different node
	(a read(1) + b read(1)) outlet
}
-- Same signal but DIFFERENT init values: different initial content, no merge.
fn diffinit() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.5);
	a write(sig); b write(sig);
	(a read(1) + b read(1)) outlet
}
-- Two feedback delays: each writes a value referencing its own read -> distinct, no merge.
fn fbtwo() S {
	let x = (fs() / 200.0) sin * 0.001;
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.0);
	a write(a read(1) * 0.5 + x);
	b write(b read(1) * 0.9 + x);
	(a read(1) + b read(1)) outlet
}

-- == MERGE: must collapse in synthc ==

-- Two delays, same signal + init, same read offset: one buffer, taps dedup. The
-- `mergesyn` render A/B (run_synthc_render_ab.sh) checks this collapses correctly.
fn twodel() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.0);
	a write(sig); b write(sig);
	(a read(1) + b read(1)) outlet
}
-- Same signal + init, DIFFERENT offsets: one ring buffer, two taps, sized to max.
fn mtap() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(4, 0.0);
	let b = delayVar(); b init(4, 0.0);
	a write(sig); b write(sig);
	(a read(1) + b read(4)) outlet
}
-- Three identical delays collapse to one (+ the lfsaw phasor = 2 buffers total).
fn three() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(2, 0.0);
	let b = delayVar(); b init(2, 0.0);
	let c = delayVar(); c init(2, 0.0);
	a write(sig); b write(sig); c write(sig);
	(a read(1) + b read(2) + c read(1)) outlet
}

checkNoMerge("fb2", fb2);
checkNoMerge("diffsig", diffsig);
checkNoMerge("diffinit", diffinit);
checkNoMerge("fbtwo", fbtwo);
-- the merged counts include the lfsaw's internal 1-sample phasor delay.
checkMerge("twodel", 2, twodel);
checkMerge("mtap", 2, mtap);
checkMerge("three", 2, three);

if (`failures == 0) {
	println("MERGEDELAYS DIFF PASS");
} else {
	println("MERGEDELAYS DIFF FAIL: " $ `failures toString);
}
