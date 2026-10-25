-- synthc/passes.x
-- Graph analysis passes for the Tzopilotl-hosted synthdef compiler.
-- Each pass mirrors the same-named method of synthdef::Synth
-- (synthdef-compiler/src/synthdef_synth.cpp).
--
-- M0: topologicalSortExprs, collectConsumers.
-- All traversals are iterative (explicit stacks) to keep recursion depth
-- independent of graph depth.

import synthdef.*;
import synthc.ir.*;
import synthc.cycles.*;
import synthc.paramgroups.*;
import synthc.types.*;

---------------------------------------------------------------------------
-- topologicalSortExprs (synthdef_synth.cpp:135)
--
-- DFS from each sink. A node is marked visited on entry (before its inputs
-- are traversed) and appended to sorted after its inputs -- and, for delay
-- nodes, after the delay buffer's initters/fixReaders/varReaders/maxDelay
-- have been traversed. Replicated exactly, including the early marking
-- (which determines the order when delays create traversal re-entry).

fn _delayIdxOfKind(k NodeKind) Int = match (k) {
	maxDelayK(d):         d;
	delayInitK(d, _):     d;
	delayFixReadK(d, _):  d;
	delayVarReadK(d, _):  d;
	delayWriteK(d):       d;
	_:                    NONE;
};

-- Traversal children of a node: inputs first, then control-flow subgraph roots
-- (mirrors the C++ visit(): inputs, then get_subgraph(i)). subs is empty for
-- ordinary nodes, so this is just `ins` for them.
fn _topoKids(ctx Ctx, n NIdx) [Int] = ctx.ins[n] $ ctx.subs[n];

-- visitDelay traversal order: initters, fixReaders, varReaders, maxDelay.
fn _delayMembers(ctx Ctx, d Int) [Int] {
	var out [Int] = [];
	out append!(ctx.delays[d].initters)
	    append!(ctx.delays[d].fixReaders)
	    append!(ctx.delays[d].varReaders);
	if (ctx.delays[d].maxDelay != NONE) { out push!(ctx.delays[d].maxDelay); }
	out
}

---------------------------------------------------------------------------
-- mergeDelays -- Faust-style delay-line sharing.
--
-- The C++ keys the merge on the writer NODE: DelayWrite::equals_ compares only
-- delayBuf (the buffer object), so each delay's writer is unique to it and the
-- pass is a no-op by construction -- two distinct delays can never share a key.
-- The intent was clearly to fold delays fed identical inputs, but keying on the
-- buffer defeats it.
--
-- synthc (the production compiler) instead keys on the WRITTEN SIGNAL, the way
-- Faust derives a delay line from the (maximally shared) signal being delayed
-- rather than from a user-allocated buffer object. Two delays merge when they
-- share a graph, an init set, a max-delay bound, AND the same hash-consed written-
-- signal node. Such delays hold bit-identical data at every sample (both written
-- the same value each frame, both advance their write head in lockstep, both
-- initialised identically), so they collapse to one ring buffer with multiple read
-- taps, sized by calcDelayLengths to the max read offset across the merged taps.
--
-- A feedback delay's written value references its own read (a per-buffer node), so
-- two feedback delays have distinct written-signal nodes and correctly never merge.
-- This diverges from the C++ oracle (which keeps both buffers); the result is
-- validated by render-equivalence -- a merged synth renders bit-identically to its
-- two-buffer C++ build -- rather than by byte-match.

-- Reader dedup signature: fixed reads key on their sample offset; var reads key
-- on interpolation + index-signal node (mirrors r->in0() in the C++).
fn _readerSig(ctx Ctx, r NIdx) String = match (ctx.kind[r]) {
	delayFixReadK(_, k):   "F" $ k toString;
	delayVarReadK(_, ip):  "V" $ ip ordinal toString $ ":"
		$ ((ctx.ins[r] length > 0) ? ctx.ins[r][0] toString : "-");
	_:                     "?";
};

-- Canonical signature for a delay-init VALUE node. A constant init folds by its
-- value (so two `init(_, 0.0)` written from distinct, un-hash-consed 0.0 literals
-- still match -- the Faust intent is "same initial content", not "same node"); a
-- non-constant init value falls back to node identity (it must be the literally
-- same signal to guarantee identical content). This is deliberately more aggressive
-- than the C++ init compare, which keys on node identity (in0().get()).
fn _initValSig(ctx Ctx, valNode Int) String = match (ctx.kind[valNode]) {
	constant(v, _): match (v) {
		ints(a):   "CI" $ a toString;
		floats(a): "CF" $ a toString;
	};
	_: "N" $ valNode toString;
};

-- Order-independent merge key, keyed on the written SIGNAL (Faust-style). Two
-- delays share a key iff same graph + same written-signal node + same max-delay
-- bound + same init set (offset + init VALUE). The written-signal node is the
-- writer's value input (ctx.ins[writer][0]); the signal graph is hash-consed at
-- import, so equal signals are the same node -- structural identity is pointer
-- identity here. (A constant written value would not fold by value, but writing a
-- bare constant into a delay is degenerate; the common foldable case is the init.)
fn _delayMergeKey(ctx Ctx, d Int) String {
	let info = ctx.delays[d];
	let wsig = (info.writer != NONE && ctx.ins[info.writer] length > 0)
		? ctx.ins[info.writer][0] : NONE;
	-- the writer's graph (passes can't reach codegen's _delayGraph; writer is set
	-- for every real delay, so graphOf[writer] is the delay's graph).
	let graph = info.writer != NONE ? ctx.graphOf[info.writer] : 0;
	var its [String] = [];
	for (n : info.initters) {
		let off = match (ctx.kind[n]) { delayInitK(_, o): o; _: 0; };
		let vsig = (ctx.ins[n] length > 0) ? ctx _initValSig(ctx.ins[n][0]) : "-";
		its push!(off toString $ ":" $ vsig);
	}
	"G" $ graph toString $ "|S" $ wsig toString $ "|M" $ info.maxDelay toString
		$ "|" $ (its sort join(","))
}

fn _repointReaderKind(k NodeKind, primary Int) NodeKind = match (k) {
	delayFixReadK(_, s):  NodeKind.delayFixReadK(primary, s);
	delayVarReadK(_, ip): NodeKind.delayVarReadK(primary, ip);
	_:                    k;
};

-- Replace every reference to oldN in any node's input list with newN. Runs
-- before topological sort / consumer collection, so all nodes are scanned.
fn _redirectIns(ctx Ctx, oldN NIdx, newN NIdx) Void {
	var i = 0;
	while (i < ctx numNodes) {
		let ins = ctx.ins[i];
		var j = 0;
		while (j < ins length) { if (ins[j] == oldN) { ins[j] = newN; } j = j + 1; }
		i = i + 1;
	}
}

