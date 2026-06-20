-- synthc/import.x
-- Imports a synthdef.x SignalGraph (the in-memory front-end graph) into a
-- synthc Ctx, mirroring the C++ s-expression builder exactly:
-- same serial allocation (including serials wasted on hash-consed nodes),
-- same constant handling, same initial rates/types/shapes, same delay
-- bookkeeping. This is what makes the analysis dumps of the two compilers
-- byte-comparable.
--
-- Reference: synthdef-compiler/src/synthdef_from_sexpr.cpp (parse* fns)
-- and addExpr/addConstantExpr in synthdef_synth.cpp.

import synthdef.*;
import synthc.ir.*;
import synthc.fold.*;
import synthc.rewrite.*;

---------------------------------------------------------------------------
-- Entry point

fn importGraph(g SignalGraph, name String, applyRewrites Bool = false) Ctx {
	var `scCtx = newCtx(name);
	var `scIdMap [Int: Int] = [:];        -- front-end expr id -> node index
	var `scDelayIdMap [Int: Int] = [:];   -- front-end DelayVar id -> delay idx
	var `scBufIdMap [Int: Int] = [:];     -- front-end BufferVar id -> buf idx
	var `scConsMap [String: Int] = [:];   -- hash-cons key -> node index
	-- Algebraic rewriting (interleaved with fold + hash-cons in _makeOp). Off by
	-- default so rewrites-off dumps stay byte-identical; the C++ gApplyRewrites
	-- mirror enables it for the real defSynth path and rewrites-on diffing.
	var `scApplyRewrites Bool = applyRewrites;

	-- Serial counters (mirror the Synth:: serial number fields).
	var `scExprSerials Int = 0;
	var `scDelaySerials Int = 0;
	var `scBufSerials Int = 0;
	var `scControlSerials Int = 0;
	var `scNoteParamSerials Int = 1;      -- 0 is reserved for gate
	var `scInletSerials Int = 0;
	var `scOutletSerials Int = 0;
	var `scRandSerials Int = 0;
	var `scDebugSerials Int = 0;

	for (e : g.exprs) {
		_importExpr(e);
	}
	`scCtx
}

fn _impError(msg String) Void {
	var ctx Ctx = `scCtx;
	ctx.errors push!(msg);
}

---------------------------------------------------------------------------
-- Node id resolution

fn _nodeOf(id Int) Int {
	var m [Int: Int] = `scIdMap;
	match (m[id]) {
		some(n): n;
		none: {
			_impError("input id %^ not found" fmt(id));
			0
		}
	}
}

fn _resolveIns(e SignalExpr) [Int] {
	var out [Int] = [];
	for (x : e.ins) {
		out push!(x.id _nodeOf);
	}
	out
}

