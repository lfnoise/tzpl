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

-- visitDelay traversal order: initters, fixReaders, varReaders, maxDelay.
fn _delayMembers(ctx Ctx, d Int) [Int] {
	var out [Int] = [];
	for (n : ctx.delays[d].initters) { out push!(n); }
	for (n : ctx.delays[d].fixReaders) { out push!(n); }
	for (n : ctx.delays[d].varReaders) { out push!(n); }
	if (ctx.delays[d].maxDelay != NONE) { out push!(ctx.delays[d].maxDelay); }
	out
}

---------------------------------------------------------------------------
-- mergeDelays (synthdef_synth.cpp:1049)
--
-- The C++ merge key is (writer, initializers, graph) and merges delays that
-- share a writer. But a DelayWrite's structural identity *is its buffer*:
-- DelayWrite::equals_ compares only delayBuf and ignores the written signal.
-- DelayWrite is hash-consed, so identity == structural identity, which means
-- each buffer has exactly one writer node and two distinct delays can NEVER
-- share a writer. The merge condition is unsatisfiable across distinct delays:
-- every group is a singleton and the pass is a no-op by construction -- not a
-- front-end accident (verified by differential dump/render).
--
-- (The name/intent suggest it meant to key on the written *signal*, to fold
-- delays fed identical inputs + init; equals_ comparing the buffer instead makes
-- that dead. We reproduce the C++ behavior exactly for byte-match -- keying on
-- the signal would diverge. The real optimization could be revisited once
-- synthc is the source of truth.)
--
-- The grouping still classifies every delay (keyed on the writer node id, the
-- synthc analog of the DelayWrite pointer); the merge body below is a faithful
-- port for the hypothetical shared-writer case.

-- Reader dedup signature: fixed reads key on their sample offset; var reads key
-- on interpolation + index-signal node (mirrors r->in0() in the C++).
fn _readerSig(ctx Ctx, r NIdx) String = match (ctx.kind[r]) {
	delayFixReadK(_, k):   "F" $ k toString;
	delayVarReadK(_, ip):  "V" $ ip ordinal toString $ ":"
		$ ((ctx.ins[r] length > 0) ? ctx.ins[r][0] toString : "-");
	_:                     "?";
};

-- Order-independent merge key. The writer node id makes each key unique in the
-- current front end, so groups stay singletons.
fn _delayMergeKey(ctx Ctx, d Int) String {
	let info = ctx.delays[d];
	var its [String] = [];
	for (n : info.initters) {
		let off = match (ctx.kind[n]) { delayInitK(_, o): o; _: 0; };
		let val = (ctx.ins[n] length > 0) ? ctx.ins[n][0] : NONE;
		its push!(off toString $ ":" $ val toString);
	}
	"W" $ info.writer toString $ "|" $ (its sort join(","))
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
	var readers [Int] = [];
	let olist = isVar ? ctx.delays[other].varReaders : ctx.delays[other].fixReaders;
	for (r : olist) { readers push!(r); }
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
-- any delay was removed. Unreachable on the current front end (singletons).
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
	while (ctx.delays length > 0) { ctx.delays pop!; }
	for (x : newDelays) { ctx.delays push!(x); }
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
		fKids push!(ctx.ins[root]);

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
					fKids push!(ctx.ins[child]);
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
		let chans0 = ctx.chans[n];
		let ch = calcShapeOf(ctx, n);
		ctx.chans[n] = ch;
		if (ch != chans0) {
			for (c : ctx.consumers[n]) { wl wlPush(c); }
		}
		match (ctx.kind[n]) {
			delayWriteK(d): {
				if (ch != ctx.delays[d].chans) {
					ctx.delays[d] = DelayInfo { ...ctx.delays[d], chans: ch };
					for (u : ctx.delays[d].initters) { wl wlPush(u); }
					for (u : ctx.delays[d].fixReaders) { wl wlPush(u); }
					for (u : ctx.delays[d].varReaders) { wl wlPush(u); }
				}
			}
			_: {}
		}
	}
}

---------------------------------------------------------------------------
-- typeInference (synthdef_synth.cpp:491)