fn _findReaderWithSig(ctx Ctx, lst [NIdx], sig String) Int {
	var i = 0;
	while (i < lst length) {
		if (ctx _readerSig(lst[i]) == sig) { return lst[i]; }
		i = i + 1;
	}
	NONE
}

-- Merge `other`'s readers (fixed or var) into `primary`: a reader whose read
-- signature is new is moved (its delay index repointed); a duplicate read is
-- collapsed by redirecting its consumers to the primary's existing reader.
fn _mergeReaders(ctx Ctx, primary Int, other Int, isVar Bool) Void {
	let plist = isVar ? ctx.delays[primary].varReaders : ctx.delays[primary].fixReaders;
	let olist = isVar ? ctx.delays[other].varReaders : ctx.delays[other].fixReaders;
	let readers = olist copy;
	for (r : readers) {
		let dup = ctx _findReaderWithSig(plist, ctx _readerSig(r));
		if (dup == NONE) {
			ctx.kind[r] = _repointReaderKind(ctx.kind[r], primary);
			plist push!(r);
		} else {
			ctx _redirectIns(r, dup);
		}
	}
}

-- Merge a group of delays (all sharing a merge key) into grp[0]. Returns true if
-- any delay was removed. Fires whenever two delays are fed the same signal --
-- e.g. `p eoc` and an explicit `z1(p)` both create a 1-sample delay of p.
fn _mergeDelayGroup(ctx Ctx, grp [Int], removed [Bool]) Bool {
	let primary = grp[0];
	var changed = false;
	var gi = 1;
	while (gi < grp length) {
		let other = grp[gi];
		ctx _mergeReaders(primary, other, false);
		ctx _mergeReaders(primary, other, true);
		-- The non-primary writer is removed: drop it as a sink root so it (and
		-- the now-orphaned non-primary delay) drop out of the sort.
		_removeFromList(ctx.sinks, ctx.delays[other].writer);
		removed[other] = true;
		changed = true;
		gi = gi + 1;
	}
	changed
}

fn _remapDelayKind(k NodeKind, remap [Int]) NodeKind = match (k) {
	maxDelayK(d):         NodeKind.maxDelayK(remap[d]);
	delayInitK(d, o):     NodeKind.delayInitK(remap[d], o);
	delayFixReadK(d, s):  NodeKind.delayFixReadK(remap[d], s);
	delayVarReadK(d, ip): NodeKind.delayVarReadK(remap[d], ip);
	delayWriteK(d):       NodeKind.delayWriteK(remap[d]);
	_:                    k;
};

-- Drop the merged-away delay records and renumber the surviving delay indices in
-- every node kind. Serials are preserved (codegen names buffers by serial).
fn _compactDelays(ctx Ctx, removed [Bool]) Void {
	let nd = removed length;
	var remap [Int] = NONE repeat(nd);
	var newDelays [DelayInfo] = [];
	var ni = 0;
	var d = 0;
	while (d < nd) {
		if (!removed[d]) { remap[d] = ni; newDelays push!(ctx.delays[d]); ni = ni + 1; }
		d = d + 1;
	}
	ctx.delays clear! append!(newDelays);
	var i = 0;
	while (i < ctx numNodes) {
		ctx.kind[i] = _remapDelayKind(ctx.kind[i], remap);
		i = i + 1;
	}
}

fn mergeDelays(ctx Ctx) Void {
	let nd = ctx.delays length;
	if (nd < 2) { return; }
	var groups [String: [Int]] = [:];
	var order [String] = [];
	var d = 0;
	while (d < nd) {
		let key = ctx _delayMergeKey(d);
		match (groups[key]) {
			some(g): { g push!(d); }
			none:    { var g [Int] = [d]; groups[key] = g; order push!(key); }
		}
		d = d + 1;
	}
	var removed [Bool] = false repeat(nd);
	var anyRemoved = false;
	for (key : order) {
		match (groups[key]) {
			some(grp): { if (grp length > 1 && ctx _mergeDelayGroup(grp, removed)) { anyRemoved = true; } }
			none:      {}
		}
	}
	if (anyRemoved) { ctx _compactDelays(removed); }
}

fn topologicalSortExprs(ctx Ctx) Void {
	var visited [Bool] = false repeat(ctx numNodes);
	var visitedDelay [Bool] = false repeat(ctx.delays length);

	-- Explicit stack frames (parallel arrays):
	--   fIsDelay: node frame or delay frame
	--   fIdx:     node index / delay index
	--   fChild:   next child to traverse
	--   fKids:    the frame's child list
	var fIsDelay [Bool] = [];
	var fIdx [Int] = [];
	var fChild [Int] = [];
	var fKids [[Int]] = [];

	for (root : ctx.sinks) {
		if (visited[root]) { continue; }
		visited[root] = true;
		fIsDelay push!(false);
		fIdx push!(root);
		fChild push!(0);
		fKids push!(ctx _topoKids(root));

		while (fIsDelay length > 0) {
			let top = fIsDelay length - 1;
			let kids = fKids[top];
			let c = fChild[top];
			if (c < kids length) {
				fChild[top] = c + 1;
				let child = kids[c];
				if (!visited[child]) {
					visited[child] = true;
					fIsDelay push!(false);
					fIdx push!(child);
					fChild push!(0);
					fKids push!(ctx _topoKids(child));
				}
			} else if (!fIsDelay[top]) {
				let n = fIdx[top];
				let d = ctx.kind[n] _delayIdxOfKind;
				if (c == kids length && d != NONE && !visitedDelay[d]) {
					-- enter visitDelay once, then come back to finish n
					visitedDelay[d] = true;
					fChild[top] = c + 1;
					fIsDelay push!(true);
					fIdx push!(d);
					fChild push!(0);
					fKids push!(_delayMembers(ctx, d));
				} else {
					ctx.sorted push!(n);
					fIsDelay pop!;
					fIdx pop!;
					fChild pop!;
					fKids pop!;
				}
			} else {
				-- delay frame finished (delay bufs are not exprs; not sorted)
				fIsDelay pop!;
				fIdx pop!;
				fChild pop!;
				fKids pop!;
			}
		}
	}
}

---------------------------------------------------------------------------
-- collectConsumers (synthdef_synth.cpp:179)

fn collectConsumers(ctx Ctx) Void {
	for (n : ctx.sorted) {
		for (i : ctx.ins[n]) {
			ctx.consumers[i] push!(n);
		}
	}
}