fn _mapId(id Int, n Int) Void {
	var m [Int: Int] = `scIdMap;
	m[id] = n;
}

---------------------------------------------------------------------------
-- Hash-cons keys
--
-- The C++ hashConsSet compares typeid + identical inputs + equals_().
-- Distinct kind prefixes stand in for typeid; input node indices are
-- canonical identities.

fn _insKey(ins [Int]) String {
	var parts [String] = [];
	for (i : ins) { parts push!(i toString); }
	parts join(",")
}

fn _specKey(spec ControlSpec) String {
	"%^,%^,%^,%^" fmt(spec.lo, spec.hi, spec.init, spec.warp ordinal)
}

fn _constValKey(v ConstVal) String = match (v) {
	ints(a): {
		var parts [String] = [];
		for (x : a) { parts push!(x toString); }
		"I:" $ parts join(",")
	}
	floats(a): {
		var parts [String] = [];
		for (x : a) { parts push!(x toString); }
		"F:" $ parts join(",")
	}
};

fn _vecOpKey(vop VecOp) String = match (vop) {
	take(n):      "t" $ n toString;
	drop(n):      "d" $ n toString;
	stride(n):    "s" $ n toString;
	stutter(n):   "u" $ n toString;
	ncyc(n):      "c" $ n toString;
	reverse:      "r";
	transpose(n): "x" $ n toString;
	rotate:       "o";
	_:            "?";
};

fn _kindKey(kind NodeKind) String = match (kind) {
	constant(v, t):       "K|" $ v _constValKey $ "|" $ t.0 toString;
	sampleRate:           "SR";
	sampleDur:            "SD";
	control(spec, _, sn): "CTL|" $ spec _specKey $ "|" $ sn toString;
	noteParamK(spec, _, sn): "NP|" $ spec _specKey $ "|" $ sn toString;
	inletK(_, sn):        "IN|" $ sn toString;
	outletK(_, sn):       "OUT|" $ sn toString;
	unopK(op):            "U|" $ op ordinal toString;
	binopK(op):           "B|" $ op ordinal toString;
	compareopK(op):       "C|" $ op ordinal toString;
	castopK(t):           "X|" $ t.0 toString;
	reduceK(op, cols):    "R|" $ op ordinal toString $ "|" $ cols toString;
	vecK(vop):            "V|" $ vop _vecOpKey;
	selectK:              "SEL";
	urandK(sn):           "UR|" $ sn toString;
	birandK(sn):          "BR|" $ sn toString;
	rand64K(sn):          "R64|" $ sn toString;
	maxDelayK(d):         "MD|" $ d toString;
	delayInitK(d, off):   "DI|" $ d toString $ "|" $ off toString;
	delayFixReadK(d, k):  "DFR|" $ d toString $ "|" $ k toString;
	delayVarReadK(d, ip): "DVR|" $ d toString $ "|" $ ip ordinal toString;
	delayWriteK(d):       "DW|" $ d toString;
	bufFixReadK(bi, index, rc, sc): "BFR|" $ bi toString $ "|" $ index toString $ "|" $ rc toString $ "|" $ sc toString;
	bufVarReadK(bi, ip, rc, sc):    "BVR|" $ bi toString $ "|" $ ip ordinal toString $ "|" $ rc toString $ "|" $ sc toString;
	bufWriteK(bi, wc, sc):          "BW|" $ bi toString $ "|" $ wc toString $ "|" $ sc toString;
	bufLengthK(bi):                 "BL|" $ bi toString;
	debugK(_, _, _, sn):  "DBG|" $ sn toString;
};

fn _consKey(kind NodeKind, ins [Int]) String = kind _kindKey $ "#" $ ins _insKey;

---------------------------------------------------------------------------
-- Node creation
--
-- _appendNode is the raw array append plus the per-kind bookkeeping that
-- C++ addExpr does after the hash-cons check (sinks/inlets/outlets/...,
-- delay record updates, RNG flag).

fn _appendNode(kind NodeKind, ins [Int], serial Int, r Rate, t NumType, ch Int) Int {
	var ctx Ctx = `scCtx;
	let idx = ctx numNodes;
	ctx.kind push!(kind);
	ctx.ins push!(ins);
	ctx.serial push!(serial);
	ctx.nrate push!(r);
	ctx.typ push!(t);
	ctx.chans push!(ch);
	ctx.cut push!(GraphCut.none);
	ctx.treeOf push!(NONE);
	ctx.consumers push!([Int]());

	if (kind isSinkKind) { ctx.sinks push!(idx); }

	match (kind) {
		inletK(_, _):        ctx.inlets push!(idx);
		outletK(_, _):       ctx.outlets push!(idx);
		control(_, _, _):    ctx.controls push!(idx);
		noteParamK(_, _, _): ctx.noteParams push!(idx);
		delayInitK(d, _):    ctx.delays[d].initters push!(idx);
		delayFixReadK(d, _): ctx.delays[d].fixReaders push!(idx);
		delayVarReadK(d, _): ctx.delays[d].varReaders push!(idx);
		delayWriteK(d): {
			if (ctx.delays[d].writer != NONE) {
				_impError("Delay buffer already has a writer");
			}
			ctx.delays[d] = DelayInfo { ...ctx.delays[d], writer: idx };
		}
		urandK(_):  { ctx.usesRng[0] = true; }
		birandK(_): { ctx.usesRng[0] = true; }
		rand64K(_): { ctx.usesRng[0] = true; }
		_: {}
	}

	idx
}

-- Mirrors addExpr(): consume an expr serial, hash-cons, then append with
-- initial type/shape. Returns the node index (an existing one if consed).
fn _addExprNode(kind NodeKind, ins [Int], r Rate, t NumType, ch Int) Int {
	let serial Int = `scExprSerials;
	`scExprSerials = serial + 1;
	_addExprNodeS(serial, kind, ins, r, t, ch)
}

