-- synthc/recurrence.x
-- Recurrence classification (block-engine plan §4.7), analysis only.
--
-- A feedback cycle of weight 1-3 (cycles.x) is a recurrence s' = F(s, x):
-- its state s is the values carried on its weighted edges (the in-cycle
-- delay reads), and x is what flows in from outside. Walking the cycle in
-- sorted order gives each node a form:
--   - a node outside the cycle is independent of s (an input or a
--     coefficient);
--   - a state read is linear;
--   - + and - of linear forms, a linear form times or divided by an
--     independent value, negation and float casts stay linear, and the
--     largest rate of any coefficient that multiplies state is tracked;
--   - anything else applied to state is nonlinear.
-- The classes, in order of preference:
--   scan          s' = s (+) x with an associative op: +, + then wrap
--                 (frac, written x - floor(x)), max, min, xor -- one state of
--                 weight 1
--   affine-block  affine in s, every state coefficient at block rate or
--                 lower (block state space, built when its group recomputes)
--   affine-scan   s' = a*s + b, first order, a or b at audio rate
--   general       higher-order affine with audio-rate coefficients, or
--                 nonlinear; the node that made it general is recorded.

import synthdef.*;
import synthc.ir.*;

enum RecClass {
	notRecurrence,          -- weight >= 4: vector-feasible as it stands
	scan(String),           -- the op: "+", "+wrap", "max", "min", "xor"
	affineBlock(Int),       -- state order
	affineScan,
	general(Int, NIdx),     -- state order (0 if nonlinear), culprit node
}

-- Each cycle's class, parallel to ctx.cycles.
fn classifyRecurrences(ctx Ctx) [RecClass] {
	var out [RecClass] = [];
	for (cy : ctx.cycles) { out push!(ctx _classify(cy)); }
	out
}

fn _order(ctx Ctx, cy CycleInfo) Int {
	var n = 0;
	for (r : cy.readers) { n = n + ctx.dweight[r]; }
	n
}

fn _inCycle(cy CycleInfo, x NIdx) Bool = cy.nodes contains(x);

fn _classify(ctx Ctx, cy CycleInfo) RecClass {
	if (cy.minWeight >= 4) { return RecClass.notRecurrence; }
	let sc = ctx _scanOp(cy);
	if (sc length > 0) { return RecClass.scan(sc); }
	-- Linear-form walk. lin[x]: x is a linear form over the state; coef[x]:
	-- the largest rate of a coefficient multiplying state in it.
	let n = ctx numNodes;
	var lin [Bool] = false repeat(n);
	var coef [Rate] = Rate.constant repeat(n);
	for (r : cy.readers) { lin[r] = true; }
	var culprit = NONE;
	for (x : cy.nodes) {
		if (lin[x]) { continue; }
		let ins = ctx.ins[x];
		var linIns = 0;
		var maxCoef = Rate.constant;
		for (i : ins) {
			if (_inCycle(cy, i)) {
				if (!lin[i]) { linIns = -100; } else { linIns = linIns + 1; if (maxCoef < coef[i]) { maxCoef = coef[i]; } }
			}
		}
		if (linIns < 0) { continue; }          -- fed by a nonlinear node: already blamed
		var ok = false;
		var c = maxCoef;
		match (ctx.kind[x]) {
			binopK(op): {
				let a = ins[0];
				let b = ins[1];
				let la = _inCycle(cy, a);
				let lb = _inCycle(cy, b);
				match (op) {
					add: { ok = true; }
					sub: { ok = true; }
					mul: {
						if (la && lb) { ok = false; }
						else {
							ok = true;
							let k = la ? b : a;
							if (c < ctx.nrate[k]) { c = ctx.nrate[k]; }
						}
					}
					div: {
						if (la && !lb) {
							ok = true;
							if (c < ctx.nrate[b]) { c = ctx.nrate[b]; }
						}
					}
					_: {}
				}
			}
			unopK(op): { match (op) { neg: { ok = true; } _: {} } }
			castopK(t): { ok = t.0 == 4 || t.0 == 8; }
			_: {}
		}
		if (ok) {
			lin[x] = true;
			coef[x] = c;
		} else if (culprit == NONE) {
			culprit = x;
		}
	}
	let order = ctx _order(cy);
	if (culprit != NONE) { return RecClass.general(0, culprit); }
	-- Every value written back to state is linear: affine.
	var maxC = Rate.constant;
	var audioCoef = NONE;
	for (x : cy.nodes) {
		if (maxC < coef[x]) { maxC = coef[x]; }
		if (coef[x] == Rate.audio && audioCoef == NONE) { audioCoef = x; }
	}
	if (!(Rate.block < maxC)) { return RecClass.affineBlock(order); }
	if (order == 1) { return RecClass.affineScan; }
	RecClass.general(order, audioCoef)
}

