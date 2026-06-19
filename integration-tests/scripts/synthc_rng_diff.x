-- synthc RNG differential test (M2).
--
-- Compares synthc's generated C++ against the C++ compiler (rewrites off) for
-- the random ops: urand (unipolar), birand (bipolar), rand64 (bits). Byte
-- matching exercises the inst-var RandState decl, the arc4seedrand init seeding,
-- and the urand{f}/birand{f}/rand64 expression emission.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkRng(name String, synthFn GraphFn) Void {
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

fn ur1() S   = urand(1) * 0.1 |> outlet;
fn br1() S   = birand(1) * 0.5 |> outlet;
fn rb1() S   = (rand64(1) & 255) f32 * 0.01 |> outlet;
-- multichannel + mixed: urand and birand feeding the same graph
fn mix() S   = (urand(1) * 0.3 + birand(1) * 0.2) |> outlet;
-- multichannel urand
fn ur2() S   = urand(2) * 0.1 |> outlet;

checkRng("ur1", ur1);
checkRng("br1", br1);
checkRng("rb1", rb1);
checkRng("mix", mix);
checkRng("ur2", ur2);

if (`failures == 0) {
	println("M2 RNG DIFF PASS");
} else {
	println("M2 RNG DIFF FAIL: " $ `failures toString);
}
