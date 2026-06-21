-- synthc/ir.x
-- Compiler-private IR for the Tzopilotl-hosted synthdef compiler.
--
-- A node is an Int index (NIdx) into the parallel arrays of Ctx.
-- Mutable arrays are reference types, so a Ctx value can be passed to every
-- pass by value while all writes are shared.
--
-- The dump format replicates the C++ Synth::dumpToString() exactly so the
-- two compilers can be differentially tested (see synthc/difftest.x).

import synthdef.*;

type NIdx = Int;

const NONE = -1;

---------------------------------------------------------------------------
-- GraphCut (mirrors synthdef_expr.hpp GraphCut)

enum GraphCut {
	none,
	unused,
	sink,
	phi,
	temp,
	broadcast,
	fanOut,
	controlFlow,
	separateLoop,
	rate,
	input,
	graph,
}

fn cutStr(c GraphCut) String = match (c) {
	none:         "None";
	unused:       "Unused";
	sink:         "Sink";
	phi:          "Phi";
	temp:         "Temp";
	broadcast:    "Broadcast";
	fanOut:       "FanOut";
	controlFlow:  "ControlFlow";
	separateLoop: "SeparateLoop";
	rate:         "Rate";
	input:        "Input";
	graph:        "Graph";
};

fn isTempVarCut(c GraphCut) Bool = match (c) {
	temp:         true;
	broadcast:    true;
	controlFlow:  true;
	separateLoop: true;
	fanOut:       true;
	_:            false;
};

fn isInstVarCut(c GraphCut) Bool = match (c) {
	rate:  true;
	input: true;
	graph: true;
	_:     false;
};

fn isNoneCut(c GraphCut) Bool = match (c) {
	none: true;
	_:    false;
};

---------------------------------------------------------------------------
-- Rate / NumType strings (mirror SignalRate::str() and NumType::str())

fn rateStr(r Rate) String = match (r) {
	constant: "Const";
	init:     "Init";
	reset:    "Reset";
	event:    "Event";
	audio:    "Audio";
};

fn numTypeStr(t NumType) String = match (t.0) {
	0:  "empty";
	1:  "i32";
	2:  "i64";
	4:  "f32";
	8:  "f64";
	15: "any";
	3:  "any_int";
	12: "any_float";
	5:  "any_32_bits";
	10: "any_64_bits";
	n:  _numTypeSetStr(n);
};

fn _numTypeSetStr(flags Int) String {
	var parts [String] = [];
	if ((flags & 1) != 0) { parts push!("i32"); }
	if ((flags & 2) != 0) { parts push!("i64"); }
	if ((flags & 4) != 0) { parts push!("f32"); }
	if ((flags & 8) != 0) { parts push!("f64"); }
	"{" $ parts join(", ") $ "}"
}

---------------------------------------------------------------------------
-- Constant values
--
-- The C++ Constant stores either an i64 vector or an f64 vector.
-- The s-expression path always produces f64 storage; i64 storage arises
-- from constant folding of integer ops.

enum ConstVal {
	ints [Int],
	floats [Float],
}

fn constSize(v ConstVal) Int = match (v) {
	ints(a):   a length;
	floats(a): a length;
};

-- Mirrors VectorT's extend_to_next_power_of_two: a constant value vector is
-- zero-extended to a power-of-two length (the C++ does this in every VectorT
-- construction, so every multichannel Constant has a power-of-two channel count).
fn padPow2(v ConstVal) ConstVal = match (v) {
	ints(a): {
		let n = a length bitCeil;
		if (n == a length) { ConstVal.ints(a) }
		else { var out = a copy; while (out length < n) { out push!(0); } ConstVal.ints(out) }
	}
	floats(a): {
		let n = a length bitCeil;
		if (n == a length) { ConstVal.floats(a) }
		else { var out = a copy; while (out length < n) { out push!(0.0); } ConstVal.floats(out) }
	}
};

fn constF64At(v ConstVal, i Int) Float = match (v) {
	ints(a):   a[i] toFloat;
	floats(a): a[i];
};

fn constI64At(v ConstVal, i Int) Int = match (v) {
	ints(a):   a[i];
	floats(a): a[i] toInt;
};

