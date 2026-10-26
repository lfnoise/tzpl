-- synthdef.x
-- The graph construction API for synths, plus playback helpers. Re-exports
-- `synthdef_ffi.*` (the synthc build and load FFIs) so `import synthdef.*;`
-- brings both. Synths are compiled by synthc: `defSynthX` in synthc/compile.x.

export synthdef_ffi.*;


---------------------------------------------------------------------------
--- Rate

-- When a value is computed, the placement lattice of the block engine:
-- constant (compile time) < init (once) < reset (per voice onset) < block
-- (once per block, falling back to per sample when an input source is not
-- constant for the block) < audio (per sample). `block` is the compiler's
-- placement of parameter groups; nothing declares it.
enum Rate {
	constant,
	init,
	reset,
	block,
	audio,
}

fn < (a Rate, b Rate) Bool = a ordinal <  b ordinal;
fn <=(a Rate, b Rate) Bool = a ordinal <= b ordinal;
fn > (a Rate, b Rate) Bool = a ordinal >  b ordinal;
fn >=(a Rate, b Rate) Bool = a ordinal >= b ordinal;

fn min(a Rate, b Rate) Rate = a <= b ? a : b;
fn max(a Rate, b Rate) Rate = a >= b ? a : b;

---------------------------------------------------------------------------
-- NumType

struct NumType(Int);

fn ~ (a NumType) NumType = NumType(~a.0);
fn | (a NumType, b NumType) NumType = NumType(a.0 | b.0);
fn & (a NumType, b NumType) NumType = NumType(a.0 & b.0);
fn << (a NumType, b Int) NumType = NumType(a.0 << b);
fn >> (a NumType, b Int) NumType = NumType(a.0 >> b);

const NO_NUM = NumType(0);
const INT32 = NumType(1);
const INT64 = NumType(2);
const FLOAT32 = NumType(4);
const FLOAT64 = NumType(8);

const ANY_INT = NumType(3);
const ANY_FLOAT = NumType(12);
const ANY_32_BITS = NumType(5);
const ANY_64_BITS = NumType(10);
const ANY_NUM = NumType(15);

fn isConcrete(o NumType) Bool = o.0 hasSingleBit;
fn isEmpty(o NumType) Bool = o.0 == 0;
fn notEmpty(o NumType) Bool = o.0 != 0;
fn is32bits(o NumType) Bool  = (o & ANY_32_BITS) notEmpty;
fn is64bits(o NumType) Bool  = (o & ANY_64_BITS) notEmpty;
fn isInt(o NumType) Bool     = (o & ANY_INT) notEmpty;
fn isFloat(o NumType) Bool   = (o & ANY_FLOAT) notEmpty;

fn to64bits(o NumType) NumType {
	let a = (o & ANY_32_BITS) << 1;
	let b = (o & ANY_64_BITS);
	a | b
}
fn to32bits(o NumType) NumType {
	let a = (o & ANY_32_BITS);
	let b = (o & ANY_64_BITS) >> 1;
	a | b
}
fn intToFloat(o NumType) NumType {
	let a = (o & ANY_INT) << 2;
	let b = (o & ANY_FLOAT);
	a | b
}

fn commonType(a NumType, b NumType) NumType {
	if (a is64bits || b is64bits) {
		a isFloat || b isFloat ? FLOAT64 : INT64
	} else {
		a isFloat || b isFloat ? FLOAT32 : INT32
	}
}

fn toString(a NumType) String {
	match (a.0) {
		NO_NUM  : "EMPTY";
		INT32   : "i32";
		INT64   : "i64";
		FLOAT32 : "f32";
		FLOAT64 : "f64";
		n : n toString;
	}
}

fn isIntegerValued(o Float) Bool = o == o floor;

fn numType(o Bool) NumType = INT32;
fn numType(o Int)  NumType = ANY_NUM;
fn numType(o Float) NumType = o isIntegerValued ? ANY_NUM : ANY_FLOAT;

---------------------------------------------------------------------------
--- Shape
--
-- A signal is a tensor of channels: up to 4 dimensions, row-major (the last
-- dimension varies fastest), unused leading dimensions 1. Channel c is the
-- row-major flattening. `rank` counts the dimensions in use from the last:
-- a scalar is rank 0, row(n) rank 1, and matrix(1, n) rank 2 although its
-- dims equal row(n)'s. Mirrors tzpl_Shape in the plugin ABI.

struct Shape {
	dims [Int],    -- always 4 entries
	rank Int,      -- 0 .. MAX_RANK
}

const MAX_RANK = 4;

fn shape(dims [Int]) Shape {
	var d [Int] = [];
	var k = dims length;
	while (k < MAX_RANK) { d push!(1); k = k + 1; }
	for (x : dims) { d push!(x); }
	Shape { dims: d, rank: dims length }
}
fn scalarShape() Shape = Shape { dims: [1, 1, 1, 1], rank: 0 };
fn row(n Int) Shape = shape([n pow2Compat]);
-- A signal of n channels given by count: a scalar for one, else a row.
fn chansShape(n Int) Shape = n == 1 ? scalarShape() : row(n);

-- Verification only: with TZPL_POW2_CHANS=1 channel counts round up to
-- powers of two as they did before the block engine, so renders can be
-- compared with references made then. Not a supported mode.
fn pow2CompatOn() Bool = match (getEnv("TZPL_POW2_CHANS")) { some(v): v == "1"; none: false; };
fn pow2Compat(n Int) Int = (pow2CompatOn() && n > 0) ? n bitCeil : n;
fn matrix(rows Int, cols Int) Shape = shape([rows, cols]);
fn cuboid(planes Int, rows Int, cols Int) Shape = shape([planes, rows, cols]);

fn chans(s Shape) Int = s.dims[0] * s.dims[1] * s.dims[2] * s.dims[3];

-- The dimensions in use, outermost first ([] for a scalar).
fn dimsInUse(s Shape) [Int] {
	var out [Int] = [];
	var d = MAX_RANK - s.rank;
	while (d < MAX_RANK) { out push!(s.dims[d]); d = d + 1; }
	out
}

fn ==(a Shape, b Shape) Bool =
	a.rank == b.rank && a.dims[0] == b.dims[0] && a.dims[1] == b.dims[1]
	&& a.dims[2] == b.dims[2] && a.dims[3] == b.dims[3];
fn !=(a Shape, b Shape) Bool = !(a == b);

fn _gcd(a Int, b Int) Int = b == 0 ? a : _gcd(b, a % b);
fn _lcm(a Int, b Int) Int = (a == 0 || b == 0) ? max(a, b) : a // _gcd(a, b) * b;

-- Each dimension is the least common multiple of a's and b's, so a 2-channel
-- signal times a 3-channel one is 6 channels, cycling each. The rank is the
-- larger. A shape with 0 channels (not yet known) is the identity.
fn broadcast(a Shape, b Shape) Shape {
	if (a chans == 0) { return b; }
	if (b chans == 0) { return a; }
	Shape { dims: [_lcm(a.dims[0], b.dims[0]), _lcm(a.dims[1], b.dims[1]),
	               _lcm(a.dims[2], b.dims[2]), _lcm(a.dims[3], b.dims[3])],
	        rank: max(a.rank, b.rank) }
}

-- Row-major strides: the last dimension has stride 1.
fn strides(s Shape) [Int] {
	[s.dims[1] * s.dims[2] * s.dims[3], s.dims[2] * s.dims[3], s.dims[3], 1]
}

-- The source channel output channel w reads when src is broadcast to out:
-- per dimension, the output index mod the source's size.
fn broadcastChannel(src Shape, out Shape, w Int) Int {
	let st = src strides;
	var rem = w;
	var p = 0;
	var d = MAX_RANK - 1;
	while (d >= 0) {
		p = p + (rem % out.dims[d] % src.dims[d]) * st[d];
		rem = rem // out.dims[d];
		d = d - 1;
	}
	p
}

