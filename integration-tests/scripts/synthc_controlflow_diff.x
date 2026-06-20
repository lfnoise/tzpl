-- synthc control-flow Tier-1 differential test (M3).
--
-- Compares synthc's analysis dump against the C++ compiler (rewrites off) for
-- if_ synths. Uses cfDumpCompare, which sorts the antecedent bracket on TREE/
-- LOOP header lines: the C++ stores tree/loop antecedents in hash-map order
-- (non-deterministic), so only the antecedent SET is compared, not its order.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkCF(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefAnalysisDump(g toSynthSexpr(name), false);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = cfDumpCompare(ctx dumpToString, theirs);
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

-- basic if/else over a captured audio signal
fn if1() S {
	let sig = (0.3 lfsaw);
	if_(sig > 0.0, fn() { sig * 0.5 }, fn() { sig * 0.1 }) |> outlet
}
-- if with no else (defaults to 0)
fn if2() S {
	let sig = (0.3 lfsaw);
	if_(sig > 0.0, fn() { sig * 0.5 }) |> outlet
}
-- nested if in the then-branch
fn if3() S {
	let sig = (0.3 lfsaw);
	if_(sig > 0.0,
		fn() { if_(sig > 0.5, fn() { sig }, fn() { sig * 0.5 }) },
		fn() { sig * 0.1 }) |> outlet
}
-- two independent ifs feeding an add
fn if4() S {
	let a = (0.3 lfsaw);
	let b = (0.4 lfsaw);
	let x = if_(a > 0.0, fn() { a }, fn() { 0.0 asSignal });
	let y = if_(b > 0.0, fn() { b }, fn() { 0.0 asSignal });
	(x + y) outlet
}

checkCF("if1", if1);
checkCF("if2", if2);
checkCF("if3", if3);
checkCF("if4", if4);

if (`failures == 0) {
	println("M3 CONTROLFLOW DIFF PASS");
} else {
	println("M3 CONTROLFLOW DIFF FAIL: " $ `failures toString);
}