---------------------------------------------------------------------------
-- Node kinds (mirror the C++ Expr class hierarchy)
--
-- Payloads carry exactly the data the C++ class carries beyond inputs:
-- per-kind serials, op tags, delay/buffer indices, etc.

enum NodeKind {
	constant(ConstVal, NumType),         -- value, init_type
	sampleRate,
	sampleDur,

	control(ControlSpec, String, Int),   -- spec, name, serial
	noteParamK(ControlSpec, String, Int),
	inletK(String, Int),                 -- name, serial
	outletK(String, Int),                -- name, serial

	unopK(UnaryOp),
	binopK(BinaryOp),
	compareopK(CompareOp),
	castopK(NumType),                    -- cast_type
	reduceK(BinaryOp, Int),              -- op, cols
	vecK(VecOp),                         -- take/drop/stride/stutter/ncyc/rotate/reverse/transpose
	selectK,

	urandK(Int),                         -- serial
	birandK(Int),
	rand64K(Int),

	maxDelayK(Int),                      -- delay index
	delayInitK(Int, Int),                -- delay index, offset
	delayFixReadK(Int, Int),             -- delay index, delay_samples
	delayVarReadK(Int, Interpolation),   -- delay index, interpolation
	delayWriteK(Int),                    -- delay index

	bufFixReadK(Int, Int, Int, Int),         -- buf index, index, readChans, startChan
	bufVarReadK(Int, Interpolation, Int, Int), -- buf index, interp, readChans, startChan
	bufWriteK(Int, Int, Int),                -- buf index, writeChans, startChan
	bufLengthK(Int),                         -- buf index

	-- Control flow (M3). Branch/body subgraph roots are held in ctx.subs[node]
	-- (each a PhiNode index), not in `ins`; `ins` carries the test/selector/count.
	ifK,                                 -- if_else: subs = [thenPhi, elsePhi]
	switchK(Int),                        -- ncases; subs = [casePhi...]
	forK,                                -- for_loop: subs = [bodyPhi]
	voicerK(Int),                        -- maxVoices; subs = [bodyPhi] (M4)
	spectralChainK(Int, Int),            -- fftSize, hopSize; subs = [bodyPhi] (M4.4)
	spectralFrameK(Int),                 -- fftSize (packed split-complex frame source)
	phiK(Int),                           -- target control-flow node (NONE until linked)
	varK(String),                        -- loop/branch variable name

	debugK(String, Int, Int, Int),       -- label, period, consecutive, serial
}

-- Is this node a sink (graph root for tree cutting)?
fn isSinkKind(k NodeKind) Bool = match (k) {
	outletK(_, _):       true;
	maxDelayK(_):        true;
	delayInitK(_, _):    true;
	delayWriteK(_):      true;
	bufWriteK(_, _, _):  true;
	debugK(_, _, _, _):  true;
	_:                   false;
};

-- Mirrors Expr::should_hash_cons().
-- Output channel count for a vec op given its input chans (mirrors each
-- VecXxxExpr::calcShape). reverse/transpose/rotate keep the size.
fn vecOutChans(inChans Int, vop VecOp) Int = match (vop) {
	take(n):      n;
	drop(n):      inChans - n;
	stride(n):    (inChans + n - 1) // n;
	stutter(n):   inChans * n;
	ncyc(n):      inChans * n;
	_:            inChans;
};

-- Dump str() for the vec ops (mirrors VecXxxExpr::str()).
fn _vecStr(vop VecOp) String = match (vop) {
	take(n):      "vec_take(%^)" fmt(n);
	drop(n):      "vec_drop(%^)" fmt(n);
	stride(n):    "vec_stride(%^)" fmt(n);
	stutter(n):   "vec_stutter(%^)" fmt(n);
	ncyc(n):      "vec_ncyc(%^)" fmt(n);
	reverse:      "vec_reverse";
	transpose(n): "vec_transpose(%^)" fmt(n);
	rotate:       "vec_rotate";
	_:            "vec_?";
};

fn shouldHashCons(k NodeKind) Bool = match (k) {
	inletK(_, _):           false;
	outletK(_, _):          false;
	noteParamK(_, _, _):    false;
	debugK(_, _, _, _):     false;
	phiK(_):                false;   -- PhiNodeExpr::should_hash_cons
	ifK:                    false;   -- branches live in subs, not the cons key,
	switchK(_):             false;   -- so distinct control-flow nodes never merge
	forK:                   false;
	voicerK(_):             false;
	spectralChainK(_, _):   false;
	spectralFrameK(_):      false;
	_:                      true;    -- varK hash-conses by name (VarExpr default)
};

