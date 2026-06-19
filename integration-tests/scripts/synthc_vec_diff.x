-- synthc vector + reduce differential test (M2).
--
-- Compares synthc's generated C++ against the C++ compiler (rewrites off) for
-- the reduce family and every front-end vec op. The vec ops keep the loop but
-- remap the channel index; byte-matching requires the VIdx symbolic-index
-- algebra (codegen.x) to reproduce the C++ VarIndex simplifications.
--
-- NOTE: multichannel comes from a constant array, not a delay-derived signal,
-- because the C++ runs VecXxx::calcShape at addExpr time -- before delay shapes
-- propagate -- which makes e.g. vec_drop underflow on a delayed input. synthc
-- (separate shape pass) is fine, but there's no C++ output to compare against.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkVec(name String, synthFn GraphFn) Void {
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

-- reduce family (changes the loop: output = cols channels)
fn sum4() S  = [200, 251, 100, 50] lfsaw * 0.1 |> sum |> outlet;
fn prod2() S = [300, 400] lfsaw * 0.2 |> product |> outlet;
fn sum2of4() S = [200, 251, 100, 50] lfsaw * 0.1 |> reduce(BinaryOp.add, 2) |> outlet;
fn maxr() S  = [200, 251, 100, 50] lfsaw * 0.1 |> maxOf |> outlet;
checkVec("sum4", sum4);
checkVec("prod2", prod2);
checkVec("sum2of4", sum2of4);
checkVec("maxr", maxr);

-- vec ops (channel-index remap). Const-array multichannel input.
let m4 = fn() S = [1.0, 2.0, 3.0, 4.0] * (0.3 lfsaw);
fn revv() S  = m4() reverse outlet;
fn takv() S  = m4() take(2) outlet;
fn drpv() S  = m4() drop(2) outlet;
fn strv() S  = m4() stride(2) outlet;
fn ncyv() S  = m4() ncyc(2) outlet;
fn trnv() S  = m4() transpose(2) outlet;
fn rotv() S  = m4() rotate(1) outlet;
fn stuv() S  = ([1.0, 2.0] * (0.3 lfsaw)) stutter(2) outlet;
checkVec("reverse", revv);
checkVec("take", takv);
checkVec("drop", drpv);
checkVec("stride", strv);
checkVec("ncyc", ncyv);
checkVec("transpose", trnv);
checkVec("rotate", rotv);
checkVec("stutter", stuv);

if (`failures == 0) {
	println("M2 VEC DIFF PASS");
} else {
	println("M2 VEC DIFF FAIL: " $ `failures toString);
}
