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
	-- Tier-1: analysis dump (antecedent-order-normalized).
	let dumpV = cfDumpCompare(ctx dumpToString, synthdefAnalysisDump(g toSynthSexpr(name), false));
	-- Tier-2: generated C++ must byte-match the C++ compiler.
	let cppV = fullCompare(genCpp(ctx, name), synthdefGenCppFromSexpr(g toSynthSexpr(name), 0, false));
	if (dumpV == "PASS" && cppV == "PASS") { println("  PASS " $ name); }
	else {
		`failures = `failures + 1;
		println("  FAIL " $ name);
		if (dumpV != "PASS") { println("  [dump] " $ dumpV); }
		if (cppV != "PASS") { println("  [cpp] " $ cppV); }
	}
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

-- for_: counted loop; body uses the loop index (a VarExpr)
fn for1() S {
	let sig = (0.3 lfsaw);
	for_("i", 4, fn(i S) { sig * (i f32) }) |> outlet
}
-- for_ with a bare-capture body (no local exprs in the body but the loop var)
fn for2() S {
	let sig = (0.3 lfsaw);
	for_("k", 3, fn(k S) { sig + (k f32) }) |> outlet
}
-- switch_: multi-way over captured signals (some branches compute, some bare)
fn sw1() S {
	let a = (0.2 lfsaw);
	let sel = a > 0.0;
	switch(sel, [fn() { a * 0.5 }, fn() { a * 0.3 }, fn() { a * 0.1 }]) |> outlet
}
fn sw2() S {
	let a = (0.2 lfsaw);
	let b = (0.3 lfsaw);
	let sel = a > 0.0;
	switch(sel, [fn() { a }, fn() { b }]) |> outlet
}
-- Real examples: nested pauses (no-else if_) with comb delays inside branches
-- (subgraph-local delays), multichannel branches, and RNG inside a branch.
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
fn pulltwo() S {
	let pullFun1 = fn(){ let amp = 0.2 * dust(4, 2) decay2(0.04, 0.3); amp * 800 fsinxosc };
	let pullFun2 = fn(){ let freq = nnhz(81 + 24 * 0.4 lfsaw + 3 * [8, 7.23] lfsaw); 0.04 * fsinxosc(freq) fadein(0.1) combn(0.2, 4) };
	let gate1 = 2.71828 fsinosc max0;
	let gate2 = 3.14159 fsinosc max0;
	let out = gate1 pause(pullFun1) + gate2 pause(pullFun2);
	out outlet
}

-- M4.3 verification: an event-rate control() used INSIDE an if_ branch
-- (subgraph). The iso-group for the control must activate consumers across the
-- subgraph boundary -- the "event-rate iso-groups spanning subgraphs" case.
fn ctrlInBranch() S {
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	let sig = (0.3 lfsaw) * 0.3;
	if_(sig > 0.0, fn() { sig * amp }, fn() { sig * 0.1 }) |> outlet
}
-- two controls, one consumed in each branch
fn twoCtrlBranch() S {
	let a = control("a", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	let b = control("b", ControlSpec { lo: 0.0, hi: 1.0, init: 0.3, warp: ControlWarp.linear });
	let sig = (0.3 lfsaw) * 0.3;
	if_(sig > 0.0, fn() { sig * a }, fn() { sig * b }) |> outlet
}

checkCF("if1", if1);
checkCF("if2", if2);
checkCF("if3", if3);
checkCF("if4", if4);
checkCF("for1", for1);
checkCF("for2", for2);
checkCF("sw1", sw1);
checkCF("sw2", sw2);
checkCF("pull_nested", pull_nested);
checkCF("pulltwo", pulltwo);
checkCF("ctrl_in_branch", ctrlInBranch);
checkCF("two_ctrl_branch", twoCtrlBranch);

if (`failures == 0) {
	println("M3 CONTROLFLOW DIFF PASS");
} else {
	println("M3 CONTROLFLOW DIFF FAIL: " $ `failures toString);
}
