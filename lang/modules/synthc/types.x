-- synthc/types.x
-- Per-kind shape and type rules for the analysis passes (originally ported
-- from the virtual methods of the C++ Expr hierarchy):
--   calcShape()          -> calcShapeOf
--   update_type()        -> updateTypeOf
--   inputTypeConstraint  -> inputConstraintOf
--   initial_type()       -> handled at import time (importer.x)
--
-- A node is an Int index into the Ctx SoA arrays. The worklist machinery
-- (WorkList) mirrors the C++ ExprIdentitySet: narrowing type inference and
-- widening shape inference both converge to an order-independent fixpoint, so
-- a simple stack with a membership array is used instead of a hash set.

import synthdef.*;
import synthc.ir.*;

---------------------------------------------------------------------------
-- Worklist

struct WorkList {
	inq [Bool],
	items [Int],
}

fn newWorkList(n Int) WorkList = WorkList { inq: false repeat(n), items: [Int]() };

fn wlPush(wl WorkList, i Int) Void {
	if (!wl.inq[i]) {
		wl.inq[i] = true;
		wl.items push!(i);
	}
}

fn wlEmpty(wl WorkList) Bool = wl.items length == 0;

fn wlPop(wl WorkList) Int {
	let i = wl.items pop!;
	wl.inq[i] = false;
	i
}

---------------------------------------------------------------------------
-- Shape rules

fn _noShape() Shape = shape([0]);

-- vec op type helpers (factored out: the join arms accumulate over all
-- inputs).
fn _vecInputConstraint(vop VecOp, nodeTyp NumType, i Int) NumType = match (vop) {
	put(_):  i == 1 ? ANY_INT : nodeTyp;
	join(_, _): nodeTyp;
	_:    i == 0 ? nodeTyp : ANY_INT;
};
fn _vecNarrowType(ctx Ctx, n NIdx, vop VecOp, ins [Int]) NumType = match (vop) {
	put(_):  ctx.typ[n] & ctx.typ[ins[0]] & ctx.typ[ins[2]];
	join(_, _): { var t = ctx.typ[n]; for (x : ins) { t = t & ctx.typ[x]; } t }
	_:    ctx.typ[n] & ctx.typ[ins[0]];
};

-- Vec op shapes. at = the index vector's chans; put = the source array's
-- chans; reduce(op) is a scalar. The axis
-- ops change one axis (reduceAxis removes it); join sums the axis over its
-- inputs; permute reorders the axes; reshape keeps the channels.
fn _vecShapeOf(ctx Ctx, vop VecOp, ins [Int]) Shape {
	var shs [Shape] = [];
	for (x : ins) { shs push!(ctx.shape[x]); }
	vecShape(vop, shs).0
}

-- The axis a single-operand axis op works along.
fn vecAxis(vop VecOp) Int = match (vop) {
	take(_, ax): ax;  drop(_, ax): ax;  stride(_, ax): ax;  stutter(_, ax): ax;
	ncyc(_, ax): ax;  slice(_, _, ax): ax;  slicev(_, ax): ax;  rotate(ax): ax;
	reverse(ax): ax;  reduceAxis(_, ax): ax;  scan(_, ax): ax;  join(ax, _): ax;
	_: -1;
};

fn _isAxisOp(vop VecOp) Bool = match (vop) {
	at(_): false;  put(_): false;  permute(_): false;  reshape(_): false;
	join(_, _): false;  reduce(_): false;  _: true;
};

-- The result shape, and an error ("" when none).
fn vecShape(vop VecOp, shs [Shape]) (Shape, String) {
	let s = shs[0];
	match (vop) {
		at(ax):        _atShape(s, shs[1], ax);
		put(ax):       _putShape(s, shs[1], ax);
		reduce(_):     (scalarShape(), "");
		join(ax, new): new ? _stackShape(shs, ax) : _joinShape(shs, ax);
		permute(p):    _permuteShape(s, p);
		reshape(dims): _reshapeShape(s, dims);
		_:             _axisOpShape(vop, s);
	}
}