-- "6", "2x3": the dimensions in use.
fn toString(s Shape) String {
	let r = s.rank;
	if (r == 0) { return "1"; }
	var parts [String] = [];
	var d = MAX_RANK - r;
	while (d < MAX_RANK) { parts push!(s.dims[d] toString); d = d + 1; }
	parts join("x")
}

-- The dims index (0 .. MAX_RANK - 1) of an axis of s: axis 0 is the
-- outermost in use, negatives count from the last. A scalar counts as a row.
-- -1 when the axis is outside -r .. r - 1.
fn axisDim(s Shape, axis Int) Int {
	let r = max(s.rank, 1);
	if (axis < 0 - r || axis >= r) { return -1; }
	axis < 0 ? MAX_RANK + axis : MAX_RANK - r + axis
}

-- s with dims index d set to n; the rank grows to cover d.
fn withDim(s Shape, d Int, n Int) Shape {
	var dims = s.dims copy;
	dims[d] = n;
	Shape { dims: dims, rank: max(s.rank, MAX_RANK - d) }
}

-- s without dims index d (rank one less).
fn removeDim(s Shape, d Int) Shape {
	var dims = [1];
	var k = 0;
	while (k < MAX_RANK) { if (k != d) { dims push!(s.dims[k]); } k = k + 1; }
	Shape { dims: dims, rank: max(s.rank, MAX_RANK - d) - 1 }
}

-- Channels per step along dims index d: the product of the dims after it.
fn innerSize(s Shape, d Int) Int {
	var n = 1;
	var k = d + 1;
	while (k < MAX_RANK) { n = n * s.dims[k]; k = k + 1; }
	n
}

-- A leading voice dimension: n copies of s (for voicers). A rank-4 shape
-- folds the new dimension into its first.
fn prependDim(s Shape, n Int) Shape {
	let r = s.rank;
	var dims = s.dims copy;
	if (r < MAX_RANK) { dims[MAX_RANK - 1 - r] = n; }
	else { dims[0] = n * dims[0]; }
	Shape { dims: dims, rank: min(r + 1, MAX_RANK) }
}

---------------------------------------------------------------------------
-- ControlSpec

enum ControlWarp {
	linear,
	exponential,
	step Float,
	signedSquare,
	cubed,
}

struct ControlSpec {
	lo Float,
	hi Float,
	init Float,
	warp ControlWarp,
}

-- Shorthand ControlSpec constructors: linear and exponential warps cover
-- almost every control, and the struct literal is noisy at definition sites.
fn lspec(lo Float, hi Float, init Float) ControlSpec =
	ControlSpec { lo: lo, hi: hi, init: init, warp: ControlWarp.linear };

fn espec(lo Float, hi Float, init Float) ControlSpec =
	ControlSpec { lo: lo, hi: hi, init: init, warp: ControlWarp.exponential };

-- How a control behaves, and which widget UIs derive for it. Matches
-- tzpl_ControlKind in the plugin ABI. Authored via the control
-- constructors: control() is continuous; trigger()/toggle()/choice()
-- produce the other kinds.
enum ControlKind {
	continuous,   -- ranged value -> slider
	trigger,      -- momentary event; nonzero arrival = one-shot edge -> button
	boolean,      -- latched 0/1 state -> toggle
	select,       -- integer choice -> number box
}

---------------------------------------------------------------------------
-- SignalExpr


type ID = Int;

struct SignalExpr {
	id ID,
	ins [SignalExpr],
	kind SignalExprKind,
}

constraint BasicValues = Bool | Int | Fraction | Float ;
constraint AsConstantSignal = BasicValues | [BasicValues] ;
constraint AsSignal = SignalExpr | AsConstantSignal ;

type S = SignalExpr;

---------------------------------------------------------------------------
-- SynthContext

---------------------------------------------------------------------------
-- SignalGraph

struct SignalGraph {
    exprs [SignalExpr],
	delays [DelayVar],
	buffers [BufferVar],
	sampleBanks [SampleBankVar],
	root SignalExpr,
}

---------------------------------------------------------------------------
-- Signal expressions

fn _nextExprId() ID {
	let out = `exprIds;
	`exprIds = `exprIds + 1;
	out
}

fn _nextDelayVarId() ID {
	let out = `delayVarIds;
	`delayVarIds = `delayVarIds + 1;
	out
}

fn _addToGraph(expr S) S {
	`curGraphExprs = `curGraphExprs push(expr);
	expr
}

fn _addToGraph(dv DelayVar) DelayVar {
	`curGraphDelays = `curGraphDelays push(dv);
	dv
}

fn _nextBufferVarId() ID {
	let out = `bufferVarIds;
	`bufferVarIds = `bufferVarIds + 1;
	out
}

fn _addToGraph(bv BufferVar) BufferVar {
	`curGraphBuffers = `curGraphBuffers push(bv);
	bv
}

fn _nextSampleBankVarId() ID {
	let out = `sampleBankVarIds;
	`sampleBankVarIds = `sampleBankVarIds + 1;
	out
}

fn _addToGraph(sb SampleBankVar) SampleBankVar {
	`curGraphSampleBanks = `curGraphSampleBanks push(sb);
	sb
}

fn _newSignalExpr(kind SignalExprKind, ins [S]) S {
	SignalExpr {
		id: _nextExprId(),
		ins: ins,
		kind: kind,
	} _addToGraph
}

fn _newSignalExpr(kind SignalExprKind) S {
	kind _newSignalExpr([S]())
}

fn delayVar() DelayVar {
	DelayVar {
		id: _nextDelayVarId(),
		maxDelay: Option<S>.none,
	} _addToGraph
}

fn delayVar(maxDelay AsSignal) DelayVar {
	let d = DelayVar {
		id: _nextDelayVarId(),
		maxDelay: Option<S>.some(maxDelay asSignal),
	} _addToGraph;
	SignalExprKind.delay(d, DelayOp.maxDelayTime) _newSignalExpr([maxDelay asSignal]);
	d
}

---------------------------------------------------------------------------
-- Constant scalars and matrices


fn scalar(x Bool) S {
	SignalExprKind.int([x toInt], ANY_NUM, scalarShape()) _newSignalExpr
}
fn scalar(x Int) S {
	SignalExprKind.int([x], ANY_NUM, scalarShape()) _newSignalExpr
}
fn scalar(x Fraction) S {
	let x = x toFloat;
	SignalExprKind.float([x], x numType, scalarShape()) _newSignalExpr
}
fn scalar(x Float) S {
	SignalExprKind.float([x], x numType, scalarShape()) _newSignalExpr
}

fn fill(chans Int, x Int) S = fill(chansShape(chans max(1)), x);
fn fill(chans Int, x Fraction) S = fill(chansShape(chans max(1)), x);
fn fill(chans Int, x Float) S = fill(chansShape(chans max(1)), x);

fn fill(sh Shape, x Int) S {
	SignalExprKind.int(x repeat(sh chans), ANY_NUM, sh) _newSignalExpr
}
fn fill(sh Shape, x Fraction) S {
	SignalExprKind.float(x toFloat repeat(sh chans), ANY_NUM, sh) _newSignalExpr
}
fn fill(sh Shape, x Float) S {
	SignalExprKind.float(x repeat(sh chans), x numType, sh) _newSignalExpr
}

fn vec(v [Bool]) S {
	SignalExprKind.int(v toInt, ANY_NUM, row(v length)) _newSignalExpr
}
fn vec(v [Int]) S {
	SignalExprKind.int(v, ANY_NUM, row(v length)) _newSignalExpr
}
fn vec(v [Fraction]) S {
	let v = v toFloat;
	SignalExprKind.float(v, ANY_FLOAT, row(v length)) _newSignalExpr
}
fn vec(v [Float]) S {
	SignalExprKind.float(v, ANY_FLOAT, row(v length)) _newSignalExpr
}