fn isConstantKind(k NodeKind) Bool = match (k) {
	constant(_, _): true;
	_:              false;
};

-- Mirrors Expr::needs_input_temp_var(i). M1 kinds with no reordering need no
-- input temp var; reduce (and the M2 vec ops) do.
fn needsInputTempVar(k NodeKind, i Int) Bool = match (k) {
	reduceK(_, _): true;
	vecK(_):       i == 0;   -- the data input is index-remapped; rotate's n (i=1) is scalar
	phiK(_):       true;     -- PhiNodeExpr::needs_input_temp_var
	_:             false;
};

-- Mirrors Expr::output_must_be_separate_loop() / gets_own_loop(). None of the
-- M1 kinds force their own loop (VecJoin/VecPut do; deferred to M2).
fn outputMustBeSeparateLoop(k NodeKind) Bool = false;

-- Mirrors Expr::input_must_be_separate_loop(i). reduce forces it; M1 basic
-- kinds do not.
fn inputMustBeSeparateLoop(k NodeKind, i Int) Bool = match (k) {
	reduceK(_, _): true;
	vecK(_):       i == 0;
	_:             false;
};

-- Mirrors Expr::is_control_flow(). if/switch/for are ControlFlowExpr; phi and
-- var are not.
fn isControlFlowKind(k NodeKind) Bool = match (k) {
	ifK:        true;
	switchK(_): true;
	forK:       true;
	voicerK(_): true;
	spectralChainK(_, _): true;
	_:          false;
};

fn isPhiKind(k NodeKind) Bool = match (k) {
	phiK(_): true;
	_:       false;
};

fn isRngKind(k NodeKind) Bool = match (k) {
	urandK(_): true; birandK(_): true; rand64K(_): true;
	_:         false;
};

---------------------------------------------------------------------------
-- Delay records (mirror DelayBuf)

struct DelayInfo {
	serial Int,
	typ NumType,
	chans Int,
	fixReaders [NIdx],
	varReaders [NIdx],
	initters [NIdx],
	writer NIdx,           -- NONE until set
	maxDelay NIdx,         -- node index of MaxDelay input bound, NONE if none
	hasMaxBound Bool,      -- true if a max bound exists (declared or derived from
	                       -- fixed delays in calcDelayLengths); drives d_mask decl
	allocSize Int,
	maxOverread Int,
}

fn newDelayInfo(serial Int) DelayInfo {
	DelayInfo {
		serial: serial,
		typ: ANY_NUM,
		chans: 1,
		fixReaders: [NIdx](),
		varReaders: [NIdx](),
		initters: [NIdx](),
		writer: NONE,
		maxDelay: NONE,
		hasMaxBound: false,
		allocSize: 0,
		maxOverread: 0,
	}
}

-- Mirrors interpOverread() in synthdef_expr.hpp.
fn interpOverread(i Interpolation) Int = match (i) {
	none:     0;
	linear:   1;
	cubic:    2;
	lagrange: 4;
	sinc:     4;
};

---------------------------------------------------------------------------
-- Trees and loops (filled in by later passes)

struct Tree {
	root NIdx,
	exprs [NIdx],
	antecedents [Int],        -- antecedent tree indices
	antecedentsSep [Bool],    -- parallel: separate-loop constraint flag
	loopOf Int,               -- loop index, NONE if none
	isoGroupOf Int,           -- iso-group index, NONE if not event-rate
	serial Int,
}

struct Loop {
	trees [Int],              -- tree indices, sorted order
	rate Rate,
	chans Int,
	isControlFlow Bool,
	isoGroup Int,             -- iso-group index, NONE for non-event loops
	antecedents [Int],        -- loop serials
	graphOf Int,              -- graph index this loop runs in (0 = root)
	serial Int,
}

-- A graph: the root (index 0) plus one per control-flow branch/body subgraph
-- (M3). Mirrors synthdef::Graph. Audio-rate loops are bucketed per graph (the
-- C++ keeps them in Graph::loops); init/reset/event loops stay synth-global.
struct GraphInfo {
	serial Int,               -- graph index (0 = root)
	parent Int,               -- parent graph index, NONE for root
	audioLoops [Int],         -- audio-rate loop indices in this graph
}

