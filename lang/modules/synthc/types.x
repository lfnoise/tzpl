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
	put:  i == 1 ? ANY_INT : nodeTyp;
	join: nodeTyp;
	_:    i == 0 ? nodeTyp : ANY_INT;
};
fn _vecNarrowType(ctx Ctx, n NIdx, vop VecOp, ins [Int]) NumType = match (vop) {
	put:  ctx.typ[n] & ctx.typ[ins[0]] & ctx.typ[ins[2]];
	join: { var t = ctx.typ[n]; for (x : ins) { t = t & ctx.typ[x]; } t }
	_:    ctx.typ[n] & ctx.typ[ins[0]];
};

-- Vec ops work on the row-major flattening of their input and yield a row.
-- at = the index vector's chans; put = the source array's chans; join = the
-- sum of all inputs; the single-input reorders use vecOutChans.
fn _vecShapeOf(ctx Ctx, vop VecOp, ins [Int]) Shape = match (vop) {
	at:   row(ctx.chans[ins[1]]);
	put:  row(ctx.chans[ins[0]]);
	join: { var raw = 0; for (x : ins) { raw = raw + ctx.chans[x]; } row(raw) }
	_:    row(vecOutChans(ctx.chans[ins[0]], vop));
};

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
		reduceK(_, cols):    row(cols);
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
		bufFixReadK(_, _, readChans, _): row(readChans);
		bufVarReadK(_, _, readChans, _): row(readChans);
		bufWriteK(_, _, _):  ctx.shape[ins[0]];
		bufLengthK(_):       scalarShape();
		bankLookupK(_):      scalarShape();
		bankFixReadK(_, readChans, _): row(readChans);
		bankVarReadK(_, readChans, _): row(readChans);
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
		spectralFrameK(_):    ctx.shape[n];               -- inputChans * fftSize, set at import
		phiK(t):             _phiShape(ctx, n, t);
		varK(_):             ctx.shape[n];   -- unset: broadcasts to its use
		debugK(_, _, _, _):  scalarShape();
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

-- PhiNode shape: broadcast(current, target, input). A voicer target has a
-- voice dimension the phi (one voice's output) does not, so it is skipped.
fn _phiShape(ctx Ctx, n NIdx, target Int) Shape {
	var sh = ctx.shape[n];
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
		spectralChainK(_, _): { _updateCF(ctx, n, wl); } -- f32 chain, narrows from body phi
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