---------------------------------------------------------------------------
-- calcDelayLengths (synthdef_synth.cpp:187)
--
-- Determine each delay buffer's allocation size. For M1 (no maxDelayTime
-- bound), the size comes from the largest fixed delay among initters/readers.
-- The C++ also allocates a maxDelay Constant node here, but it is created
-- after the topological sort so it never appears in the dump; we only compute
-- allocSize/maxOverread, which codegen needs later.

fn calcDelayLengths(ctx Ctx) Void {
	var di = 0;
	while (di < ctx.delays length) {
		let d = ctx.delays[di];
		var maxOverread = 0;
		for (rd : d.varReaders) {
			match (ctx.kind[rd]) {
				delayVarReadK(_, interp): { maxOverread = max(maxOverread, interp interpOverread); }
				_: {}
			}
		}
		var maxFixed = 0;
		for (u : d.initters) {
			match (ctx.kind[u]) { delayInitK(_, off): { maxFixed = max(maxFixed, off); } _: {} }
		}
		for (u : d.fixReaders) {
			match (ctx.kind[u]) { delayFixReadK(_, k): { maxFixed = max(maxFixed, k); } _: {} }
		}
		let hasMax = d.maxDelay != NONE;
		if (!hasMax && maxFixed == 0) {
			ctx.errors push!("Delay line %^ has no specified bound." fmt(d.serial));
		}
		let headroom = d.varReaders length == 0 ? 0 : max(1, maxOverread);
		-- For a fixed-bound delay the alloc size is bit_ceil(maxFixed + headroom).
		-- A maxDelayTime bound is resolved during codegen (runtime alloc); here
		-- we record the fixed-size case.
		let bound = hasMax ? maxFixed : maxFixed;
		let allocSize = hasMax ? 0 : (bound + headroom) max(1) bitCeil;
		-- The C++ creates a maxDelay Constant for any fixed-bound delay, so a
		-- max bound exists whenever the delay was bounded at all.
		let hasMaxBound = hasMax || maxFixed > 0;
		ctx.delays[di] = DelayInfo { ...d, maxOverread: maxOverread, allocSize: allocSize, hasMaxBound: hasMaxBound };
		di = di + 1;
	}
}

---------------------------------------------------------------------------
-- shapeInference (synthdef_synth.cpp:450)
--
-- Worklist fixpoint: recompute each node's channel count; when it changes,
-- re-queue consumers (and, for delay writers, the buffer's members once the
-- buffer channel count grows).

fn shapeInference(ctx Ctx) Void {
	let wl = newWorkList(ctx numNodes);
	for (n : ctx.sorted) { wl wlPush(n); }
	while (!(wl wlEmpty)) {
		let n = wl wlPop;
		let sh0 = ctx.shape[n];
		let sh = calcShapeOf(ctx, n);
		ctx.shape[n] = sh;
		ctx.chans[n] = sh chans;
		if (sh != sh0) {
			for (c : ctx.consumers[n]) { wl wlPush(c); }
			-- Control-flow shape edges aren't `consumers` edges: a phi feeds its
			-- target, and a control-flow node feeds its branch/body phis (subs).
			match (ctx.kind[n]) {
				phiK(t):    { if (t != NONE) { wl wlPush(t); } }
				ifK:        { for (s : ctx.subs[n]) { wl wlPush(s); } }
				switchK(_): { for (s : ctx.subs[n]) { wl wlPush(s); } }
				forK:       { for (s : ctx.subs[n]) { wl wlPush(s); } }
				voicerK(_): { for (s : ctx.subs[n]) { wl wlPush(s); } }
				spectralChainK(_, _): { for (s : ctx.subs[n]) { wl wlPush(s); } }
				_: {}
			}
		}
		-- A delay's shape covers its writer AND its init values (broadcast).
		-- Widen-only keeps the fixpoint monotone: a pure at/put feedback cycle
		-- has no shape source other than the init, and the writer pass must not
		-- shrink it back.
		match (ctx.kind[n]) {
			delayWriteK(d): { _widenDelay(ctx, d, sh, wl); }
			delayInitK(d, _): { _widenDelay(ctx, d, ctx.shape[ctx.ins[n][0]], wl); }
			_: {}
		}
	}
	ctx _settleUnsetShapes;
}

-- A value computed only from loop variables has no shape of its own after
-- inference: it is a scalar. (The loop variable itself and a delay init
-- stay unset.)
fn _settleUnsetShapes(ctx Ctx) Void {
	for (n : ctx.sorted) {
		if (ctx.shape[n] chans != 0) { continue; }
		match (ctx.kind[n]) {
			varK(_): {}
			delayInitK(_, _): {}
			_: { ctx.shape[n] = scalarShape(); ctx.chans[n] = 1; }
		}
	}
}

fn _widenDelay(ctx Ctx, d Int, sh Shape, wl WorkList) Void {
	let old = ctx.delays[d].shape;
	let w = broadcast(old, sh);
	if (w == old) { return; }
	ctx.delays[d] = DelayInfo { ...ctx.delays[d], shape: w, chans: w chans };
	for (u : ctx.delays[d].initters) { wl wlPush(u); }
	for (u : ctx.delays[d].fixReaders) { wl wlPush(u); }
	for (u : ctx.delays[d].varReaders) { wl wlPush(u); }
	if (ctx.delays[d].writer != NONE) { wl wlPush(ctx.delays[d].writer); }
}

---------------------------------------------------------------------------
-- typeInference (synthdef_synth.cpp:491)

fn typeInference(ctx Ctx) Void {
	let wl = newWorkList(ctx numNodes);
	for (n : ctx.sorted) { wl wlPush(n); }
	ctx drainTypes(wl);
}

---------------------------------------------------------------------------
-- Widen on conflict (mirrors Synth::typeInferenceWithWidening /
-- insertWideningCasts in synthdef_synth.cpp, step for step).
--
-- Plain unification first: an operation and its operands get one type, and
-- every graph that infers today takes that path unchanged. Only when it has no
-- solution (an f64 signal meeting an f32 one) is inference re-run in widening
-- mode -- operands no wider than their operation -- to find the operands that
-- need a cast; those get an explicit cast and inference runs again.

struct _TypeSnap {
	typ [NumType],
	cmpTyp [NumType],
	delayTyp [NumType],
	nErrors Int,
}

fn _snapTypes(ctx Ctx) _TypeSnap {
	var dt [NumType] = [];
	for (d : ctx.delays) { dt push!(d.typ); }
	_TypeSnap { typ: ctx.typ copy, cmpTyp: ctx.cmpTyp copy, delayTyp: dt,
	            nErrors: ctx.errors length }
}