-- A constant tensor of the given shape; values fill it cyclically in
-- row-major order.
fn tensor(sh Shape, v [Int]) S {
	var out [Int] = [];
	var i = 0;
	while (i < sh chans) { out push!(v[i % v length]); i = i + 1; }
	SignalExprKind.int(out, ANY_NUM, sh) _newSignalExpr
}
fn tensor(sh Shape, v [Float]) S {
	var out [Float] = [];
	var i = 0;
	while (i < sh chans) { out push!(v[i % v length]); i = i + 1; }
	SignalExprKind.float(out, ANY_FLOAT, sh) _newSignalExpr
}

fn asSignal(x S) S = x;
fn asSignal(x Bool) S = x scalar;
fn asSignal(x Int) S = x scalar;
fn asSignal(x Fraction) S = x scalar;
fn asSignal(x Float) S = x scalar;
fn asSignal(x [Bool]) S = x vec;
fn asSignal(x [Int]) S = x vec;
fn asSignal(x [Fraction]) S = x vec;
fn asSignal(x [Float]) S = x vec;


---------------------------------------------------------------------------
-- Signal Operators

enum UnaryOp {
	-- prefix ops
	abs, neg, not, bitNot,

	-- mixed
	sign,

	-- float
	floor, ceil, trunc, round,

	sqrt, cbrt,
	log, log2, log10, log1p,
	exp, exp2, exp10, expm1,

	sin, cos, tan,
	asin, acos, atan,
	sinh, cosh, tanh,
	asinh, acosh, atanh,
	sinpi, cospi, tanpi,

	lgamma, tgamma,
	erf, erfc,

	-- bit ops
	clz, ctz, clo, cto,
	rotr, rotl,

	bitCeil, bitFloor,
	bitWidth, popCount, hasSingleBit,

	-- complex ops
	-- real, imag, arg, norm, conj, proj,
}

enum BinaryOp {
	add, sub, mul, div, idiv,
	mod, bend,

	lt, le, eq, ne, gt, ge,

	bitAnd, bitOr, bitXor,
	shiftLeft, shiftRight, unsignedShiftRight,

	gcd, lcm,

	cmp,
	min, max,

	pow, atan2, hypot, copysign,
}

enum CompareOp {
    lt, le, eq, ne, ge, gt
}

enum CastOp {
	i32,
	i64,
	f32,
	f64,
}

-- The NumType bit of a cast's target type.
fn numTypeInt(op CastOp) Int {
	match (op) {
		i32 : 1;
		i64 : 2;
		f32 : 4;
		f64 : 8;
	}
}

-- Shape operations. An `Int` axis is as written (resolved against the
-- operand's rank by the compiler): 0 is the outermost, negatives count from
-- the last (cols = -1).
enum VecOp {
	at Int,                  -- axis; the index is input 1
	put Int,                 -- axis; the index is input 1, the values input 2
	take(Int, Int),          -- n, axis
	drop(Int, Int),          -- n, axis
	stride(Int, Int),        -- n, axis
	stutter(Int, Int),       -- n, axis
	ncyc(Int, Int),          -- n, axis
	slice(Int, Int, Int),    -- offset, length, axis
	slicev(Int, Int),        -- length, axis; the offset is input 1 (a signal)
	rotate Int,              -- axis; the amount is input 1 (a signal)
	reverse Int,             -- axis
	permute [Int],           -- axes: output axis k is input axis perm[k]
	reshape [Int],           -- dims (one may be -1); the flattening is unchanged
	join(Int, Bool),         -- axis, whether it is a new axis (stack)
	reduce BinaryOp,         -- every channel to one
	reduceAxis(BinaryOp, Int),   -- along an axis, which goes away
	scan(BinaryOp, Int),     -- running op along an axis
}

enum Interpolation {
	none,
	linear,     -- 2 points
	cubic,      -- 4 points
	lagrange,   -- 8 points
	sinc,       -- 8 points
}

fn toLispString(i Interpolation) String = match(i) {
	none:     "none";
	linear:   "linear";
	cubic:    "cubic";
	lagrange: "lagrange";
	sinc:     "sinc";
};

enum DelayOp {
	maxDelayTime,
	init Int,
	read Int,
	vread Interpolation,
	write,
}

enum RandOp {
    irand(Int, Int),
    frand(Float, Float),
    unipolar,
    bipolar,
    bits,
}

struct SignalType {
	rate Rate,
	elem NumType,
	shape Shape,
}

-- An input's UI metadata (the old controls): its spec, whose init is the
-- value the input holds while unconnected, and the widget kind.
struct InletSpec {
	spec ControlSpec,
	kind ControlKind,
}

struct DelayVar {
	id ID,
	maxDelay Option<S>,
}

struct BufferVar {
	id ID,
}

enum BufferOp {
	fixRead(Int, Int, Int),         -- index, readChans, startChan
	vread(Interpolation, Int, Int), -- interp, readChans, startChan
	write(Int, Int),                -- writeChans, startChan
	length,
}

-- A sample bank slot: a swappable pitch/velocity-mapped set of samples
-- (see audio_engine.loadSampleBank). A per-note lookup resolves (pitch,
-- velocity) to one sample of the bank.
struct SampleBankVar {
	id ID,
}

-- The value of a bank lookup: the sample resolved for this note. Wraps the
-- lookup expression; read/vread and the metadata accessors reference it.
struct BankSample {
	expr S,
}

enum BankOp {
	fixRead(Int, Int, Int),         -- index, readChans, startChan
	vread(Interpolation, Int, Int), -- interp, readChans, startChan
	rootKey,
	sampleRate,
	length,
	loopStart,
	loopEnd,
	hasLoop,
}

enum SignalExprKind {
	sampleRate,
	sampleDur,
	sharedIn(Int, Rate),

	int ([Int], NumType, Shape),
	float ([Float], NumType, Shape),

	unop UnaryOp,
	binop BinaryOp,
	compareop CompareOp,
	castop CastOp,
	random (RandOp, Rate, Shape),
	vecop VecOp,

	inlet(NumType, Shape, String, Option<InletSpec>),
	outlet(String),

	-- A column (or `chans` columns) of the enclosing voicer's `notes` input.
	noteParam(ControlSpec, Int, String),

	delay(DelayVar, DelayOp),
	buffer(BufferVar, BufferOp),
	bankLookup(SampleBankVar),
	bank(BankOp),

	if_(SignalGraph, SignalGraph),
	for_(Int, SignalGraph),
	switch_([SignalGraph]),
	select,
	select2,
	varexpr String,
	voicer(Int, SignalGraph, Float),   -- maxVoices, body, silence seconds
	-- In a voice body: the voice is done when this is nonzero after release.
	voiceDone,
	spectralChain(Int, Int, SignalGraph),
	spectralFrame Int,

	debug(String, Int, Int),
}

---------------------------------------------------------------------------
-- Basic Units

-- fs = sample rate
fn fs() S {
    SignalExprKind.sampleRate _newSignalExpr
}

-- T = sample duration = 1/fs
fn T() S {
    SignalExprKind.sampleDur _newSignalExpr
}

-- Read one slot of the engine's shared-input table: a small set of values
-- (mouse position, free user slots) written continuously by the app and read
-- by every playing synth with no control-message traffic. Slots 0..2 are the
-- mouse (see the mouseX/mouseY/mouseButton ugens); slots 3 and up are free
-- for your own values via setSharedInput. Audio rate reads the value every
-- sample; Rate.init samples it once when the synth starts. Changes are NOT
-- sample-accurately scheduled -- use a control for scheduled changes.
fn sharedIn(slot Int, rate Rate = Rate.audio) S {
    SignalExprKind.sharedIn(slot, rate) _newSignalExpr
}

-- An input port. Shape and element type are fixed; what feeds it is up to
-- the engine at run time (a connection, or a value set on it).
fn inlet(typ NumType, chans Int = 1, name String = "in") S = inlet(typ, chansShape(chans max(1)), name);

fn inlet(typ NumType, sh Shape, name String) S {
    SignalExprKind.inlet(typ, sh, name, Option<InletSpec>.none) _newSignalExpr
}

fn outlet(a S, name String = "out") S {
    SignalExprKind.outlet(name) _newSignalExpr([a])
}

-- A control: an f32 input with a spec. Its spec's init is its value while
-- nothing sets or connects it; UIs derive a widget from the kind. Like any
-- input it can be set, ramped, or patched from another signal at run time.
fn _specInlet(name String, spec ControlSpec, kind ControlKind, sh Shape) S {
    SignalExprKind.inlet(FLOAT32, sh, name,
        Option<InletSpec>.some(InletSpec { spec: spec, kind: kind })) _newSignalExpr
}

fn control(name String, spec ControlSpec, chans Int = 1) S =
    _specInlet(name, spec, ControlKind.continuous, chansShape(chans max(1)));
fn control(name String, spec ControlSpec, sh Shape) S =
    _specInlet(name, spec, ControlKind.continuous, sh);

-- Momentary control (0..1, init 0): set with an impulse, so a nonzero value
-- lasts one sample (retrigger an envelope, reset a phase). UIs render a button.
fn trigger(name String, chans Int = 1) S =
    _specInlet(name, ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear },
               ControlKind.trigger, chansShape(chans max(1)));

