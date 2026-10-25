-- synthc/ir.x
-- Compiler-private IR for the Tzopilotl-hosted synthdef compiler.
--
-- A node is an Int index (NIdx) into the parallel arrays of Ctx.
-- Mutable arrays are reference types, so a Ctx value can be passed to every
-- pass by value while all writes are shared.
--
-- The analysis dump (dumpToString) is the compiler's golden-test surface:
-- shapes, rates, types, cuts, trees, delay-weighted cycles and parameter
-- groups (run_synthc_dump_test.sh).

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
	block:    "Block";
	audio:    "Audio";
};

-- Widening (mirror NumType::widen / up / down in synthdef_signal_type.hpp).
-- Order i32 < i64 < f32 < f64 is the flag-bit order, so max is the larger bit.
-- numWiden: { max(a, b) | a in x, b in y }; numUp: types no narrower than some
-- member; numDown: types no wider than some member.
fn numWiden(x NumType, y NumType) NumType {
	var out = 0;
	var a = 1;
	while (a <= 8) {
		if ((x.0 & a) != 0) {
			var b = 1;
			while (b <= 8) {
				if ((y.0 & b) != 0) { out = out | max(a, b); }
				b = b << 1;
			}
		}
		a = a << 1;
	}
	NumType(out)
}
fn numUp(x NumType) NumType {
	if (x.0 == 0) { return NO_NUM; }
	let lo = x.0 & (0 - x.0);
	NumType(15 & ~(lo - 1))
}
fn numDown(x NumType) NumType {
	if (x.0 == 0) { return NO_NUM; }
	var hi = 8;
	while ((x.0 & hi) == 0) { hi = hi >> 1; }
	NumType(hi | (hi - 1))
}

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
	sharedInK(Int, Rate),                -- slot, rate (both are in the cons key)

	-- A note parameter: column `col` (and following, for multichannel) of the
	-- voicer's `notes` input row of the current voice.
	noteParamK(ControlSpec, String, Int),  -- spec, name, column
	inletK(String, Int, Option<InletSpec>), -- name, serial, spec (controls)
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

	-- Sample banks: the lookup latches (pitch, velocity) per note-on and
	-- resolves one sample; reads/accessors reference it via ins[0].
	bankLookupK(Int),                        -- bank index
	bankFixReadK(Int, Int, Int),             -- index, readChans, startChan
	bankVarReadK(Interpolation, Int, Int),   -- interp, readChans, startChan (ins = [lookup, index])
	bankRootKeyK,
	bankSampleRateK,
	bankLengthK,
	bankLoopStartK,
	bankLoopEndK,
	bankHasLoopK,

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
	bankLookupK(_):      true;
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
	at:           "vec_at";
	put:          "vec_put";
	join:         "vec_join";
	_:            "vec_?";
};

-- A vop whose output is built by its own block (memcpy/scatter), so it gets its
-- own loop with no per-channel for-wrapper. Mirrors VecPut/VecJoin gets_own_loop().
fn _vecGetsOwnLoop(vop VecOp) Bool = match (vop) { put: true; join: true; _: false; };