-- A scan: one state read r of weight 1 whose written value is r (+) x, with
-- x from outside the cycle, possibly wrapped as a - floor(a). Returns the
-- op, or "" when the cycle is not of that shape.
fn _scanOp(ctx Ctx, cy CycleInfo) String {
	if (cy.readers length != 1) { return ""; }
	let r = cy.readers[0];
	if (ctx.dweight[r] != 1) { return ""; }
	let v = ctx.dsrc[r];
	let acc = ctx _accumOp(cy, v, r);
	if (acc length > 0) {
		return cy.nodes length == 2 ? acc : "";
	}
	-- v = a - floor(a), a = r + x
	match (ctx.kind[v]) {
		binopK(op): {
			match (op) {
				sub: {
					let a = ctx.ins[v][0];
					let f = ctx.ins[v][1];
					let isFloorOfA = match (ctx.kind[f]) {
						unopK(u): (match (u) { floor: ctx.ins[f][0] == a; _: false; });
						_: false;
					};
					if (isFloorOfA && ctx _accumOp(cy, a, r) == "+" && cy.nodes length == 4) {
						return "+wrap";
					}
				}
				_: {}
			}
		}
		_: {}
	}
	""
}

-- Is n = r (op) x (either order) with x outside the cycle, for an
-- associative op? Returns the op name or "".
fn _accumOp(ctx Ctx, cy CycleInfo, n NIdx, r NIdx) String {
	match (ctx.kind[n]) {
		binopK(op): {
			let a = ctx.ins[n][0];
			let b = ctx.ins[n][1];
			let shape = (a == r && !(_inCycle(cy, b))) || (b == r && !(_inCycle(cy, a)));
			if (!shape) { return ""; }
			match (op) {
				add:    "+";
				max:    "max";
				min:    "min";
				bitXor: "xor";
				_:      "";
			}
		}
		_: ""
	}
}

fn recClassStr(ctx Ctx, c RecClass) String = match (c) {
	notRecurrence:      "vector";
	scan(op):           "scan(%^)" fmt(op);
	affineBlock(order): "affine-block order %^" fmt(order);
	affineScan:         "affine-scan";
	general(order, x):  (order > 0 ? "general (audio-rate coefficients, order %^) at %^ %^" fmt(order, ctx.serial[x], nodeStr(ctx, x))
	                               : "general (nonlinear) at %^ %^" fmt(ctx.serial[x], nodeStr(ctx, x)));
};

-- The block form of a first-order cycle (block-engine M7): its kind, or ""
-- when the cycle runs as a plain recurrence. A first-order cycle is one
-- state read of weight 1 on a delay kept as a single value (allocSize 1)
-- with no other reader, in the root graph, one channel throughout.
-- TZPL_FORMS=serial: every loop in its plain serial form (no block forms,
-- no channel- or sample-vector loops), the reference for the
-- equivalence-of-forms test (block engine plan M8).
fn serialForms() Bool = match (getEnv("TZPL_FORMS")) { some(v): v == "serial"; none: false; };

fn blockFormOf(ctx Ctx, cy CycleInfo, c RecClass) String {
	-- TZPL_STRICT_RECURRENCES=1: every recurrence in its own order (plain
	-- recurrences, exact against the serial code), for reference renders.
	let strict = match (getEnv("TZPL_STRICT_RECURRENCES")) { some(v): v == "1"; none: false; };
	if (strict || serialForms()) { return ""; }
	let kind = match (c) {
		scan(op):           (op == "+" || op == "+wrap") ? "scan" $ op : "";
		affineScan:         "affine";
		affineBlock(order): order == 1 ? "affine" : "statespace";
		_:                  "";
	};
	if (kind length == 0) { return ""; }
	-- Block-rate coefficients (any order): the state space, which checks per
	-- block whether its coefficient groups vary (codegen: then the first-order
	-- scan when firstOrderScanOK, else the plain recurrence).
	let ss = match (c) { affineBlock(_): ctx stateSpaceModel(cy).ok; _: false; };
	if (ss) { return "statespace"; }
	if (kind == "statespace") { return ""; }
	-- Scans and audio-rate first-order affine maps: the two-pass scan.
	ctx firstOrderScanOK(cy) ? kind : ""
}