-- at along an axis: a scalar index removes the axis, a row of k sets it to
-- k. On a row (or scalar) a, the index's channel count, as before.
fn _atShape(a Shape, ix Shape, ax Int) (Shape, String) {
	if (a chans == 0 || ix chans == 0) { return (a, ""); }
	if (a.rank <= 1) { return (chansShape(ix chans), ""); }
	let d = a axisDim(ax);
	if (d < 0) { return (a, _axisErr(ax, a)); }
	if (ix.rank >= 2) { return (a, "at: the index must be a scalar or a row, not %^" fmt(ix toString)); }
	if (ix.rank == 0) { return (a removeDim(d), ""); }
	(a withDim(d, ix chans), "")
}

fn _putShape(a Shape, ix Shape, ax Int) (Shape, String) {
	if (a chans == 0 || a.rank <= 1) { return (a, ""); }
	if (a axisDim(ax) < 0) { return (a, _axisErr(ax, a)); }
	if (ix.rank >= 2) { return (a, "put: the index must be a scalar or a row, not %^" fmt(ix toString)); }
	(a, "")
}

fn _axisErr(ax Int, s Shape) String = "axis %^ is outside a rank-%^ signal" fmt(ax, max(s.rank, 1));

fn _axisOpShape(vop VecOp, s Shape) (Shape, String) {
	if (s chans == 0) { return (s, ""); }        -- not known yet
	let ax = vop vecAxis;
	let d = s axisDim(ax);
	if (d < 0) { return (s, _axisErr(ax, s)); }
	let a = s.dims[d];
	if (vop isReduceAxis) {
		return (s removeDim(d), "");
	}
	let len = match (vop) {
		take(n, _):     n;
		drop(n, _):     a - n;
		stride(n, _):   (a + n - 1) // n;
		stutter(n, _):  a * n;
		ncyc(n, _):     a * n;
		slice(_, l, _): l;
		slicev(l, _):   l;
		_:              a;
	};
	if (len < 1) { return (s, "%^ leaves no elements along axis %^" fmt(vop _vecName, ax)); }
	-- A scalar stays one when the op leaves it one element.
	if (s.rank == 0 && len == 1) { return (s, ""); }
	let out = s withDim(d, len);
	-- Rows keep the verification-only power-of-two rounding (row()).
	if (out.rank == 1) { return (row(len), ""); }
	(out, "")
}

fn isReduceAxis(vop VecOp) Bool = match (vop) { reduceAxis(_, _): true; _: false; };

fn _vecName(vop VecOp) String = match (vop) {
	take(_, _): "take";  drop(_, _): "drop";  stride(_, _): "stride";
	stutter(_, _): "stutter";  ncyc(_, _): "ncyc";  slice(_, _, _): "slice";
	slicev(_, _): "slice";  _: "shape op";
};

-- Concatenation along an axis of the inputs at their largest rank (a scalar
-- counts as a row). Inputs not known yet (0 channels) are skipped.
fn _joinShape(shs [Shape], ax Int) (Shape, String) {
	var r = 1;
	for (x : shs) { if (x chans > 0) { r = max(r, x.rank); } }
	var out = noShapeOfRank(r);
	var d = -1;
	var total = 0;
	var first = true;
	for (x0 : shs) {
		if (x0 chans == 0) { continue; }
		let x = Shape { dims: x0.dims, rank: r };
		if (first) {
			d = x axisDim(ax);
			if (d < 0) { return (x, _axisErr(ax, x)); }
			out = x;
			first = false;
		} else {
			var k = 0;
			while (k < MAX_RANK) {
				if (k != d && x.dims[k] != out.dims[k]) {
					return (out, "join: shapes %^ and %^ differ off axis %^" fmt(out toString, x toString, ax));
				}
				k = k + 1;
			}
		}
		total = total + x.dims[d];
	}
	if (first) { return (noShapeOfRank(1), ""); }
	if (r == 1) { return (chansShape(total pow2Compat), ""); }
	(out withDim(d, total), "")
}