fn shouldHashCons(k NodeKind) Bool = match (k) {
	inletK(_, _, _):        false;
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
-- vec input materialization. at needs only input 0 as a temp array; put/join
-- materialize all inputs; the reorders need input 0.
fn needsInputTempVar(k NodeKind, i Int) Bool = match (k) {
	reduceK(_, _): true;
	vecK(vop):     match (vop) { put: true; join: true; _: i == 0; };
	phiK(_):       true;     -- PhiNodeExpr::needs_input_temp_var
	_:             false;
};

-- Mirrors Expr::gets_own_loop() for the value-producing kinds synthc forms loops
-- around here (control flow is handled separately via isControlFlowKind). put/join
-- build their output array in a block.
fn getsOwnLoop(k NodeKind) Bool = match (k) { vecK(vop): vop _vecGetsOwnLoop; _: false; };

-- Mirrors Expr::output_must_be_separate_loop() == gets_own_loop().
fn outputMustBeSeparateLoop(k NodeKind) Bool = getsOwnLoop(k);

-- Mirrors Expr::input_must_be_separate_loop(i). reduce + the single-input vec
-- reorders force input 0; at forces all inputs; put/join force all (gets_own_loop).
fn inputMustBeSeparateLoop(k NodeKind, i Int) Bool = match (k) {
	reduceK(_, _): true;
	vecK(vop):     match (vop) { at: true; put: true; join: true; _: i == 0; };
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

fn isVoicerKind(k NodeKind) Bool = match (k) {
	voicerK(_): true;
	_:          false;
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
	shape Shape,
	chans Int,             -- shape chans
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
		shape: scalarShape(),
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
-- Feedback cycles (cycles.x): the strongly connected components of the graph
-- with delay-weighted edges.

struct CycleInfo {
	nodes [NIdx],          -- the component, in sorted order
	readers [NIdx],        -- delay readers whose edge lies inside it
	minWeight Int,         -- weight of its lightest cycle
}

-- A parameter group (paramgroups.x): block-placed nodes with one source set.
struct ParamGroup {
	sources [String],      -- input names / note columns, sorted
	nodes [NIdx],          -- in sorted order
	perVoice Bool,         -- fed by note parameters: runs per voice
}

---------------------------------------------------------------------------
-- Trees and loops (filled in by later passes)

struct Tree {
	root NIdx,
	exprs [NIdx],
	antecedents [Int],        -- antecedent tree indices
	antecedentsSep [Bool],    -- parallel: separate-loop constraint flag
	loopOf Int,               -- loop index, NONE if none
	serial Int,
}

struct Loop {
	trees [Int],              -- tree indices, sorted order
	rate Rate,
	chans Int,
	isControlFlow Bool,
	antecedents [Int],        -- loop serials
	graphOf Int,              -- graph index this loop runs in (0 = root)
	serial Int,
}

-- A graph: the root (index 0) plus one per control-flow branch/body subgraph
-- (M3). Mirrors synthdef::Graph. Audio-rate loops are bucketed per graph (the
-- C++ keeps them in Graph::loops); init/reset loops stay synth-global.
struct GraphInfo {
	serial Int,               -- graph index (0 = root)
	parent Int,               -- parent graph index, NONE for root
	audioLoops [Int],         -- audio-rate loop indices in this graph
}

---------------------------------------------------------------------------
-- Ctx: the whole compilation state, struct-of-arrays

struct Ctx {
	name String,
	tags [String],            -- category tags for the loadTags emission

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
	shape [Shape],
	chans [Int],              -- shape chans, kept in step with `shape`
	graphOf [Int],            -- graph index each node belongs to (0 = root)
	cut [GraphCut],
	treeOf [Int],             -- tree index, NONE if none
	consumers [[NIdx]],
	suppressed [Bool],        -- set by inplaceDelayOps: the node's tree is
	                          -- folded into an in-place delay element access
	                          -- and must not be emitted

	-- graph-level node lists (indices into the node arrays)
	sorted [NIdx],
	sinks [NIdx],
	inlets [NIdx],
	outlets [NIdx],
	noteParams [NIdx],
	notesInlets [NIdx],       -- per voicer: its `notes` input {maxVoices, cols}
	voicerNotes [NIdx],       -- parallel: the voicer node

	delays [DelayInfo],
	dsrc [NIdx],              -- per node: for a delay reader, the value its
	                          -- delay's writer stores (the delay edge's source)
	dweight [Int],            -- per node: that edge's delay weight
	cycles [CycleInfo],
	paramGroups [ParamGroup],
	groupOf [Int],            -- per node: its parameter group, NONE if none
	delayAllocs [Int],        -- delay indices needing runtime allocation
	bufSerials [Int],         -- per-buffer SampleBuf serial (buf index -> serial)
	bankSerials [Int],        -- per-bank SampleBank serial (bank index -> serial)

	trees [Tree],
	sortedTrees [Int],        -- tree indices in sorted order
	graphs [GraphInfo],       -- root (index 0) + control-flow subgraphs (M3);
	                          -- per-graph audio loops live in GraphInfo.audioLoops
	loops [Loop],
	initLoops [Int],
	resetLoops [Int],
	blockLoops [Int],         -- block-placed (parameter group) loops
	usesRng [Bool],           -- single element; mutable through Ctx copies
	cmpTyp [NumType],         -- per node: a compare's input_type (the C++
	                          -- CompareOpExpr::input_type); ANY_NUM elsewhere
	widening [Bool],          -- single element: type inference in widening
	                          -- mode (C++ gWidenTypes)
	nextSerial [Int],         -- single element: the expr serial counter at the
	                          -- end of import (C++ exprSerialNos), for nodes
	                          -- created during analysis
	errors [String],          -- import/pass errors (no exceptions in the
	                          -- language; callers must check)
}

fn newCtx(name String) Ctx {
	Ctx {
		name: name,
		tags: [String](),
		kind: [NodeKind](),
		ins: [[NIdx]](),
		subs: [[NIdx]](),
		serial: [Int](),
		nrate: [Rate](),
		typ: [NumType](),
		shape: [Shape](),
		chans: [Int](),
		graphOf: [Int](),
		cut: [GraphCut](),
		treeOf: [Int](),
		consumers: [[NIdx]](),
		suppressed: [Bool](),
		sorted: [NIdx](),
		sinks: [NIdx](),
		inlets: [NIdx](),
		outlets: [NIdx](),
		noteParams: [NIdx](),
		notesInlets: [NIdx](),
		voicerNotes: [NIdx](),
		delays: [DelayInfo](),
		dsrc: [NIdx](),
		dweight: [Int](),
		cycles: [CycleInfo](),
		paramGroups: [ParamGroup](),
		groupOf: [Int](),
		delayAllocs: [Int](),
		bufSerials: [Int](),
		bankSerials: [Int](),
		trees: [Tree](),
		sortedTrees: [Int](),
		graphs: [GraphInfo { serial: 0, parent: NONE, audioLoops: [Int]() }],
		loops: [Loop](),
		initLoops: [Int](),
		resetLoops: [Int](),
		blockLoops: [Int](),
		usesRng: [false],
		cmpTyp: [NumType](),
		widening: [false],
		nextSerial: [0],
		errors: [String](),
	}
}

fn numNodes(ctx Ctx) Int = ctx.kind length;

-- True if the control-flow node `n` (if/switch/for/voicer/spectralChain)
-- guards a side-effecting write (delayWrite, bufWrite, outlet, ...) in one of
-- its branch/body subgraphs, even when the node's own result value has no
-- consumers. Mirrors ControlFlowExpr::has_side_effect_subgraph in
-- synthdef_expr.cpp (the C++ synthdef-compiler). Used by _addCFNode
-- (importer.x) to keep such a node reachable from the sinks-rooted
-- topological walk, and by findGraphCuts (passes.x) to keep it from being
-- marked Unused and swept away -- otherwise a write like `y <- white(chans)`
-- used only as a statement (if_'s own result discarded) silently never runs.
fn hasSideEffectSubgraph(ctx Ctx, n NIdx) Bool {
	for (phi : ctx.subs[n]) {
		if (ctx _graphHasSideEffect(ctx.graphOf[phi])) { return true; }
	}
	false
}

fn _graphHasSideEffect(ctx Ctx, g Int) Bool {
	var i = 0;
	while (i < ctx numNodes) {
		if (ctx.graphOf[i] == g) {
			if (ctx.kind[i] isSinkKind) { return true; }
			if (ctx.kind[i] isControlFlowKind && ctx hasSideEffectSubgraph(i)) { return true; }
		}
		i = i + 1;
	}
	false
}

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
		sharedInK(_, _):     "sharedin";
		noteParamK(_, name, col): "noteParam(%^@%^)" fmt(name, col);
		inletK(name, _, sp): match (sp) { some(_): "control(%^)" fmt(name); none: "inlet(%^)" fmt(name); };
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
		bankLookupK(_):      "bank_lookup";
		bankFixReadK(index, _, _): "bank_fix_read(%^)" fmt(index);
		bankVarReadK(_, _, _): "bank_var_read";
		bankRootKeyK:        "bank_root_key";
		bankSampleRateK:     "bank_sample_rate";
		bankLengthK:         "bank_length";
		bankLoopStartK:      "bank_loop_start";
		bankLoopEndK:        "bank_loop_end";
		bankHasLoopK:        "bank_has_loop";
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

-- index, serial, [inputs], #inputs, #consumers, rate, cut, type, shape, str
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
		$ padLeft(ctx.shape[n] toString, 5) $ " "
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
		$ padLeft(ctx.shape[root] toString, 5) $ "\n");
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

fn _warpStr(w ControlWarp) String = match (w) {
	linear:       "lin";
	exponential:  "exp";
	step(sz):     "step(%^)" fmt(sz);
	signedSquare: "sqr";
	cubed:        "cub";
};

fn _kindStr(k ControlKind) String = match (k) {
	continuous: "continuous";
	trigger:    "trigger";
	boolean:    "toggle";
	select:     "choice";
};

-- The synth's ports: inputs (with specs and the voicer's `notes`) and
-- outputs, as the plugin ABI will present them.
fn _printPorts(ctx Ctx, out [String]) Void {
	for (n : ctx.inlets) {
		match (ctx.kind[n]) {
			inletK(name, sn, sp): {
				let specText = match (sp) {
					some(is): " %^ [%^ %^] init %^ %^" fmt(is.kind _kindStr, is.spec.lo,
						is.spec.hi, is.spec.init, is.spec.warp _warpStr);
					none: "";
				};
				out push!("IN %^ %^ %^ %^%^\n" fmt(sn, name, ctx.typ[n] numTypeStr,
					ctx.shape[n] toString, specText));
			}
			_: {}
		}
	}
	for (n : ctx.outlets) {
		match (ctx.kind[n]) {
			outletK(name, sn): {
				out push!("OUT %^ %^ %^ %^\n" fmt(sn, name, ctx.typ[n] numTypeStr,
					ctx.shape[n] toString));
			}
			_: {}
		}
	}
}

