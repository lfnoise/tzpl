-- Graphs from the retired synthc_rewrite_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

fn reassoc() S = (0.4 lfsaw + 2.0) + 3.0 |> outlet;
fn foldy() S = ((0.4 lfsaw * 2.0 + 1.0) - 1.0) * 0.5 |> outlet;
fn absmm() S = (([3.0, -2.0] lfsaw abs) min(0.5) max(0.1)) |> outlet;
fn powy() S = (0.3 lfsaw + 1.5) pow(2.0) |> outlet;
fn blite() S = 0.4 lfsaw * 24 + 8 lfsaw * 3 + 81 |> nnhz sinosc * 0.04 |> outlet;
fn bubbles() S = 0.4 lfsaw * 24 + [8, 7.23] lfsaw * 3 + 81 |> nnhz sinosc * 0.04 |> combn(0.2, 4) outlet;
fn mod1() S {
	let c = 1.4 fsinosc bilin(0.1, 0.95);
	let in = [60, 61] lfsaw([0, 0.25]) * 0.1;
	in chain(4, fn(x S)S{x onepole(c)}) outlet
}

dumpCorpus([
	("reassoc", reassoc),
	("foldy", foldy),
	("absmm", absmm),
	("powy", powy),
	("bubbles_lite", blite),
	("bubbles", bubbles),
	("mod1_test", mod1),
]);
