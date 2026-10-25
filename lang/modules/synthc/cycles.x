-- synthc/cycles.x
-- The signal flow graph with delay-weighted edges (block-engine plan §4.2).
--
-- A delay read d(k) is an edge from the value written to the delay, with
-- delay weight k. With those edges the graph has real cycles: feedback loops
-- are its strongly connected components (SCCs). A cycle's weight is the sum
-- of the delay weights around it: weight >= 4 (a vector of samples) can run a
-- vector at a time, weight 1-3 is a recurrence the compiler must run in a
-- serial loop or find a parallel form for (§4.7). A cycle of weight 0 is a
-- zero-delay feedback loop, which has no value: an error.
--
-- A variable read d.vread(i) gets weight max(1, the static lower bound of
-- its earliest sample), from interval analysis of i (constants, min/max,
-- clamps, arithmetic); 1 when nothing is known.
--
-- The topological sort (passes.x) never sees these edges -- a reader has no
-- input edge to its writer -- so it orders the graph with the delay edges cut.

import synthdef.*;
import synthc.ir.*;

---------------------------------------------------------------------------
-- Interval analysis (for variable delay read indices)

struct Interval {
	lo Float,
	hi Float,
}

fn _unknown() Interval = Interval { lo: 0.0 - 1.0e300, hi: 1.0e300 };
fn _point(x Float) Interval = Interval { lo: x, hi: x };

fn _hull(a Interval, b Interval) Interval =
	Interval { lo: min(a.lo, b.lo), hi: max(a.hi, b.hi) };

fn _isBounded(a Interval) Bool = a.lo > 0.0 - 1.0e299 && a.hi < 1.0e299;

fn _mulI(a Interval, b Interval) Interval {
	if (!(a _isBounded) || !(b _isBounded)) { return _unknown(); }
	let p1 = a.lo * b.lo;
	let p2 = a.lo * b.hi;
	let p3 = a.hi * b.lo;
	let p4 = a.hi * b.hi;
	Interval { lo: min(min(p1, p2), min(p3, p4)), hi: max(max(p1, p2), max(p3, p4)) }
}

fn _constInterval(v ConstVal) Interval {
	let n = v constSize;
	if (n == 0) { return _unknown(); }
	var r = _point(constF64At(v, 0));
	var i = 1;
	while (i < n) { r = _hull(r, _point(constF64At(v, i))); i = i + 1; }
	r
}

-- The range of node n's values, memoized in `memo` (NONE-filled lo marks
-- "not computed" via the parallel `done` array).
fn intervalOf(ctx Ctx, n NIdx, memo [Interval], done [Bool]) Interval {
	if (done[n]) { return memo[n]; }
	done[n] = true;            -- cycles through delays read as unknown
	memo[n] = _unknown();
	let ins = ctx.ins[n];
	let r = match (ctx.kind[n]) {
		constant(v, _): _constInterval(v);
		sampleRate:     Interval { lo: 1.0, hi: 1.0e300 };
		sampleDur:      Interval { lo: 0.0, hi: 1.0 };
		castopK(_):     intervalOf(ctx, ins[0], memo, done);
		unopK(op): match (op) {
			neg: { let a = intervalOf(ctx, ins[0], memo, done); Interval { lo: 0.0 - a.hi, hi: 0.0 - a.lo } }
			abs: {
				let a = intervalOf(ctx, ins[0], memo, done);
				if (a.lo >= 0.0) { a }
				else if (a.hi <= 0.0) { Interval { lo: 0.0 - a.hi, hi: 0.0 - a.lo } }
				else { Interval { lo: 0.0, hi: max(0.0 - a.lo, a.hi) } }
			}
			floor: { let a = intervalOf(ctx, ins[0], memo, done); Interval { lo: a.lo floor, hi: a.hi floor } }
			ceil:  { let a = intervalOf(ctx, ins[0], memo, done); Interval { lo: a.lo ceil, hi: a.hi ceil } }
			round: { let a = intervalOf(ctx, ins[0], memo, done); Interval { lo: a.lo round, hi: a.hi round } }
			trunc: { let a = intervalOf(ctx, ins[0], memo, done); Interval { lo: a.lo floor, hi: a.hi ceil } }
			_: _unknown();
		};
		binopK(op): {
			let a = intervalOf(ctx, ins[0], memo, done);
			let b = intervalOf(ctx, ins[1], memo, done);
			match (op) {
				add: Interval { lo: a.lo + b.lo, hi: a.hi + b.hi };
				sub: Interval { lo: a.lo - b.hi, hi: a.hi - b.lo };
				mul: _mulI(a, b);
				min: Interval { lo: min(a.lo, b.lo), hi: min(a.hi, b.hi) };
				max: Interval { lo: max(a.lo, b.lo), hi: max(a.hi, b.hi) };
				_:   _unknown();
			}
		}
		selectK: {
			var r = intervalOf(ctx, ins[1], memo, done);
			var i = 2;
			while (i < ins length) { r = _hull(r, intervalOf(ctx, ins[i], memo, done)); i = i + 1; }
			r
		}
		_: _unknown();
	};
	memo[n] = r;
	r
}