-- Can a first-order cycle run as the two-pass block scan? Not when its
-- inputs read another delay: that delay's loop would share the region.
fn firstOrderScanOK(ctx Ctx, cy CycleInfo) Bool =
	ctx _firstOrderOK(cy) && !(ctx _inputsReadDelays(cy));

fn _firstOrderOK(ctx Ctx, cy CycleInfo) Bool {
	if (cy.readers length != 1) { return false; }
	let r = cy.readers[0];
	if (ctx.dweight[r] != 1) { return false; }
	for (x : cy.nodes) {
		if (ctx.graphOf[x] != 0 || ctx.chans[x] != 1) { return false; }
	}
	let d = match (ctx.kind[r]) { delayFixReadK(di, _): di; _: NONE; };
	if (d == NONE) { return false; }
	let di = ctx.delays[d];
	if (di.allocSize != 1 || di.fixReaders length != 1 || di.varReaders length != 0) { return false; }
	if (di.writer == NONE || ctx.chans[di.writer] > 1) { return false; }
	-- float state only (the scans and affine maps are float)
	let t = ctx.typ[ctx.dsrc[r]];
	if (!(t.0 == 4 || t.0 == 8)) { return false; }
	-- The two passes each evaluate the cycle's inputs; a random draw inlined
	-- into them would be drawn twice.
	for (x : cy.nodes) {
		for (i : ctx.ins[x]) {
			if (!(cy.nodes contains(i)) && ctx _inlinedDraw(i)) { return false; }
		}
	}
	true
}

-- Do the cycle's inlined outside inputs read a delay?
fn _inputsReadDelays(ctx Ctx, cy CycleInfo) Bool {
	for (x : cy.nodes) {
		for (i : ctx.ins[x]) {
			if (!(cy.nodes contains(i)) && ctx _inlinedReadsDelay(i)) { return true; }
		}
	}
	false
}

fn _inlinedReadsDelay(ctx Ctx, n NIdx) Bool {
	if (ctx isMaterialized(n) || ctx.nrate[n] != Rate.audio) { return false; }
	if (ctx readDelayOf(n) != NONE) { return true; }
	for (i : ctx.ins[n]) { if (ctx _inlinedReadsDelay(i)) { return true; } }
	false
}

-- Does the value of n, as inlined into its consumer (single consumer,
-- audio rate: not materialized), draw a random number?
fn _inlinedDraw(ctx Ctx, n NIdx) Bool {
	if (ctx.nrate[n] != Rate.audio || ctx.consumers[n] length > 1) { return false; }
	match (ctx.kind[n]) {
		urandK(_):  { return true; }
		birandK(_): { return true; }
		rand64K(_): { return true; }
		_: {}
	}
	for (i : ctx.ins[n]) { if (ctx _inlinedDraw(i)) { return true; } }
	false
}

-- Choose the block-form cycles: ctx.blockCycles.
fn selectBlockCycles(ctx Ctx) Void {
	let classes = ctx classifyRecurrences;
	ctx.blockCycles clear!;
	ctx.blockMembers clear!;
	var i = 0;
	while (i < ctx.cycles length) {
		let form = ctx blockFormOf(ctx.cycles[i], classes[i]);
		if (form length > 0) {
			ctx.blockCycles push!(i);
			ctx.blockMembers push!(ctx _blockMembers(ctx.cycles[i], form));
		}
		i = i + 1;
	}
}