-- Restore the snapshot's nodes (nodes added since keep their own types) and
-- drop errors reported since.
fn _restoreTypes(ctx Ctx, s _TypeSnap) Void {
	var i = 0;
	while (i < s.typ length) { ctx.typ[i] = s.typ[i]; ctx.cmpTyp[i] = s.cmpTyp[i]; i = i + 1; }
	var di = 0;
	while (di < s.delayTyp length) {
		ctx.delays[di] = DelayInfo { ...ctx.delays[di], typ: s.delayTyp[di] };
		di = di + 1;
	}
	while (ctx.errors length > s.nErrors) { ctx.errors pop!; }
}

-- An empty type left on a live non-constant node (what
-- setNonConcreteTypesToDefault rejects).
fn _leftEmptyType(ctx Ctx) Bool {
	for (n : ctx.sorted) {
		if (ctx.typ[n] isEmpty && ctx.consumers[n] length > 0
				&& !(ctx.kind[n] isConstantKind)) { return true; }
	}
	false
}

-- Same preference as setNonConcreteTypesToDefault.
fn _preferredType(t NumType) NumType {
	if ((t & FLOAT32) notEmpty) { return FLOAT32; }
	if ((t & FLOAT64) notEmpty) { return FLOAT64; }
	if ((t & INT32) notEmpty) { return INT32; }
	if ((t & INT64) notEmpty) { return INT64; }
	t
}

fn _settle(ctx Ctx, n NIdx) Void {
	let wl = newWorkList(ctx numNodes);
	wl wlPush(n);
	propagateTypes(ctx, n, wl);
	ctx drainTypes(wl);
}

-- The serial the next analysis-time expr takes: the import's counter plus one
-- per maxDelay Constant the C++ calcDelayLengths created (a delay bounded only
-- by its fixed readers/initters), each of which burned a userial.
fn _analysisSerialBase(ctx Ctx) Int {
	var n = ctx.nextSerial[0];
	for (d : ctx.delays) { if (d.maxDelay == NONE && d.hasMaxBound) { n = n + 1; } }
	n
}

