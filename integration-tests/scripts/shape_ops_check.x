-- Tensor shape operations along an axis (block engine plan 4.8, M8b).
--
-- Each case applies one shape operation to a tensor of the numbers 1..n and
-- outputs (the result - the expected tensor), so every output channel is
-- exactly 0 when the operation is right. The expected tensor comes from the
-- reference below, which works on flat arrays and multi-indices, not on the
-- compiler's index maps. The input is scaled by a control held at 1 and by
-- (1 + sinosc(0)), which is 1 at audio rate, so the operation runs in the
-- plugin rather than folding at compile time.
--
-- SHAPE_OPS_MODE=compile (with SYNTHC_CORPUS_MODE=compile) builds the
-- plugins for forms_equiv --zero; otherwise this prints each case's analysis
-- dump and an EXPECT line with the result shape the dump's OUT line must show.
-- SHAPE_OPS_MODE=errors prints the dumps of cases the compiler must refuse.
-- run_shape_ops_test.sh runs all three.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

---------------------------------------------------------------------------
-- Reference: row-major tensors as flat arrays with dims (outer first).

fn prod(dims [Int]) Int {
	var n = 1;
	for (d : dims) { n = n * d; }
	n
}

fn multiIdx(dims [Int], w Int) [Int] {
	var idx [Int] = [];
	for (d : dims) { idx push!(0); }
	var rem = w;
	var k = dims length - 1;
	while (k >= 0) { idx[k] = rem % dims[k]; rem = rem // dims[k]; k = k - 1; }
	idx
}

fn flatIdx(dims [Int], idx [Int]) Int {
	var w = 0;
	var k = 0;
	while (k < dims length) { w = w * dims[k] + idx[k]; k = k + 1; }
	w
}

fn wrap(i Int, n Int) Int = ((i % n) + n) % n;

fn ax(a Int, r Int) Int = a < 0 ? a + r : a;

-- Each output element reads the source element `src` maps its multi-index to.
fn gather(xs [Float], sdims [Int], odims [Int], src ([Int]) [Int]) [Float] {
	var out [Float] = [];
	var w = 0;
	while (w < prod(odims)) {
		out push!(xs[flatIdx(sdims, src(multiIdx(odims, w)))]);
		w = w + 1;
	}
	out
}

fn withAt(dims [Int], k Int, v Int) [Int] {
	var d = dims copy;
	d[k] = v;
	d
}

-- An axis op: the output length along axis k, and the source index along it.
fn alongAxis(xs [Float], sdims [Int], a Int, len Int, f (Int) Int) ([Float], [Int]) {
	let k = ax(a, sdims length);
	let odims = withAt(sdims, k, len);
	let ys = gather(xs, sdims, odims, fn(idx [Int]) [Int] { withAt(idx, k, f(idx[k])) });
	(ys, odims)
}

fn refReduce(xs [Float], sdims [Int], a Int, scan Bool) ([Float], [Int]) {
	let k = ax(a, sdims length);
	var odims [Int] = [];
	var j = 0;
	while (j < sdims length) { if (scan || j != k) { odims push!(sdims[j]); } j = j + 1; }
	var out [Float] = [];
	var w = 0;
	while (w < prod(odims)) {
		let oi = multiIdx(odims, w);
		var si = sdims copy;
		var q = 0;
		j = 0;
		while (j < sdims length) {
			if (j == k) { si[j] = 0; } else { si[j] = oi[q]; }
			if (scan || j != k) { q = q + 1; }
			j = j + 1;
		}
		let upto = scan ? oi[k] : sdims[k] - 1;
		var z = 0.0;
		var e = 0;
		while (e <= upto) { z = z + xs[flatIdx(sdims, withAt(si, k, e))]; e = e + 1; }
		out push!(z);
		w = w + 1;
	}
	(out, odims)
}

fn refPermute(xs [Float], sdims [Int], perm [Int]) ([Float], [Int]) {
	let r = sdims length;
	var odims [Int] = [];
	for (p : perm) { odims push!(sdims[p]); }
	let ys = gather(xs, sdims, odims, fn(idx [Int]) [Int] {
		var si = sdims copy;
		var k = 0;
		while (k < r) { si[perm[k]] = idx[k]; k = k + 1; }
		si
	});
	(ys, odims)
}

-- Concatenation along axis k of a and b (b's values follow a's).
fn refJoin(xs [Float], adims [Int], ys [Float], bdims [Int], a Int) ([Float], [Int]) {
	let k = ax(a, adims length);
	let odims = withAt(adims, k, adims[k] + bdims[k]);
	var out [Float] = [];
	var w = 0;
	while (w < prod(odims)) {
		let idx = multiIdx(odims, w);
		if (idx[k] < adims[k]) { out push!(xs[flatIdx(adims, idx)]); }
		else { out push!(ys[flatIdx(bdims, withAt(idx, k, idx[k] - adims[k]))]); }
		w = w + 1;
	}
	(out, odims)
}

-- a and b along a new axis k.
fn refStack(xs [Float], ys [Float], dims [Int], k Int) ([Float], [Int]) {
	var odims [Int] = [];
	var j = 0;
	while (j <= dims length) {
		if (j == k) { odims push!(2); }
		if (j < dims length) { odims push!(dims[j]); }
		j = j + 1;
	}
	var out [Float] = [];
	var w = 0;
	while (w < prod(odims)) {
		let idx = multiIdx(odims, w);
		var si [Int] = [];
		j = 0;
		while (j < idx length) { if (j != k) { si push!(idx[j]); } j = j + 1; }
		out push!(idx[k] == 0 ? xs[flatIdx(dims, si)] : ys[flatIdx(dims, si)]);
		w = w + 1;
	}
	(out, odims)
}

---------------------------------------------------------------------------
-- Cases

fn iota(n Int, from Float) [Float] {
	var xs [Float] = [];
	var i = 0;
	while (i < n) { xs push!(from + i toFloat); i = i + 1; }
	xs
}

fn tshape(dims [Int]) Shape = dims length == 0 ? scalarShape() : shape(dims);

-- The input tensor, computed in the plugin.
fn live(xs [Float], dims [Int]) S {
	let one = control("one", lspec(0.0, 2.0, 1.0));
	tensor(tshape(dims), xs) * one * (1.0 + sinosc(0.0))
}

fn dimsStr(dims [Int]) String {
	if (dims length == 0) { return "1"; }
	var parts [String] = [];
	for (d : dims) { parts push!(d toString); }
	parts join("x")
}

var cases [(String, GraphFn)] = [];
var expects [String] = [];

-- `got` builds the operation's signal (inside the graph being made). The
-- dump outputs it alone (its OUT line is its shape); the compiled plugin
-- outputs its difference from the reference.
let compiling = match (getEnv("SHAPE_OPS_MODE")) { some(m): m == "compile"; none: false; };
fn add(name String, got GraphFn, want ([Float], [Int])) Void {
	let (ys, odims) = want;
	if (compiling) {
		cases push!((name, fn() S { got() - tensor(tshape(odims), ys) |> outlet }));
	} else {
		cases push!((name, fn() S { got() |> outlet }));
	}
	expects push!("EXPECT %^ %^" fmt(name, dimsStr(odims)));
}

let d234 = [2, 3, 4];
let x234 = iota(24, 1.0);
let d34 = [3, 4];
let x34 = iota(12, 1.0);
let d6 = [6];
let x6 = iota(6, 1.0);

fn axisCases(tag String, xs [Float], dims [Int]) Void {
	let r = dims length;
	var a0 = 0 - r;
	while (a0 < r) {
		let a = a0;          -- a fresh binding per case (the builders run later)
		let k = ax(a, r);
		let n = dims[k];
		let t = "%^_ax%^" fmt(tag, a < 0 ? "m" $ (0 - a) toString : a toString);
		add("take_" $ t, fn() S { live(xs, dims) take(2, a) }, alongAxis(xs, dims, a, 2, fn(i Int) Int { i % n }));
		add("takex_" $ t, fn() S { live(xs, dims) take(n + 1, a) }, alongAxis(xs, dims, a, n + 1, fn(i Int) Int { i % n }));
		add("drop_" $ t, fn() S { live(xs, dims) drop(1, a) }, alongAxis(xs, dims, a, n - 1, fn(i Int) Int { i + 1 }));
		add("stride_" $ t, fn() S { live(xs, dims) stride(2, a) }, alongAxis(xs, dims, a, (n + 1) // 2, fn(i Int) Int { i * 2 }));
		add("stutter_" $ t, fn() S { live(xs, dims) stutter(2, a) }, alongAxis(xs, dims, a, n * 2, fn(i Int) Int { i // 2 }));
		add("ncyc_" $ t, fn() S { live(xs, dims) ncyc(2, a) }, alongAxis(xs, dims, a, n * 2, fn(i Int) Int { i % n }));
		add("slice_" $ t, fn() S { live(xs, dims) slice(1, n, a) }, alongAxis(xs, dims, a, n, fn(i Int) Int { (i + 1) % n }));
		add("reverse_" $ t, fn() S { live(xs, dims) reverse(a) }, alongAxis(xs, dims, a, n, fn(i Int) Int { n - 1 - i }));
		add("rotate_" $ t, fn() S { live(xs, dims) rotate(1, a) }, alongAxis(xs, dims, a, n, fn(i Int) Int { wrap(i + 1, n) }));
		add("rotneg_" $ t, fn() S { live(xs, dims) rotate(-2, a) }, alongAxis(xs, dims, a, n, fn(i Int) Int { wrap(i - 2, n) }));
		add("slicev_" $ t, fn() S { live(xs, dims) slice(control("off", lspec(0.0, 8.0, 2.0)) i32, 2, a) }, alongAxis(xs, dims, a, 2, fn(i Int) Int { wrap(i + 2, n) }));
		add("sum_" $ t, fn() S { live(xs, dims) sum(a) }, refReduce(xs, dims, a, false));
		add("scan_" $ t, fn() S { live(xs, dims) scan(BinaryOp.add, a) }, refReduce(xs, dims, a, true));
		add("join_" $ t, fn() S { [live(xs, dims), live(iota(prod(dims), 100.0), dims)] join(a) },
			refJoin(xs, dims, iota(prod(dims), 100.0), dims, a));
		a0 = a0 + 1;
	}
	-- New axes at every position of the result.
	var k0 = 0;
	while (k0 <= r && r < MAX_RANK) {
		let k = k0;
		let ys = iota(prod(dims), 100.0);
		add("stack_%^_%^" fmt(tag, k), fn() S { [live(xs, dims), live(ys, dims)] stack(k) }, refStack(xs, ys, dims, k));
		k0 = k0 + 1;
	}
}

axisCases("r1", x6, d6);
axisCases("r2", x34, d34);
axisCases("r3", x234, d234);

add("sum_all", fn() S { live(x234, d234) sum }, (iota(1, 300.0), [Int]()));

-- Constants and shapes: nested-array literals and AsShape dims ([Int]) give
-- the same tensors as tensor(Shape, flat values).
fn one() S = control("one", lspec(0.0, 2.0, 1.0)) * (1.0 + sinosc(0.0));
add("lit_nest2", fn() S { tensor([[1.0, 2.0, 3.0, 4.0], [5.0, 6.0, 7.0, 8.0], [9.0, 10.0, 11.0, 12.0]]) * one() },
	(x34, d34));
add("lit_nest3", fn() S { tensor([[[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]], [[7.0, 8.0], [9.0, 10.0], [11.0, 12.0]]]) * one() },
	(iota(12, 1.0), [2, 3, 2]));
add("lit_nest2i", fn() S { tensor([[1, 2], [3, 4]]) * one() }, (iota(4, 1.0), [2, 2]));
add("lit_dims", fn() S { tensor([3, 4], x34) * one() }, (x34, d34));
add("lit_cyclic", fn() S { tensor([2, 3], [1.0, 2.0]) * one() }, ([1.0, 2.0, 1.0, 2.0, 1.0, 2.0], [2, 3]));
add("fill_dims", fn() S { fill([2, 2], 0.5) * one() }, ([0.5, 0.5, 0.5, 0.5], [2, 2]));
add("ctl_dims", fn() S { control("g", lspec(0.0, 1.0, 0.25), [2, 3]) * one() },
	([0.25, 0.25, 0.25, 0.25, 0.25, 0.25], [2, 3]));
add("trig_dims", fn() S { trigger("t", [2, 3]) * one() }, ([0.0, 0.0, 0.0, 0.0, 0.0, 0.0], [2, 3]));
add("toggle_dims", fn() S { toggle("b", 1.0, [3, 2]) * one() }, ([1.0, 1.0, 1.0, 1.0, 1.0, 1.0], [3, 2]));
add("choice_dims", fn() S { choice("c", 4, 2, [2, 2]) * one() }, ([2.0, 2.0, 2.0, 2.0], [2, 2]));
-- The driver plays voice 0 (gate on), so the voices sum to the init value.
add("noteparam_dims", fn() S {
	voicer(2, fn() S { noteParam("p", lspec(0.0, 1.0, 0.5), [2, 3]) * gate() }) sum(0) * one()
}, ([0.5, 0.5, 0.5, 0.5, 0.5, 0.5], [2, 3]));
-- Random generators with shapes: 1 wherever a channel is in the
-- generator's range, so the whole result is ones of the shape.
fn inRange(x S, lo Float, hi Float) S = (x >= lo) * (x <= hi);
let ones23 = ([1.0, 1.0, 1.0, 1.0, 1.0, 1.0], [2, 3]);
add("frand_dims", fn() S { inRange(frand(2.0, 3.0, [2, 3]), 2.0, 3.0) * one() }, ones23);
add("irand_dims", fn() S { inRange(irand(2, 5, [2, 3]), 2.0, 5.0) * one() }, ones23);
add("exprand_dims", fn() S { inRange(exprand(100.0, 1000.0, [2, 3]), 99.9, 1000.1) * one() }, ones23);
add("white_dims", fn() S { inRange(white([2, 3]), -1.0, 1.0) * one() }, ones23);
add("violet_dims", fn() S { inRange(violet([2, 3]), -1.0, 1.0) * one() }, ones23);
add("red_dims", fn() S { inRange(red([2, 3]), -1.0, 1.0) * one() }, ones23);
add("gray_dims", fn() S { inRange(gray([2, 3]), -1.0, 1.0) * one() }, ones23);
add("pinkf_dims", fn() S { inRange(pinkf([2, 3]), -4.0, 4.0) * one() }, ones23);
add("pinkfe_dims", fn() S { inRange(pinkfe([2, 3]), -4.0, 4.0) * one() }, ones23);
add("blue_dims", fn() S { inRange(blue([2, 3]), -16.0, 16.0) * one() }, ones23);
add("pink_dims", fn() S { inRange(pink([2, 3]), -4.0, 4.0) * one() }, ones23);
add("coin_dims", fn() S { inRange(coin(0.5, [2, 3]), 0.0, 1.0) * one() }, ones23);
add("velvet_dims", fn() S { inRange(velvet(1000.0, [2, 3]), 0.0, 1.0) * one() }, ones23);
add("dust_dims", fn() S { inRange(dust(1000.0, [2, 3]), 0.0, 1.0) * one() }, ones23);
add("dust2_dims", fn() S { inRange(dust2(1000.0, [2, 3]), -1.0, 1.0) * one() }, ones23);
add("dustep_dims", fn() S { inRange(dustep(1000.0, [2, 3]), -1.0, 1.0) * one() }, ones23);
add("lfnoise0_dims", fn() S { inRange((50.0 asSignal) lfnoise0([2, 3]), -1.0, 1.0) * one() }, ones23);
add("lfnoise1_dims", fn() S { inRange((50.0 asSignal) lfnoise1([2, 3]), -1.0, 1.0) * one() }, ones23);
add("lfnoise3_dims", fn() S { inRange((50.0 asSignal) lfnoise3([2, 3]), -2.0, 2.0) * one() }, ones23);
add("transpose_r2", fn() S { live(x34, d34) transpose }, refPermute(x34, d34, [1, 0]));
add("transpose_r3", fn() S { live(x234, d234) transpose }, refPermute(x234, d234, [2, 1, 0]));
add("permute_r3", fn() S { live(x234, d234) transpose([1, 2, 0]) }, refPermute(x234, d234, [1, 2, 0]));
add("permute_neg_r3", fn() S { live(x234, d234) transpose([-1, 0, 1]) }, refPermute(x234, d234, [2, 0, 1]));
add("reshape_r3", fn() S { live(x234, d234) reshape([4, 6]) }, (x234, [4, 6]));
add("reshape_infer", fn() S { live(x234, d234) reshape([-1, 3]) }, (x234, [8, 3]));
add("reshape_row", fn() S { live(x234, d234) reshape([24]) }, (x234, [24]));
-- A 1 x n matrix stays rank 2; reversing its axis 0 (one row) is the identity.
add("matrix_1xn", fn() S { live(x6, [1, 6]) reverse(0) }, (x6, [1, 6]));
-- Composition: rows reversed, then each row's sum.
add("compose", fn() S { live(x234, d234) reverse(-2) sum(-1) },
	refReduce(alongAxis(x234, d234, -2, 3, fn(i Int) Int { 2 - i }).0, d234, -1, false));
-- Views read inline by reductions, scans and other views (no copies).
add("stride_sum", fn() S { live(x6, d6) stride(2) sum },
	(iota(1, 9.0), [Int]()));
add("reshape_sum0", fn() S { live(x6, d6) reshape([3, 2]) sum(0) },
	refReduce(x6, [3, 2], 0, false));
add("take_scan_r3", fn() S { live(x234, d234) take(2, 1) scan(BinaryOp.add, -1) },
	refReduce(alongAxis(x234, d234, 1, 2, fn(i Int) Int { i }).0, [2, 2, 4], -1, true));
add("rev_transpose", fn() S { live(x34, d34) reverse(0) transpose },
	refPermute(alongAxis(x34, d34, 0, 3, fn(i Int) Int { 2 - i }).0, d34, [1, 0]));
add("view_chain_sum", fn() S { live(x234, d234) reverse(-1) stride(2, 1) sum(0) },
	refReduce(alongAxis(alongAxis(x234, d234, -1, 4, fn(i Int) Int { 3 - i }).0, d234, 1, 2,
		fn(i Int) Int { i * 2 }).0, [2, 2, 4], 0, false));

-- Dynamic views read inline: gathers through a signal offset.
add("slicev_sum0_r2", fn() S { live(x34, d34) slice(control("off", lspec(0.0, 8.0, 2.0)) i32, 2, 0) sum(0) },
	refReduce(alongAxis(x34, d34, 0, 2, fn(i Int) Int { wrap(i + 2, 3) }).0, [2, 4], 0, false));
-- 1..6 rotated by -1 is 6 1 2 3 4 5; every other element sums to 12.
add("rotate_stride_sum", fn() S { live(x6, d6) rotate(control("amt", lspec(-8.0, 8.0, -1.0)) i32) stride(2) sum },
	(iota(1, 12.0), [Int]()));
add("rotate_scan_r2", fn() S { live(x34, d34) rotate(1, -1) scan(BinaryOp.add, -1) },
	refReduce(alongAxis(x34, d34, -1, 4, fn(i Int) Int { wrap(i + 1, 4) }).0, d34, -1, true));

-- at / put along an axis.
fn ints(xs [Int]) S = tensor(row(xs length), xs @ toFloat) * control("one", lspec(0.0, 2.0, 1.0)) i32;
fn ctlIdx(name String, v Float) S = control(name, lspec(-8.0, 8.0, v)) i32;
add("at_row_r2", fn() S { live(x34, d34) at(ctlIdx("i", 1.0), 0) }, (alongAxis(x34, d34, 0, 1, fn(i Int) Int { 1 }).0, [4]));
add("at_col_r2", fn() S { live(x34, d34) at(ctlIdx("i", -1.0), -1) }, (alongAxis(x34, d34, -1, 1, fn(i Int) Int { 3 }).0, [3]));
add("at_cols_r2", fn() S { live(x34, d34) at(ints([2, 0, 5]), -1) },
	alongAxis(x34, d34, -1, 3, fn(i Int) Int { [2, 0, 1][i] }));
add("at_planes_r3", fn() S { live(x234, d234) at(ints([1, 1, 0]), 0) },
	alongAxis(x234, d234, 0, 3, fn(i Int) Int { [1, 1, 0][i] }));
add("at_mid_r3", fn() S { live(x234, d234) at(ctlIdx("i", 2.0), 1) },
	(alongAxis(x234, d234, 1, 1, fn(i Int) Int { 2 }).0, [2, 4]));
-- put: row 1 of a 3x4 replaced by 101..104; columns 3 and 0 by a 3x2.
fn refPut(xs [Float], dims [Int], k Int, idx [Int], vs [Float]) [Float] {
	var out = xs copy;
	var w = 0;
	while (w < prod(dims)) {
		let mi = multiIdx(dims, w);
		var j = 0;
		while (j < idx length) {
			if (wrap(idx[j], dims[k]) == mi[k]) {
				var vi = mi copy;
				vi[k] = j;
				var vd = dims copy;
				vd[k] = idx length;
				out[w] = vs[flatIdx(vd, vi)];
			}
			j = j + 1;
		}
		w = w + 1;
	}
	out
}
add("put_row_r2", fn() S { live(x34, d34) put(ctlIdx("i", 1.0), tensor(row(4), iota(4, 101.0)), 0) },
	(refPut(x34, d34, 0, [1], iota(4, 101.0)), d34));
add("put_cols_r2", fn() S { live(x34, d34) put(ints([3, 0]), tensor(matrix(3, 2), iota(6, 201.0)), -1) },
	(refPut(x34, d34, 1, [3, 0], iota(6, 201.0)), d34));

-- An axis op on a scalar treats it as a row.
add("scalar_stutter", fn() S { live([5.0], [Int]()) stutter(3) }, ([5.0, 5.0, 5.0], [3]));

-- Inside a voicer: each voice applies the operation to its own tensor (a
-- note parameter held at 1 makes it voice-local) and outputs its difference
-- from the reference, so every voice, sounding or not, outputs 0. Shapes 2x4
-- (power-of-two voice widths: flat voice mode) and 3x4 (per-voice structs).
-- The dump outputs the voicer itself: voices x the result shape.
fn voiced(name String, xs [Float], dims [Int], op (S) S, want ([Float], [Int])) Void {
	let (ys, odims) = want;
	cases push!((name, fn() S {
		voicer(2, fn() S {
			let one = noteParam("one", lspec(0.0, 2.0, 1.0));
			let r = op(tensor(tshape(dims), xs) * one * (1.0 + sinosc(0.0)));
			compiling ? r - tensor(tshape(odims), ys) : r
		}) |> outlet
	}));
	expects push!("EXPECT %^ %^" fmt(name, dimsStr([2] $ odims)));
}

-- Voices that differ: only voice 0's gate is on (forms_equiv --zero), so
-- (1 + gate) doubles voice 0. A view of a per-voice value that indexed
-- without the voice's base would read voice 0's values in every voice.
-- Partial widths (a 4-wide value viewed 2 wide) in flat SIMD voice loops.
fn voicedGate(name String, nv Int, xs [Float], dims [Int], op (S) S, want ([Float], [Int])) Void {
	let (ys, odims) = want;
	cases push!((name, fn() S {
		voicer(nv, fn() S {
			let k = 1.0 + gate();
			let r = op(tensor(tshape(dims), xs) * k * (1.0 + sinosc(0.0)));
			compiling ? r - tensor(tshape(odims), ys) * k : r
		}) |> outlet
	}));
	expects push!("EXPECT %^ %^" fmt(name, dimsStr([nv] $ odims)));
}
for (nv : [4, 8]) {
	let t = "v" $ nv toString;
	voicedGate("gtake_" $ t, nv, iota(4, 1.0), [4], fn(x S) S { x take(2) }, ([1.0, 2.0], [2]));
	voicedGate("gdrop_" $ t, nv, iota(4, 1.0), [4], fn(x S) S { x drop(2) }, ([3.0, 4.0], [2]));
	voicedGate("gstride_" $ t, nv, iota(4, 1.0), [4], fn(x S) S { x stride(2) }, ([1.0, 3.0], [2]));
	voicedGate("gslice_" $ t, nv, iota(4, 1.0), [4], fn(x S) S { x slice(1, 2) }, ([2.0, 3.0], [2]));
	voicedGate("grev_" $ t, nv, iota(4, 1.0), [4], fn(x S) S { x reverse }, ([4.0, 3.0, 2.0, 1.0], [4]));
	voicedGate("gtake2d_" $ t, nv, iota(8, 1.0), [2, 4], fn(x S) S { x take(2, cols) }, ([1.0, 2.0, 5.0, 6.0], [2, 2]));
}

let x24 = iota(8, 1.0);
let d24 = [2, 4];
for (vc : [("v24", x24, d24), ("v34", x34, d34)]) {
	let (tag, xs, dims) = vc;
	let n0 = dims[0];
	let n1 = dims[1];
	voiced("vtake0_" $ tag, xs, dims, fn(x S) S { x take(1, 0) }, alongAxis(xs, dims, 0, 1, fn(i Int) Int { i }));
	voiced("vrev_" $ tag, xs, dims, fn(x S) S { x reverse }, alongAxis(xs, dims, -1, n1, fn(i Int) Int { n1 - 1 - i }));
	voiced("vstut0_" $ tag, xs, dims, fn(x S) S { x stutter(2, 0) }, alongAxis(xs, dims, 0, n0 * 2, fn(i Int) Int { i // 2 }));
	voiced("vrot_" $ tag, xs, dims, fn(x S) S { x rotate(1) }, alongAxis(xs, dims, -1, n1, fn(i Int) Int { wrap(i + 1, n1) }));
	voiced("vsum0_" $ tag, xs, dims, fn(x S) S { x sum(0) }, refReduce(xs, dims, 0, false));
	voiced("vsum1_" $ tag, xs, dims, fn(x S) S { x sum(1) }, refReduce(xs, dims, 1, false));
	voiced("vscan0_" $ tag, xs, dims, fn(x S) S { x scan(BinaryOp.add, 0) }, refReduce(xs, dims, 0, true));
	voiced("vtrans_" $ tag, xs, dims, fn(x S) S { x transpose }, refPermute(xs, dims, [1, 0]));
	voiced("vjoin1_" $ tag, xs, dims, fn(x S) S { [x, x] join(1) }, refJoin(xs, dims, xs, dims, 1));
	voiced("vstack1_" $ tag, xs, dims, fn(x S) S { [x, x] stack(1) }, refStack(xs, xs, dims, 1));
	voiced("vatcols_" $ tag, xs, dims, fn(x S) S { x at(tensor(row(2), [3.0, 1.0]) i32, -1) },
		alongAxis(xs, dims, -1, 2, fn(i Int) Int { [3, 1][i] }));
	voiced("vputrow_" $ tag, xs, dims, fn(x S) S { x put(1, tensor(row(4), iota(4, 101.0)), 0) },
		(refPut(xs, dims, 0, [1], iota(4, 101.0)), dims));
}

-- Cases the compiler must refuse.
var bad [(String, GraphFn)] = [];
bad push!(("bad_axis", fn() S { live(x34, d34) take(1, 2) |> outlet }));
bad push!(("bad_neg_axis", fn() S { live(x34, d34) sum(-3) |> outlet }));
bad push!(("bad_drop", fn() S { live(x6, d6) drop(6) |> outlet }));
bad push!(("bad_join", fn() S { [live(x34, d34), live(x6, [2, 3])] join(0) |> outlet }));
bad push!(("bad_stack", fn() S { [live(x34, d34), live(x6, d6)] stack |> outlet }));
bad push!(("bad_reshape", fn() S { live(x34, d34) reshape([5, -1]) |> outlet }));
bad push!(("bad_at", fn() S { live(x34, d34) at(live([0.0, 1.0, 2.0, 0.0], [2, 2]) i32, 0) |> outlet }));
bad push!(("bad_perm", fn() S { live(x234, d234) transpose([0, 0, 1]) |> outlet }));

let mode = match (getEnv("SHAPE_OPS_MODE")) { some(m): m; none: "dump"; };
if (mode == "errors") {
	dumpCorpus(bad);
} else {
	if (mode != "compile") { for (e : expects) { e println; } }
	dumpCorpus(cases);
}