-- The nodes whose trees make up a block cycle's loops: its own nodes and
-- the writers of its delays, and for a state space the inlined input
-- expressions and the input delays' writers too.
fn _blockMembers(ctx Ctx, cy CycleInfo, form String) [NIdx] {
	var out = cy.nodes copy;
	var delays [Int] = [];
	if (form == "statespace") {
		let m = ctx stateSpaceModel(cy);
		out append!(m.chain);
		delays = m.delays;
	} else {
		for (r : cy.readers) { let d = ctx readDelayOf(r); if (d != NONE && !(delays contains(d))) { delays push!(d); } }
	}
	for (d : delays) {
		let w = ctx.delays[d].writer;
		if (w != NONE && !(out contains(w))) { out push!(w); }
	}
	-- input ports read by them: a port's tree holds no code, but the loop it
	-- lands in would otherwise be an antecedent of the cycle's
	var ports [NIdx] = [];
	for (x : out) {
		for (i : ctx.ins[x]) {
			let isPort = match (ctx.kind[i]) { inletK(_, _, _): true; _: false; };
			if (isPort && !(out contains(i)) && !(ports contains(i))) { ports push!(i); }
		}
	}
	out append!(ports);
	out
}

-- Record each cycle's class and strategy for the analysis dump: "block"
-- with its form for a cycle the generator runs over the block,
-- "channel-vector" for a plain recurrence over 4 or more channels (4 to a
-- vector), "serial" for one over fewer.
fn annotateRecurrences(ctx Ctx) Void {
	let classes = ctx classifyRecurrences;
	var i = 0;
	while (i < ctx.cycles length) {
		let c = classes[i];
		let form = ctx blockFormOf(ctx.cycles[i], c);
		-- not in block form: 4 or more channels run the recurrence 4 channels to
		-- a vector, fewer one sample of one channel at a time
		let cc = ctx.chans[ctx.cycles[i].nodes[0]];
		var cv = cc >= 4 && cc % 4 == 0;
		for (x : ctx.cycles[i].nodes) { match (ctx.kind[x]) { vecK(_): { cv = false; } _: {} } }
		let strategy = match (c) {
			notRecurrence: "";
			_: form length > 0 ? "block (%^)" fmt(form) : (cv ? "channel-vector" : "serial");
		};
		ctx.cycles[i] = CycleInfo { ...ctx.cycles[i], cls: ctx recClassStr(c), strategy: strategy };
		i = i + 1;
	}
}