fn typeInference(ctx Ctx) Void {
	let wl = newWorkList(ctx numNodes);
	for (n : ctx.sorted) { wl wlPush(n); }
	while (!(wl wlEmpty)) {
		let n = wl wlPop;
		updateTypeOf(ctx, n, wl);
	}
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
-- setDelayReaderRates (synthdef_synth.cpp:226)
-- Promote delay writer + readers to event rate when the written value is
-- event-rate.

fn setDelayReaderRates(ctx Ctx) Void {
	var di = 0;
	while (di < ctx.delays length) {
		let d = ctx.delays[di];
		if (d.writer != NONE) {
			if (ctx.nrate[ctx.ins[d.writer][0]] == Rate.event) {
				ctx.nrate[d.writer] = Rate.event;
			}
		}
		let isEvent = d.writer != NONE && ctx.nrate[d.writer] == Rate.event;
		if (isEvent) {
			for (u : d.fixReaders) { ctx.nrate[u] = Rate.event; }
			for (u : d.varReaders) { ctx.nrate[u] = max(ctx.nrate[u], Rate.event); }
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
	inletK(_, _):        true;
	control(_, _, _):    true;
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
				-- All M1 nodes share the root graph, so the graph-cut branch is
				-- never taken; rate/separate-loop/broadcast follow in priority.
				if (ctx.nrate[n] != ctx.nrate[inp]) {
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
			} else if (ctx.consumers[n] length == 0) {
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
					loopOf: NONE, isoGroupOf: NONE, serial: serialNo,
				});
				serialNo = serialNo + 1;
				-- Trace with fresh per-tree accumulators.
				var `curTreeIdx Int = treeIdx;
				var `curVisited [Bool] = false repeat(ctx numNodes);
				var `curExprs [Int] = [];
				var `curAnte [Int] = [];
				var `curAnteSep [Bool] = [];
				_traceTreeExpr(ctx, root, root, NONE, 0);
				ctx.trees[treeIdx] = Tree {
					root: root, exprs: `curExprs,
					antecedents: `curAnte, antecedentsSep: `curAnteSep,
					loopOf: NONE, isoGroupOf: NONE, serial: ctx.trees[treeIdx].serial,
				};
			}
		}
	}
}

---------------------------------------------------------------------------
-- addDelayAntecedents (synthdef_synth.cpp:523)
--
-- A delay writer's tree must run after every reader's tree in the same cycle
-- (so the read sees the previous sample). Add reader trees as antecedents of
-- the writer's tree (non-separate-loop).

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

fn addDelayAntecedents(ctx Ctx) Void {
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
	while (dst length > 0) { dst pop!; }
	for (x : src) { dst push!(x); }
}

---------------------------------------------------------------------------
-- treesToLoops (synthdef_synth.cpp:757)  -- M1: no iso-groups (isoGroup=null)
--
-- Fuse trees into loops. Trees join an existing loop with the same rate and
-- channel count when the loop can access all the tree's antecedents and there
-- are no separate-loop constraints; otherwise a new loop is created. Control-
-- flow and gets-own-loop roots always get a fresh loop.

fn _intListHas(lst [Int], x Int) Bool {
	for (e : lst) { if (e == x) { return true; } }
	false
}

---------------------------------------------------------------------------
-- computeIsoGroups (synthdef_synth.cpp:674)
--
-- Group event-rate trees by their transitive control-dependency set, build the
-- activation graph between groups, and topologically sort it. Each group then
-- becomes its own set of event loops; processEvents runs only the groups whose
-- controls changed. Runs after sortTrees, before treesToLoops.

fn _isControlNode(ctx Ctx, n NIdx) Bool = match (ctx.kind[n]) { control(_, _, _): true; _: false; };

-- Insert control node index x into a sorted-unique list (set semantics).
fn _ctrlInsert(lst [Int], x Int) Void {
	var i = 0;
	while (i < lst length) {
		if (lst[i] == x) { return; }
		if (lst[i] > x) {
			lst push!(0);
			var j = lst length - 1;
			while (j > i) { lst[j] = lst[j - 1]; j = j - 1; }
			lst[i] = x;
			return;
		}
		i = i + 1;
	}
	lst push!(x);
}

fn _ctrlListEq(a [Int], b [Int]) Bool {
	if (a length != b length) { return false; }
	var i = 0;
	while (i < a length) { if (a[i] != b[i]) { return false; } i = i + 1; }
	true
}