-- Same, but with the serial (userial) already consumed by the caller. _makeOp
-- consumes the candidate's serial before rewriting -- mirroring the C++ Expr
-- ctor, where `new BinaryOpExpr` burns a userial even if addExpr() then rewrites
-- or hash-conses it away -- so a rewritten/duplicate node still leaves the same
-- serial gap.
fn _addExprNodeS(serial Int, kind NodeKind, ins [Int], r Rate, t NumType, ch Int) Int {
	let conses = kind shouldHashCons;
	let key = conses ? _consKey(kind, ins) : "";
	var m [String: Int] = `scConsMap;
	if (conses) {
		match (m[key]) {
			some(n): { return n; }
			none: {}
		}
	}

	let idx = _appendNode(kind, ins, serial, r, t, ch);
	if (conses) {
		m[key] = idx;
	}
	idx
}

-- Mirrors addConstantExpr(): no hash-consing, no calcShape, type stays
-- NumType::any (the init_type lives in the kind payload for inference).
fn _addConstantNode(kind NodeKind, ch Int) Int {
	let serial Int = `scExprSerials;
	`scExprSerials = serial + 1;
	_appendNode(kind, [Int](), serial, Rate.constant, ANY_NUM, ch)
}

-- A constant produced by constant folding. Unlike imported constants, the C++
-- routes folded constants through addExpr: they are hash-consed and get their
-- type set to init_type (any_float for f64 results, any for i64, or the cast
-- target type). Mirror that by reusing _addExprNode with a constant kind.
fn _addFoldedConstant(v ConstVal, initType NumType) Int {
	_addExprNode(NodeKind.constant(v, initType), [Int](), Rate.constant, initType, v constSize)
}

fn _constValOf(ctx Ctx, n NIdx) ConstVal = match (ctx.kind[n]) {
	constant(v, _): v;
	_:              ConstVal.floats([0.0]);   -- unreachable: guarded by isConstantKind
};

-- init_type for a non-cast fold result: f64 storage -> any_float, i64 -> any
-- (matching the Constant constructor defaults).
fn _foldInitType(v ConstVal) NumType = match (v) {
	ints(_):   ANY_NUM;
	floats(_): ANY_FLOAT;
};

---------------------------------------------------------------------------
-- Rate helpers

fn _maxRateOf(ins [Int]) Rate {
	var ctx Ctx = `scCtx;
	var r = Rate.constant;
	for (i : ins) {
		r = max(r, ctx.nrate[i]);
	}
	r
}

