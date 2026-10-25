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
fn blockFormOf(ctx Ctx, cy CycleInfo, c RecClass) String {
	-- TZPL_STRICT_RECURRENCES=1: every recurrence in its own order (plain
	-- recurrences, exact against the serial code), for reference renders.
	let strict = match (getEnv("TZPL_STRICT_RECURRENCES")) { some(v): v == "1"; none: false; };
	if (strict) { return ""; }
	let kind = match (c) {
		scan(op):           (op == "+" || op == "+wrap") ? "scan" $ op : "";
		affineScan:         "affine";
		affineBlock(order): order == 1 ? "affine" : "";
		_:                  "";
	};
	if (kind length == 0) { return ""; }
	if (cy.readers length != 1) { return ""; }
	let r = cy.readers[0];
	if (ctx.dweight[r] != 1) { return ""; }
	for (x : cy.nodes) {
		if (ctx.graphOf[x] != 0 || ctx.chans[x] != 1) { return ""; }
	}
	let d = match (ctx.kind[r]) { delayFixReadK(di, _): di; _: NONE; };
	if (d == NONE) { return ""; }
	let di = ctx.delays[d];
	if (di.allocSize != 1 || di.fixReaders length != 1 || di.varReaders length != 0) { return ""; }
	if (di.writer == NONE || ctx.chans[di.writer] > 1) { return ""; }
	-- float state only (the scans and affine maps are float)
	let t = ctx.typ[ctx.dsrc[r]];
	if (!(t.0 == 4 || t.0 == 8)) { return ""; }
	-- The two passes each evaluate the cycle's inputs; a random draw inlined
	-- into them would be drawn twice.
	for (x : cy.nodes) {
		for (i : ctx.ins[x]) {
			if (!(cy.nodes contains(i)) && ctx _inlinedDraw(i)) { return ""; }
		}
	}
	kind
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
	var i = 0;
	while (i < ctx.cycles length) {
		if (ctx blockFormOf(ctx.cycles[i], classes[i]) length > 0) { ctx.blockCycles push!(i); }
		i = i + 1;
	}
}

-- Record each cycle's class and strategy for the analysis dump: "block"
-- with its form for a cycle the generator runs over the block, "serial" for
-- a plain per-sample recurrence.
fn annotateRecurrences(ctx Ctx) Void {
	let classes = ctx classifyRecurrences;
	var i = 0;
	while (i < ctx.cycles length) {
		let c = classes[i];
		let form = ctx blockFormOf(ctx.cycles[i], c);
		let strategy = match (c) {
			notRecurrence: "";
			_: form length > 0 ? "block (%^)" fmt(form) : "serial";
		};
		ctx.cycles[i] = CycleInfo { ...ctx.cycles[i], cls: ctx recClassStr(c), strategy: strategy };
		i = i + 1;
	}
}
