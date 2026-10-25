-- synthc/importer.x
-- Imports a synthdef.x SignalGraph (the in-memory front-end graph) into a
-- synthc Ctx: folding, rewriting and hash-consing as nodes are built, with
-- each node's initial rate, type and shape, and the delay bookkeeping.
-- Serial allocation still follows the retired C++ s-expression builder
-- (including serials spent on hash-consed nodes), so dumps stay stable.

import synthdef.*;
import synthc.ir.*;
import synthc.fold.*;
import synthc.rewrite.*;

---------------------------------------------------------------------------
-- Entry point

fn importGraph(g SignalGraph, name String, applyRewrites Bool = false,
               tags [String] = [String]()) Ctx {
	var `scCtx = newCtx(name);
	`scCtx.tags append!(tags);
	var `scIdMap [Int: Int] = [:];        -- front-end expr id -> node index
	var `scDelayIdMap [Int: Int] = [:];   -- front-end DelayVar id -> delay idx
	var `scBufIdMap [Int: Int] = [:];     -- front-end BufferVar id -> buf idx
	var `scBankIdMap [Int: Int] = [:];    -- front-end SampleBankVar id -> bank idx
	var `scConsMap [String: Int] = [:];   -- hash-cons key -> node index
	-- Algebraic rewriting (interleaved with fold + hash-cons in _makeOp). Off by
	-- default so rewrites-off dumps stay byte-identical; the C++ gApplyRewrites
	-- mirror enables it for the real defSynth path and rewrites-on diffing.
	var `scApplyRewrites Bool = applyRewrites;

	-- Serial counters (mirror the Synth:: serial number fields).
	var `scExprSerials Int = 0;
	var `scDelaySerials Int = 0;
	var `scBufSerials Int = 0;
	var `scBankSerials Int = 0;
	-- Graph being imported into (0 = root). M3.2's subgraph import pushes/pops
	-- this; for non-control-flow synths it stays 0.
	var `scCurGraph Int = 0;
	-- Note parameter columns of the voicer body being imported: gate is
	-- column 0, the others follow in declaration order.
	var `scNoteCol Int = 1;
	var `scInletSerials Int = 0;
	var `scOutletSerials Int = 0;
	var `scRandSerials Int = 0;
	var `scDebugSerials Int = 0;
	-- The most recently imported SpectralFrameInput node, so the enclosing
	-- spectral_chain can set its shape (= inputChans * fftSize) once the input is
	-- known (mirrors the C++ spectral_chain() setting frame->chans afterwards).
	var `scLastSpectralFrame Int = NONE;

	for (e : g.exprs) {
		_importExpr(e);
	}
	`scCtx.nextSerial[0] = `scExprSerials;
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
	at:           "@";
	put:          "P";
	join:         "J";
	_:            "?";
};

fn _kindKey(kind NodeKind) String = match (kind) {
	constant(v, t):       "K|" $ v _constValKey $ "|" $ t.0 toString;
	sampleRate:           "SR";
	sampleDur:            "SD";
	sharedInK(slot, r):   "SI|" $ slot toString $ "|" $ r ordinal toString;
	noteParamK(spec, _, col): "NP|" $ spec _specKey $ "|" $ col toString;
	inletK(_, sn, _):     "IN|" $ sn toString;
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
	bankLookupK(bi):                "BKL|" $ bi toString;
	bankFixReadK(index, rc, sc):    "BKF|" $ index toString $ "|" $ rc toString $ "|" $ sc toString;
	bankVarReadK(ip, rc, sc):       "BKV|" $ ip ordinal toString $ "|" $ rc toString $ "|" $ sc toString;
	bankRootKeyK:                   "BKR";
	bankSampleRateK:                "BKS";
	bankLengthK:                    "BKN";
	bankLoopStartK:                 "BLS";
	bankLoopEndK:                   "BLE";
	bankHasLoopK:                   "BHL";
	ifK:                  "IF";
	switchK(nc):          "SW|" $ nc toString;
	forK:                 "FOR";
	phiK(t):              "PHI|" $ t toString;
	varK(name):           "VAR|" $ name;
	debugK(_, _, _, sn):  "DBG|" $ sn toString;
};

-- Hash-consing is per-graph (each C++ Graph has its own hashConsSet), so the
-- cons key is prefixed with the current graph index: an identical expression in
-- a different subgraph is a distinct node (and stays inlined locally rather than
-- becoming a cross-graph instance variable).
fn _consKey(kind NodeKind, ins [Int]) String =
	`scCurGraph toString $ "@" $ kind _kindKey $ "#" $ ins _insKey;

---------------------------------------------------------------------------
-- Node creation
--
-- _appendNode is the raw array append plus the per-kind bookkeeping that
-- C++ addExpr does after the hash-cons check (sinks/inlets/outlets/...,
-- delay record updates, RNG flag).

fn _appendNode(kind NodeKind, ins [Int], serial Int, r Rate, t NumType, sh Shape) Int {
	var ctx Ctx = `scCtx;
	let idx = ctx numNodes;
	ctx.kind push!(kind);
	ctx.ins push!(ins);
	ctx.subs push!([Int]());
	ctx.serial push!(serial);
	ctx.nrate push!(r);
	ctx.typ push!(t);
	ctx.shape push!(sh);
	ctx.chans push!(sh chans);
	ctx.graphOf push!(`scCurGraph);
	ctx.cut push!(GraphCut.none);
	ctx.treeOf push!(NONE);
	ctx.consumers push!([Int]());
	ctx.suppressed push!(false);
	ctx.cmpTyp push!(ANY_NUM);

	if (kind isSinkKind) { ctx.sinks push!(idx); }

	match (kind) {
		inletK(_, _, _):     ctx.inlets push!(idx);
		outletK(_, _):       ctx.outlets push!(idx);
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
fn _addExprNode(kind NodeKind, ins [Int], r Rate, t NumType, sh Shape) Int {
	let serial Int = `scExprSerials;
	`scExprSerials = serial + 1;
	_addExprNodeS(serial, kind, ins, r, t, sh)
}

-- Same, but with the serial (userial) already consumed by the caller. _makeOp
-- consumes the candidate's serial before rewriting -- mirroring the C++ Expr
-- ctor, where `new BinaryOpExpr` burns a userial even if addExpr() then rewrites
-- or hash-conses it away -- so a rewritten/duplicate node still leaves the same
-- serial gap.
fn _addExprNodeS(serial Int, kind NodeKind, ins [Int], r Rate, t NumType, sh Shape) Int {
	let conses = kind shouldHashCons;
	let key = conses ? _consKey(kind, ins) : "";
	var m [String: Int] = `scConsMap;
	if (conses) {
		match (m[key]) {
			some(n): { return n; }
			none: {}
		}
	}

	let idx = _appendNode(kind, ins, serial, r, t, sh);
	if (conses) {
		m[key] = idx;
	}
	idx
}

-- Mirrors addConstantExpr(): no hash-consing, no calcShape, type stays
-- NumType::any (the init_type lives in the kind payload for inference).
fn _addConstantNode(kind NodeKind, sh Shape) Int {
	let serial Int = `scExprSerials;
	`scExprSerials = serial + 1;
	_appendNode(kind, [Int](), serial, Rate.constant, ANY_NUM, sh)
}

-- A constant produced by constant folding. Unlike imported constants, the C++
-- routes folded constants through addExpr: they are hash-consed and get their
-- type set to init_type (any_float for f64 results, any for i64, or the cast
-- target type). Mirror that by reusing _addExprNode with a constant kind.
fn _addFoldedConstant(v ConstVal, initType NumType, sh Shape) Int {
	_addExprNode(NodeKind.constant(v, initType), [Int](), Rate.constant, initType, sh)
}

-- A shape not known yet (0 channels): the identity of broadcast.
fn noShape() Shape = shape([0]);

---------------------------------------------------------------------------
-- Control flow (M3): subgraph import + control-flow node creation.

-- Allocate a new subgraph child of the current graph; return its index.
fn _newGraph() Int {
	var ctx Ctx = `scCtx;
	let g = ctx.graphs length;
	ctx.graphs push!(GraphInfo { serial: g, parent: `scCurGraph, audioLoops: [Int]() });
	g
}

-- Import a branch/body SignalGraph into a fresh subgraph and wrap its root in a
-- PhiNode (mirrors parseGraph(): new Graph context, then addExpr(PhiNodeExpr)).
-- Branch exprs may reference enclosing-graph node ids directly (closure capture);
-- those resolve via the global `scIdMap and get a GraphCut.graph cut later.
fn _importSubgraph(sg SignalGraph) Int {
	let g = _newGraph();
	var `scCurGraph Int = g;            -- import the branch into the subgraph
	for (e : sg.exprs) { _importExpr(e); }
	var ctx Ctx = `scCtx;
	let root = sg.root.id _nodeOf;
	-- PhiNodeExpr(in): rate = in.rate (raised to the target's rate by setTarget,
	-- applied by the caller); initial_type any; chans from calcShape (M3.3).
	_addExprNode(NodeKind.phiK(NONE), [root], ctx.nrate[root], ANY_NUM, ctx.shape[root])
}

-- Append a control-flow node (if/switch/for): not hash-consed, carries its
-- branch/body PhiNode roots in ctx.subs.
fn _addCFNode(kind NodeKind, ins [Int], subs [Int], r Rate, t NumType, sh Shape) Int {
	let idx = _addExprNode(kind, ins, r, t, sh);
	var ctx Ctx = `scCtx;
	ctx.subs[idx] = subs;
	-- A branch/body that writes to a delay (or other sink) purely for its
	-- side effect -- e.g. `if_(trig, fn(){ y <- white(chans) })` used as a
	-- statement -- leaves this node with no consumers of its own. Without
	-- this it would never be visited by the sinks-rooted topological walk
	-- (topologicalSortExprs) and the guarded write would silently never
	-- execute. See hasSideEffectSubgraph (ir.x).
	if (ctx hasSideEffectSubgraph(idx)) { ctx.sinks push!(idx); }
	idx
}

-- Link a PhiNode to its control-flow target and raise its rate (setTarget).
fn _linkPhi(phi Int, target Int, r Rate) Void {
	var ctx Ctx = `scCtx;
	ctx.kind[phi] = NodeKind.phiK(target);
	ctx.nrate[phi] = max(ctx.nrate[phi], r);
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

-- A control-flow node's rate. Branching once per block on a block-placed
-- condition is a later optimisation (plan §4.4); for now control flow runs
-- per sample unless everything is const/init/reset.
fn _cfRate(r Rate) Rate = r == Rate.block ? Rate.audio : r;

fn _broadcastShape(ins [Int]) Shape {
	var ctx Ctx = `scCtx;
	var sh = noShape();
	for (i : ins) {
		sh = broadcast(sh, ctx.shape[i]);
	}
	sh
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
	clz: true;  ctz: true;  popCount: true;  bitWidth: true;
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

-- True for values readable at note-on time: constants, note params,
-- controls (inputs with a spec), and init-rate values.
fn _bankLookupInputOK(n NIdx) Bool {
	var ctx Ctx = `scCtx;
	match (ctx.kind[n]) {
		constant(_, _):      true;
		noteParamK(_, _, _): true;
		inletK(_, _, sp):    match (sp) { some(_): true; none: false; };
		_:                   ctx.nrate[n] <= Rate.init;
	}
}

-- SampleBankVar id -> bank index, assigning a SampleBank serial on first
-- reference, mirroring nextSampleBankSerialNo() / getOrCreateSampleBank.
fn _bankIdxFor(sbId Int) Int {
	var m [Int: Int] = `scBankIdMap;
	match (m[sbId]) {
		some(bi): bi;
		none: {
			let serial Int = `scBankSerials;
			`scBankSerials = serial + 1;
			var ctx Ctx = `scCtx;
			let bi = ctx.bankSerials length;
			ctx.bankSerials push!(serial);
			m[sbId] = bi;
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

fn _constInitOf(ctx Ctx, n NIdx) NumType = match (ctx.kind[n]) {
	constant(_, t): t;
	_:              ANY_NUM;
};

-- A folded constant keeps its operands' types (mirrors foldedType in
-- synthdef_expr.cpp): `1.0 f64` stays f64 through a fold, conflicting fixed
-- types widen, and untyped operands give the storage default as before.
fn _foldedType1(a NumType, r ConstVal) NumType {
	let d = r _foldInitType;
	let t = a & d;
	(t notEmpty) ? t : d
}
fn _foldedType2(a NumType, b NumType, r ConstVal) NumType {
	let d = r _foldInitType;
	var t = a & b & d;
	if (t isEmpty) { t = numWiden(a, b) & d; }
	(t notEmpty) ? t : d
}

fn _foldUnopNode(ctx Ctx, op UnaryOp, ins [Int]) Int {
	let r = foldUnary(_constValOf(ctx, ins[0]), op);
	_addFoldedConstant(r, _foldedType1(_constInitOf(ctx, ins[0]), r), ctx.shape[ins[0]])
}
-- A binary fold works on both operands expanded to their broadcast shape.
fn _gathered(ctx Ctx, n NIdx, out Shape) ConstVal =
	gatherShape(_constValOf(ctx, n), ctx.shape[n], out);
fn _foldBinopNode(ctx Ctx, op BinaryOp, ins [Int]) Int {
	let sh = broadcast(ctx.shape[ins[0]], ctx.shape[ins[1]]);
	let r = foldBinary(_gathered(ctx, ins[0], sh), _gathered(ctx, ins[1], sh), op);
	_addFoldedConstant(r, _foldedType2(_constInitOf(ctx, ins[0]), _constInitOf(ctx, ins[1]), r), sh)
}
fn _foldCmpNode(ctx Ctx, op CompareOp, ins [Int]) Int {
	let sh = broadcast(ctx.shape[ins[0]], ctx.shape[ins[1]]);
	let r = foldCompare(_gathered(ctx, ins[0], sh), _gathered(ctx, ins[1], sh), op);
	_addFoldedConstant(r, r _foldInitType, sh)
}
fn _foldOp(ctx Ctx, kind NodeKind, ins [Int]) Option<Int> = match (kind) {
	unopK(op):      canFoldUnary(_constValOf(ctx, ins[0]), op)
	                    ? _someInt(_foldUnopNode(ctx, op, ins)) : _noInt();
	binopK(op):     _someInt(_foldBinopNode(ctx, op, ins));
	compareopK(op): _someInt(_foldCmpNode(ctx, op, ins));
	_:              _noInt();
};

fn _opRate(ctx Ctx, kind NodeKind, ins [Int]) Rate = match (kind) {
	binopK(_):     _maxRateOf(ins);
	compareopK(_): _maxRateOf(ins);
	_:             ctx.nrate[ins[0]];
};
fn _opShape(ctx Ctx, kind NodeKind, ins [Int]) Shape = match (kind) {
	binopK(_):     _broadcastShape(ins);
	compareopK(_): _broadcastShape(ins);
	_:             ctx.shape[ins[0]];
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
	_addExprNodeS(serial, kind, ins, ctx _opRate(kind, ins), _opType(kind, ins), ctx _opShape(kind, ins))
}

-- A scalar constant node for a rule RHS literal (hash-consed f64, like the C++
-- constant(double)).
fn _scalarConst(c Float) Int {
	let v = ConstVal.floats([c]);
	_addFoldedConstant(v, v _foldInitType, scalarShape())
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
		return _addConstantNode(NodeKind.constant(r, r _foldInitType), row(r constSize));
	}
	_addExprNode(NodeKind.reduceK(op, cols), ins, ctx.nrate[ins[0]], ctx.typ[ins[0]], row(cols))
}

-- Output channel count of a vec op over flattened channels. at = the index
-- vector's chans; put = the source array's chans; join = sum of all inputs;
-- the single-input reorders use vecOutChans. The result is a row (vec ops
-- work on the row-major flattening; shaped views are a later step).
fn _vecChans(ctx Ctx, vop VecOp, ins [Int]) Int = match (vop) {
	at:   ctx.chans[ins[1]];
	put:  ctx.chans[ins[0]];
	join: { var raw = 0; for (x : ins) { raw = raw + ctx.chans[x]; } raw }
	_:    vecOutChans(ctx.chans[ins[0]], vop);
};
fn _vecRate(ctx Ctx, vop VecOp, ins [Int]) Rate = match (vop) {
	rotate: _maxRateOf(ins);               -- rotate's amount can be a signal
	at:     _maxRateOf(ins);               -- max(a, i)
	put:    _maxRateOf(ins);               -- max(a, i, v)
	join:   _maxRateOf(ins);               -- max of all inputs
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
		_: {
			-- take/drop/stride/stutter/ncyc/rotate/reverse/transpose/at/put/join:
			-- no folding, initial type = any (narrowed to the input type by inference).
			_mapId(e.id, _addExprNode(NodeKind.vecK(vop), ins,
				ctx _vecRate(vop, ins), ANY_NUM, row(ctx _vecChans(vop, ins))));
		}
	}
}

---------------------------------------------------------------------------
-- Per-kind import (mirrors the parse* functions)

fn _importExpr(e SignalExpr) Void {
	var ctx Ctx = `scCtx;
	match (e.kind) {
		int(a, typ, sh): {
			-- Constants keep f64 storage (folding yields i64 only through casts).
			let n = _addConstantNode(NodeKind.constant(ConstVal.floats(a toFloat), typ), sh);
			_mapId(e.id, n);
		}
		float(a, typ, sh): {
			let n = _addConstantNode(NodeKind.constant(ConstVal.floats(a), typ), sh);
			_mapId(e.id, n);
		}
		sampleRate: {
			_mapId(e.id, _addExprNode(NodeKind.sampleRate, [Int](), Rate.init, ANY_NUM, scalarShape()));
		}
		sampleDur: {
			_mapId(e.id, _addExprNode(NodeKind.sampleDur, [Int](), Rate.init, ANY_NUM, scalarShape()));
		}
		sharedIn(slot, srate): {
			-- Mirrors the C++ sharedIn() factory validation.
			if (slot < 0 || slot >= 16) { _impError("sharedIn: slot out of range"); }
			match (srate) {
				init: {}
				audio: {}
				_: { _impError("sharedIn: only init and audio rate supported"); }
			}
			_mapId(e.id, _addExprNode(NodeKind.sharedInK(slot, srate), [Int](),
				srate, ANY_FLOAT, scalarShape()));
		}
		noteParam(spec, chans, name): {
			-- A column (or `chans` columns) of the voicer's `notes` input, read
			-- for the current voice. Gate is column 0; the others are assigned
			-- in declaration order. Placed at block: constant while a note
			-- holds still, recomputed when its row changes.
			var col = 0;
			if (name != "gate") {
				col = `scNoteCol;
				`scNoteCol = col + chans;
			}
			_mapId(e.id, _addExprNode(NodeKind.noteParamK(spec, name, col), [Int](),
				Rate.block, FLOAT32, row(chans)));
		}
		inlet(typ, sh, name, spec): {
			-- An input port. One with a spec (a control) is nominally block
			-- placed: its value is usually held, so what depends on it alone is
			-- a parameter group, computed per sample only while it moves. A
			-- plain inlet is declared as a signal input and placed at audio.
			let sn Int = `scInletSerials;
			`scInletSerials = sn + 1;
			let r = match (spec) { some(_): Rate.block; none: Rate.audio; };
			_mapId(e.id, _addExprNode(NodeKind.inletK(name, sn, spec), [Int](),
				r, typ, sh));
		}
		-- An outlet is a port the engine patches into the audio graph: every
		-- port is audio rate at the ABI (a value held for the block is simply
		-- reported constant).
		outlet(name): {
			let sn Int = `scOutletSerials;
			`scOutletSerials = sn + 1;
			let ins = e _resolveIns;
			_mapId(e.id, _addExprNode(NodeKind.outletK(name, sn), ins,
				Rate.audio, ANY_NUM, ctx.shape[ins[0]]));
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
				_mapId(e.id, _addFoldedConstant(r, castType, ctx.shape[ins[0]]));
				return;
			}
			_mapId(e.id, _addExprNode(NodeKind.castopK(castType), ins,
				ctx.nrate[ins[0]], castType, ctx.shape[ins[0]]));
		}
		select: {
			let ins = e _resolveIns;
			_mapId(e.id, _addExprNode(NodeKind.selectK, ins,
				_maxRateOf(ins), ANY_NUM, _broadcastShape(ins)));
		}
		select2: {
			let ins = e _resolveIns;
			_mapId(e.id, _addExprNode(NodeKind.selectK, ins,
				_maxRateOf(ins), ANY_NUM, _broadcastShape(ins)));
		}
		random(op, rrate, sh): {
			match (op) {
				unipolar: {
					let sn Int = `scRandSerials;
					`scRandSerials = sn + 1;
					_mapId(e.id, _addExprNode(NodeKind.urandK(sn), [Int](),
						rrate, ANY_FLOAT, sh));
				}
				bipolar: {
					let sn Int = `scRandSerials;
					`scRandSerials = sn + 1;
					_mapId(e.id, _addExprNode(NodeKind.birandK(sn), [Int](),
						rrate, ANY_FLOAT, sh));
				}
				bits: {
					let sn Int = `scRandSerials;
					`scRandSerials = sn + 1;
					_mapId(e.id, _addExprNode(NodeKind.rand64K(sn), [Int](),
						rrate, INT64, sh));
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
						Rate.init, ANY_NUM, ctx.shape[ins[0]]);
					ctx.delays[d] = DelayInfo { ...ctx.delays[d], maxDelay: n };
					_mapId(e.id, n);
				}
				init(offset): {
					let ins = e _resolveIns;
					-- A delay init's own shape stays unset (its value widens the delay).
					_mapId(e.id, _addExprNode(NodeKind.delayInitK(d, offset), ins,
						Rate.init, ANY_NUM, noShape()));
				}
				read(offset): {
					_mapId(e.id, _addExprNode(NodeKind.delayFixReadK(d, offset), [Int](),
						Rate.audio, ANY_NUM, ctx.delays[d].shape));
				}
				vread(interp): {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.delayVarReadK(d, interp), ins,
						Rate.audio, ANY_NUM, ctx.delays[d].shape));
				}
				write: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.delayWriteK(d), ins,
						Rate.audio, ANY_NUM, ctx.shape[ins[0]]));
				}
			}
		}
		debug(label, period, consecutive): {
			let sn Int = `scDebugSerials;
			`scDebugSerials = sn + 1;
			let ins = e _resolveIns;
			_mapId(e.id, _addExprNode(NodeKind.debugK(label, period, consecutive, sn), ins,
				ctx.nrate[ins[0]], ANY_NUM, scalarShape()));
		}
		vecop(vop): { _importVecOp(e, vop); }
		buffer(bv, op): {
			let bi = bv.id _bufIdxFor;
			-- BufExpr: audio rate, initial_type f64 (concrete). update_type is
			-- empty (no input constraints), so inference leaves these untouched.
			match (op) {
				fixRead(index, readChans, startChan): {
					_mapId(e.id, _addExprNode(NodeKind.bufFixReadK(bi, index, readChans, startChan), [Int](),
						Rate.audio, FLOAT64, row(readChans)));
				}
				vread(interp, readChans, startChan): {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bufVarReadK(bi, interp, readChans, startChan), ins,
						Rate.audio, FLOAT64, row(readChans)));
				}
				write(writeChans, startChan): {
					let ins = e _resolveIns;
					-- BufWrite::calcShape sets chans = in0().chans.
					_mapId(e.id, _addExprNode(NodeKind.bufWriteK(bi, writeChans, startChan), ins,
						Rate.audio, FLOAT64, ctx.shape[ins[0]]));
				}
				length: {
					_mapId(e.id, _addExprNode(NodeKind.bufLengthK(bi), [Int](),
						Rate.audio, FLOAT64, scalarShape()));
				}
			}
		}
		bankLookup(sb): {
			let bi = sb.id _bankIdxFor;
			let ins = e _resolveIns;
			-- The inputs are latched at note-on: reject anything that cannot
			-- be evaluated at that moment (mirrors bankLookupInputOK).
			for (n : ins) {
				if (!(n _bankLookupInputOK)) {
					_impError("bank lookup pitch/velocity must be a note param, control, constant, or init-rate value (computable at note-on)");
				}
			}
			-- BankLookup: rate FORCED to Reset (a latch), i32, chans 1, sink.
			_mapId(e.id, _addExprNode(NodeKind.bankLookupK(bi), ins,
				Rate.reset, INT32, scalarShape()));
		}
		bank(op): {
			match (op) {
				fixRead(index, readChans, startChan): {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankFixReadK(index, readChans, startChan), ins,
						Rate.audio, FLOAT64, row(readChans)));
				}
				vread(interp, readChans, startChan): {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankVarReadK(interp, readChans, startChan), ins,
						Rate.audio, FLOAT64, row(readChans)));
				}
				rootKey: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankRootKeyK, ins,
						Rate.reset, FLOAT32, scalarShape()));
				}
				sampleRate: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankSampleRateK, ins,
						Rate.reset, FLOAT64, scalarShape()));
				}
				length: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankLengthK, ins,
						Rate.reset, FLOAT64, scalarShape()));
				}
				loopStart: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankLoopStartK, ins,
						Rate.reset, FLOAT64, scalarShape()));
				}
				loopEnd: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankLoopEndK, ins,
						Rate.reset, FLOAT64, scalarShape()));
				}
				hasLoop: {
					let ins = e _resolveIns;
					_mapId(e.id, _addExprNode(NodeKind.bankHasLoopK, ins,
						Rate.reset, INT32, scalarShape()));
				}
			}
		}
		if_(thenSg, elseSg): {
			let ins = e _resolveIns;                  -- [test]
			let thenPhi = _importSubgraph(thenSg);
			let elsePhi = _importSubgraph(elseSg);
			var ctx Ctx = `scCtx;
			-- IfElseExpr rate = controlFlowRate(maxRate({test, then_phi,
			-- else_phi})); type any; chans = broadcast(then, else) (refined
			-- by calcShape in M3.3).
			let r = _cfRate(max(_maxRateOf(ins), max(ctx.nrate[thenPhi], ctx.nrate[elsePhi])));
			let sh = broadcast(ctx.shape[thenPhi], ctx.shape[elsePhi]);
			let ifNode = _addCFNode(NodeKind.ifK, ins, [thenPhi, elsePhi], r, ANY_NUM, sh);
			_linkPhi(thenPhi, ifNode, r);
			_linkPhi(elsePhi, ifNode, r);
			_mapId(e.id, ifNode);
		}
		for_(_, bodySg): {
			-- ins[0] is the count constant (front end emits [count asSignal]).
			let ins = e _resolveIns;
			let bodyPhi = _importSubgraph(bodySg);
			var ctx Ctx = `scCtx;
			let r = _cfRate(max(_maxRateOf(ins), ctx.nrate[bodyPhi]));
			let forNode = _addCFNode(NodeKind.forK, ins, [bodyPhi], r, ANY_NUM, ctx.shape[bodyPhi]);
			_linkPhi(bodyPhi, forNode, r);
			_mapId(e.id, forNode);
		}
		switch_(cases): {
			let ins = e _resolveIns;                  -- [selector]
			var subs [Int] = [];
			for (sg : cases) { subs push!(_importSubgraph(sg)); }
			var ctx Ctx = `scCtx;
			var r = _maxRateOf(ins);
			var sh = noShape();
			for (s : subs) { r = max(r, ctx.nrate[s]); sh = broadcast(sh, ctx.shape[s]); }
			r = _cfRate(r);
			let swNode = _addCFNode(NodeKind.switchK(cases length), ins, subs, r, ANY_NUM, sh);
			for (s : subs) { _linkPhi(s, swNode, r); }
			_mapId(e.id, swNode);
		}
		varexpr(name): {
			-- VarExpr: no inputs, initial_type any_int, audio rate; chans stays 0
			-- (Expr default; calcShape is a no-op), so it broadcasts to its use.
			-- Hash-consed by name via the VAR| cons key.
			_mapId(e.id, _addExprNode(NodeKind.varK(name), [Int](), Rate.audio, ANY_INT, noShape()));
		}
		voicer(maxVoices, bodySg): {
			-- One subgraph (the voice body), its root wrapped in a PhiNode. The
			-- voicer's shape is the body root's with a leading voice dimension,
			-- computed from the root directly (not broadcast over the phi) to
			-- break the voicer<->phi shape cycle; see _voicerShape in types.x.
			-- The body's note parameters are columns of the synth's `notes`
			-- input, {maxVoices, 1 + params}, created here once the body has
			-- declared them.
			var `scNoteCol Int = 1;
			let bodyPhi = _importSubgraph(bodySg);
			let cols Int = `scNoteCol;
			var ctx Ctx = `scCtx;
			let bodyRoot = ctx.ins[bodyPhi][0];
			-- Audio rate: the voicer runs its voices every sample.
			let r = Rate.audio;
			let sh = prependDim(ctx.shape[bodyRoot], maxVoices);
			let vNode = _addCFNode(NodeKind.voicerK(maxVoices), [Int](), [bodyPhi], r, ANY_NUM, sh);
			_linkPhi(bodyPhi, vNode, r);
			_mapId(e.id, vNode);
			let sn Int = `scInletSerials;
			`scInletSerials = sn + 1;
			let notesName = ctx.notesInlets length == 0 ? "notes" : "notes%^" fmt(ctx.notesInlets length);
			let ni = _addExprNode(NodeKind.inletK(notesName, sn, Option<InletSpec>.none), [Int](),
				Rate.block, FLOAT32, matrix(maxVoices, cols));
			ctx.notesInlets push!(ni);
			ctx.voicerNotes push!(vNode);
		}
		spectralFrame(fftSize): {
			-- The packed split-complex FFT frame: a source node (no inputs), type
			-- f32. Its chans (inputChans * fftSize) is set by the enclosing
			-- spectral_chain once its input is known; default to fftSize (mono).
			let n = _addExprNode(NodeKind.spectralFrameK(fftSize), [Int](), Rate.audio, FLOAT32, row(fftSize));
			`scLastSpectralFrame = n;
			_mapId(e.id, n);
		}
		spectralChain(fftSize, hopSize, bodySg): {
			-- SpectralChainExpr is a ControlFlowExpr with one subgraph (the body,
			-- operating on the frame). rate/type from the input; chans = input chans.
			let ins = e _resolveIns;                  -- [input]
			let frame = `scLastSpectralFrame;
			let bodyPhi = _importSubgraph(bodySg);
			var ctx Ctx = `scCtx;
			let r = max(_maxRateOf(ins), ctx.nrate[bodyPhi]);
			let sh = ctx.shape[ins[0]];
			-- The frame's shape is inputChans * fftSize.
			if (frame != NONE) {
				ctx.shape[frame] = row(sh chans * fftSize);
				ctx.chans[frame] = sh chans * fftSize;
			}
			let scNode = _addCFNode(NodeKind.spectralChainK(fftSize, hopSize), ins, [bodyPhi], r, FLOAT32, sh);
			_linkPhi(bodyPhi, scNode, r);
			_mapId(e.id, scNode);
		}
	}
}