fn _broadcastChans(ins [Int]) Int {
	var ctx Ctx = `scCtx;
	var ch = 0;
	for (i : ins) {
		ch = max(ch, ctx.chans[i]);
	}
	ch
}

---------------------------------------------------------------------------
-- Supported-op checks (mirror the C++ s-expression parser's op maps; ops
-- outside these sets are rejected by the reference compiler)

fn _unopSupported(op UnaryOp) Bool = match (op) {
	neg: true;  not: true;  bitNot: true;  abs: true;
	floor: true;  ceil: true;  trunc: true;  round: true;
	sqrt: true;  cbrt: true;
	exp: true;  exp2: true;  exp10: true;  expm1: true;
	log: true;  log2: true;  log10: true;  log1p: true;
	sin: true;  cos: true;  tan: true;
	asin: true;  acos: true;  atan: true;
	sinh: true;  cosh: true;  tanh: true;
	asinh: true;  acosh: true;  atanh: true;
	sinpi: true;  cospi: true;  tanpi: true;
	erf: true;  erfc: true;
	_: false;
};

fn _binopSupported(op BinaryOp) Bool = match (op) {
	add: true;  sub: true;  mul: true;  div: true;  mod: true;
	bitAnd: true;  bitOr: true;  bitXor: true;
	shiftLeft: true;  shiftRight: true;  unsignedShiftRight: true;
	min: true;  max: true;
	pow: true;  hypot: true;  atan2: true;  copysign: true;
	_: false;
};

---------------------------------------------------------------------------
-- Delay records

fn _delayIdxFor(dvId Int) Int {
	var m [Int: Int] = `scDelayIdMap;
	match (m[dvId]) {
		some(d): d;
		none: {
			let serial Int = `scDelaySerials;
			`scDelaySerials = serial + 1;
			var ctx Ctx = `scCtx;
			let d = ctx.delays length;
			ctx.delays push!(newDelayInfo(serial));
			m[dvId] = d;
			d
		}
	}
}

-- BufferVar id -> buffer index, assigning a SampleBuf serial on first reference
-- (creation order), mirroring nextSampleBufSerialNo() / getOrCreateSampleBuf.
fn _bufIdxFor(bvId Int) Int {
	var m [Int: Int] = `scBufIdMap;
	match (m[bvId]) {
		some(bi): bi;
		none: {
			let serial Int = `scBufSerials;
			`scBufSerials = serial + 1;
			var ctx Ctx = `scCtx;
			let bi = ctx.bufSerials length;
			ctx.bufSerials push!(serial);
			m[bvId] = bi;
			bi
		}
	}
}

---------------------------------------------------------------------------
-- Unified op constructor: fold -> rewrite -> hash-cons (mirrors addExpr() with
-- gApplyRewrites). The three optimizations interleave because a matched rule's
-- RHS is built by re-entering _makeOp, so a rewrite can expose a fold, a fold a
-- new hash-cons hit, etc. -- the same fixpoint the C++ reaches.

fn _noInt() Option<Int> = Option<Int>.none;
fn _someInt(n Int) Option<Int> = Option<Int>.some(n);

fn _allConst(ctx Ctx, ins [Int]) Bool {
	var i = 0;
	while (i < ins length) {
		if (!(ctx.kind[ins[i]] isConstantKind)) { return false; }
		i = i + 1;
	}
	true
}

fn _foldUnopNode(ctx Ctx, op UnaryOp, ins [Int]) Int {
	let r = foldUnary(_constValOf(ctx, ins[0]), op);
	_addFoldedConstant(r, r _foldInitType)
}
fn _foldBinopNode(ctx Ctx, op BinaryOp, ins [Int]) Int {
	let r = foldBinary(_constValOf(ctx, ins[0]), _constValOf(ctx, ins[1]), op);
	_addFoldedConstant(r, r _foldInitType)
}
fn _foldCmpNode(ctx Ctx, op CompareOp, ins [Int]) Int {
	let r = foldCompare(_constValOf(ctx, ins[0]), _constValOf(ctx, ins[1]), op);
	_addFoldedConstant(r, r _foldInitType)
}
fn _foldOp(ctx Ctx, kind NodeKind, ins [Int]) Option<Int> = match (kind) {
	unopK(op):      _someInt(_foldUnopNode(ctx, op, ins));
	binopK(op):     _someInt(_foldBinopNode(ctx, op, ins));
	compareopK(op): _someInt(_foldCmpNode(ctx, op, ins));
	_:              _noInt();
};

fn _opRate(ctx Ctx, kind NodeKind, ins [Int]) Rate = match (kind) {
	binopK(_):     _maxRateOf(ins);
	compareopK(_): _maxRateOf(ins);
	_:             ctx.nrate[ins[0]];
};
fn _opChans(ctx Ctx, kind NodeKind, ins [Int]) Int = match (kind) {
	binopK(_):     _broadcastChans(ins);
	compareopK(_): _broadcastChans(ins);
	_:             ctx.chans[ins[0]];
};
fn _opType(kind NodeKind, ins [Int]) NumType = match (kind) {
	binopK(op): op isFloatBinop ? ANY_FLOAT : (op isIntBinop ? ANY_INT : ANY_NUM);
	unopK(op):  op isFloatUnop ? ANY_FLOAT : (op isIntUnop ? ANY_INT : ANY_NUM);
	_:          ANY_NUM;
};

fn _makeOp(kind NodeKind, ins [Int]) Int {
	var ctx Ctx = `scCtx;
	if (ctx _allConst(ins)) {
		match (_foldOp(ctx, kind, ins)) {
			some(n): { return n; }
			none: {}
		}
	}
	-- Non-constant op: C++ constructs the Expr here, burning a serial in the ctor
	-- before addExpr() rewrites/hash-conses it. Consume that candidate serial now
	-- so a rewritten-away candidate still leaves the same serial gap as C++.
	let serial Int = `scExprSerials;
	`scExprSerials = serial + 1;
	if (`scApplyRewrites) {
		match (tryRewriteMatch(ctx, kind, ins)) {
			some(hit): { return _buildRHS(hit.rhs, hit.subs); }
			none: {}
		}
	}
	_addExprNodeS(serial, kind, ins, ctx _opRate(kind, ins), _opType(kind, ins), ctx _opChans(kind, ins))
}

-- A scalar constant node for a rule RHS literal (hash-consed f64, like the C++
-- constant(double)).
fn _scalarConst(c Float) Int {
	let v = ConstVal.floats([c]);
	_addFoldedConstant(v, v _foldInitType)
}
fn _rhsMonoOp(subs [Int], m Int) UnaryOp {
	let n = subs subsNode((m == 0) ? VMONOUP : VMONODN);
	match (`scCtx.kind[n]) { unopK(op): op; _: UnaryOp.neg; }
}

-- Interpret a matched rule's RHS pattern, building nodes via _makeOp.
fn _buildRHS(p Pat, subs [Int]) Int = match (p) {
	pvar(id):   subs subsNode(id);
	pscalar(c): _scalarConst(c);
	pposnum:    subs subsNode(VPOS);
	pnegnum:    subs subsNode(VNEG);
	punop(op, a):     _makeOp(NodeKind.unopK(op), [_buildRHS(a, subs)]);
	pbinop(op, a, b): _makeOp(NodeKind.binopK(op), [_buildRHS(a, subs), _buildRHS(b, subs)]);
	pcmpop(op, a, b): _makeOp(NodeKind.compareopK(op), [_buildRHS(a, subs), _buildRHS(b, subs)]);
	pmono(m, a):      _makeOp(NodeKind.unopK(subs _rhsMonoOp(m)), [_buildRHS(a, subs)]);
};

---------------------------------------------------------------------------
-- Vector / reduce ops (front-end VecOp). Reduce changes the loop iteration
-- count (output = cols channels); the pure vec ops remap the channel index.

-- reduce(op, cols): C++ ReduceExpr(a->rate, {a}), chans=cols, type=in type. A
-- constant input folds via reduce_int/reduce_float (addConstantExpr, no hash-cons).
fn _makeReduce(ctx Ctx, ins [Int], op BinaryOp, cols Int) Int {
	if (ctx.kind[ins[0]] isConstantKind) {
		let r = foldReduce(_constValOf(ctx, ins[0]), cols, op);
		return _addConstantNode(NodeKind.constant(r, r _foldInitType), r constSize);
	}
	_addExprNode(NodeKind.reduceK(op, cols), ins, ctx.nrate[ins[0]], ctx.typ[ins[0]], cols)
}

fn _vecChans(ctx Ctx, vop VecOp, ins [Int]) Int = vecOutChans(ctx.chans[ins[0]], vop);
fn _vecRate(ctx Ctx, vop VecOp, ins [Int]) Rate = match (vop) {
	rotate: _maxRateOf(ins);               -- rotate's amount can be a signal
	_:      ctx.nrate[ins[0]];
};

fn _importVecOp(e SignalExpr, vop VecOp) Void {
	var ctx Ctx = `scCtx;
	let ins = e _resolveIns;
	match (vop) {
		reduce(op, cols): { _mapId(e.id, _makeReduce(ctx, ins, op, cols)); }
		sum(cols):        { _mapId(e.id, _makeReduce(ctx, ins, BinaryOp.add, cols)); }
		prod(cols):       { _mapId(e.id, _makeReduce(ctx, ins, BinaryOp.mul, cols)); }
		minOf(cols):      { _mapId(e.id, _makeReduce(ctx, ins, BinaryOp.min, cols)); }
		maxOf(cols):      { _mapId(e.id, _makeReduce(ctx, ins, BinaryOp.max, cols)); }
		at:   { _impError("vec_at not exposed by the front-end"); }
		put:  { _impError("vec_put not exposed by the front-end"); }
		join: { _impError("vec_join not exposed by the front-end"); }
		_: {
			-- take/drop/stride/stutter/ncyc/rotate/reverse/transpose: no folding,
			-- initial type = any (narrowed to the input type by inference).
			_mapId(e.id, _addExprNode(NodeKind.vecK(vop), ins,
				ctx _vecRate(vop, ins), ANY_NUM, ctx _vecChans(vop, ins)));
		}
	}
}

---------------------------------------------------------------------------
-- Per-kind import (mirrors the parse* functions)

fn _importExpr(e SignalExpr) Void {
	var ctx Ctx = `scCtx;
	match (e.kind) {
		int(a, typ): {
			-- parseConstant always builds f64 storage with explicit chans
			let values = a toFloat;
			let n = _addConstantNode(NodeKind.constant(ConstVal.floats(values), typ), values length);
			_mapId(e.id, n);
		}
		float(a, typ): {
			let n = _addConstantNode(NodeKind.constant(ConstVal.floats(a), typ), a length);
			_mapId(e.id, n);
		}
		sampleRate: {
			_mapId(e.id, _addExprNode(NodeKind.sampleRate, [Int](), Rate.init, ANY_NUM, 1));
		}
		sampleDur: {
			_mapId(e.id, _addExprNode(NodeKind.sampleDur, [Int](), Rate.init, ANY_NUM, 1));
		}
		control(spec, chans, name): {
			let sn Int = `scControlSerials;
			`scControlSerials = sn + 1;
			_mapId(e.id, _addExprNode(NodeKind.control(spec, name, sn), [Int](),
				Rate.event, FLOAT32, chans));
		}
		noteParam(spec, chans, name): {
			var sn = 0;
			if (name != "gate") {
				sn = `scNoteParamSerials;
				`scNoteParamSerials = sn + 1;
			}
			_mapId(e.id, _addExprNode(NodeKind.noteParamK(spec, name, sn), [Int](),
				Rate.audio, FLOAT32, chans));
		}
		inlet(typ, chans, name): {
			let sn Int = `scInletSerials;
			`scInletSerials = sn + 1;
			_mapId(e.id, _addExprNode(NodeKind.inletK(name, sn), [Int](),
				Rate.audio, typ, chans));
		}
		outlet(name): {
			let sn Int = `scOutletSerials;
			`scOutletSerials = sn + 1;
			let ins = e _resolveIns;
			_mapId(e.id, _addExprNode(NodeKind.outletK(name, sn), ins,
				ctx.nrate[ins[0]], ANY_NUM, ctx.chans[ins[0]]));
		}
		unop(op): {
			if (!(op _unopSupported)) {
				_impError("Unknown unary operator: " $ op tag toString);
				return;
			}
			-- _makeOp folds (if constant), rewrites (if enabled), then hash-conses.
			_mapId(e.id, _makeOp(NodeKind.unopK(op), e _resolveIns));
		}
		binop(op): {
			if (!(op _binopSupported)) {
				_impError("Unknown binary operator: " $ op tag toString);
				return;
			}
			_mapId(e.id, _makeOp(NodeKind.binopK(op), e _resolveIns));
		}
		compareop(op): {
			_mapId(e.id, _makeOp(NodeKind.compareopK(op), e _resolveIns));
		}
		castop(op): {
			let ins = e _resolveIns;
			let castType = NumType(op numTypeInt);
			if (ctx.kind[ins[0]] isConstantKind) {
				let r = foldCast(_constValOf(ctx, ins[0]), op);
				_mapId(e.id, _addFoldedConstant(r, castType));
				return;
			}
			_mapId(e.id, _addExprNode(NodeKind.castopK(castType), ins,
				ctx.nrate[ins[0]], castType, ctx.chans[ins[0]]));
		}
		select: {
			let ins = e _resolveIns;
			-- SelectExpr::calcShape: chans of test, broadcast with values
			_mapId(e.id, _addExprNode(NodeKind.selectK, ins,
				_maxRateOf(ins), ANY_NUM, _broadcastChans(ins)));
		}
		select2: {
			let ins = e _resolveIns;
			_mapId(e.id, _addExprNode(NodeKind.selectK, ins,
				_maxRateOf(ins), ANY_NUM, _broadcastChans(ins)));
		}
		random(op, rrate, chans): {
			match (op) {
				unipolar: {
					let sn Int = `scRandSerials;
					`scRandSerials = sn + 1;
					_mapId(e.id, _addExprNode(NodeKind.urandK(sn), [Int](),
						rrate, ANY_FLOAT, chans));
				}
				bipolar: {
					let sn Int = `scRandSerials;
					`scRandSerials = sn + 1;
					_mapId(e.id, _addExprNode(NodeKind.birandK(sn), [Int](),
						rrate, ANY_FLOAT, chans));
				}
				bits: {
					let sn Int = `scRandSerials;
					`scRandSerials = sn + 1;
					_mapId(e.id, _addExprNode(NodeKind.rand64K(sn), [Int](),
						rrate, INT64, chans));
				}
				frand(_, _): { _impError("FRand is not supported by the synthdef compiler"); }
				irand(_, _): { _impError("IRand is not supported by the synthdef compiler"); }
			}
		}
		delay(dv, op): {
			let d = dv.id _delayIdxFor;
			match (op) {
				maxDelayTime: {
					let ins = e _resolveIns;
					let n = _addExprNode(NodeKind.maxDelayK(d), ins,
						Rate.init, ANY_NUM, ctx.chans[ins[0]]);
					ctx.delays[d] = DelayInfo { ...ctx.delays[d], maxDelay: n };
					_mapId(e.id, n);
				}
				init(offset): {
					let ins = e _resolveIns;
					-- DelayInit::calcShape is a no-op: chans stays 0
					_mapId(e.id, _addExprNode(NodeKind.delayInitK(d, offset), ins,
						Rate.init, ANY_NUM, 0));
				}
				read(offset): {
					_mapId(e.id, _addExprNode(NodeKind.delayFixReadK(d, offset), [Int](),
						Rate.audio, ANY_NUM, ctx.delays[d].chans));
				}
				vread(interp): {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.delayVarReadK(d, interp), ins,
						Rate.audio, ANY_NUM, ctx.delays[d].chans));
				}
				write: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.delayWriteK(d), ins,
						Rate.audio, ANY_NUM, ctx.chans[ins[0]]));
				}
			}
		}
		debug(label, period, consecutive): {
			let sn Int = `scDebugSerials;
			`scDebugSerials = sn + 1;
			let ins = e _resolveIns;
			_mapId(e.id, _addExprNode(NodeKind.debugK(label, period, consecutive, sn), ins,
				ctx.nrate[ins[0]], ANY_NUM, 1));
		}
		vecop(vop): { _importVecOp(e, vop); }
		buffer(bv, op): {
			let bi = bv.id _bufIdxFor;
			-- BufExpr: audio rate, initial_type f64 (concrete). update_type is
			-- empty (no input constraints), so inference leaves these untouched.
			match (op) {
				fixRead(index, readChans, startChan): {
					_mapId(e.id, _addExprNode(NodeKind.bufFixReadK(bi, index, readChans, startChan), [Int](),
						Rate.audio, FLOAT64, readChans));
				}
				vread(interp, readChans, startChan): {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bufVarReadK(bi, interp, readChans, startChan), ins,
						Rate.audio, FLOAT64, readChans));
				}
				write(writeChans, startChan): {
					let ins = e _resolveIns;
					-- BufWrite::calcShape sets chans = in0().chans.
					_mapId(e.id, _addExprNode(NodeKind.bufWriteK(bi, writeChans, startChan), ins,
						Rate.audio, FLOAT64, ctx.chans[ins[0]]));
				}
				length: {
					_mapId(e.id, _addExprNode(NodeKind.bufLengthK(bi), [Int](),
						Rate.audio, FLOAT64, 1));
				}
			}
		}
		if_(_, _): { _impError("if_ not yet supported by synthc (M3)"); }
		for_(_, _): { _impError("for_ not yet supported by synthc (M3)"); }
		switch_(_): { _impError("switch_ not yet supported by synthc (M3)"); }
		varexpr(_): { _impError("varexpr not yet supported by synthc (M3)"); }
		voicer(_, _): { _impError("voicer not yet supported by synthc (M4)"); }
		spectralChain(_, _, _): { _impError("spectralChain not yet supported by synthc (M4)"); }
		spectralFrame(_): { _impError("spectralFrame not yet supported by synthc (M4)"); }
	}
}