-- An iso-group: a set of event-rate trees sharing the same transitive control
-- dependency set. Mirrors synthdef::IsoGroup. `controls` is a sorted list of
-- control node indices (set identity); `activates` lists downstream group
-- indices; `serial` is the final topological position.
struct IsoGroup {
	trees [Int],
	loops [Int],
	controls [NIdx],
	activates [Int],
	serial Int,
}

---------------------------------------------------------------------------
-- Ctx: the whole compilation state, struct-of-arrays

struct Ctx {
	name String,

	-- per-node parallel arrays, indexed by NIdx
	kind [NodeKind],
	ins [[NIdx]],
	subs [[NIdx]],            -- control-flow branch/body PhiNode roots (empty
	                          -- for ordinary nodes); the synthc analog of
	                          -- IfElseExpr.then_expr/else_expr/cases
	serial [Int],             -- C++ userial (with gaps where nodes were
	                          -- hash-consed away or folded)
	nrate [Rate],
	typ [NumType],
	chans [Int],
	graphOf [Int],            -- graph index each node belongs to (0 = root)
	cut [GraphCut],
	treeOf [Int],             -- tree index, NONE if none
	consumers [[NIdx]],

	-- graph-level node lists (indices into the node arrays)
	sorted [NIdx],
	sinks [NIdx],
	inlets [NIdx],
	outlets [NIdx],
	controls [NIdx],
	noteParams [NIdx],

	delays [DelayInfo],
	delayAllocs [Int],        -- delay indices needing runtime allocation
	bufSerials [Int],         -- per-buffer SampleBuf serial (buf index -> serial)

	trees [Tree],
	sortedTrees [Int],        -- tree indices in sorted order
	isoGroups [IsoGroup],     -- event-rate iso-groups (empty if no controls)
	isoOrder [Int],           -- iso-group indices in topological order
	graphs [GraphInfo],       -- root (index 0) + control-flow subgraphs (M3);
	                          -- per-graph audio loops live in GraphInfo.audioLoops
	loops [Loop],
	initLoops [Int],
	resetLoops [Int],
	eventLoops [Int],
	usesRng [Bool],           -- single element; mutable through Ctx copies
	errors [String],          -- import/pass errors (no exceptions in the
	                          -- language; callers must check)
}

fn newCtx(name String) Ctx {
	Ctx {
		name: name,
		kind: [NodeKind](),
		ins: [[NIdx]](),
		subs: [[NIdx]](),
		serial: [Int](),
		nrate: [Rate](),
		typ: [NumType](),
		chans: [Int](),
		graphOf: [Int](),
		cut: [GraphCut](),
		treeOf: [Int](),
		consumers: [[NIdx]](),
		sorted: [NIdx](),
		sinks: [NIdx](),
		inlets: [NIdx](),
		outlets: [NIdx](),
		controls: [NIdx](),
		noteParams: [NIdx](),
		delays: [DelayInfo](),
		delayAllocs: [Int](),
		bufSerials: [Int](),
		trees: [Tree](),
		sortedTrees: [Int](),
		isoGroups: [IsoGroup](),
		isoOrder: [Int](),
		graphs: [GraphInfo { serial: 0, parent: NONE, audioLoops: [Int]() }],
		loops: [Loop](),
		initLoops: [Int](),
		resetLoops: [Int](),
		eventLoops: [Int](),
		usesRng: [false],
		errors: [String](),
	}
}

fn numNodes(ctx Ctx) Int = ctx.kind length;

---------------------------------------------------------------------------
-- Number formatting (mirrors synthdef_str_util.cpp ftos)

fn ftosI32(x Int) String = x toString;
fn ftosI64(x Int) String = x toString $ "LL";