-- An existing cast of `inp` to `t` in graph `g` (the C++ addExpr hash-conses
-- the new CastOpExpr against the op's graph).
fn _findCast(ctx Ctx, inp NIdx, t NumType, g Int) Int {
	var i = 0;
	while (i < ctx numNodes) {
		match (ctx.kind[i]) {
			castopK(ct): {
				if (ct == t && ctx.graphOf[i] == g && ctx.ins[i] length == 1
						&& ctx.ins[i][0] == inp) { return i; }
			}
			_: {}
		}
		i = i + 1;
	}
	NONE
}

fn _appendCastNode(ctx Ctx, inp NIdx, t NumType, g Int, serial Int) Int {
	let idx = ctx numNodes;
	ctx.kind push!(NodeKind.castopK(t));
	ctx.ins push!([inp]);
	ctx.subs push!([Int]());
	ctx.serial push!(serial);
	ctx.nrate push!(ctx.nrate[inp]);
	ctx.typ push!(t);
	ctx.chans push!(ctx.chans[inp]);
	ctx.graphOf push!(g);
	ctx.cut push!(GraphCut.none);
	ctx.treeOf push!(NONE);
	ctx.consumers push!([Int]());
	ctx.suppressed push!(false);
	ctx.cmpTyp push!(ANY_NUM);
	idx
}

-- Solve with widening, choose concrete types in sorted order with the default
-- preference, then cast each non-constant operand narrower than its
-- operation. Returns the number of casts, or -1 when widening has no solution
-- either.
fn _insertWideningCasts(ctx Ctx) Int {
	let errs0 = ctx.errors length;
	ctx.widening[0] = true;
	ctx typeInference;
	for (n : ctx.sorted) {
		if (ctx.errors length > errs0) { break; }
		if (!(ctx.typ[n] isConcrete)) {
			ctx.typ[n] = ctx.typ[n] _preferredType;
			_settle(ctx, n);
		}
		match (ctx.kind[n]) {
			compareopK(_): {
				if (!(ctx.cmpTyp[n] isConcrete)) {
					ctx.cmpTyp[n] = ctx.cmpTyp[n] _preferredType;
					_settle(ctx, n);
				}
			}
			_: {}
		}
	}
	ctx.widening[0] = false;
	if (ctx.errors length > errs0) { return -1; }
	-- An emptied fixed-type node (unchecked by _propagateTypes) means widening
	-- has no solution either: keep the original error (mirrors the C++).
	if (ctx _leftEmptyType) { return -1; }

	var eOp [Int] = [];
	var eIn [Int] = [];
	var eTo [NumType] = [];
	for (n : ctx.sorted) {
		var first = 0;
		var to = NO_NUM;
		var isOp = true;
		match (ctx.kind[n]) {
			binopK(_):     { to = ctx.typ[n]; }
			compareopK(_): { to = ctx.cmpTyp[n]; }
			selectK:       { to = ctx.typ[n]; first = 1; }
			_:             { isOp = false; }
		}
		if (!isOp) { continue; }
		var i = first;
		while (i < ctx.ins[n] length) {
			let inp = ctx.ins[n][i];
			if (!(ctx.kind[inp] isConstantKind) && ctx.typ[inp] != to) {
				eOp push!(n); eIn push!(i); eTo push!(to);
			}
			i = i + 1;
		}
	}

	var serial = ctx _analysisSerialBase;
	var k = 0;
	while (k < eOp length) {
		let op = eOp[k];
		let inp = ctx.ins[op][eIn[k]];
		let g = ctx.graphOf[op];
		let s = serial;
		serial = serial + 1;                -- `new CastOpExpr` burns a userial
		var c = _findCast(ctx, inp, eTo[k], g);
		if (c == NONE) { c = _appendCastNode(ctx, inp, eTo[k], g, s); }
		let row = ctx.ins[op];
		row[eIn[k]] = c;
		k = k + 1;
	}
	eOp length
}

fn typeInferenceWithWidening(ctx Ctx) Void {
	let before = ctx _snapTypes;
	ctx typeInference;
	if (ctx.errors length == before.nErrors && !(ctx _leftEmptyType)) { return; }

	_restoreTypes(ctx, before);
	let n = ctx _insertWideningCasts;
	_restoreTypes(ctx, before);
	if (n <= 0) {
		-- Report the original conflict, exactly as before.
		ctx typeInference;
		return;
	}

	-- The casts are new nodes: redo the passes that index the graph, keeping
	-- the sort to what it held before plus the casts (mirrors the C++).
	var keep [Bool] = false repeat(ctx numNodes);
	for (s : ctx.sorted) { keep[s] = true; }
	var i = before.typ length;
	while (i < ctx numNodes) { keep[i] = true; i = i + 1; }
	ctx.sorted clear!;
	for (cs : ctx.consumers) { cs clear!; }
	ctx topologicalSortExprs;
	let resorted = ctx.sorted copy;
	ctx.sorted clear!;
	for (s : resorted) { if (keep[s]) { ctx.sorted push!(s); } }
	ctx collectConsumers;
	ctx calcDelayLengths;
	ctx shapeInference;
	ctx typeInference;
}

---------------------------------------------------------------------------
-- setNonConcreteTypesToDefault (synthdef_synth.cpp:504)
-- Preference order f32 > f64 > i32 > i64.

fn _defaultType(t NumType) NumType {
	if (t isConcrete) { return t; }
	if ((t & FLOAT32) notEmpty) { return FLOAT32; }
	if ((t & FLOAT64) notEmpty) { return FLOAT64; }
	if ((t & INT32) notEmpty) { return INT32; }
	if ((t & INT64) notEmpty) { return INT64; }
	t
}

fn setNonConcreteTypesToDefault(ctx Ctx) Void {
	for (n : ctx.sorted) {
		-- A live node whose type intersected to empty has conflicting
		-- constraints (e.g. a float select index where an integer is
		-- required). The conflict can arrive via a consumer's input-type
		-- constraint, which _propagateTypes applies without checking, so
		-- refuse it here rather than emit an "empty"-typed declaration
		-- that cannot compile. Constants are exempt: they are deduped
		-- across consumers whose demands may conflict, and codegen emits
		-- them inline in each consumer's own type.
		if (ctx.typ[n] isEmpty && ctx.consumers[n] length > 0
				&& !(ctx.kind[n] isConstantKind)) {
			ctx.errors push!(("type error: node %^ %^ has no possible type -- "
				$ "a consumer's input-type constraint conflicts with what the "
				$ "node produces (e.g. a float select index where an integer "
				$ "is required)") fmt(ctx.serial[n], nodeStr(ctx, n)));
		}
		ctx.typ[n] = _defaultType(ctx.typ[n]);
	}
	-- Each delay buffer takes its writer's (now concrete) type.
	var di = 0;
	while (di < ctx.delays length) {
		let d = ctx.delays[di];
		if (d.writer != NONE) {
			ctx.delays[di] = DelayInfo { ...d, typ: ctx.typ[d.writer] };
		}
		di = di + 1;
	}
}

---------------------------------------------------------------------------
-- findGraphCuts (synthdef_synth.cpp:251)
--
-- Annotates each node with a GraphCut that marks where a value must be
-- materialized as a variable rather than inlined. Two phases: the first marks
-- INPUTS (Input/Temp/Graph/Rate/SeparateLoop/Broadcast), the second marks the
-- expr itself (Sink/Phi/Unused/FanOut/SeparateLoop/ControlFlow).

-- setGraphCut: overwrite only if currently None, or if upgrading a temp-var cut
-- to an inst-var cut (mirrors synthdef_synth.cpp setGraphCut).
fn _setCut(ctx Ctx, n NIdx, c GraphCut) Void {
	let cur = ctx.cut[n];
	let upgrade = cur isTempVarCut && c isInstVarCut;
	match (cur) {
		none: { ctx.cut[n] = c; }
		_: { if (upgrade) { ctx.cut[n] = c; } }
	}
}

fn _isInputKind(k NodeKind) Bool = match (k) {
	inletK(_, _, _):     true;
	noteParamK(_, _, _): true;
	_:                   false;
};

fn findGraphCuts(ctx Ctx) Void {
	-- Phase 1: mark inputs.
	for (n : ctx.sorted) {
		if (ctx.kind[n] _isInputKind) { _setCut(ctx, n, GraphCut.input); }
		let ins = ctx.ins[n];
		var i = 0;
		while (i < ins length) {
			let inp = ins[i];
			if (ctx.kind[inp] isConstantKind) {
				-- constants are never cut
			} else {
				if (needsInputTempVar(ctx.kind[n], i)) { _setCut(ctx, inp, GraphCut.temp); }
				-- A cross-graph reference (input lives in a different graph) is an
				-- instance variable; otherwise rate/separate-loop/broadcast.
				if (ctx.graphOf[n] != ctx.graphOf[inp]) {
					_setCut(ctx, inp, GraphCut.graph);
				} else if (ctx.nrate[n] != ctx.nrate[inp]) {
					_setCut(ctx, inp, GraphCut.rate);
				} else if (outputMustBeSeparateLoop(ctx.kind[inp])) {
					_setCut(ctx, inp, GraphCut.separateLoop);
				} else if (ctx.chans[inp] < ctx.chans[n]) {
					_setCut(ctx, inp, GraphCut.broadcast);
				}
			}
			i = i + 1;
		}
	}
	-- Phase 2: mark exprs.
	for (n : ctx.sorted) {
		if (ctx.kind[n] isConstantKind) {
			-- skip
		} else {
			if (ctx.kind[n] isSinkKind) {
				_setCut(ctx, n, GraphCut.sink);
			} else if (ctx.kind[n] isPhiKind) {
				_setCut(ctx, n, GraphCut.phi);
			} else if (ctx.consumers[n] length == 0
				&& !(ctx.kind[n] isControlFlowKind && ctx hasSideEffectSubgraph(n))) {
				-- A control-flow node with an unconsumed result still needs a
				-- ControlFlow cut (not Unused) when one of its branches
				-- guards a side effect (see hasSideEffectSubgraph, ir.x) --
				-- falls through to the isControlFlowKind case below.
				_setCut(ctx, n, GraphCut.unused);
			} else if (ctx.consumers[n] length > 1) {
				_setCut(ctx, n, GraphCut.fanOut);
			} else if (inputMustBeSeparateLoop(ctx.kind[n], 0)) {
				_setCut(ctx, n, GraphCut.separateLoop);
			} else if (ctx.kind[n] isControlFlowKind) {
				_setCut(ctx, n, GraphCut.controlFlow);
			}
		}
	}
}

---------------------------------------------------------------------------
-- cutGraphToTrees (synthdef_synth.cpp:357)
--
-- Each node with cut != None becomes the root of an ExprTree. traceTree walks
-- backward from a root through cut==None inputs, pulling them into the tree (in
-- post-order), and records cut inputs as antecedent trees. The boolean on each
-- antecedent is a separate-loop constraint (always false for M1 kinds, but the
-- structure is ported for later milestones).

-- Append node `inp` as an antecedent of the tree under construction, OR-ing the
-- separate-loop flag if already present. `` `curAnte ``/`` `curAnteSep `` are the
-- accumulators for the tree currently being traced.
fn _addAntecedent(anteTreeIdx Int, sep Bool) Void {
	var ante [Int] = `curAnte;
	var anteSep [Bool] = `curAnteSep;
	var i = 0;
	while (i < ante length) {
		if (ante[i] == anteTreeIdx) {
			anteSep[i] = anteSep[i] || sep;
			return;
		}
		i = i + 1;
	}
	ante push!(anteTreeIdx);
	anteSep push!(sep);
}

-- Recursive backward trace. `dstN` is the consumer node and `dstI` its input
-- index (for input-side separate-loop constraints), or NONE at the root.
fn _traceTreeExpr(ctx Ctx, n NIdx, rootN NIdx, dstN NIdx, dstI Int) Void {
	var visited [Bool] = `curVisited;
	if (visited[n]) { return; }
	visited[n] = true;
	let isInline = ctx.cut[n] isNoneCut || n == rootN;
	if (isInline) {
		ctx.treeOf[n] = `curTreeIdx;
		let ins = ctx.ins[n];
		var i = 0;
		while (i < ins length) {
			_traceTreeExpr(ctx, ins[i], rootN, n, i);
			i = i + 1;
		}
		var exprs [Int] = `curExprs;
		exprs push!(n);
	} else {
		-- n is the root of an already-built antecedent tree.
		let anteTreeIdx = ctx.treeOf[n];
		let outputLoop = outputMustBeSeparateLoop(ctx.kind[n]);
		let inputLoop = dstN == NONE ? false : inputMustBeSeparateLoop(ctx.kind[dstN], dstI);
		_addAntecedent(anteTreeIdx, outputLoop || inputLoop);
	}
}

-- Trace one tree from its root with fresh per-tree accumulators. These are kept
-- in this helper (not inline in cutGraphToTrees' loop) so the dynamic-var
-- bindings restore on each return: a loop body's `var ` declarations would
-- otherwise accumulate (they restore on function exit, not per iteration) and
-- overflow the dynamic-scope stack on synths with many trees.
fn _buildTree(ctx Ctx, treeIdx Int, root NIdx) Void {
	var `curTreeIdx Int = treeIdx;
	var `curVisited [Bool] = false repeat(ctx numNodes);
	var `curExprs [Int] = [];
	var `curAnte [Int] = [];
	var `curAnteSep [Bool] = [];
	_traceTreeExpr(ctx, root, root, NONE, 0);
	ctx.trees[treeIdx] = Tree {
		root: root, exprs: `curExprs,
		antecedents: `curAnte, antecedentsSep: `curAnteSep,
		loopOf: NONE, serial: ctx.trees[treeIdx].serial,
	};
}

fn cutGraphToTrees(ctx Ctx) Void {
	var serialNo = 1;
	for (root : ctx.sorted) {
		match (ctx.cut[root]) {
			none: {}
			_: {
				let treeIdx = ctx.trees length;
				-- Reserve the slot so antecedent lookups by index resolve.
				ctx.trees push!(Tree {
					root: root, exprs: [Int](),
					antecedents: [Int](), antecedentsSep: [Bool](),
					loopOf: NONE, serial: serialNo,
				});
				serialNo = serialNo + 1;
				ctx _buildTree(treeIdx, root);
			}
		}
	}
}

---------------------------------------------------------------------------
-- Tree antecedents. (Delay writers no longer take their readers' trees as
-- antecedents: a read is a weighted edge from the written value (cycles.x),
-- and block-sized rings let a writer run before its readers.)

fn _treeAddAntecedent(ctx Ctx, treeIdx Int, anteTreeIdx Int, sep Bool) Void {
	let t = ctx.trees[treeIdx];
	var ante [Int] = t.antecedents;
	var anteSep [Bool] = t.antecedentsSep;
	var i = 0;
	while (i < ante length) {
		if (ante[i] == anteTreeIdx) { anteSep[i] = anteSep[i] || sep; return; }
		i = i + 1;
	}
	ante push!(anteTreeIdx);
	anteSep push!(sep);
}

---------------------------------------------------------------------------
-- inplaceDelayOps (mirrors synthdef_synth.cpp inplaceDelayOps)
--
-- A DelayWrite whose value is VecPut(read of the same single-slot delay,
-- idx, val) replaces ONE element of the state, so it is emitted as a single
-- indexed store instead of materializing the read, copying the whole vector
-- through the put, and storing every lane back. When every other consumer
-- of the read is a scalar VecAt, the read's materialization is suppressed
-- too and the VecAts load the state directly (appended to fixReaders). The
-- block code generator (M4) must order those loads before the store.

fn _inplaceForWrite(ctx Ctx, n NIdx, d Int) Void {
	let info = ctx.delays[d];
	if (info.allocSize != 1) { return; }
	if (info.varReaders length != 0) { return; }
	let isAudio = match (ctx.nrate[n]) { audio: true; _: false; };
	if (!isAudio) { return; }
	if (ctx.graphOf[n] != 0) { return; }
	let p = ctx.ins[n][0];
	let isPut = match (ctx.kind[p]) {
		vecK(vop): match (vop) { put: true; _: false; };
		_: false;
	};
	if (!isPut) { return; }
	if (ctx.consumers[p] length != 1) { return; }
	let r = ctx.ins[p][0];
	let readsSame = match (ctx.kind[r]) {
		delayFixReadK(rd, _): rd == d;
		_: false;
	};
	if (!readsSame) { return; }
	if (ctx.graphOf[r] != 0) { return; }
	if (ctx.chans[ctx.ins[p][1]] != 1 || ctx.chans[ctx.ins[p][2]] != 1) { return; }

	ctx.suppressed[p] = true;
	ctx.chans[n] = 1;

	-- iterate r's consumers distinct in first-occurrence order (the C++
	-- consumer bag is distinct; synthc's array carries one entry per edge)
	var allAts = true;
	var ats [Int] = [];
	var seen = Set(p);
	for (c : ctx.consumers[r]) {
		if (seen contains(c)) { continue; }
		seen insert!(c);
		let ok = match (ctx.kind[c]) {
			vecK(vop): match (vop) {
				at: ctx.chans[c] == 1 && ctx.chans[ctx.ins[c][1]] == 1;
				_: false;
			};
			_: false;
		};
		if (!ok) { allAts = false; break; }
		ats push!(c);
	}
	if (allAts) {
		ctx.suppressed[r] = true;
		info.fixReaders append!(ats);
	}
}

fn inplaceDelayOps(ctx Ctx) Void {
	for (n : ctx.sorted) {
		match (ctx.kind[n]) {
			delayWriteK(d): { _inplaceForWrite(ctx, n, d); }
			_: {}
		}
	}
}


---------------------------------------------------------------------------
-- addSubgraphAntecedents (synthdef_synth.cpp:545)
--
-- A control-flow node's tree depends on each of its branch/body subgraph trees
-- (the PhiNode roots in ctx.subs), with a separate-loop constraint (the `true`
-- flag) so the branch runs in its own loop inside the control-flow block.

fn addSubgraphAntecedents(ctx Ctx) Void {
	for (n : ctx.sorted) {
		let ntree = ctx.treeOf[n];
		for (sub : ctx.subs[n]) {
			let stree = ctx.treeOf[sub];
			if (stree != ntree) { _treeAddAntecedent(ctx, ntree, stree, true); }
		}
	}
}

---------------------------------------------------------------------------
-- sortTrees (synthdef_synth.cpp:571)
--
-- Topologically sort trees by their antecedent edges (DFS post-order), visiting
-- in root-sorted order to match the C++ traversal.

fn _sortTreesVisit(ctx Ctx, treeIdx Int) Void {
	var visited [Bool] = `treeVisited;
	if (visited[treeIdx]) { return; }
	visited[treeIdx] = true;
	for (a : ctx.trees[treeIdx].antecedents) {
		_sortTreesVisit(ctx, a);
	}
	var sorted [Int] = `sortedTreesAcc;
	sorted push!(treeIdx);
}

fn sortTrees(ctx Ctx) Void {
	var `treeVisited [Bool] = false repeat(ctx.trees length);
	-- Bind the accumulator to the shared ctx.sortedTrees array so pushes are
	-- visible through the by-value Ctx copy.
	var `sortedTreesAcc [Int] = ctx.sortedTrees;
	for (root : ctx.sorted) {
		if (ctx.treeOf[root] != NONE) {
			if (root == ctx.trees[ctx.treeOf[root]].root) {
				_sortTreesVisit(ctx, ctx.treeOf[root]);
			}
		}
	}
}

---------------------------------------------------------------------------
-- removeDeadCode (synthdef_synth.cpp:386)
--
-- Remove trees whose root is Unused (no consumers), iterating because removing
-- a tree can leave its inputs with no consumers. Sinks/phi never become unused.
-- For well-formed M1 synths this is usually a no-op.

fn _treeOfRoot(ctx Ctx, n NIdx) Int = ctx.treeOf[n];

fn removeDeadCode(ctx Ctx) Void {
	-- Collect initially-unused trees.
	var deadTree [Bool] = false repeat(ctx.trees length);
	for (n : ctx.sorted) {
		match (ctx.cut[n]) {
			unused: { if (ctx.treeOf[n] != NONE) { deadTree[ctx.treeOf[n]] = true; } }
			_: {}
		}
	}
	var changed = true;
	while (changed) {
		changed = false;
		var newSorted [Int] = [];
		for (n : ctx.sorted) {
			let t = ctx.treeOf[n];
			if (t != NONE && deadTree[t]) {
				-- Drop n: detach from its inputs' consumer lists.
				for (inp : ctx.ins[n]) {
					_removeConsumer(ctx, inp, n);
					if (ctx.consumers[inp] length == 0) {
						-- input now unused; if it is a separate tree, kill it too
						if (ctx.treeOf[inp] != NONE && ctx.treeOf[inp] != t) {
							match (ctx.cut[inp]) {
								none: {}
								_: {
									if (!deadTree[ctx.treeOf[inp]]) {
										ctx.cut[inp] = GraphCut.unused;
										deadTree[ctx.treeOf[inp]] = true;
										changed = true;
									}
								}
							}
						}
					}
				}
				-- detach delay readers
				match (ctx.kind[n]) {
					delayFixReadK(d, _): { _removeFromList(ctx.delays[d].fixReaders, n); }
					delayVarReadK(d, _): { _removeFromList(ctx.delays[d].varReaders, n); }
					_: {}
				}
			} else {
				newSorted push!(n);
			}
		}
		_replaceIntArray(ctx.sorted, newSorted);
	}
}

fn _removeConsumer(ctx Ctx, n NIdx, consumer NIdx) Void {
	_removeFromList(ctx.consumers[n], consumer);
}

fn _removeFromList(lst [Int], x Int) Void {
	-- remove first occurrence of x from lst in place
	var idx = NONE;
	var i = 0;
	while (i < lst length) {
		if (lst[i] == x) { idx = i; i = lst length; } else { i = i + 1; }
	}
	if (idx != NONE) {
		var j = idx;
		while (j < lst length - 1) { lst[j] = lst[j + 1]; j = j + 1; }
		lst pop!;
	}
}

fn _replaceIntArray(dst [Int], src [Int]) Void {
	-- overwrite dst's contents with src (dst is a shared reference)
	dst clear! append!(src);
}

---------------------------------------------------------------------------
-- treesToLoops
--
-- Fuse trees into loops. Trees join an existing loop with the same rate and
-- channel count when the loop can access all the tree's antecedents and there
-- are no separate-loop constraints; otherwise a new loop is created. Control-
-- flow and gets-own-loop roots always get a fresh loop.

fn _intListHas(lst [Int], x Int) Bool {
	for (e : lst) { if (e == x) { return true; } }
	false
}

fn _newLoop(ctx Ctx, rate Rate, chans Int, isCF Bool, graphOf Int) Int {
	let li = ctx.loops length;
	ctx.loops push!(Loop {
		trees: [Int](), rate: rate, chans: chans,
		isControlFlow: isCF, antecedents: [Int](),
		graphOf: graphOf, serial: li,
	});
	li
}

fn _loopAddTree(ctx Ctx, li Int, treeIdx Int) Void {
	ctx.loops[li].trees push!(treeIdx);
	ctx.trees[treeIdx] = Tree { ...ctx.trees[treeIdx], loopOf: li };
	-- Build loop antecedents from the tree's antecedent trees' loops.
	for (ante : ctx.trees[treeIdx].antecedents) {
		let al = ctx.trees[ante].loopOf;
		if (al != NONE && al != li) {
			let serial = ctx.loops[al].serial;
			if (!_intListHas(ctx.loops[li].antecedents, serial)) {
				ctx.loops[li].antecedents push!(serial);
			}
		}
	}
}

-- canAccessAllAntecedentsOf: every antecedent tree's loop must run no later
-- than this loop (lower rate, or same rate with serial <= this loop's serial).
fn _canAccessAllAntecedents(ctx Ctx, li Int, treeIdx Int) Bool {
	let lp = ctx.loops[li];
	for (ante : ctx.trees[treeIdx].antecedents) {
		let al = ctx.trees[ante].loopOf;
		if (al == NONE) { return false; }
		let arate = ctx.loops[al].rate;
		let aserial = ctx.loops[al].serial;
		let ok = arate < lp.rate || (arate == lp.rate && aserial <= lp.serial);
		if (!ok) { return false; }
	}
	true
}

-- hasNoSeparateLoopConstraintsWith: no separate-loop edge between this tree and
-- any tree already in the loop. (Always true for M1 kinds.)
fn _hasNoSeparateLoopConstraints(ctx Ctx, li Int, treeIdx Int) Bool {
	for (loopTreeIdx : ctx.loops[li].trees) {
		-- this tree's antecedents that are loopTree with a sep flag
		let t = ctx.trees[treeIdx];
		var i = 0;
		while (i < t.antecedents length) {
			if (t.antecedents[i] == loopTreeIdx && t.antecedentsSep[i]) { return false; }
			i = i + 1;
		}
		-- loopTree's antecedents that are this tree with a sep flag
		let lt = ctx.trees[loopTreeIdx];
		var k = 0;
		while (k < lt.antecedents length) {
			if (lt.antecedents[k] == treeIdx && lt.antecedentsSep[k]) { return false; }
			k = k + 1;
		}
	}
	true
}

fn treesToLoops(ctx Ctx) Void {
	for (treeIdx : ctx.sortedTrees) {
		let root = ctx.trees[treeIdx].root;
		let rate = ctx.nrate[root];
		let chans = ctx.chans[root];
		let go = ctx.graphOf[root];
		if (ctx.kind[root] isControlFlowKind) {
			let li = _newLoop(ctx, rate, chans, true, go);
			_loopAddTree(ctx, li, treeIdx);
		} else if (outputMustBeSeparateLoop(ctx.kind[root])) {
			let li = _newLoop(ctx, rate, chans, false, go);
			_loopAddTree(ctx, li, treeIdx);
		} else {
			var placed = false;
			var li = 0;
			while (li < ctx.loops length && !placed) {
				let lp = ctx.loops[li];
				if (!lp.isControlFlow && lp.rate == rate && lp.chans == chans
					&& lp.graphOf == go) {
					if (_canAccessAllAntecedents(ctx, li, treeIdx)
						&& _hasNoSeparateLoopConstraints(ctx, li, treeIdx)) {
						_loopAddTree(ctx, li, treeIdx);
						placed = true;
					}
				}
				li = li + 1;
			}
			if (!placed) {
				let nl = _newLoop(ctx, rate, chans, false, go);
				_loopAddTree(ctx, nl, treeIdx);
			}
		}
	}
}

---------------------------------------------------------------------------
-- splitRates (synthdef_synth.cpp:843)
-- Partition loops into init / reset / block / audio buckets by rate.

fn splitRates(ctx Ctx) Void {
	var li = 0;
	while (li < ctx.loops length) {
		match (ctx.loops[li].rate) {
			constant: { ctx.initLoops push!(li); }
			init:     { ctx.initLoops push!(li); }
			reset:    { ctx.resetLoops push!(li); }
			block:    { ctx.blockLoops push!(li); }
			audio:    { ctx.graphs[ctx.loops[li].graphOf].audioLoops push!(li); }
		}
		li = li + 1;
	}
}

---------------------------------------------------------------------------
-- Drivers

-- M0 pipeline: import-time state is already in ctx; run the structural
-- passes only.
fn analyzeM0(ctx Ctx) Ctx {
	ctx mergeDelays;
	ctx topologicalSortExprs;
	ctx collectConsumers;
	ctx
}

-- Analysis up to (and including) type inference and rates.
fn analyzeTypes(ctx Ctx) Ctx {
	ctx mergeDelays;
	ctx topologicalSortExprs;
	ctx collectConsumers;
	ctx calcDelayLengths;
	ctx shapeInference;
	ctx typeInferenceWithWidening;
	ctx setNonConcreteTypesToDefault;
	ctx findGraphCuts;
	ctx
}

-- Analysis through tree construction (no loops yet).
fn analyzeTrees(ctx Ctx) Ctx {
	ctx analyzeTypes;
	ctx cutGraphToTrees;
	ctx removeDeadCode;
	ctx inplaceDelayOps;
	ctx addSubgraphAntecedents;
	ctx sortTrees;
	ctx
}

-- The serial code generator (M4) computes everything per sample: there are
-- no parameter groups yet, so block placement is audio.
fn demoteBlockRate(ctx Ctx) Void {
	var n = 0;
	while (n < ctx numNodes) {
		if (ctx.nrate[n] == Rate.block) { ctx.nrate[n] = Rate.audio; }
		n = n + 1;
	}
}

-- In a per-sample body a delay's writer must come after its readers' trees
-- (a read sees the previous sample, and rings are sized for read-then-write).
-- The block code generator replaces this with block-sized rings (M7/M8).
fn addDelayOrdering(ctx Ctx) Void {
	for (n : ctx.sorted) {
		match (ctx.kind[n]) {
			delayWriteK(d): {
				let wtree = ctx.treeOf[n];
				for (rd : ctx.delays[d].fixReaders) {
					if (ctx.treeOf[rd] != wtree) { _treeAddAntecedent(ctx, wtree, ctx.treeOf[rd], false); }
				}
				for (rd : ctx.delays[d].varReaders) {
					if (ctx.treeOf[rd] != wtree) { _treeAddAntecedent(ctx, wtree, ctx.treeOf[rd], false); }
				}
			}
			_: {}
		}
	}
}

-- The pipeline the serial code generator runs on.
fn analyzeForCodegen(ctx Ctx) Ctx {
	ctx demoteBlockRate;
	ctx analyzeTypes;
	ctx cutGraphToTrees;
	ctx removeDeadCode;
	ctx inplaceDelayOps;
	ctx addDelayOrdering;
	ctx addSubgraphAntecedents;
	ctx sortTrees;
	ctx treesToLoops;
	ctx splitRates;
	ctx
}

-- The full analysis pipeline.
fn analyze(ctx Ctx) Ctx {
	ctx analyzeTrees;
	ctx findCycles;
	ctx findParamGroups;
	ctx treesToLoops;
	ctx splitRates;
	ctx
}
