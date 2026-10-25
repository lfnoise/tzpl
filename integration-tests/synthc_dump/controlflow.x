-- Graphs from the retired synthc_controlflow_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

fn if1() S {
	let sig = (0.3 lfsaw);
	if_(sig > 0.0, fn() { sig * 0.5 }, fn() { sig * 0.1 }) |> outlet
}
fn if2() S {
	let sig = (0.3 lfsaw);
	if_(sig > 0.0, fn() { sig * 0.5 }) |> outlet
}
fn if3() S {
	let sig = (0.3 lfsaw);
	if_(sig > 0.0,
		fn() { if_(sig > 0.5, fn() { sig }, fn() { sig * 0.5 }) },
		fn() { sig * 0.1 }) |> outlet
}
fn if4() S {
	let a = (0.3 lfsaw);
	let b = (0.4 lfsaw);
	let x = if_(a > 0.0, fn() { a }, fn() { 0.0 asSignal });
	let y = if_(b > 0.0, fn() { b }, fn() { 0.0 asSignal });
	(x + y) outlet
}
fn for1() S {
	let sig = (0.3 lfsaw);
	for_("i", 4, fn(i S) { sig * (i f32) }) |> outlet
}
fn for2() S {
	let sig = (0.3 lfsaw);
	for_("k", 3, fn(k S) { sig + (k f32) }) |> outlet
}
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
fn ctrlInBranch() S {
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	let sig = (0.3 lfsaw) * 0.3;
	if_(sig > 0.0, fn() { sig * amp }, fn() { sig * 0.1 }) |> outlet
}
fn twoCtrlBranch() S {
	let a = control("a", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	let b = control("b", ControlSpec { lo: 0.0, hi: 1.0, init: 0.3, warp: ControlWarp.linear });
	let sig = (0.3 lfsaw) * 0.3;
	if_(sig > 0.0, fn() { sig * a }, fn() { sig * b }) |> outlet
}

dumpCorpus([
	("if1", if1),
	("if2", if2),
	("if3", if3),
	("if4", if4),
	("for1", for1),
	("for2", for2),
	("sw1", sw1),
	("sw2", sw2),
	("pull_nested", pull_nested),
	("pulltwo", pulltwo),
	("ctrl_in_branch", ctrlInBranch),
	("two_ctrl_branch", twoCtrlBranch),
]);