-- Samples newer than the read position an interpolator touches: its weight
-- is reduced by this many (cubic reads one sample after the position, the
-- 8-point kernels three).
fn _interpLookahead(i Interpolation) Int = match (i) {
	none:     0;
	linear:   0;
	cubic:    1;
	lagrange: 3;
	sinc:     3;
};

---------------------------------------------------------------------------
-- Delay edges

-- For every delay reader: the node whose value the delay's writer stores
-- (ctx.dsrc) and the edge's weight (ctx.dweight). Readers of a delay with no
-- writer read only its init values and get no edge.
fn findDelayEdges(ctx Ctx) Void {
	let n = ctx numNodes;
	ctx.dsrc clear!;
	ctx.dweight clear!;
	var i = 0;
	while (i < n) { ctx.dsrc push!(NONE); ctx.dweight push!(0); i = i + 1; }
	var memo [Interval] = [];
	var done [Bool] = [];
	i = 0;
	while (i < n) { memo push!(_unknown()); done push!(false); i = i + 1; }

	for (d : ctx.delays) {
		if (d.writer == NONE) { continue; }
		let v = ctx.ins[d.writer][0];
		for (r : d.fixReaders) {
			match (ctx.kind[r]) {
				delayFixReadK(_, k): { ctx.dsrc[r] = v; ctx.dweight[r] = k; }
				_: {}
			}
		}
		for (r : d.varReaders) {
			match (ctx.kind[r]) {
				delayVarReadK(_, interp): {
					let iv = intervalOf(ctx, ctx.ins[r][0], memo, done);
					var w = 1;
					if (iv.lo > 0.0 - 1.0e299 && iv.lo < 1.0e9) {
						w = max(1, iv.lo floor toInt - _interpLookahead(interp));
					}
					ctx.dsrc[r] = v;
					ctx.dweight[r] = w;
				}
				_: {}
			}
		}
	}
}

---------------------------------------------------------------------------
-- Strongly connected components

-- Predecessors of n in the signal flow graph: its inputs, its branch/body
-- phis (control flow), and the written value behind a delay read.
fn _preds(ctx Ctx, n NIdx) [Int] {
	var p = ctx.ins[n] $ ctx.subs[n];
	if (ctx.dsrc[n] != NONE) { p push!(ctx.dsrc[n]); }
	p
}

-- Weight of the edge p -> n (0 unless it is n's delay edge).
fn _edgeWeight(ctx Ctx, p NIdx, n NIdx) Int =
	(ctx.dsrc[n] == p && ctx.dsrc[n] != NONE) ? ctx.dweight[n] : 0;

-- Tarjan's algorithm, iterative, over the predecessor relation (the SCCs of
-- the reversed graph are the same). Returns the components with more than
-- one node or a self edge.
fn _sccs(ctx Ctx) [[Int]] {
	let n = ctx numNodes;
	var index [Int] = (0 - 1) repeat(n);
	var low [Int] = 0 repeat(n);
	var onStack [Bool] = false repeat(n);
	var stack [Int] = [];
	var out [[Int]] = [];
	var counter = 0;

	var fNode [Int] = [];
	var fKids [[Int]] = [];
	var fNext [Int] = [];

	for (root : ctx.sorted) {
		if (index[root] >= 0) { continue; }
		index[root] = counter; low[root] = counter; counter = counter + 1;
		stack push!(root); onStack[root] = true;
		fNode push!(root); fKids push!(_preds(ctx, root)); fNext push!(0);
		while (fNode length > 0) {
			let top = fNode length - 1;
			let v = fNode[top];
			let kids = fKids[top];
			let c = fNext[top];
			if (c < kids length) {
				fNext[top] = c + 1;
				let w = kids[c];
				if (index[w] < 0) {
					index[w] = counter; low[w] = counter; counter = counter + 1;
					stack push!(w); onStack[w] = true;
					fNode push!(w); fKids push!(_preds(ctx, w)); fNext push!(0);
				} else if (onStack[w]) {
					low[v] = min(low[v], index[w]);
				}
			} else {
				if (low[v] == index[v]) {
					var comp [Int] = [];
					var done = false;
					while (!done) {
						let w = stack pop!;
						onStack[w] = false;
						comp push!(w);
						done = w == v;
					}
					let selfEdge = comp length == 1 && ctx.dsrc[v] == v;
					if (comp length > 1 || selfEdge) { out push!(comp); }
				}
				fNode pop!; fKids pop!; fNext pop!;
				if (fNode length > 0) {
					let parent = fNode[fNode length - 1];
					low[parent] = min(low[parent], low[v]);
				}
			}
		}
	}
	out
}