-- Latched on/off control (0 or 1). UIs render a toggle.
fn toggle(name String, init Float = 0.0, chans Int = 1) S =
    _specInlet(name, ControlSpec { lo: 0.0, hi: 1.0, init: init, warp: ControlWarp.linear },
               ControlKind.boolean, chansShape(chans max(1)));

-- Integer choice control: 0 .. numChoices-1 in unit steps. UIs render a
-- number box.
fn choice(name String, numChoices Int, init Int = 0, chans Int = 1) S =
    _specInlet(name, ControlSpec { lo: 0.0, hi: (numChoices - 1) toFloat, init: init toFloat,
                                   warp: ControlWarp.step(1.0) },
               ControlKind.select, chansShape(chans max(1)));

-- A note parameter of the enclosing voicer: `chans` columns of its voiced
-- synth's `notes` input (gate is column 0; the others follow in the order
-- they are declared). Set per note by noteOn / noteSetParams.
fn noteParam(name String, spec ControlSpec, chans Int = 1) S {
    SignalExprKind.noteParam(spec, chans max(1) pow2Compat, name) _newSignalExpr
}

fn gate() S {
    noteParam("gate", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear })
}

-- `maxVoices` copies of the voice body, one per sounding note. A released
-- voice is skipped once it is done: when its body declares `doneWhen`, once
-- that condition holds; otherwise once its output has stayed below -120 dB
-- for `silence` seconds (the default is longer than the period of any
-- audible pitch, so a slow tremolo's dips do not end a voice). `silence` 0
-- turns skipping off entirely, `doneWhen` included: with 4 or 8 cheap
-- voices the checks can cost more than skipping saves.
fn voicer(maxVoices Int, voiceFn GraphFn, silence Float = 0.2) S {
    let bodyGraph = voiceFn _makeSubGraph;
    SignalExprKind.voicer(maxVoices, bodyGraph, silence max(0.0)) _newSignalExpr
}

-- In a voice body: declare the voice done once its gate is off and `cond`
-- is nonzero (for example its envelope has finished). Returns `x`, so it
-- can sit in a chain: `osc * env doneWhen(env <= 0.0)`. A done voice
-- outputs 0 and costs nothing until its next note.
fn doneWhen(x S, cond S) S {
    SignalExprKind.voiceDone _newSignalExpr([cond]);
    x
}

fn spectralChain(input S, fftSize Int, hopSize Int, bodyFn GraphFn1) S {
	let frameExpr S = SignalExprKind.spectralFrame(fftSize) _newSignalExpr;
	let bodyGraph SignalGraph = bodyFn _makeSubGraph1(frameExpr);
	SignalExprKind.spectralChain(fftSize, hopSize, bodyGraph) _newSignalExpr([input])
}

-- Periodically prints the value of a signal to stderr.
-- `period` is in samples; every `period` samples, the next `consecutive` samples
-- of the signal (all channels) are dumped along with the absolute sample time
-- and the supplied label. Returns the input signal unchanged so it can be
-- inserted in a chain: `x debug("x", 1024, 4)`.
fn debug(input S, label String = "debug", period Int = 4096, consecutive Int = 1) S {
    SignalExprKind.debug(label, period, consecutive) _newSignalExpr([input]);
    input
}

---------------------------------------------------------------------------
-- Random numbers

-- Expressed via urand rather than SignalExprKind.random(RandOp.frand(...))
-- directly: the synthc importer has no lowering for RandOp.frand/irand (see
-- lang/modules/synthc/importer.x, "FRand/IRand is not supported by the
-- synthdef compiler") and urand's lowering is already exercised end-to-end.
fn frand(lo Float, hi Float, chans Int = 1, rate Rate = Rate.audio) S =
    lo + (hi - lo) * urand(chans, rate);
-- Uniform integer in [lo, hi] inclusive. Same urand-based construction as
-- frand above, for the same reason (no synthc lowering for RandOp.irand);
-- the min() guards the one-in-2^24 f32 case where u*(n) rounds up to n.
fn irand(lo Int, hi Int, chans Int = 1, rate Rate = Rate.audio) S =
    (lo + min(hi - lo, (urand(chans, rate) * (hi - lo + 1)) floor)) i32;
fn urand(chans Int = 1, rate Rate = Rate.audio) S = urand(chansShape(chans max(1)), rate);
fn birand(chans Int = 1, rate Rate = Rate.audio) S = birand(chansShape(chans max(1)), rate);
fn rand64(chans Int = 1, rate Rate = Rate.audio) S = rand64(chansShape(chans max(1)), rate);
fn urand(sh Shape, rate Rate) S {
    SignalExprKind.random(RandOp.unipolar, rate, sh) _newSignalExpr
}
fn birand(sh Shape, rate Rate) S {
    SignalExprKind.random(RandOp.bipolar, rate, sh) _newSignalExpr
}
fn rand64(sh Shape, rate Rate) S {
    SignalExprKind.random(RandOp.bits, rate, sh) _newSignalExpr
}

---------------------------------------------------------------------------

fn _newUnaryOp(op UnaryOp, a S) S {
    SignalExprKind.unop(op) _newSignalExpr([a])
}

fn _newBinaryOp(op BinaryOp, a S, b S) S {
    SignalExprKind.binop(op) _newSignalExpr([a, b])
}

fn _newCompareOp(op CompareOp, a S, b S) S {
    SignalExprKind.compareop(op) _newSignalExpr([a, b])
}

fn _newCastOp(op CastOp, a S) S {
    SignalExprKind.castop(op) _newSignalExpr([a])
}

---------------------------------------------------------------------------
-- Unary Signal Operators

fn - (a S) S = UnaryOp.neg _newUnaryOp(a);
fn ! (a S) S = UnaryOp.not _newUnaryOp(a);
fn ~ (a S) S = UnaryOp.bitNot _newUnaryOp(a);

fn abs (a S) S = UnaryOp.abs _newUnaryOp(a);

fn sign (a S) S = UnaryOp.sign _newUnaryOp(a);
fn floor (a S) S = UnaryOp.floor _newUnaryOp(a);
fn ceil (a S) S = UnaryOp.ceil _newUnaryOp(a);
fn trunc (a S) S = UnaryOp.trunc _newUnaryOp(a);
fn round (a S) S = UnaryOp.round _newUnaryOp(a);

fn sqrt (a S) S = UnaryOp.sqrt _newUnaryOp(a);
fn cbrt (a S) S = UnaryOp.cbrt _newUnaryOp(a);

fn log (a S) S = UnaryOp.log _newUnaryOp(a);
fn log2 (a S) S = UnaryOp.log2 _newUnaryOp(a);
fn log10 (a S) S = UnaryOp.log10 _newUnaryOp(a);
fn log1p (a S) S = UnaryOp.log1p _newUnaryOp(a);

fn exp (a S) S = UnaryOp.exp _newUnaryOp(a);
fn exp2 (a S) S = UnaryOp.exp2 _newUnaryOp(a);
fn exp10 (a S) S = UnaryOp.exp10 _newUnaryOp(a);
fn expm1 (a S) S = UnaryOp.expm1 _newUnaryOp(a);

fn sin (a S) S = UnaryOp.sin _newUnaryOp(a);
fn cos (a S) S = UnaryOp.cos _newUnaryOp(a);
fn tan (a S) S = UnaryOp.tan _newUnaryOp(a);
fn asin (a S) S = UnaryOp.asin _newUnaryOp(a);
fn acos (a S) S = UnaryOp.acos _newUnaryOp(a);
fn atan (a S) S = UnaryOp.atan _newUnaryOp(a);

fn sinh (a S) S = UnaryOp.sinh _newUnaryOp(a);
fn cosh (a S) S = UnaryOp.cosh _newUnaryOp(a);
fn tanh (a S) S = UnaryOp.tanh _newUnaryOp(a);
fn asinh (a S) S = UnaryOp.asinh _newUnaryOp(a);
fn acosh (a S) S = UnaryOp.acosh _newUnaryOp(a);
fn atanh (a S) S = UnaryOp.atanh _newUnaryOp(a);

fn sinpi (a S) S = UnaryOp.sinpi _newUnaryOp(a);
fn cospi (a S) S = UnaryOp.cospi _newUnaryOp(a);
fn tanpi (a S) S = UnaryOp.tanpi _newUnaryOp(a);

fn lgamma (a S) S = UnaryOp.lgamma _newUnaryOp(a);
fn tgamma (a S) S = UnaryOp.tgamma _newUnaryOp(a);

fn erf (a S) S = UnaryOp.erf _newUnaryOp(a);
fn erfc (a S) S = UnaryOp.erfc _newUnaryOp(a);

fn clz (a S) S = UnaryOp.clz _newUnaryOp(a);
fn ctz (a S) S = UnaryOp.ctz _newUnaryOp(a);
fn clo (a S) S = UnaryOp.clo _newUnaryOp(a);
fn cto (a S) S = UnaryOp.cto _newUnaryOp(a);
fn rotr (a S) S = UnaryOp.rotr _newUnaryOp(a);
fn rtol (a S) S = UnaryOp.rotl _newUnaryOp(a);
fn bitCeil (a S) S = UnaryOp.bitCeil _newUnaryOp(a);
fn bitFloor (a S) S = UnaryOp.bitFloor _newUnaryOp(a);
fn bitWidth (a S) S = UnaryOp.bitWidth _newUnaryOp(a);
fn popCount (a S) S = UnaryOp.popCount _newUnaryOp(a);
fn hasSingleBit (a S) S = UnaryOp.hasSingleBit _newUnaryOp(a);

---------------------------------------------------------------------------
-- Binary Signal Operators

fn + (a S, b S) S = BinaryOp.add _newBinaryOp(a, b);
fn + (a S, b AsConstantSignal) S = BinaryOp.add _newBinaryOp(a, b asSignal);
fn + (a AsConstantSignal, b S) S = BinaryOp.add _newBinaryOp(a asSignal, b);

fn - (a S, b S) S = BinaryOp.sub _newBinaryOp(a, b);
fn - (a S, b AsConstantSignal) S = BinaryOp.sub _newBinaryOp(a, b asSignal);
fn - (a AsConstantSignal, b S) S = BinaryOp.sub _newBinaryOp(a asSignal, b);

fn * (a S, b S) S = BinaryOp.mul _newBinaryOp(a, b);
fn * (a S, b AsConstantSignal) S = BinaryOp.mul _newBinaryOp(a, b asSignal);
fn * (a AsConstantSignal, b S) S = BinaryOp.mul _newBinaryOp(a asSignal, b);

fn / (a S, b S) S = BinaryOp.div _newBinaryOp(a, b);
fn / (a S, b AsConstantSignal) S = BinaryOp.div _newBinaryOp(a, b asSignal);
fn / (a AsConstantSignal, b S) S = BinaryOp.div _newBinaryOp(a asSignal, b);

fn // (a S, b S) S = BinaryOp.idiv _newBinaryOp(a, b);
fn // (a S, b AsConstantSignal) S = BinaryOp.idiv _newBinaryOp(a, b asSignal);
fn // (a AsConstantSignal, b S) S = BinaryOp.idiv _newBinaryOp(a asSignal, b);

fn % (a S, b S) S = BinaryOp.mod _newBinaryOp(a, b);
fn % (a S, b AsConstantSignal) S = BinaryOp.mod _newBinaryOp(a, b asSignal);
fn % (a AsConstantSignal, b S) S = BinaryOp.mod _newBinaryOp(a asSignal, b);

fn & (a S, b S) S = BinaryOp.bitAnd _newBinaryOp(a, b);
fn & (a S, b AsConstantSignal) S = BinaryOp.bitAnd _newBinaryOp(a, b asSignal);
fn & (a AsConstantSignal, b S) S = BinaryOp.bitAnd _newBinaryOp(a asSignal, b);

fn | (a S, b S) S = BinaryOp.bitOr _newBinaryOp(a, b);
fn | (a S, b AsConstantSignal) S = BinaryOp.bitOr _newBinaryOp(a, b asSignal);
fn | (a AsConstantSignal, b S) S = BinaryOp.bitOr _newBinaryOp(a asSignal, b);

fn ^ (a S, b S) S = BinaryOp.bitXor _newBinaryOp(a, b);
fn ^ (a S, b AsConstantSignal) S = BinaryOp.bitXor _newBinaryOp(a, b asSignal);
fn ^ (a AsConstantSignal, b S) S = BinaryOp.bitXor _newBinaryOp(a asSignal, b);

fn << (a S, b S) S = BinaryOp.shiftLeft _newBinaryOp(a, b);
fn << (a S, b AsConstantSignal) S = BinaryOp.shiftLeft _newBinaryOp(a, b asSignal);
fn << (a AsConstantSignal, b S) S = BinaryOp.shiftLeft _newBinaryOp(a asSignal, b);

fn >> (a S, b S) S = BinaryOp.shiftRight _newBinaryOp(a, b);
fn >> (a S, b AsConstantSignal) S = BinaryOp.shiftRight _newBinaryOp(a, b asSignal);
fn >> (a AsConstantSignal, b S) S = BinaryOp.shiftRight _newBinaryOp(a asSignal, b);

fn >>> (a S, b S) S = BinaryOp.unsignedShiftRight _newBinaryOp(a, b);
fn >>> (a S, b AsConstantSignal) S = BinaryOp.unsignedShiftRight _newBinaryOp(a, b asSignal);
fn >>> (a AsConstantSignal, b S) S = BinaryOp.unsignedShiftRight _newBinaryOp(a asSignal, b);

fn min (a S, b S) S = BinaryOp.min _newBinaryOp(a, b);
fn min (a S, b AsConstantSignal) S = BinaryOp.min _newBinaryOp(a, b asSignal);
fn min (a AsConstantSignal, b S) S = BinaryOp.min _newBinaryOp(a asSignal, b);

fn max (a S, b S) S = BinaryOp.max _newBinaryOp(a, b);
fn max (a S, b AsConstantSignal) S = BinaryOp.max _newBinaryOp(a, b asSignal);
fn max (a AsConstantSignal, b S) S = BinaryOp.max _newBinaryOp(a asSignal, b);

fn pow (a S, b S) S = BinaryOp.pow _newBinaryOp(a, b);
fn pow (a S, b AsConstantSignal) S = BinaryOp.pow _newBinaryOp(a, b asSignal);
fn pow (a AsConstantSignal, b S) S = BinaryOp.pow _newBinaryOp(a asSignal, b);

fn atan2 (a S, b S) S = BinaryOp.atan2 _newBinaryOp(a, b);
fn atan2 (a S, b AsConstantSignal) S = BinaryOp.atan2 _newBinaryOp(a, b asSignal);
fn atan2 (a AsConstantSignal, b S) S = BinaryOp.atan2 _newBinaryOp(a asSignal, b);

fn hypot (a S, b S) S = BinaryOp.hypot _newBinaryOp(a, b);
fn hypot (a S, b AsConstantSignal) S = BinaryOp.hypot _newBinaryOp(a, b asSignal);
fn hypot (a AsConstantSignal, b S) S = BinaryOp.hypot _newBinaryOp(a asSignal, b);

fn copysign (a S, b S) S = BinaryOp.copysign _newBinaryOp(a, b);
fn copysign (a S, b AsConstantSignal) S = BinaryOp.copysign _newBinaryOp(a, b asSignal);
fn copysign (a AsConstantSignal, b S) S = BinaryOp.copysign _newBinaryOp(a asSignal, b);


-- functions for using binary operators in space separated pipeline syntax.

fn add (a S, b S) S = BinaryOp.add _newBinaryOp(a, b);
fn add (a S, b AsConstantSignal) S = BinaryOp.add _newBinaryOp(a, b asSignal);
fn add (a AsConstantSignal, b S) S = BinaryOp.add _newBinaryOp(a asSignal, b);

fn sub (a S, b S) S = BinaryOp.sub _newBinaryOp(a, b);
fn sub (a S, b AsConstantSignal) S = BinaryOp.sub _newBinaryOp(a, b asSignal);
fn sub (a AsConstantSignal, b S) S = BinaryOp.sub _newBinaryOp(a asSignal, b);

fn mul (a S, b S) S = BinaryOp.mul _newBinaryOp(a, b);
fn mul (a S, b AsConstantSignal) S = BinaryOp.mul _newBinaryOp(a, b asSignal);
fn mul (a AsConstantSignal, b S) S = BinaryOp.mul _newBinaryOp(a asSignal, b);

fn div (a S, b S) S = BinaryOp.div _newBinaryOp(a, b);
fn div (a S, b AsConstantSignal) S = BinaryOp.div _newBinaryOp(a, b asSignal);
fn div (a AsConstantSignal, b S) S = BinaryOp.div _newBinaryOp(a asSignal, b);

fn mod (a S, b S) S = BinaryOp.mod _newBinaryOp(a, b);
fn mod (a S, b AsConstantSignal) S = BinaryOp.mod _newBinaryOp(a, b asSignal);
fn mod (a AsConstantSignal, b S) S = BinaryOp.mod _newBinaryOp(a asSignal, b);

---------------------------------------------------------------------------
-- Signal Comparison Operators

fn < (a S, b S) S = CompareOp.lt _newCompareOp(a, b);
fn < (a S, b AsConstantSignal) S = CompareOp.lt _newCompareOp(a, b asSignal);
fn < (a AsConstantSignal, b S) S = CompareOp.lt _newCompareOp(a asSignal, b);

fn <= (a S, b S) S = CompareOp.le _newCompareOp(a, b);
fn <= (a S, b AsConstantSignal) S = CompareOp.le _newCompareOp(a, b asSignal);
fn <= (a AsConstantSignal, b S) S = CompareOp.le _newCompareOp(a asSignal, b);

fn == (a S, b S) S = CompareOp.eq _newCompareOp(a, b);
fn == (a S, b AsConstantSignal) S = CompareOp.eq _newCompareOp(a, b asSignal);
fn == (a AsConstantSignal, b S) S = CompareOp.eq _newCompareOp(a asSignal, b);

fn != (a S, b S) S = CompareOp.ne _newCompareOp(a, b);
fn != (a S, b AsConstantSignal) S = CompareOp.ne _newCompareOp(a, b asSignal);
fn != (a AsConstantSignal, b S) S = CompareOp.ne _newCompareOp(a asSignal, b);

fn >= (a S, b S) S = CompareOp.ge _newCompareOp(a, b);
fn >= (a S, b AsConstantSignal) S = CompareOp.ge _newCompareOp(a, b asSignal);
fn >= (a AsConstantSignal, b S) S = CompareOp.ge _newCompareOp(a asSignal, b);

fn > (a S, b S) S = CompareOp.gt _newCompareOp(a, b);
fn > (a S, b AsConstantSignal) S = CompareOp.gt _newCompareOp(a, b asSignal);
fn > (a AsConstantSignal, b S) S = CompareOp.gt _newCompareOp(a asSignal, b);

---------------------------------------------------------------------------
-- Signal Cast Operators

fn i32 (a AsSignal) S = CastOp.i32 _newCastOp(a asSignal);
fn i64 (a AsSignal) S = CastOp.i64 _newCastOp(a asSignal);
fn f32 (a AsSignal) S = CastOp.f32 _newCastOp(a asSignal);
fn f64 (a AsSignal) S = CastOp.f64 _newCastOp(a asSignal);

---------------------------------------------------------------------------
-- Delay Operators

fn init(d DelayVar, index Int, s AsSignal) DelayVar {
    SignalExprKind.delay(d, DelayOp.init(index)) _newSignalExpr([s asSignal]);
    d
}

fn at(d DelayVar, index Int) S = d read(index);

fn read(d DelayVar, index Int) S {
	-- d(0) is a delay of ZERO samples: the value written this sample. It
	-- resolves at graph-build time to the written signal itself, so the
	-- write must already have been built -- a d(0) read before the write
	-- is a zero-delay feedback loop, which has no defined value.
	if (index == 0) {
		match (`delayWrites[d.id]) {
			some(s): { return s; }
			none: { panic("d(0) read before the delay's write was built (zero-delay feedback?)"); }
		}
	}
	SignalExprKind.delay(d, DelayOp.read(index)) _newSignalExpr
}

fn vread(d DelayVar, index AsSignal, interp Interpolation = Interpolation.cubic) S {
    SignalExprKind.delay(d, DelayOp.vread(interp)) _newSignalExpr([index asSignal])
}

fn write(d DelayVar, s AsSignal) S {
	let sig = s asSignal;
	SignalExprKind.delay(d, DelayOp.write) _newSignalExpr([sig]);
	`delayWrites = `delayWrites put(d.id, sig);
	sig
}
fn write(s AsSignal, d DelayVar) S = d write(s);

fn call(d DelayVar, index Int = 0) S = d read(index);
fn call(d DelayVar, index S, interp Interpolation = Interpolation.cubic) S = d vread(index, interp);

fn <- (d DelayVar, s AsSignal) S = d write(s asSignal);
fn -> (s AsSignal, d DelayVar) S = s asSignal write(d);

---------------------------------------------------------------------------
--- Buffer operations

fn bufferVar() BufferVar {
	BufferVar { id: _nextBufferVarId() } _addToGraph
}

fn read(b BufferVar, index Int, chans Int = 1, startChan Int = 0) S {
	SignalExprKind.buffer(b, BufferOp.fixRead(index, chans, startChan)) _newSignalExpr
}

fn vread(b BufferVar, index AsSignal, interp Interpolation = Interpolation.cubic,
         chans Int = 1, startChan Int = 0) S {
	SignalExprKind.buffer(b, BufferOp.vread(interp, chans, startChan)) _newSignalExpr([index asSignal])
}

fn write(b BufferVar, value AsSignal, index AsSignal, chans Int = 0, startChan Int = 0) S {
	SignalExprKind.buffer(b, BufferOp.write(chans, startChan)) _newSignalExpr([value asSignal, index asSignal])
}
fn write(value AsSignal, b BufferVar, index AsSignal, chans Int = 0, startChan Int = 0) S {
	SignalExprKind.buffer(b, BufferOp.write(chans, startChan)) _newSignalExpr([value asSignal, index asSignal])
}

fn length(b BufferVar) S {
	SignalExprKind.buffer(b, BufferOp.length) _newSignalExpr
}

---------------------------------------------------------------------------
--- Sample bank operations

fn sampleBankVar() SampleBankVar {
	SampleBankVar { id: _nextSampleBankVarId() } _addToGraph
}

-- Resolve (pitch, velocity) to one sample of the bank. Evaluated once per
-- note-on inside a voicer (Rate.reset) -- the inputs are latched at that
-- moment, so they must be note params, controls, constants, or init-rate
-- values. In a non-voicer graph the lookup runs once at init.
fn lookup(b SampleBankVar, pitch AsSignal, velocity AsSignal) BankSample {
	BankSample { expr:
		SignalExprKind.bankLookup(b) _newSignalExpr([pitch asSignal, velocity asSignal]) }
}

fn read(h BankSample, index Int, chans Int = 1, startChan Int = 0) S {
	SignalExprKind.bank(BankOp.fixRead(index, chans, startChan)) _newSignalExpr([h.expr])
}

fn vread(h BankSample, index AsSignal, interp Interpolation = Interpolation.cubic,
         chans Int = 1, startChan Int = 0) S {
	SignalExprKind.bank(BankOp.vread(interp, chans, startChan)) _newSignalExpr([h.expr, index asSignal])
}

-- Metadata of the resolved sample (reset-rate signals): the MIDI note at
-- which it plays back unshifted, its source-file sample rate, and its
-- length in frames.
fn rootKey(h BankSample) S {
	SignalExprKind.bank(BankOp.rootKey) _newSignalExpr([h.expr])
}

fn sampleRate(h BankSample) S {
	SignalExprKind.bank(BankOp.sampleRate) _newSignalExpr([h.expr])
}

fn length(h BankSample) S {
	SignalExprKind.bank(BankOp.length) _newSignalExpr([h.expr])
}

-- The resolved sample's sustain loop: start/end in frames of the source
-- file, loopEnd exclusive and possibly fractional. When the sample has no
-- loop (hasLoop 0) the range is still usable -- 0 to the sample's length --
-- so loopPhasor needs no validity check.
fn loopStart(h BankSample) S {
	SignalExprKind.bank(BankOp.loopStart) _newSignalExpr([h.expr])
}

fn loopEnd(h BankSample) S {
	SignalExprKind.bank(BankOp.loopEnd) _newSignalExpr([h.expr])
}

fn hasLoop(h BankSample) S {
	SignalExprKind.bank(BankOp.hasLoop) _newSignalExpr([h.expr])
}

-- A per-voice looping playhead over the resolved sample: advances by `rate`
-- frames per engine sample (starting from 0 at note-on). While `sustain` is
-- positive and the sample has a loop, crossing the loop end wraps by
-- subtracting the exact loop length -- the fractional overshoot carries
-- through, so the loop period is sub-sample accurate at any rate; feed the
-- result to vread and the interpolator handles the fractional positions.
-- When `sustain` falls to 0 (or the sample has no loop) the playhead runs
-- on past the loop toward the end of the sample. Assumes |rate| is smaller
-- than the loop length (one subtraction per sample).
fn loopPhasor(h BankSample, rate AsSignal, sustain AsSignal) S {
	let d = delayVar();
	let pos = d read(1);
	let next = pos + (rate asSignal);
	let wrap = ((sustain asSignal) > 0.0) * (h hasLoop) * (next >= (h loopEnd));
	select2(wrap, next - ((h loopEnd) - (h loopStart)), next) -> d;
	pos
}

---------------------------------------------------------------------------
--- Vector operations

-- at(a, i, axis): elements of a along the axis at the (integer) index i,
-- wrapping. A scalar i picks one slice (the axis goes away); a row of k
-- indices gives the axis length k. On a row: channels i of a.
fn at(a S, i S, axis Int = -1) S = SignalExprKind.vecop(VecOp.at(axis)) _newSignalExpr([a asSignal, i asSignal]);
fn at(a S, i AsSignal, axis Int = -1) S = SignalExprKind.vecop(VecOp.at(axis)) _newSignalExpr([a asSignal, i asSignal]);
fn at(a AsSignal, i S, axis Int = -1) S = SignalExprKind.vecop(VecOp.at(axis)) _newSignalExpr([a asSignal, i asSignal]);

-- put(a, i, v, axis): a with the elements along the axis at the indices i
-- replaced by v (v shaped like at(a, i, axis); it cycles if smaller).
fn put(a AsSignal, i AsSignal, v AsSignal, axis Int = -1) S {
    SignalExprKind.vecop(VecOp.put(axis)) _newSignalExpr([a asSignal, i asSignal, v asSignal])
}

-- fn matmul(a S, b S) S        = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);
-- fn matmul(a S, b AsSignal) S = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);
-- fn matmul(a AsSignal, b S) S = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);

-- Axes: 0 is the outermost, negatives count from the last, so these name the
-- same axis at any rank. The shape operations default to the last axis.
const cols = -1;
const rows = -2;
const planes = -3;

fn take(a S, n Int, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.take(n pow2Compat, axis)) _newSignalExpr([a]);

fn drop(a S, n Int, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.drop(n, axis)) _newSignalExpr([a]);

fn stride(a S, n Int, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.stride(n pow2Compat, axis)) _newSignalExpr([a]);

fn stutter(a S, n Int, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.stutter(n pow2Compat, axis)) _newSignalExpr([a]);

fn ncyc(a S, n Int, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.ncyc(n pow2Compat, axis)) _newSignalExpr([a]);

-- `length` elements from `offset` along the axis, wrapping past its end.
fn slice(a S, offset Int, length Int, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.slice(offset, length, axis)) _newSignalExpr([a]);
-- A varying offset (an integer signal, as for `at`; the length stays fixed).
fn slice(a S, offset S, length Int, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.slicev(length, axis)) _newSignalExpr([a, offset]);

-- Output element k along the axis reads element k + amount, wrapping.
fn rotate(a AsSignal, amount AsSignal, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.rotate(axis)) _newSignalExpr([a asSignal, amount asSignal]);

fn reverse(a S, axis Int = -1) S = SignalExprKind.vecop(VecOp.reverse(axis)) _newSignalExpr([a]);

-- The axes reversed (a matrix transposed).
fn transpose(a S) S = SignalExprKind.vecop(VecOp.permute([Int]())) _newSignalExpr([a]);
-- The axes permuted: output axis k is input axis perm[k].
fn transpose(a S, perm [Int]) S = SignalExprKind.vecop(VecOp.permute(perm)) _newSignalExpr([a]);

-- The same channels in a new shape (one dim may be -1: whatever is left).
fn reshape(a S, dims [Int]) S = SignalExprKind.vecop(VecOp.reshape(dims)) _newSignalExpr([a]);
fn reshape(a S, sh Shape) S = a reshape(sh dimsInUse);

-- Concatenation along an axis; the other dims must agree.
fn join(a [S], axis Int = -1) S = SignalExprKind.vecop(VecOp.join(axis, false)) _newSignalExpr(a);
-- The signals (all of one shape) along a new axis, which is `axis` of the
-- result: [l, r] stack is a 2 x n matrix of n-channel l and r.
fn stack(a [S], axis Int = 0) S = SignalExprKind.vecop(VecOp.join(axis, true)) _newSignalExpr(a);

-- reduce(op): every channel to one. reduce(op, axis): along an axis, which
-- goes away (a voicer of stereo voices, voices x 2, sum(0) is stereo).
fn reduce(a S, op BinaryOp) S = SignalExprKind.vecop(VecOp.reduce(op)) _newSignalExpr([a]);
fn reduce(a S, op BinaryOp, axis Int) S =
	SignalExprKind.vecop(VecOp.reduceAxis(op, axis)) _newSignalExpr([a]);
-- Running op along an axis: element k combines elements 0..k.
fn scan(a S, op BinaryOp, axis Int = -1) S =
	SignalExprKind.vecop(VecOp.scan(op, axis)) _newSignalExpr([a]);

fn sum(a S) S = a reduce(BinaryOp.add);
fn sum(a S, axis Int) S = a reduce(BinaryOp.add, axis);

fn product(a S) S = a reduce(BinaryOp.mul);
fn product(a S, axis Int) S = a reduce(BinaryOp.mul, axis);

fn minOf(a S) S = a reduce(BinaryOp.min);
fn minOf(a S, axis Int) S = a reduce(BinaryOp.min, axis);

fn maxOf(a S) S = a reduce(BinaryOp.max);
fn maxOf(a S, axis Int) S = a reduce(BinaryOp.max, axis);


---------------------------------------------------------------------------
--- Making Graphs

type GraphFn = fn() S;
type GraphFn1 = fn(S) S;

fn _makeTopGraph(f GraphFn) SignalGraph {
	-- fresh graph state
	var `curGraphExprs [SignalExpr] = [];
	var `curGraphDelays [DelayVar] = [];
	var `curGraphBuffers [BufferVar] = [];
	var `curGraphSampleBanks [SampleBankVar] = [];
	var `exprIds Int = 0;
	var `delayVarIds Int = 0;
	var `bufferVarIds Int = 0;
	var `sampleBankVarIds Int = 0;
	var `delayWrites [Int: S] = [:];   -- delayVar id -> written signal (for d(0))

	let root S = f();

	let graph = SignalGraph {
	    exprs: `curGraphExprs,
		delays: `curGraphDelays,
		buffers: `curGraphBuffers,
		sampleBanks: `curGraphSampleBanks,
		root: root,
	};

	graph
}

fn _makeSubGraph(f GraphFn) SignalGraph {
	-- fresh graph state
	var `curGraphExprs [SignalExpr] = [];
	var `curGraphDelays [DelayVar] = [];
	var `curGraphBuffers [BufferVar] = [];
	var `curGraphSampleBanks [SampleBankVar] = [];

	let root S = f();

	let graph = SignalGraph {
	    exprs: `curGraphExprs,
		delays: `curGraphDelays,
		buffers: `curGraphBuffers,
		sampleBanks: `curGraphSampleBanks,
		root: root,
	};

	graph
}

fn _makeSubGraph1 (f GraphFn1, x S) SignalGraph {
	-- fresh graph state
	var `curGraphExprs [SignalExpr] = [];
	var `curGraphDelays [DelayVar] = [];
	var `curGraphBuffers [BufferVar] = [];
	var `curGraphSampleBanks [SampleBankVar] = [];

	let root = f(x);

	let graph = SignalGraph {
	    exprs: `curGraphExprs,
		delays: `curGraphDelays,
		buffers: `curGraphBuffers,
		sampleBanks: `curGraphSampleBanks,
		root: root,
	};

	graph
}

---------------------------------------------------------------------------
--- Control Flow Operators

fn if_(test S, thenFn GraphFn, elseFn GraphFn) S {
    let thenGraph SignalGraph = thenFn _makeSubGraph;
    let elseGraph SignalGraph = elseFn _makeSubGraph;
    SignalExprKind.if_(thenGraph, elseGraph) _newSignalExpr([test])
}

fn if_(test S, thenFn GraphFn) S {
    let thenGraph SignalGraph = thenFn _makeSubGraph;
    let elseGraph SignalGraph = fn() { 0 asSignal } _makeSubGraph;
    SignalExprKind.if_(thenGraph, elseGraph) _newSignalExpr([test])
}

fn if_(test Bool, thenFn GraphFn, elseFn GraphFn) S = test ? thenFn() : elseFn();
fn if_(test Bool, thenFn GraphFn) S = test ? thenFn() : 0 asSignal;

fn for_(varname String, count Int, bodyFn GraphFn1) S {
	let varexpr S = SignalExprKind.varexpr(varname) _newSignalExpr;
    let bodyGraph SignalGraph = bodyFn _makeSubGraph1(varexpr);
    -- The count flows as a signal input (a constant node), matching the C++
    -- ForLoopExpr(S count, ...); parseForExpr expects an (count) inputs list.
    SignalExprKind.for_(count, bodyGraph) _newSignalExpr([count asSignal])
}

fn switch(test, funs [GraphFn]) S {
    -- Build a subgraph per case. (An explicit loop, not `funs _makeSubGraph`:
    -- auto-mapping a function with a function-typed parameter has no codegen.)
    var graphs [SignalGraph] = [];
    for (f : funs) { graphs push!(f _makeSubGraph); }
    SignalExprKind.switch_(graphs) _newSignalExpr([test])
}

fn select(test S, exprs [AsSignal]) S {
    let ins [S] = [test] $ exprs asSignal;
    SignalExprKind.select _newSignalExpr(ins)
}

fn select2(test AsSignal, ifOne AsSignal, ifZero AsSignal) S {
    test asSignal select([ifZero asSignal, ifOne asSignal])
}

-- Build the in-memory SignalGraph for a synth function without compiling.
-- Used by the Tzopilotl-hosted compiler (synthc) and its tests.
fn makeGraph(synthFun GraphFn) SignalGraph = synthFun _makeTopGraph;

---------------------------------------------------------------------------
-- Category tags

-- Session-default tags from the TZPL_DEFAULT_TAGS environment variable
-- (comma-separated), appended to every synthdef compiled via defSynthX. Test harnesses set TZPL_DEFAULT_TAGS=test so everything they
-- compile is born tagged and the plugin browser can filter it out.
fn defaultSynthTags() [String] {
	match (getEnv("TZPL_DEFAULT_TAGS")) {
		Option.some(s): {
			var out [String] = [];
			for (t : s split(",")) {
				let tt = t trim;
				if (tt length > 0 && !(out contains(tt))) { out push!(tt); }
			}
			out
		}
		Option.none: [String]();
	}
}

-- Declared tags followed by session defaults, deduped in first-occurrence
-- order.
fn mergeSynthTags(declared [String], defaults [String]) [String] {
	var out [String] = [];
	for (t : declared) { if (t length > 0 && !(out contains(t))) { out push!(t); } }
	for (t : defaults) { if (t length > 0 && !(out contains(t))) { out push!(t); } }
	out
}

---------------------------------------------------------------------------
-- Playing synthdefs

import audio_engine as ae;

var _nextNodeID = 1000;

fn play(defName String, nodeID Int) Int {
	-- A defSynthX of this name still compiling in the background
	-- (its Future not awaited) is waited for here, so the node is created
	-- against a def that exists rather than failing with errNodeDefNotFound.
	-- Already-loaded or never-compiled defs pass straight through.
	synthDefReady(defName) await;
	ae.begin();
	ae.newNode(defName, nodeID);
	ae.connect(nodeID, 0, 0, 0);
	ae.sched(0);
	nodeID
}

fn play(defName String) Int {
	let nodeID = _nextNodeID;
	_nextNodeID = _nextNodeID + 1;
	play(defName, nodeID)
}

fn stop(nodeID Int) Int {
	ae.begin();
	ae.freeNode(nodeID);
	ae.sched(0);
	nodeID
}

import clock.*;

coro fn playFor(defName String, seconds Float) Float {
	let id = play(defName);
	"Now Playing: %^" fmt(defName) println;
	yield seconds;
	stop(id);
}

---------------------------------------------------------------------------
---------------------------------------------------------------------------