-- Format an f64 the way the C++ ftos() does:
--   * 0.0 / -0.0 special-cased
--   * scientific when |x| < 1e-4 or >= 1e8
--   * integer-valued non-scientific values printed with one decimal: "440.0"
--   * otherwise the shortest round-trip decimal
--
-- The lang's toString(Float) is shortest-round-trip, which covers the last
-- case. The scientific-notation edge cases may format differently than C++;
-- they are rare in real synthdefs and will be reconciled when hit by tests.
fn ftosF64(x Float) String {
	if (x == 0.0) {
		-- distinguish -0.0: 1/-0.0 == -inf < 0
		return 1.0 / x < 0.0 ? "-0.0" : "0.0";
	}
	let a = x abs;
	let useSci = a < 0.0001 || a >= 100000000.0;
	if (!useSci && x floor == x) {
		return x toInt toString $ ".0";
	}
	x toString
}

-- f32 constants need the shortest round-trip for the f32 value (which differs
-- from the f64 shortest form, e.g. 0.70794576f vs 0.7079457843841379f), so route
-- through the C++ generator's exact formatter via FFI rather than ftosF64+"f".
fn ftosF32(x Float) String = ftosF32Cpp(x);

-- Constant::str(): per-element formatting selected by the node's
-- (post-inference) concrete type; non-concrete falls back to the storage
-- type's natural formatting.
fn constStr(v ConstVal, typ NumType) String {
	let n = v constSize;
	var parts [String] = [];
	var i = 0;
	while (i < n) {
		parts push!(_constElemStr(v, i, typ));
		i = i + 1;
	}
	n == 1 ? parts[0] : "{" $ parts join(", ") $ "}"
}

fn _constElemDefaultStr(v ConstVal, i Int) String = match (v) {
	ints(a):   ftosI64(a[i]);
	floats(a): ftosF64(a[i]);
};

fn _constElemStr(v ConstVal, i Int, typ NumType) String {
	match (typ.0) {
		1: ftosI32(v constI64At(i));
		2: ftosI64(v constI64At(i));
		4: ftosF32(v constF64At(i));
		8: ftosF64(v constF64At(i));
		_: _constElemDefaultStr(v, i);
	}
}

---------------------------------------------------------------------------
-- Op spellings for the dump (mirror to_string(UnaryOp/BinaryOp/CompareOp))

fn unopStr(op UnaryOp) String = match (op) {
	neg: "-";  not: "!";  bitNot: "~";  abs: "abs";
	sign: "unknown";          -- C++ has no Sign unary op; lowered elsewhere
	floor: "floor";  ceil: "ceil";  trunc: "trunc";  round: "round";
	sqrt: "sqrt";  cbrt: "cbrt";
	log: "log";  log2: "log2";  log10: "log10";  log1p: "log1p";
	exp: "exp";  exp2: "exp2";  exp10: "exp10";  expm1: "expm1";
	sin: "sin";  cos: "cos";  tan: "tan";
	asin: "asin";  acos: "acos";  atan: "atan";
	sinh: "sinh";  cosh: "cosh";  tanh: "tanh";
	asinh: "asinh";  acosh: "acosh";  atanh: "atanh";
	sinpi: "sinpi";  cospi: "cospi";  tanpi: "tanpi";
	lgamma: "unknown";  tgamma: "unknown";
	erf: "erf";  erfc: "erfc";
	clz: "clz";  ctz: "ctz";  clo: "unknown";  cto: "unknown";
	rotr: "unknown";  rotl: "unknown";
	bitCeil: "unknown";  bitFloor: "unknown";
	bitWidth: "numbits";  popCount: "popcount";  hasSingleBit: "unknown";
};

fn binopStr(op BinaryOp) String = match (op) {
	add: "+";  sub: "-";  mul: "*";  div: "/";  idiv: "unknown";
	mod: "mod";  bend: "unknown";
	lt: "unknown";  le: "unknown";  eq: "unknown";
	ne: "unknown";  gt: "unknown";  ge: "unknown";
	bitAnd: "&";  bitOr: "|";  bitXor: "^";
	shiftLeft: "<<";  shiftRight: ">>";  unsignedShiftRight: ">>>";
	gcd: "unknown";  lcm: "unknown";  cmp: "unknown";
	min: "min";  max: "max";
	pow: "pow";  atan2: "atan2";  hypot: "hypot";  copysign: "copysign";
};

fn compareopStr(op CompareOp) String = match (op) {
	lt: "<";  le: "<=";  eq: "==";  ne: "!=";  ge: ">=";  gt: ">";
};