-- Signals of one shape along a new axis (`axis` of the result). A
-- one-channel signal broadcasts to the others' shape.
fn _stackShape(shs [Shape], ax Int) (Shape, String) {
	var s = noShapeOfRank(1);
	var n = 0;
	for (x : shs) {
		if (x chans <= 1) { continue; }
		if (n == 0) { s = x; }
		else if (x != s) {
			return (s, "stack: shapes %^ and %^ differ" fmt(s toString, x toString));
		}
		n = n + 1;
	}
	if (n == 0) {
		for (x : shs) { if (x chans == 1) { s = x; n = 1; break; } }
		if (n == 0) { return (s, ""); }
	}
	if (s.rank >= MAX_RANK) { return (s, "stack: a rank-%^ signal has no room for a new axis" fmt(MAX_RANK)); }
	let r = s.rank + 1;
	let k = ax < 0 ? ax + r : ax;
	if (k < 0 || k >= r) { return (s, "axis %^ is outside a rank-%^ result" fmt(ax, r)); }
	var dims [Int] = [];
	let inDims = s dimsInUse;
	var j = 0;
	while (j < r) {
		if (j == k) { dims push!(shs length); }
		else { dims push!(inDims[j < k ? j : j - 1]); }
		j = j + 1;
	}
	(shape(dims), "")
}

fn noShapeOfRank(r Int) Shape = Shape { dims: [1, 1, 1, 0], rank: r };

-- Axes permuted: output axis k is input axis perm[k] (perm [] reverses them).
fn _permuteShape(s Shape, perm [Int]) (Shape, String) {
	let p = resolvePerm(s, perm);
	if (p length == 0) { return (s, "transpose: %^ is not a permutation of %^ axes" fmt(perm, s.rank)); }
	let r = s.rank;
	var dims = s.dims copy;
	var k = 0;
	while (k < r) { dims[MAX_RANK - r + k] = s.dims[MAX_RANK - r + p[k]]; k = k + 1; }
	(Shape { dims: dims, rank: r }, "")
}

-- perm as axes 0 .. r - 1 ([] = reversed); [] when it is not a permutation.
-- A rank-0 or rank-1 signal has only the identity.
fn resolvePerm(s Shape, perm [Int]) [Int] {
	let r = s.rank;
	var p [Int] = [];
	if (perm length == 0) {
		var k = r - 1;
		while (k >= 0) { p push!(k); k = k - 1; }
		return r == 0 ? [0] : p;
	}
	if (perm length != r) { return [Int](); }
	var seen [Bool] = [false, false, false, false];
	for (x : perm) {
		let ax = x < 0 ? x + r : x;
		if (ax < 0 || ax >= r || seen[ax]) { return [Int](); }
		seen[ax] = true;
		p push!(ax);
	}
	p
}

-- The same channels in new dims; one dim may be -1 (inferred).
fn _reshapeShape(s Shape, dims [Int]) (Shape, String) {
	if (s chans == 0) { return (s, ""); }
	if (dims length > MAX_RANK) { return (s, "reshape: more than %^ dims" fmt(MAX_RANK)); }
	var known = 1;
	var free = -1;
	var k = 0;
	for (x : dims) {
		if (x == -1 && free < 0) { free = k; }
		else if (x < 1) { return (s, "reshape: bad dim %^" fmt(x)); }
		else { known = known * x; }
		k = k + 1;
	}
	var d = dims copy;
	if (free >= 0) {
		if (s chans % known != 0) { return (s, "reshape: %^ channels do not divide by %^" fmt(s chans, known)); }
		d[free] = s chans // known;
	} else if (known != s chans) {
		return (s, "reshape: %^ is not %^ channels" fmt(dims, s chans));
	}
	if (d length == 0) { return (scalarShape(), ""); }
	(shape(d), "")
}