-- Block state space (affine, block-rate coefficients): the cycle as
-- s' = Ss s + Sx x, y = Ys s + Yx x over a state vector of delay taps.
--  - Each delay read by the cycle contributes its taps y[-1] .. y[-K], K its
--    largest read weight, as a shift register: a single value (K = 1) or an
--    inline ring of at least K samples. Every read of it is in the cycle.
--  - The cycle's outside audio inputs are linear expressions (+, -, * or /
--    by a lower-rate value, negation, float casts), inlined into it, over
--    materialized audio values (the inputs, read from block buffers) and
--    reads of input delays: delays outside the cycle whose every read feeds
--    the cycle this way and whose written value is again such an expression.
--    An input delay contributes its taps as well (a biquad's x[-1], x[-2] in
--    direct form I, ring's x z2).
--  - The outputs are the cycle values used outside it (other than by its
--    delays' writers).
-- Root graph, float states of one type; 1 to 8 states, 1 to 4 inputs and
-- outputs. Every cycle node and delay has the same channel count C (1 to 3),
-- and each channel runs as its own state space (inputs and inlined
-- expressions have C channels or 1, broadcast).
struct SSModel {
	ok Bool,
	delays [Int],      -- delay indices: the cycle's own, then input delays
	depth [Int],       -- taps per delay
	base [Int],        -- first state index per delay
	inputs [NIdx],     -- materialized audio values (block buffers)
	outputs [NIdx],    -- cycle values used outside
	chain [NIdx],      -- inlined outside nodes rebuilt in the step (incl. input delay reads)
	nstates Int,
	chans Int,         -- C: channels, each its own state space
}

fn _noModel() SSModel = SSModel { ok: false, delays: [Int](), depth: [Int](), base: [Int](),
	inputs: [NIdx](), outputs: [NIdx](), chain: [NIdx](), nstates: 0, chans: 0 };

fn readDelayOf(ctx Ctx, n NIdx) Int = match (ctx.kind[n]) { delayFixReadK(di, _): di; _: NONE; };

-- A delay usable as state: C channels, root graph, fixed reads only, of
-- weight 1..allocSize (1 for a single value); returns its depth or 0.
fn _delayDepth(ctx Ctx, d Int, t NumType, C Int) Int {
	let di = ctx.delays[d];
	if (di.varReaders length != 0 || di.writer == NONE || di.allocSize < 1) { return 0; }
	if (di.chans != C || ctx.chans[di.writer] != C || ctx.graphOf[di.writer] != 0) { return 0; }
	var k = 0;
	for (r : di.fixReaders) {
		if (ctx.chans[r] != C || ctx.graphOf[r] != 0) { return 0; }
		if (ctx.typ[ctx.dsrc[r]].0 != t.0) { return 0; }
		let w = ctx.dweight[r];
		if (w < 1 || w > di.allocSize) { return 0; }
		if (w > k) { k = w; }
	}
	k
}

-- Walk an outside audio value n feeding the model: collect inputs, chain
-- nodes and input delays. False if it is not linear over them.
fn _ssExplore(ctx Ctx, cy CycleInfo, n NIdx, m SSModel, inDelays [Int]) Bool {
	if (m.chain contains(n) || m.inputs contains(n)) { return true; }
	if (ctx.chans[n] != 1 && ctx.chans[n] != m.chans) { return false; }
	let d = ctx readDelayOf(n);
	if (d != NONE && !(ctx isMaterialized(n))) {
		m.chain push!(n);
		if (inDelays contains(d)) { return true; }
		inDelays push!(d);
		let src = ctx.dsrc[n];
		if (ctx.nrate[src] != Rate.audio) { return true; }
		return ctx _ssExplore(cy, src, m, inDelays);
	}
	if (ctx isMaterialized(n)) {
		m.inputs push!(n);
		return true;
	}
	let linear = match (ctx.kind[n]) {
		binopK(op): (match (op) { add: true; sub: true; mul: true; div: true; _: false; });
		unopK(op): (match (op) { neg: true; _: false; });
		castopK(t): t.0 == 4 || t.0 == 8;
		_: false;
	};
	if (!linear) { return false; }
	m.chain push!(n);
	var audioIns = 0;
	for (i : ctx.ins[n]) {
		if (ctx.nrate[i] == Rate.audio) {
			if (cy.nodes contains(i)) { return false; }
			audioIns = audioIns + 1;
			if (!(ctx _ssExplore(cy, i, m, inDelays))) { return false; }
		}
	}
	-- a product or quotient of two audio values is not linear in the inputs
	match (ctx.kind[n]) {
		binopK(op): { match (op) { mul: { if (audioIns > 1) { return false; } } div: { if (ctx.nrate[ctx.ins[n][1]] == Rate.audio) { return false; } } _: {} } }
		_: {}
	}
	true
}

-- The block state-space model of a cycle; ok is false when it doesn't fit.
fn stateSpaceModel(ctx Ctx, cy CycleInfo) SSModel {
	var m = _noModel();
	if (cy.readers length == 0) { return m; }
	let C = ctx.chans[cy.nodes[0]];
	-- Strategy (plan 4.2 step 6, the first entry of the cost table): with 4 or
	-- more channels the plain recurrence runs 4 channels to a vector, and
	-- independent vector chains hide its latency; measured faster than a state
	-- space per channel (block engine plan M8).
	if (C >= 4) { return m; }
	for (x : cy.nodes) { if (ctx.graphOf[x] != 0 || ctx.chans[x] != C) { return m; } }
	m = SSModel { ...m, chans: C };
	let t = ctx.typ[ctx.dsrc[cy.readers[0]]];
	if (!(t.0 == 4 || t.0 == 8)) { return m; }
	-- the cycle's own delays
	var own [Int] = [];
	for (r : cy.readers) {
		let d = ctx readDelayOf(r);
		if (d == NONE) { return m; }
		if (!(own contains(d))) { own push!(d); }
	}
	for (d : own) {
		for (r : ctx.delays[d].fixReaders) { if (!(cy.readers contains(r))) { return m; } }
	}
	-- the inputs
	var inDelays [Int] = [];
	for (x : cy.nodes) {
		for (i : ctx.ins[x]) {
			if (cy.nodes contains(i) || ctx.nrate[i] != Rate.audio) { continue; }
			if (!(ctx _ssExplore(cy, i, m, inDelays))) { return _noModel(); }
		}
	}
	-- every read of an input delay is in the chain, so nothing else needs its
	-- per-sample values; a chain node's one consumer is the cycle or the chain
	for (d : inDelays) {
		if (own contains(d)) { return _noModel(); }
		for (r : ctx.delays[d].fixReaders) { if (!(m.chain contains(r))) { return _noModel(); } }
	}
	for (c : m.chain) {
		for (u : ctx.consumers[c]) {
			let isWriter = match (ctx.kind[u]) { delayWriteK(_): true; _: false; };
			if (!(cy.nodes contains(u)) && !(m.chain contains(u)) && !isWriter) { return _noModel(); }
		}
	}
	-- the state layout
	var n = 0;
	for (d : own $ inDelays) {
		let k = ctx _delayDepth(d, t, C);
		if (k == 0) { return _noModel(); }
		m.delays push!(d);
		m.depth push!(k);
		m.base push!(n);
		n = n + k;
	}
	-- the outputs
	for (x : cy.nodes) {
		for (u : ctx.consumers[x]) {
			let isWriter = match (ctx.kind[u]) { delayWriteK(_): true; _: false; };
			if (!(cy.nodes contains(u)) && !isWriter && !(m.outputs contains(x))) { m.outputs push!(x); }
		}
	}
	if (n < 1 || n > 8 || m.inputs length < 1 || m.inputs length > 4
	    || m.outputs length < 1 || m.outputs length > 4) { return _noModel(); }
	SSModel { ...m, ok: true, nstates: n }
}

-- The model's input delays (outside the cycle).
fn ssInputDelays(ctx Ctx, cy CycleInfo, m SSModel) [Int] {
	var out [Int] = [];
	for (d : m.delays) {
		var mine = false;
		for (r : cy.readers) { if (ctx readDelayOf(r) == d) { mine = true; } }
		if (!mine) { out push!(d); }
	}
	out
}

fn isMaterialized(ctx Ctx, n NIdx) Bool =
	match (ctx.kind[n]) { inletK(_, _, _): true; _: !(ctx.cut[n] isNoneCut); };


-- The state each loop touches, by loop index: "d<delay>" for a delay it
-- reads or writes, "rng" for a random draw, "b<buffer>" for a buffer (root
-- loops, through enclosing control-flow or voicer nodes).
fn loopStateKeys(ctx Ctx) [[String]] {
	var keys [[String]] = [];
	for (li : ctx.loops) { keys push!([String]()); }
	var di = 0;
	while (di < ctx.delays length) {
		let d = ctx.delays[di];
		var members [NIdx] = d.fixReaders copy;
		members append!(d.varReaders);
		if (d.writer != NONE) { members push!(d.writer); }
		for (x : members) { ctx _addLoopKey(keys, x, "d%^" fmt(di)); }
		di = di + 1;
	}
	for (x : ctx.sorted) {
		match (ctx.kind[x]) {
			urandK(_): { ctx _addLoopKey(keys, x, "rng"); }
			birandK(_): { ctx _addLoopKey(keys, x, "rng"); }
			rand64K(_): { ctx _addLoopKey(keys, x, "rng"); }
			bufWriteK(b, _, _): { ctx _addLoopKey(keys, x, "b%^" fmt(b)); }
			bufFixReadK(b, _, _, _): { ctx _addLoopKey(keys, x, "b%^" fmt(b)); }
			bufVarReadK(b, _, _, _): { ctx _addLoopKey(keys, x, "b%^" fmt(b)); }
			_: {}
		}
	}
	keys
}

fn _addLoopKey(ctx Ctx, keys [[String]], x NIdx, k String) Void {
	var cur = x;
	while (cur != NONE && ctx.graphOf[cur] != 0) { cur = ctx _ownerOfGraph(ctx.graphOf[cur]); }
	if (cur == NONE) { return; }
	let t = ctx.treeOf[cur];
	if (t == NONE) { return; }
	let li = ctx.trees[t].loopOf;
	if (li == NONE || keys[li] contains(k)) { return; }
	keys[li] push!(k);
}

fn _ownerOfGraph(ctx Ctx, g Int) NIdx {
	for (x : ctx.sorted) {
		for (sub : ctx.subs[x]) { if (ctx.graphOf[sub] == g) { return x; } }
	}
	NONE
}