-- Transitive control set of a tree, memoized in `transCtrl / `transDone.
fn _computeTransitiveControls(ctx Ctx, treeIdx Int) Void {
	var done [Bool] = `transDone;
	if (done[treeIdx]) { return; }
	done[treeIdx] = true;
	var result [Int] = [];
	let tree = ctx.trees[treeIdx];
	-- A tree whose own root is a control reads that control directly, so it
	-- must re-run (re-loading the control into its instance var) when that
	-- control changes. Without this the control-reading iso-group has an empty
	-- activation set and never fires, leaving the synth silent.
	if (ctx _isControlNode(tree.root)) {
		_ctrlInsert(result, tree.root);
	}
	for (ante : tree.antecedents) {
		let anteRoot = ctx.trees[ante].root;
		if (ctx _isControlNode(anteRoot)) {
			_ctrlInsert(result, anteRoot);
		} else if (ctx.nrate[anteRoot] == Rate.event) {
			_computeTransitiveControls(ctx, ante);
			let ac [[Int]] = `transCtrl;
			for (c : ac[ante]) { _ctrlInsert(result, c); }
		}
	}
	let tc [[Int]] = `transCtrl;
	tc[treeIdx] = result;
}

fn computeIsoGroups(ctx Ctx) Void {
	-- Collect event-rate trees (in sortedTrees order) and their control sets.
	var `transCtrl [[Int]] = [];
	var `transDone [Bool] = false repeat(ctx.trees length);
	var ti = 0;
	while (ti < ctx.trees length) { `transCtrl push!([Int]()); ti = ti + 1; }

	var eventTrees [Int] = [];
	for (treeIdx : ctx.sortedTrees) {
		if (ctx.nrate[ctx.trees[treeIdx].root] == Rate.event) {
			eventTrees push!(treeIdx);
			_computeTransitiveControls(ctx, treeIdx);
		}
	}
	if (eventTrees length == 0) { return; }

	-- Group by identical control set (first-appearance order).
	var tcAll [[Int]] = `transCtrl;
	for (treeIdx : eventTrees) {
		let cs [Int] = tcAll[treeIdx];
		var found = NONE;
		var gi = 0;
		while (gi < ctx.isoGroups length && found == NONE) {
			if (_ctrlListEq(ctx.isoGroups[gi].controls, cs)) { found = gi; }
			gi = gi + 1;
		}
		if (found == NONE) {
			found = ctx.isoGroups length;
			ctx.isoGroups push!(IsoGroup {
				trees: [Int](), loops: [Int](), controls: cs,
				activates: [Int](), serial: found,
			});
		}
		ctx.isoGroups[found].trees push!(treeIdx);
		ctx.trees[treeIdx] = Tree { ...ctx.trees[treeIdx], isoGroupOf: found };
	}

	-- Activation graph: if a tree in group B has an antecedent tree in group A,
	-- then A activates B.
	var gi2 = 0;
	while (gi2 < ctx.isoGroups length) {
		var upstream [Int] = [];
		for (t : ctx.isoGroups[gi2].trees) {
			for (ante : ctx.trees[t].antecedents) {
				let ag = ctx.trees[ante].isoGroupOf;
				if (ag != NONE && ag != gi2 && !_intListHas(upstream, ag)) { upstream push!(ag); }
			}
		}
		for (u : upstream) {
			if (!_intListHas(ctx.isoGroups[u].activates, gi2)) {
				ctx.isoGroups[u].activates push!(gi2);
			}
		}
		gi2 = gi2 + 1;
	}

	-- Kahn topological sort by activation edges.
	var inDeg [Int] = 0 repeat(ctx.isoGroups length);
	var g3 = 0;
	while (g3 < ctx.isoGroups length) {
		for (d : ctx.isoGroups[g3].activates) { inDeg[d] = inDeg[d] + 1; }
		g3 = g3 + 1;
	}
	var queue [Int] = [];
	var g4 = 0;
	while (g4 < ctx.isoGroups length) { if (inDeg[g4] == 0) { queue push!(g4); } g4 = g4 + 1; }
	var qHead = 0;
	while (qHead < queue length) {
		let g = queue[qHead];
		qHead = qHead + 1;
		ctx.isoOrder push!(g);
		for (d : ctx.isoGroups[g].activates) {
			inDeg[d] = inDeg[d] - 1;
			if (inDeg[d] == 0) { queue push!(d); }
		}
	}
	-- Final serials reflect topological order.
	var pos = 0;
	while (pos < ctx.isoOrder length) {
		let g = ctx.isoOrder[pos];
		ctx.isoGroups[g] = IsoGroup { ...ctx.isoGroups[g], serial: pos };
		pos = pos + 1;
	}
}

fn _newLoop(ctx Ctx, rate Rate, chans Int, isCF Bool, isoGroup Int, graphOf Int) Int {
	let li = ctx.loops length;
	ctx.loops push!(Loop {
		trees: [Int](), rate: rate, chans: chans,
		isControlFlow: isCF, isoGroup: isoGroup, antecedents: [Int](),
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
		let ig = ctx.trees[treeIdx].isoGroupOf;
		let go = ctx.graphOf[root];
		if (ctx.kind[root] isControlFlowKind) {
			let li = _newLoop(ctx, rate, chans, true, ig, go);
			_loopAddTree(ctx, li, treeIdx);
		} else if (outputMustBeSeparateLoop(ctx.kind[root])) {
			let li = _newLoop(ctx, rate, chans, false, ig, go);
			_loopAddTree(ctx, li, treeIdx);
		} else {
			var placed = false;
			var li = 0;
			while (li < ctx.loops length && !placed) {
				let lp = ctx.loops[li];
				if (!lp.isControlFlow && lp.rate == rate && lp.chans == chans
					&& lp.isoGroup == ig && lp.graphOf == go) {
					if (_canAccessAllAntecedents(ctx, li, treeIdx)
						&& _hasNoSeparateLoopConstraints(ctx, li, treeIdx)) {
						_loopAddTree(ctx, li, treeIdx);
						placed = true;
					}
				}
				li = li + 1;
			}
			if (!placed) {
				let nl = _newLoop(ctx, rate, chans, false, ig, go);
				_loopAddTree(ctx, nl, treeIdx);
			}
		}
	}
	-- Populate IsoGroup.loops (the distinct loops of each group's trees).
	var gi = 0;
	while (gi < ctx.isoGroups length) {
		var seen [Int] = [];
		for (t : ctx.isoGroups[gi].trees) {
			let lp = ctx.trees[t].loopOf;
			if (lp != NONE && !_intListHas(seen, lp)) {
				seen push!(lp);
				ctx.isoGroups[gi].loops push!(lp);
			}
		}
		gi = gi + 1;
	}
}

---------------------------------------------------------------------------
-- splitRates (synthdef_synth.cpp:843)
-- Partition loops into init / reset / event / audio buckets by rate.

fn splitRates(ctx Ctx) Void {
	var li = 0;
	while (li < ctx.loops length) {
		match (ctx.loops[li].rate) {
			constant: { ctx.initLoops push!(li); }
			init:     { ctx.initLoops push!(li); }
			reset:    { ctx.resetLoops push!(li); }
			event:    { ctx.eventLoops push!(li); }
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

-- M1 analysis up to (and including) type inference + rates. Trees/loops are
-- added in later stages.
fn analyzeTypes(ctx Ctx) Ctx {
	ctx mergeDelays;
	ctx topologicalSortExprs;
	ctx collectConsumers;
	ctx calcDelayLengths;
	ctx shapeInference;
	ctx typeInference;
	ctx setNonConcreteTypesToDefault;
	ctx setDelayReaderRates;
	ctx findGraphCuts;
	ctx
}

-- M1 analysis through tree construction (no loops yet).
fn analyzeTrees(ctx Ctx) Ctx {
	ctx analyzeTypes;
	ctx cutGraphToTrees;
	ctx removeDeadCode;
	ctx addDelayAntecedents;
	ctx sortTrees;
	ctx
}

-- Full analysis pipeline. Mirrors synthdef::Synth::graphAnalysis() for the
-- supported node set, including iso-groups (so control synths' event-loop
-- grouping matches the C++), including mergeDelays. Subgraphs remain deferred.
fn analyzeM1(ctx Ctx) Ctx {
	ctx analyzeTrees;
	ctx computeIsoGroups;
	ctx treesToLoops;
	ctx splitRates;
	ctx
}