-- The recomputed shape of node n from its inputs. Elementwise ops broadcast
-- (lcm per dimension); sources keep the shape they were declared with.
fn calcShapeOf(ctx Ctx, n NIdx) Shape {
	let ins = ctx.ins[n];
	match (ctx.kind[n]) {
		constant(_, _):      ctx.shape[n];      -- declared (or folded) shape
		sampleRate:          scalarShape();
		sampleDur:           scalarShape();
		sharedInK(_, _):     scalarShape();
		noteParamK(_, _, _): ctx.shape[n];      -- fixed at import
		inletK(_, _, _):     ctx.shape[n];
		outletK(_, _):       ctx.shape[ins[0]];
		unopK(_):            ctx.shape[ins[0]];
		binopK(_):           broadcast(ctx.shape[ins[0]], ctx.shape[ins[1]]);
		compareopK(_):       broadcast(ctx.shape[ins[0]], ctx.shape[ins[1]]);
		castopK(_):          ctx.shape[ins[0]];
		reduceK(_, cols):    chansShape(cols);
		vecK(vop):           _vecShapeOf(ctx, vop, ins);
		selectK:             _selectShape(ctx, n);
		urandK(_):           ctx.shape[n];      -- fixed at import
		birandK(_):          ctx.shape[n];
		rand64K(_):          ctx.shape[n];
		maxDelayK(_):        ctx.shape[ins[0]];
		delayInitK(_, _):    ctx.shape[n];      -- stays unset
		delayFixReadK(d, _): ctx.delays[d].shape;
		delayVarReadK(d, _): ctx.delays[d].shape;
		delayWriteK(_):      ctx.shape[ins[0]];
		bufFixReadK(_, _, readChans, _): chansShape(readChans);
		bufVarReadK(_, _, readChans, _): chansShape(readChans);
		bufWriteK(_, _, _):  ctx.shape[ins[0]];
		bufLengthK(_):       scalarShape();
		bankLookupK(_):      scalarShape();
		bankFixReadK(_, readChans, _): chansShape(readChans);
		bankVarReadK(_, readChans, _): chansShape(readChans);
		bankRootKeyK:        scalarShape();
		bankSampleRateK:     scalarShape();
		bankLengthK:         scalarShape();
		bankLoopStartK:      scalarShape();
		bankLoopEndK:        scalarShape();
		bankHasLoopK:        scalarShape();
		-- Control flow: a control-flow node broadcasts over its branch/body phis
		-- (the test/selector/count input is scalar and doesn't contribute); a
		-- phi broadcasts its (current, target, input) shapes.
		ifK:                 _cfShape(ctx, n);
		switchK(_):          _cfShape(ctx, n);
		forK:                _cfShape(ctx, n);
		voicerK(mv):         _voicerShape(ctx, n, mv);
		spectralChainK(_, _): ctx.shape[ctx.ins[n][0]];   -- output = input shape
		spectralFrameK(fft):  _spectralFrameShapeOf(ctx, n, fft);
		phiK(t):             _phiShape(ctx, n, t);
		varK(_):             ctx.shape[n];   -- unset: broadcasts to its use
		debugK(_, _, _, _):  scalarShape();
		voiceDoneK:          scalarShape();
	}
}

-- Control-flow node shape: broadcast over the branch/body phis (ctx.subs).
fn _cfShape(ctx Ctx, n NIdx) Shape {
	var sh = _noShape();
	for (s : ctx.subs[n]) { sh = broadcast(sh, ctx.shape[s]); }
	sh
}

-- Voicer shape: the body root's with a leading voice dimension, computed
-- from the body root rather than broadcasting over the phi -- the phi
-- broadcasts UP to the voicer's (target), so reading it back would be
-- circular.
fn _voicerShape(ctx Ctx, n NIdx, maxVoices Int) Shape {
	let bodyPhi = ctx.subs[n][0];
	let bodyRoot = ctx.ins[bodyPhi][0];
	prependDim(ctx.shape[bodyRoot], maxVoices)
}

-- A spectral frame is [chans, 2, fftSize/2] over the chain's input channels.
fn _spectralFrameShapeOf(ctx Ctx, n NIdx, fft Int) Shape {
	let chain = ctx spectralChainOfFrame(n);
	if (chain == NONE) { return ctx.shape[n]; }
	spectralFrameShape(ctx.shape[ctx.ins[chain][0]] chans, fft)
}

-- PhiNode shape: broadcast(current, target, input). A voicer target has a
-- voice dimension the phi (one voice's output) does not, so it is skipped;
-- a spectral body's phi is the chain's frame.
fn _phiShape(ctx Ctx, n NIdx, target Int) Shape {
	var sh = ctx.shape[n];
	-- A spectral body's result is a whole frame (a scalar result fills it),
	-- not the chain's time-domain output.
	if (target != NONE) {
		match (ctx.kind[target]) {
			spectralChainK(fft, _): {
				return spectralFrameShape(ctx.shape[ctx.ins[target][0]] chans, fft);
			}
			_: {}
		}
	}
	if (target != NONE && !(ctx.kind[target] isVoicerKind)) {
		sh = broadcast(sh, ctx.shape[target]);
	}
	broadcast(sh, ctx.shape[ctx.ins[n][0]])
}

fn _selectShape(ctx Ctx, n NIdx) Shape {
	var sh = _noShape();
	for (i : ctx.ins[n]) { sh = broadcast(sh, ctx.shape[i]); }
	sh
}

---------------------------------------------------------------------------
-- Type rules -- mirrors Expr::update_type() / inputTypeConstraint()
--
-- updateTypeOf intersects the node's type with the relevant input types,
-- errors on an empty intersection, and (when something narrowed) re-queues the
-- dependents via the shared propagate helpers. Delay reads/writes also couple
-- through the delay buffer's type.

fn _checkType(ctx Ctx, n NIdx, t NumType) Bool {
	if (t isEmpty) {
		ctx.errors push!("type error: node %^ %^ intersected to empty"
			fmt(ctx.serial[n], nodeStr(ctx, n)));
		return false;
	}
	true
}

-- inputTypeConstraint(i): the type a node imposes on its input i. Defaults to
-- the node's own type; a few kinds override.
fn inputConstraintOf(ctx Ctx, n NIdx, i Int) NumType = match (ctx.kind[n]) {
	compareopK(_): ctx.widening[0] ? numDown(ctx.cmpTyp[n]) : ctx.typ[n];
	                                    -- unifying: handled via input_type below
	castopK(_):    ANY_NUM;             -- cast input is unconstrained
	selectK:       i == 0 ? ANY_INT : (ctx.widening[0] ? numDown(ctx.typ[n]) : ctx.typ[n]);
	binopK(_):     ctx.widening[0] ? numDown(ctx.typ[n]) : ctx.typ[n];
	-- vec input constraints: put's index (i=1) is integer, its array+value are the
	-- output type; join constrains every input to the output type (base default);
	-- at + the reorders constrain the data input (0) to the type, indices to int.
	vecK(vop):     _vecInputConstraint(vop, ctx.typ[n], i);
	ifK:           ANY_INT;             -- test must be integer (input 0)
	switchK(_):    ANY_INT;             -- selector must be integer
	forK:          ANY_INT;             -- count must be integer
	spectralChainK(_, _): ANY_NUM;      -- audio input, unconstrained
	_:             ctx.typ[n];
};

-- propagate_input_type + propagate_output_type (Expr::propagate_types).
fn _propagateTypes(ctx Ctx, n NIdx, wl WorkList) Void {
	let ins = ctx.ins[n];
	var i = 0;
	while (i < ins length) {
		let inp = ins[i];
		let newt = inputConstraintOf(ctx, n, i) & ctx.typ[inp];
		if (newt != ctx.typ[inp]) {
			ctx.typ[inp] = newt;
			wl wlPush(inp);
		}
		i = i + 1;
	}
	for (c : ctx.consumers[n]) { wl wlPush(c); }
}

fn _propagateDelayTypes(ctx Ctx, d Int, wl WorkList) Void {
	for (u : ctx.delays[d].fixReaders) { wl wlPush(u); }
	for (u : ctx.delays[d].varReaders) { wl wlPush(u); }
	for (u : ctx.delays[d].initters) { wl wlPush(u); }
	if (ctx.delays[d].writer != NONE) { wl wlPush(ctx.delays[d].writer); }
}

-- Intersect ctx.typ[n] with `newt`; on change, store and propagate. Returns
-- false on a type error (empty intersection).
fn _narrow(ctx Ctx, n NIdx, newt NumType, wl WorkList) Bool {
	if (!_checkType(ctx, n, newt)) { return false; }
	if (newt != ctx.typ[n]) {
		ctx.typ[n] = newt;
		_propagateTypes(ctx, n, wl);
	}
	true
}

fn updateTypeOf(ctx Ctx, n NIdx, wl WorkList) Void {
	let ins = ctx.ins[n];
	match (ctx.kind[n]) {
		outletK(_, _):       { ctx.typ[n] = ctx.typ[ins[0]]; }
		debugK(_, _, _, _):  { ctx.typ[n] = ctx.typ[ins[0]]; }
		voiceDoneK:          { ctx.typ[n] = ctx.typ[ins[0]]; }
		unopK(_):            { _narrow(ctx, n, ctx.typ[n] & ctx.typ[ins[0]], wl); }
		binopK(_):           {
			-- Widening: the result is no narrower than either operand.
			_narrow(ctx, n, ctx.widening[0]
				? ctx.typ[n] & numUp(ctx.typ[ins[0]]) & numUp(ctx.typ[ins[1]])
				: ctx.typ[n] & ctx.typ[ins[0]] & ctx.typ[ins[1]], wl);
		}
		compareopK(_):       { _updateCompare(ctx, n, wl); }
		castopK(_):          {}                  -- output is cast_type; input unconstrained
		reduceK(_, _):       { _narrow(ctx, n, ctx.typ[n] & ctx.typ[ins[0]], wl); }
		-- put narrows by its array + value (skip the integer index); join by all
		-- inputs; at + the reorders narrow by the data input.
		vecK(vop):           { _narrow(ctx, n, _vecNarrowType(ctx, n, vop, ins), wl); }
		selectK:             { _updateSelect(ctx, n, wl); }
		ifK:                 { _updateCF(ctx, n, wl); }
		switchK(_):          { _updateCF(ctx, n, wl); }
		forK:                { _updateCF(ctx, n, wl); }
		voicerK(_):          { _updateCF(ctx, n, wl); }   -- no test input; narrows from body phi
		spectralChainK(_, _): {}   -- f32 output from the overlap-add buffer; the body phi writes the f32 frame
		phiK(target):        { _updatePhi(ctx, n, target, wl); }
		delayFixReadK(d, _): { _updateDelayRead(ctx, n, d, wl); }
		delayVarReadK(d, _): { _updateDelayRead(ctx, n, d, wl); }
		delayWriteK(d):      { _updateDelayWrite(ctx, n, d, wl); }
		_:                   {}                  -- constants, fs, sd, control,
		                                         -- noteParam, inlet, random,
		                                         -- maxDelay, delayInit: fixed
	}
}

-- CompareOp: the OUTPUT type stays unconstrained (`any`); only the merged
-- input_type narrows, and a change re-queues consumers/inputs so the operand
-- types converge. Mirrors CompareOpExpr::update_type.
fn _updateCompare(ctx Ctx, n NIdx, wl WorkList) Void {
	let ins = ctx.ins[n];
	if (ctx.widening[0]) {
		-- Widening (mirrors CompareOpExpr::update_type under gWidenTypes): the
		-- stored input_type is no narrower than either operand.
		let newIn = ctx.cmpTyp[n] & numUp(ctx.typ[ins[0]]) & numUp(ctx.typ[ins[1]]);
		if (!_checkType(ctx, n, newIn)) { return; }
		if (newIn != ctx.cmpTyp[n]) {
			ctx.cmpTyp[n] = newIn;
			_propagateTypes(ctx, n, wl);
		}
		return;
	}
	let merged = ctx.typ[ins[0]] & ctx.typ[ins[1]];
	if (!_checkType(ctx, n, merged)) { return; }
	-- push the merged constraint back onto both operands
	var changed = false;
	var i = 0;
	while (i < ins length) {
		let inp = ins[i];
		let newt = merged & ctx.typ[inp];
		if (newt != ctx.typ[inp]) { ctx.typ[inp] = newt; wl wlPush(inp); changed = true; }
		i = i + 1;
	}
	if (changed) { for (c : ctx.consumers[n]) { wl wlPush(c); } }
}

-- Select: type = type & (each value input, skipping the selector at index 0).
fn _updateSelect(ctx Ctx, n NIdx, wl WorkList) Void {
	let ins = ctx.ins[n];
	var newt = ctx.typ[n];
	var i = 1;
	while (i < ins length) {
		newt = newt & (ctx.widening[0] ? numUp(ctx.typ[ins[i]]) : ctx.typ[ins[i]]);
		i = i + 1;
	}
	_narrow(ctx, n, newt, wl);
}

-- Public form of _propagateTypes, for the widening driver in passes.x.
fn propagateTypes(ctx Ctx, n NIdx, wl WorkList) Void = _propagateTypes(ctx, n, wl);

-- The operations whose operand constraints widening relaxes.
fn isWideningOp(ctx Ctx, n NIdx) Bool = match (ctx.kind[n]) {
	binopK(_):     true;
	compareopK(_): true;
	selectK:       true;
	_:             false;
};

-- propagate_input_type alone (no consumer re-queue).
fn _propagateInputs(ctx Ctx, n NIdx, wl WorkList) Void {
	let ins = ctx.ins[n];
	var i = 0;
	while (i < ins length) {
		let inp = ins[i];
		let newt = inputConstraintOf(ctx, n, i) & ctx.typ[inp];
		if (newt != ctx.typ[inp]) {
			ctx.typ[inp] = newt;
			wl wlPush(inp);
		}
		i = i + 1;
	}
}

-- Mirrors drainTypes in synthdef_synth.cpp: under widening, a visit to a
-- widening op also applies its operand constraints (update_type propagates
-- them only when the node's own type changes).
fn drainTypes(ctx Ctx, wl WorkList) Void {
	while (!(wl wlEmpty)) {
		let n = wl wlPop;
		if (ctx.widening[0] && ctx isWideningOp(n)) { _propagateInputs(ctx, n, wl); }
		updateTypeOf(ctx, n, wl);
	}
}

-- Control-flow node (if/switch/for): type = type & each branch/body phi type
-- (the subs). On change, propagate (constrains the test/selector/count input to
-- any_int via inputConstraintOf, re-queues consumers) and push the merged type
-- back to each branch phi. Mirrors IfElseExpr/SwitchExpr/ForLoopExpr::update_type.
fn _updateCF(ctx Ctx, n NIdx, wl WorkList) Void {
	-- Constrain the test/selector/count input(s) to any_int. This is an
	-- invariant, so apply it on every visit, not only when the node's own type
	-- narrows. (The C++ applies it inside the type-change branch, but its
	-- worklist order runs the node from `any` before a consumer pins its type;
	-- applying it unconditionally reaches the same fixpoint independent of order.)
	let ins = ctx.ins[n];
	var i = 0;
	while (i < ins length) {
		let inp = ins[i];
		let it = inputConstraintOf(ctx, n, i) & ctx.typ[inp];
		if (it != ctx.typ[inp]) { ctx.typ[inp] = it; wl wlPush(inp); }
		i = i + 1;
	}
	-- Narrow own type from the branch/body phis; on change re-queue consumers
	-- and push the merged type back to each phi.
	var newt = ctx.typ[n];
	for (s : ctx.subs[n]) { newt = newt & ctx.typ[s]; }
	if (!_checkType(ctx, n, newt)) { return; }
	if (newt != ctx.typ[n]) {
		ctx.typ[n] = newt;
		for (c : ctx.consumers[n]) { wl wlPush(c); }
		for (s : ctx.subs[n]) {
			if (ctx.typ[s] != newt) { ctx.typ[s] = newt; wl wlPush(s); }
		}
	}
}

-- PhiNode: type = type & in0; on change, propagate to in0 (+ consumers) and
-- re-queue the control-flow target. Mirrors PhiNodeExpr::update_type.
fn _updatePhi(ctx Ctx, n NIdx, target Int, wl WorkList) Void {
	let newt = ctx.typ[n] & ctx.typ[ctx.ins[n][0]];
	if (!_checkType(ctx, n, newt)) { return; }
	if (newt != ctx.typ[n]) {
		ctx.typ[n] = newt;
		_propagateTypes(ctx, n, wl);
		if (target != NONE && ctx.typ[target] != newt) { wl wlPush(target); }
	}
}

fn _updateDelayRead(ctx Ctx, n NIdx, d Int, wl WorkList) Void {
	let newt = ctx.typ[n] & ctx.delays[d].typ;
	if (!_checkType(ctx, n, newt)) { return; }
	if (newt != ctx.typ[n]) {
		ctx.typ[n] = newt;
		_propagateTypes(ctx, n, wl);
	}
	if (ctx.delays[d].typ != newt) {
		ctx.delays[d] = DelayInfo { ...ctx.delays[d], typ: newt };
		_propagateDelayTypes(ctx, d, wl);
	}
}

fn _updateDelayWrite(ctx Ctx, n NIdx, d Int, wl WorkList) Void {
	let ins = ctx.ins[n];
	let newt = ctx.typ[n] & ctx.typ[ins[0]] & ctx.delays[d].typ;
	if (!_checkType(ctx, n, newt)) { return; }
	ctx.typ[n] = newt;
	_propagateTypes(ctx, n, wl);
	if (ctx.delays[d].typ != newt) {
		ctx.delays[d] = DelayInfo { ...ctx.delays[d], typ: newt };
		_propagateDelayTypes(ctx, d, wl);
	}
}
