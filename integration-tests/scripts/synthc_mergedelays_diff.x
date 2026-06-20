-- synthc mergeDelays differential test (M2).
--
-- mergeDelays merges delay buffers that share a writer, graph, and initializer
-- set. The C++ merge key is writer-identity-based and the front end gives every
-- delayVar() its own writer node, so the pass is a structural no-op on every
-- realizable graph. This test pins that: synthc's output (with mergeDelays run
-- first, matching the C++ pipeline order) must still byte-match the C++
-- generator for delay-bearing synths, including two independent identical delays
-- that must NOT be merged.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkDel(name String, synthFn GraphFn) Void {
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

-- Two independent delays with identical init + written signal: distinct writer
-- nodes, so they stay separate (no merge), matching the C++.
fn twodel() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.0);
	a write(sig); b write(sig);
	(a read(1) + b read(1)) outlet
}

-- Single feedback delay read at two fixed offsets from one buffer.
fn fb2() S {
	let drive = (fs() / 330.0) sin * 0.001;
	let y = delayVar(); y init(1, 0.0);
	let o = y read(1) * 0.7 + y read(2) * 0.2 + drive;
	y write(o); o outlet
}

checkDel("twodel", twodel);
checkDel("fb2", fb2);

if (`failures == 0) {
	println("M2 MERGEDELAYS DIFF PASS");
} else {
	println("M2 MERGEDELAYS DIFF FAIL: " $ `failures toString);
}
