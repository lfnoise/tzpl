-- synthc shared-input differential test.
--
-- Compares synthc's generated C++ against the C++ compiler (rewrites off) for
-- the sharedIn ugen: audio-rate and init-rate reads, hash-cons dedup of same
-- slot+rate reads, and the exported tzpl_sharedInput pointer preamble. Byte
-- matching exercises the SharedInExpr emission and the fallback/extern block.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkSI(name String, synthFn GraphFn) Void {
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

-- raw audio-rate read
fn si1() S = sharedIn(0) * 0.1 |> outlet;
-- two slots + dedup: the two sharedIn(0) reads hash-cons to one node
fn si2() S = (sharedIn(0) + sharedIn(1) + sharedIn(0)) * 0.25 |> outlet;
-- an init-rate read is a distinct node from an audio-rate read of the same slot
fn si3() S = (sharedIn(3, Rate.init) + sharedIn(3)) * 0.5 |> outlet;
-- the mouse ugens: lag + exponential/linear map composites
fn simouse() S = sinosc(mouseXExp(110.0, 880.0)) * mouseY(0.0, 0.5) |> outlet;

checkSI("si1", si1);
checkSI("si2", si2);
checkSI("si3", si3);
checkSI("simouse", simouse);

if (`failures == 0) {
	println("SHAREDIN DIFF PASS");
} else {
	println("SHAREDIN DIFF FAIL: " $ `failures toString);
}
