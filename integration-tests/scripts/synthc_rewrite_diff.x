-- synthc rewrites-ON differential test (M2).
--
-- Runs synthc with the algebraic rewriter ENABLED (importGraph(.., true)) and
-- compares its generated C++ against the C++ compiler with rewrites ON
-- (synthdefGenCppFromSexpr(sexpr, 0, true)). A byte match proves synthc's
-- rewrite engine reproduces the C++ engine exactly -- same rules, same fixpoint
-- (rewrite interleaved with constant folding + hash-consing), and the same
-- userial allocation (the candidate-serial discipline: a node that is rewritten
-- or hash-consed away still burns a serial, mirroring the C++ Expr ctor).
--
-- Audio-only synths (no event-rate controls -> no iso-groups) byte-match the
-- full generated source. The rewrites-OFF path is covered by
-- synthc_analysis_diff.x.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkRW(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefGenCppFromSexpr(g toSynthSexpr(name), 0, true);
	let ctx = g importGraph(name, true) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (import/analysis errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = fullCompare(genCpp(ctx, name), theirs);
	if (v == "PASS") {
		println("  PASS " $ name);
	} else {
		`failures = `failures + 1;
		println("  FAIL " $ name);
		println(v);
	}
}

-- reassociation + fold-after-rewrite: (s+2)+3 -> (2+3)+s -> s+5
fn reassoc() S = (0.4 lfsaw + 2.0) + 3.0 |> outlet;
checkRW("reassoc", reassoc);

-- identities / fold cascade: ((x*2+1)-1)*0.5
fn foldy() S = ((0.4 lfsaw * 2.0 + 1.0) - 1.0) * 0.5 |> outlet;
checkRW("foldy", foldy);

-- abs / min / max rules
fn absmm() S = (([3.0, -2.0] lfsaw abs) min(0.5) max(0.1)) |> outlet;
checkRW("absmm", absmm);

-- power rules: pow(x,2) -> x*x
fn powy() S = (0.3 lfsaw + 1.5) pow(2.0) |> outlet;
checkRW("powy", powy);

-- the M1 exit synths (lfsaw expansions exercise many rules via phasor*2-1)
fn blite() S = 0.4 lfsaw * 24 + 8 lfsaw * 3 + 81 |> nnhz sinosc * 0.04 |> outlet;
checkRW("bubbles_lite", blite);

fn bubbles() S = 0.4 lfsaw * 24 + [8, 7.23] lfsaw * 3 + 81 |> nnhz sinosc * 0.04 |> combn(0.2, 4) outlet;
checkRW("bubbles", bubbles);

-- mod1_test: byte-matched rewrites-off but rendered differently because defSynth
-- rewrites and synthc did not. With rewrites on it must byte-match (and render
-- identically).
fn mod1() S {
	let c = 1.4 fsinosc bilin(0.1, 0.95);
	let in = [60, 61] lfsaw([0, 0.25]) * 0.1;
	in chain(4, fn(x S)S{x onepole(c)}) outlet
}
checkRW("mod1_test", mod1);

if (`failures == 0) {
	println("M2 REWRITE DIFF PASS");
} else {
	println("M2 REWRITE DIFF FAIL: " $ `failures toString);
}