fn dumpToString(ctx Ctx) String {
	var out [String] = [];
	out push!("SYNTH " $ ctx.name $ "\n");
	out push!("-- PORTS\n");
	_printPorts(ctx, out);
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
	out push!("-- BLOCK\n");
	printLoops(ctx, out, ctx.blockLoops);
	out push!("-- AUDIO\n");
	for (g : ctx.graphs) {
		out push!("GRAPH %^\n" fmt(g.serial));
		printLoops(ctx, out, g.audioLoops);
	}
	out push!("-- PARAMETER GROUPS\n");
	var gi = 0;
	while (gi < ctx.paramGroups length) {
		let pg = ctx.paramGroups[gi];
		var nodes [String] = [];
		for (x : pg.nodes) { nodes push!(ctx.serial[x] toString); }
		out push!("GROUP %^ sources [%^]%^ nodes [%^]\n"
			fmt(gi, pg.sources join(" "), pg.perVoice ? " per-voice" : "", nodes join(" ")));
		gi = gi + 1;
	}
	out push!("-- CYCLES\n");
	var ci = 0;
	while (ci < ctx.cycles length) {
		let cy = ctx.cycles[ci];
		var nodes [String] = [];
		for (x : cy.nodes) { nodes push!(ctx.serial[x] toString); }
		var edges [String] = [];
		for (r : cy.readers) {
			edges push!("%^<-%^:%^" fmt(ctx.serial[r], ctx.serial[ctx.dsrc[r]], ctx.dweight[r]));
		}
		let kind = cy.minWeight >= 4 ? "vector" : "recurrence";
		out push!("CYCLE %^ weight %^ %^ nodes [%^] delays [%^]\n"
			fmt(ci, cy.minWeight, kind, nodes join(" "), edges join(" ")));
		ci = ci + 1;
	}
	for (d : ctx.delays) {
		out push!("DELAY %^ %^ %^\n" fmt(d.serial, d.typ numTypeStr, d.shape toString));
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
		$ ctx.shape[n] toString $ " "
		$ nodeStr(ctx, n) $ "\n");
}
