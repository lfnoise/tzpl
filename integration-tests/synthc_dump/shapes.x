-- Block-engine front end: tensor shapes (lcm broadcasting per dimension),
-- controls as inputs with specs, note parameters as columns of `notes`,
-- the new rates, delay-weighted cycles and parameter groups.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

-- 2 channels times 3 channels broadcast to 6 (no power-of-two rounding).
fn lcm23() S = (vec([1.0, 2.0]) * (fs() / vec([100.0, 200.0, 300.0])) sin) |> outlet;

-- A matrix input times a row: {2,3} * {3} -> {2,3}; a column {2,1} -> {2,3}.
fn matrixBroadcast() S {
	let m = inlet(FLOAT32, matrix(2, 3), "m");
	let r = tensor(row(3), [1.0, 2.0, 3.0]);
	let c = tensor(matrix(2, 1), [10.0, 20.0]);
	(m * r + c) |> outlet
}

-- Constant folding broadcasts too: {2} + {3} folds to a {6} constant.
fn foldBroadcast() S = (vec([1.0, 2.0]) + vec([10.0, 20.0, 30.0])) * urand(1) |> outlet;

-- Controls of every kind, and a shaped control.
fn controlKinds() S {
	let f = control("freq", espec(20.0, 2000.0, 440.0));
	let g = control("gains", lspec(0.0, 1.0, 0.5), matrix(2, 2));
	let t = trigger("trig");
	let m = toggle("mute");
	let c = choice("mode", 4);
	(fs() / f) sin * g * (1.0 - m) + t + c |> outlet
}

-- A parameter group: coefficient design from two controls is block placed;
-- the one-pole recurrence that uses it is a weight-1 cycle.
fn onePole() S {
	let cutoff = control("cutoff", espec(20.0, 20000.0, 1000.0));
	let q = control("q", lspec(0.0, 1.0, 0.5));
	let a = (0.0 - 6.283185307 * cutoff / fs()) exp * q;
	let x = inlet(FLOAT32, 2, "in");
	let d = delayVar();
	let y = x * (1.0 - a) + a * d(1);
	d <- y;
	y outlet
}

-- A second-order recurrence (weights 1 and 2 on one cycle) and a
-- feed-forward small delay (no cycle).
fn biquadish() S {
	let x = inlet(FLOAT32, 1, "in");
	let d = delayVar();
	let y = x + 1.8 * d(1) - 0.9 * d(2);
	d <- y;
	(y - x * 0.5) outlet
}

-- Variable delay reads: a clamped index bounds the cycle weight; an
-- unbounded one gets weight 1.
fn varReads() S {
	let x = inlet(FLOAT32, 1, "in");
	let dt = control("dt", lspec(0.0, 0.1, 0.01));
	let d = delayVar(0.2);
	let r1 = d vread(max(64.0, dt * fs()), Interpolation.linear);
	let e = delayVar(0.2);
	let r2 = e vread(dt * fs(), Interpolation.cubic);
	d <- x + 0.5 * r1;
	e <- x + 0.5 * r2;
	r1 + r2 |> outlet
}

-- A voicer: gate and note parameters become columns of `notes`
-- {voices, 1 + params}; per-voice parameter groups.
fn voiced() S {
	let v = voicer(4, fn() S {
		let g = gate();
		let freq = noteParam("freq", espec(20.0, 2000.0, 440.0));
		let pan = noteParam("pan", lspec(0.0, 1.0, 0.5), 2);
		let inc = freq / fs();
		let d = delayVar();
		let ph = d(1);
		d <- (ph + inc) % 1.0;
		(ph * 6.283185307) sin * g * pan
	});
	v sum |> outlet
}

dumpCorpus([
	("lcm23", lcm23),
	("matrix_broadcast", matrixBroadcast),
	("fold_broadcast", foldBroadcast),
	("control_kinds", controlKinds),
	("one_pole", onePole),
	("biquadish", biquadish),
	("var_reads", varReads),
	("voiced", voiced),
]);