---------------------------------------------------------------------------
-- Cycles

-- Shortest path weight from `from` to `to` along edges inside the component
-- (inComp), Dijkstra by array scan: components are small.
fn _shortest(ctx Ctx, comp [Int], inComp [Bool], from NIdx, to NIdx) Int {
	let big = 1000000000;
	var dist [Int: Int] = [:];
	for (x : comp) { dist[x] = big; }
	dist[from] = 0;
	var settled [Int: Bool] = [:];
	var k = 0;
	while (k < comp length) {
		var best = NONE;
		var bestD = big;
		for (x : comp) {
			let dx = get(dist, x, big);
			if (!get(settled, x, false) && dx < bestD) { best = x; bestD = dx; }
		}
		if (best == NONE) { break; }
		settled[best] = true;
		if (best == to) { return bestD; }
		-- successors of best inside the component: nodes that list it as a pred
		for (y : ctx.consumers[best] $ _subTargets(ctx, best) $ _delayReadersOf(ctx, best)) {
			if (y < inComp length && inComp[y]) {
				let nd = bestD + _edgeWeight(ctx, best, y);
				if (nd < get(dist, y, big)) { dist[y] = nd; }
			}
		}
		k = k + 1;
	}
	big
}

-- A phi's control-flow target (the phi is one of the target's subs).
fn _subTargets(ctx Ctx, n NIdx) [Int] = match (ctx.kind[n]) {
	phiK(t): t != NONE ? [t] : [Int]();
	_:       [Int]();
};

-- The delay readers whose delay edge starts at n.
fn _delayReadersOf(ctx Ctx, n NIdx) [Int] {
	var out [Int] = [];
	for (d : ctx.delays) {
		if (d.writer != NONE && ctx.ins[d.writer][0] == n) {
			for (r : d.fixReaders) { out push!(r); }
			for (r : d.varReaders) { out push!(r); }
		}
	}
	out
}

-- Is there a cycle of weight-0 edges inside the component? (Data edges are
-- acyclic, so only a 0-weight delay edge -- which the front end forbids --
-- could close one; checked structurally anyway.)
fn _hasZeroCycle(ctx Ctx, comp [Int], inComp [Bool]) Bool {
	for (r : comp) {
		if (ctx.dsrc[r] != NONE && ctx.dweight[r] == 0 && inComp[ctx.dsrc[r]]) {
			if (_shortest(ctx, comp, inComp, r, ctx.dsrc[r]) == 0) { return true; }
		}
	}
	false
}

fn findCycles(ctx Ctx) Void {
	ctx findDelayEdges;
	ctx.cycles clear!;
	for (comp0 : _sccs(ctx)) {
		var inComp [Bool] = false repeat(ctx numNodes);
		for (x : comp0) { inComp[x] = true; }
		-- In topological (sorted) order, for a stable dump.
		var comp [Int] = [];
		for (x : ctx.sorted) { if (inComp[x]) { comp push!(x); } }
		var readers [Int] = [];
		var minW = 1000000000;
		for (r : comp) {
			let v = ctx.dsrc[r];
			if (v != NONE && inComp[v]) {
				readers push!(r);
				let back = _shortest(ctx, comp, inComp, r, v);
				minW = min(minW, ctx.dweight[r] + back);
			}
		}
		if (_hasZeroCycle(ctx, comp, inComp)) {
			var serials [Int] = [];
			for (x : comp) { serials push!(ctx.serial[x]); }
			ctx.errors push!("zero-delay feedback cycle through %^" fmt(serials));
		}
		ctx.cycles push!(CycleInfo { nodes: comp, readers: readers, minWeight: minW,
		                             cls: "", strategy: "" });
	}
}