-- Is the unary op float-only? (mirrors is_float_unary_op)
fn isFloatUnop(op UnaryOp) Bool = match (op) {
	floor: true;  ceil: true;  round: true;  trunc: true;
	sqrt: true;  cbrt: true;
	log: true;  log2: true;  log10: true;  log1p: true;
	exp: true;  exp2: true;  exp10: true;  expm1: true;
	sinpi: true;  cospi: true;  tanpi: true;
	sin: true;  cos: true;  tan: true;
	asin: true;  acos: true;  atan: true;
	sinh: true;  cosh: true;  tanh: true;
	asinh: true;  acosh: true;  atanh: true;
	erf: true;  erfc: true;
	_: false;
};

-- Is the unary op integer-only? (mirrors is_integer_unary_op)
fn isIntUnop(op UnaryOp) Bool = match (op) {
	not: true;  bitNot: true;
	clz: true;  ctz: true;  popCount: true;  bitWidth: true;
	_: false;
};

-- (mirrors is_float_binary_op)
fn isFloatBinop(op BinaryOp) Bool = match (op) {
	pow: true;  hypot: true;  atan2: true;  copysign: true;
	_: false;
};

-- (mirrors is_integer_binary_op)
fn isIntBinop(op BinaryOp) Bool = match (op) {
	bitAnd: true;  bitOr: true;  bitXor: true;
	shiftLeft: true;  shiftRight: true;  unsignedShiftRight: true;
	_: false;
};

---------------------------------------------------------------------------
-- Node str() (mirrors the per-class Expr::str())

fn nodeStr(ctx Ctx, n NIdx) String {
	match (ctx.kind[n]) {
		constant(v, _):      constStr(v, ctx.typ[n]);
		sampleRate:          "fs";
		sampleDur:           "sd";
		control(_, _, _):    "control";
		noteParamK(_, _, _): "noteParam";
		inletK(_, _):        "inlet";
		outletK(_, _):       "outlet";
		unopK(op):           op unopStr;
		binopK(op):          op binopStr;
		compareopK(op):      op compareopStr;
		castopK(t):          "cast_" $ t numTypeStr;
		reduceK(op, cols):   "reduce(%^, %^)" fmt(op binopStr, cols);
		vecK(vop):           _vecStr(vop);
		selectK:             "select";
		urandK(_):           "urand";
		birandK(_):          "birand";
		rand64K(_):          "rand64";
		maxDelayK(_):        "max_delay";
		delayInitK(_, _):    "delay_init";
		delayFixReadK(_, k): "delay_fix_read(%^)" fmt(k);
		delayVarReadK(_, _): "delay_var_read";
		delayWriteK(_):      "delay_write";
		bufFixReadK(_, index, _, _): "buf_fix_read(%^)" fmt(index);
		bufVarReadK(_, _, _, _): "buf_var_read";
		bufWriteK(_, _, _):  "buf_write";
		bufLengthK(_):       "buf_length";
		ifK:                 "if_else";
		switchK(_):          "switch";
		forK:                "for_loop";
		voicerK(_):          "voicer";
		spectralChainK(fft, hop): "spectral_chain(%^, %^)" fmt(fft, hop);
		spectralFrameK(fft): "spectral_frame(%^)" fmt(fft);
		phiK(_):             "PhiNode";
		varK(name):          name;
		debugK(label, _, _, _): "debug(\"%^\")" fmt(label);
	}
}

---------------------------------------------------------------------------
-- Dump printing (mirrors printExpr/printTree/printLoops/dumpToString)

fn padLeft(s String, n Int) String = s length >= n ? s : padLeft(" " $ s, n);
fn padRight(s String, n Int) String = s length >= n ? s : padRight(s $ " ", n);

fn _inputsString(ctx Ctx, n NIdx) String {
	var parts [String] = [];
	for (i : ctx.ins[n]) {
		parts push!(ctx.serial[i] toString);
	}
	parts join(" ")
}

-- "   {:>4} {:>4} [{:<16}] {:>2} {:>2} {:<8} {:<9} {:<3} {:>3} {}\n"
fn printExpr(ctx Ctx, out [String], n NIdx, i Int) Void {
	let line = "   "
		$ padLeft(i toString, 4) $ " "
		$ padLeft(ctx.serial[n] toString, 4)
		$ " [" $ padRight(_inputsString(ctx, n), 16) $ "] "
		$ padLeft(ctx.ins[n] length toString, 2) $ " "
		$ padLeft(ctx.consumers[n] length toString, 2) $ " "
		$ padRight(ctx.nrate[n] rateStr, 8) $ " "
		$ padRight(ctx.cut[n] cutStr, 9) $ " "
		$ padRight(ctx.typ[n] numTypeStr, 3) $ " "
		$ padLeft(ctx.chans[n] toString, 3) $ " "
		$ nodeStr(ctx, n) $ "\n";
	out push!(line);
}

