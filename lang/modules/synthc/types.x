-- synthc/types.x
-- Per-kind shape and type rules for the analysis passes, ported from the
-- virtual methods of the C++ Expr hierarchy (synthdef_expr.cpp/.hpp):
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
-- Shape (channel count) rules -- mirrors Expr::calcShape()

fn _broadcastC(a Int, b Int) Int = max(a, b);

-- Returns the recomputed channel count for node n from its inputs.
fn calcShapeOf(ctx Ctx, n NIdx) Int {
	let ins = ctx.ins[n];
	match (ctx.kind[n]) {
		constant(v, _):      v constSize;
		sampleRate:          1;
		sampleDur:           1;
		control(_, _, _):    ctx.chans[n];     -- fixed at import
		noteParamK(_, _, _): ctx.chans[n];
		inletK(_, _):        ctx.chans[n];
		outletK(_, _):       ctx.chans[ins[0]];
		unopK(_):            ctx.chans[ins[0]];
		binopK(_):           _broadcastC(ctx.chans[ins[0]], ctx.chans[ins[1]]);
		compareopK(_):       _broadcastC(ctx.chans[ins[0]], ctx.chans[ins[1]]);
		castopK(_):          ctx.chans[ins[0]];
		reduceK(_, cols):    cols;
		vecK(vop):           vecOutChans(ctx.chans[ins[0]], vop);
		selectK:             _selectChans(ctx, n);
		urandK(_):           ctx.chans[n];      -- fixed at import
		birandK(_):          ctx.chans[n];
		rand64K(_):          ctx.chans[n];
		maxDelayK(_):        ctx.chans[ins[0]];
		delayInitK(_, _):    ctx.chans[n];      -- no-op: stays 0
		delayFixReadK(d, _): ctx.delays[d].chans;
		delayVarReadK(d, _): ctx.delays[d].chans;
		delayWriteK(_):      ctx.chans[ins[0]];
		bufFixReadK(_, _, readChans, _): readChans;
		bufVarReadK(_, _, readChans, _): readChans;
		bufWriteK(_, _, _):  ctx.chans[ins[0]];   -- BufWrite::calcShape: in0().chans
		bufLengthK(_):       1;
		-- Control flow: a control-flow node broadcasts over its branch/body phis
		-- (the test/selector/count input is scalar and doesn't contribute); a
		-- phi broadcasts its (current, target, input) channel counts.
		ifK:                 _cfChans(ctx, n);
		switchK(_):          _cfChans(ctx, n);
		forK:                _cfChans(ctx, n);
		phiK(t):             _phiChans(ctx, n, t);
		varK(_):             ctx.chans[n];   -- fixed at import (scalar loop var)
		debugK(_, _, _, _):  1;
	}
}

-- Control-flow node shape: broadcast over the branch/body phis (ctx.subs).
fn _cfChans(ctx Ctx, n NIdx) Int {
	var ch = 0;
	for (s : ctx.subs[n]) { ch = _broadcastC(ch, ctx.chans[s]); }
	ch
}

-- PhiNode shape: broadcast(current, target, input). Mirrors PhiNodeExpr::calcShape.
fn _phiChans(ctx Ctx, n NIdx, target Int) Int {
	var ch = ctx.chans[n];
	if (target != NONE) { ch = _broadcastC(ch, ctx.chans[target]); }
	_broadcastC(ch, ctx.chans[ctx.ins[n][0]])
}

fn _selectChans(ctx Ctx, n NIdx) Int {
	let ins = ctx.ins[n];
	var ch = ctx.chans[ins[0]];
	var i = 1;
	while (i < ins length) {
		ch = _broadcastC(ch, ctx.chans[ins[i]]);
		i = i + 1;
	}
	ch
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
	compareopK(_): ctx.typ[n];          -- handled via input_type below; see note
	castopK(_):    ANY_NUM;             -- cast input is unconstrained
	selectK:       i == 0 ? ANY_INT : ctx.typ[n];
	vecK(_):       i == 0 ? ctx.typ[n] : ANY_INT;   -- data input flows; rotate's index is integer
	ifK:           ANY_INT;             -- test must be integer (input 0)
	switchK(_):    ANY_INT;             -- selector must be integer
	forK:          ANY_INT;             -- count must be integer
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
		binopK(_):           { _narrow(ctx, n, ctx.typ[n] & ctx.typ[ins[0]] & ctx.typ[ins[1]], wl); }
		compareopK(_):       { _updateCompare(ctx, n, wl); }
		castopK(_):          {}                  -- output is cast_type; input unconstrained
		reduceK(_, _):       { _narrow(ctx, n, ctx.typ[n] & ctx.typ[ins[0]], wl); }
		vecK(_):             { _narrow(ctx, n, ctx.typ[n] & ctx.typ[ins[0]], wl); }
		selectK:             { _updateSelect(ctx, n, wl); }
		ifK:                 { _updateCF(ctx, n, wl); }
		switchK(_):          { _updateCF(ctx, n, wl); }
		forK:                { _updateCF(ctx, n, wl); }
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
		newt = newt & ctx.typ[ins[i]];
		i = i + 1;
	}
	_narrow(ctx, n, newt, wl);
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