-- "    TREE {:<4} [{}] {:<6} {:<9} {:<3} {:>4}\n"
fn printTree(ctx Ctx, out [String], ti Int) Void {
	let tree = ctx.trees[ti];
	var ante [String] = [];
	var k = 0;
	while (k < tree.antecedents length) {
		-- antecedents are tree indices; the dump prints the tree's serial.
		let aser = ctx.trees[tree.antecedents[k]].serial;
		ante push!(aser toString $ (tree.antecedentsSep[k] ? "*" : ""));
		k = k + 1;
	}
	let root = tree.root;
	out push!("    TREE "
		$ padRight(tree.serial toString, 4)
		$ " [" $ ante join(" ") $ "] "
		$ padRight(ctx.nrate[root] rateStr, 6) $ " "
		$ padRight(ctx.cut[root] cutStr, 9) $ " "
		$ padRight(ctx.typ[root] numTypeStr, 3) $ " "
		$ padLeft(ctx.chans[root] toString, 4) $ "\n");
	var i = 0;
	while (i < tree.exprs length) {
		printExpr(ctx, out, tree.exprs[i], i);
		i = i + 1;
	}
}

-- "  LOOP {:>2} [{}] {:<6} {}\n"
fn printLoops(ctx Ctx, out [String], loopIdxs [Int]) Void {
	for (li : loopIdxs) {
		let lp = ctx.loops[li];
		-- Sort antecedent serials so the dump is deterministic and matches the
		-- C++ printLoops (its loop_antecedents is an unordered_set, now sorted).
		var ante [String] = [];
		for (s : lp.antecedents sort) {
			ante push!(s toString);
		}
		out push!("  LOOP "
			$ padLeft(lp.serial toString, 2)
			$ " [" $ ante join(" ") $ "] "
			$ padRight(lp.rate rateStr, 6) $ " "
			$ lp.chans toString $ "\n");
		for (t : lp.trees) {
			printTree(ctx, out, t);
		}
	}
}

fn dumpToString(ctx Ctx) String {
	var out [String] = [];
	out push!("SYNTH " $ ctx.name $ "\n");
	out push!("-- SORTED EXPRS\n");
	var i = 0;
	while (i < ctx.sorted length) {
		printExpr(ctx, out, ctx.sorted[i], i);
		i = i + 1;
	}
	out push!("-- TREES\n");
	for (t : ctx.sortedTrees) {
		printTree(ctx, out, t);
	}
	out push!("-- INIT\n");
	printLoops(ctx, out, ctx.initLoops);
	out push!("-- RESET\n");
	printLoops(ctx, out, ctx.resetLoops);
	out push!("-- EVENT\n");
	printLoops(ctx, out, ctx.eventLoops);
	out push!("-- AUDIO\n");
	for (g : ctx.graphs) {
		out push!("GRAPH %^\n" fmt(g.serial));
		printLoops(ctx, out, g.audioLoops);
	}
	for (d : ctx.delays) {
		out push!("DELAY %^ %^ %^\n" fmt(d.serial, d.typ numTypeStr, d.chans));
		for (n : d.initters) { _printDelayMember(ctx, out, n); }
		for (n : d.fixReaders) { _printDelayMember(ctx, out, n); }
		for (n : d.varReaders) { _printDelayMember(ctx, out, n); }
		if (d.writer != NONE) { _printDelayMember(ctx, out, d.writer); }
	}
	out join
}

-- "  {:>4} {:<3} {} {}\n"
fn _printDelayMember(ctx Ctx, out [String], n NIdx) Void {
	out push!("  "
		$ padLeft(ctx.serial[n] toString, 4) $ " "
		$ padRight(ctx.typ[n] numTypeStr, 3) $ " "
		$ ctx.chans[n] toString $ " "
		$ nodeStr(ctx, n) $ "\n");
}
