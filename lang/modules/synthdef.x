-- synthdef.x
-- Script wrapper for the synthdef compiler FFI. Re-exports
-- `synthdef_ffi.*` (currently `compileSynthDef` and `compileSynthDefAndLoad`)
-- so users can write `import synthdef.*;` and get both the FFI functions
-- and the Tzopilotl-side graph construction API defined below.

export synthdef_ffi.*;

---------------------------------------------------------------------------
--- Rate

enum Rate {
	constant,
	init,
	reset,
	event,
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
--- Chans

type Chans = Int;

fn asChans(a Chans) Chans = a max(1) bitCeil;

fn broadcast(a Chans, b Chans) Chans = max(a, b);

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
	SignalExprKind.int([x toInt], ANY_NUM) _newSignalExpr
}
fn scalar(x Int) S {
	SignalExprKind.int([x], ANY_NUM) _newSignalExpr
}
fn scalar(x Fraction) S {
	let x = x toFloat;
	SignalExprKind.float([x], x numType) _newSignalExpr
}
fn scalar(x Float) S {
	SignalExprKind.float([x], x numType) _newSignalExpr
}

fn fill(chans Int, x Int) S {
	SignalExprKind.int(x repeat(chans asChans), ANY_NUM) _newSignalExpr
}
fn fill(chans Int, x Fraction) S {
	SignalExprKind.float(x toFloat repeat(chans asChans), ANY_NUM) _newSignalExpr
}
fn fill(chans Int, x Float) S {
	SignalExprKind.float(x repeat(chans asChans), x numType) _newSignalExpr
}

fn vec(v [Bool]) S {
	SignalExprKind.int(v toInt, ANY_NUM) _newSignalExpr
}
fn vec(v [Int]) S {
	SignalExprKind.int(v, ANY_NUM) _newSignalExpr
}
fn vec(v [Fraction]) S {
	let v = v toFloat;
	SignalExprKind.float(v, ANY_FLOAT) _newSignalExpr
}
fn vec(v [Float]) S {
	SignalExprKind.float(v, ANY_FLOAT) _newSignalExpr
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

enum VecOp {
	at,
	put,
	take Int,
	drop Int,
	stride Int,
	stutter Int,
	ncyc Int,
	rotate,
	reverse,
	transpose Int,
	join,
	reduce(BinaryOp, Chans),
	sum Int,
	prod Int,
	minOf Int,
	maxOf Int,
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
	chans Chans,
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

enum SignalExprKind {
	sampleRate,
	sampleDur,

	int ([Int], NumType),
	float ([Float], NumType),

	unop UnaryOp,
	binop BinaryOp,
	compareop CompareOp,
	castop CastOp,
	random (RandOp, Rate, Chans),
	vecop VecOp,

	inlet(NumType, Chans, String),
	outlet(String),

	control(ControlSpec, Chans, String),
	noteParam(ControlSpec, Chans, String),

	delay(DelayVar, DelayOp),
	buffer(BufferVar, BufferOp),

	if_(SignalGraph, SignalGraph),
	for_(Int, SignalGraph),
	switch_([SignalGraph]),
	select,
	select2,
	varexpr String,
	voicer(Int, SignalGraph),
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

fn inlet(typ NumType, chans Chans = 1, name String = "in") S {
    SignalExprKind.inlet(typ, chans asChans, name) _newSignalExpr
}

fn outlet(a S, name String = "out") S {
    SignalExprKind.outlet(name) _newSignalExpr([a])
}

fn control(name String, spec ControlSpec, chans Chans = 1) S {
    SignalExprKind.control(spec, chans asChans, name) _newSignalExpr
}

fn noteParam(name String, spec ControlSpec, chans Chans = 1) S {
    SignalExprKind.noteParam(spec, chans asChans, name) _newSignalExpr
}

fn gate() S {
    noteParam("gate", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear })
}

fn voicer(maxVoices Int, voiceFn GraphFn) S {
    let bodyGraph = voiceFn _makeSubGraph;
    SignalExprKind.voicer(maxVoices, bodyGraph) _newSignalExpr
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

fn frand(lo Float, hi Float, chans Chans = 1, rate Rate = Rate.audio) S {
    SignalExprKind.random(RandOp.frand(lo, hi), rate, chans asChans) _newSignalExpr
}
fn irand(lo Int, hi Int, chans Chans = 1, rate Rate = Rate.audio) S {
    SignalExprKind.random(RandOp.irand(lo, hi), rate, chans asChans) _newSignalExpr
}
fn urand(chans Chans = 1, rate Rate = Rate.audio) S {
    SignalExprKind.random(RandOp.unipolar, rate, chans asChans) _newSignalExpr
}
fn birand(chans Chans = 1, rate Rate = Rate.audio) S {
    SignalExprKind.random(RandOp.bipolar, rate, chans asChans) _newSignalExpr
}
fn rand64(chans Chans = 1, rate Rate = Rate.audio) S {
    SignalExprKind.random(RandOp.bits, rate, chans asChans) _newSignalExpr
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

fn at(d DelayVar, index Int) S {
    SignalExprKind.delay(d, DelayOp.read(index)) _newSignalExpr
}

fn read(d DelayVar, index Int) S {
    SignalExprKind.delay(d, DelayOp.read(index)) _newSignalExpr
}

fn vread(d DelayVar, index AsSignal, interp Interpolation = Interpolation.cubic) S {
    SignalExprKind.delay(d, DelayOp.vread(interp)) _newSignalExpr([index asSignal])
}

fn write(d DelayVar, s AsSignal) S {
    SignalExprKind.delay(d, DelayOp.write) _newSignalExpr([s asSignal]);
	s
}
fn write(s AsSignal, d DelayVar) S {
    SignalExprKind.delay(d, DelayOp.write) _newSignalExpr([s asSignal]);
	s
}

fn call(d DelayVar, index Int = 0) S = d read(index);
fn call(d DelayVar, index S, interp Interpolation = Interpolation.cubic) S = d vread(index);

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
--- Vector operations

fn at(a S, i S) S        = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);
fn at(a S, i AsSignal) S = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);
fn at(a AsSignal, i S) S = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);

fn put(a AsSignal, i AsSignal, v AsSignal) S {
    SignalExprKind.vecop(VecOp.put) _newSignalExpr([a asSignal, i asSignal, v asSignal])
}

-- fn matmul(a S, b S) S        = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);
-- fn matmul(a S, b AsSignal) S = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);
-- fn matmul(a AsSignal, b S) S = SignalExprKind.vecop(VecOp.at) _newSignalExpr([a asSignal, i asSignal]);

fn take(a S, n Chans) S = SignalExprKind.vecop(VecOp.take(n asChans)) _newSignalExpr([a]);

fn drop(a S, n Chans) S = SignalExprKind.vecop(VecOp.drop(n)) _newSignalExpr([a]);

fn stride(a S, n Chans) S = SignalExprKind.vecop(VecOp.stride(n asChans)) _newSignalExpr([a]);

fn stutter(a S, n Chans) S = SignalExprKind.vecop(VecOp.stutter(n asChans)) _newSignalExpr([a]);

fn ncyc(a S, n Int) S = SignalExprKind.vecop(VecOp.ncyc(n asChans)) _newSignalExpr([a]);

fn rotate(a AsSignal, b AsSignal) S = SignalExprKind.vecop(VecOp.rotate) _newSignalExpr([a asSignal, b asSignal]);

fn reverse(a S) S = SignalExprKind.vecop(VecOp.reverse) _newSignalExpr([a]);

fn transpose(a S, n Chans) S = SignalExprKind.vecop(VecOp.transpose(n asChans)) _newSignalExpr([a]);

fn join(a [S]) S = SignalExprKind.vecop(VecOp.join) _newSignalExpr(a);

fn reduce(a S, op BinaryOp, chans Chans = 1) S {
    SignalExprKind.vecop(VecOp.reduce(op, chans asChans)) _newSignalExpr([a])
}
fn sum(a S, chans Chans = 1) S = a reduce(BinaryOp.add, chans asChans);

fn product(a S, chans Chans = 1) S = a reduce(BinaryOp.mul, chans asChans);

fn minOf(a S, chans Chans = 1) S = a reduce(BinaryOp.min, chans asChans);

fn maxOf(a S, chans Chans = 1) S = a reduce(BinaryOp.max, chans asChans);


---------------------------------------------------------------------------
--- Making Graphs

type GraphFn = fn() S;
type GraphFn1 = fn(S) S;

fn _makeTopGraph(f GraphFn) SignalGraph {
	-- fresh graph state
	var `curGraphExprs [SignalExpr] = [];
	var `curGraphDelays [DelayVar] = [];
	var `curGraphBuffers [BufferVar] = [];
	var `exprIds Int = 0;
	var `delayVarIds Int = 0;
	var `bufferVarIds Int = 0;

	let root S = f();

	let graph = SignalGraph {
	    exprs: `curGraphExprs,
		delays: `curGraphDelays,
		buffers: `curGraphBuffers,
		root: root,
	};

	graph
}

fn _makeSubGraph(f GraphFn) SignalGraph {
	-- fresh graph state
	var `curGraphExprs [SignalExpr] = [];
	var `curGraphDelays [DelayVar] = [];
	var `curGraphBuffers [BufferVar] = [];

	let root S = f();

	let graph = SignalGraph {
	    exprs: `curGraphExprs,
		delays: `curGraphDelays,
		buffers: `curGraphBuffers,
		root: root,
	};

	graph
}

fn _makeSubGraph1 (f GraphFn1, x S) SignalGraph {
	-- fresh graph state
	var `curGraphExprs [SignalExpr] = [];
	var `curGraphDelays [DelayVar] = [];
	var `curGraphBuffers [BufferVar] = [];

	let root = f(x);

	let graph = SignalGraph {
	    exprs: `curGraphExprs,
		delays: `curGraphDelays,
		buffers: `curGraphBuffers,
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
    let graphs [SignalGraph] = funs _makeSubGraph;
    SignalExprKind.switch_(graphs) _newSignalExpr([test])
}

fn select(test S, exprs [AsSignal]) S {
    let ins [S] = [test] $ exprs asSignal;
    SignalExprKind.select _newSignalExpr(ins)
}

fn select2(test AsSignal, ifOne AsSignal, ifZero AsSignal) S {
    test asSignal select([ifZero asSignal, ifOne asSignal])
}

---------------------------------------------------------------------------
-- To S-Expressions

fn parens(s String)   String = "(%^)" fmt(s);
fn brackets(s String) String = "[%^]" fmt(s);
fn braces(s String)   String = "{%^}" fmt(s);
fn quotes(s String)   String = "\"%^\"" fmt(s);

fn indent(s String, n Int = 1) String = n <= 0 ? s : "    "  $ indent(s, n-1);

fn separatedString(strings [String], separator String = " ") String  = strings fold1(fn(z, s) { z $ separator $ s });
fn separatedString<T>(values [T], separator String = " ") String = values @ toString separatedString(separator);


fn inputsToLisp(o S) String = o.ins.id separatedString parens;

fn idsToLisp(o [S]) String = o.id separatedString parens;
fn numbersToLisp(o [Int]) String = o separatedString parens;

fn toLisp(o ControlSpec) String {
    "(ControlSpec %^ %^ %^ %^)" fmt(o.lo, o.hi, o.init, o.warp ordinal)
}

fn numTypeInt(op CastOp) Int {
	match (op) {
		i32 : 1;
		i64 : 2;
		f32 : 4;
		f64 : 8;
	}
}

fn toLisp(graph SignalGraph, indentLevel Int) String {
	let sexprLines = graph.exprs toLisp(indentLevel);

	-- Join with newlines and add indentation
	let text = sexprLines indent(indentLevel + 1) separatedString("\n");

	"\n" $ "(Graph %^ (\n%^))" fmt(graph.root.id, text) indent(indentLevel)
}

fn toLisp(o S, indentLevel Int) String {
    match (o.kind) {
        sampleRate : "(%^ SampleRate)" fmt(o.id);
        sampleDur : "(%^ SampleDur)" fmt(o.id);
        int(a, typ) : "(%^ Constant %^ %^ %^)" fmt(o.id, a length, typ.0, a separatedString parens);
        float(a, typ) : "(%^ Constant %^ %^ %^)" fmt(o.id, a length, typ.0, a separatedString parens);
        unop(op) : "(%^ UnaryOp %^ %^)" fmt(o.id, op tag toString, o inputsToLisp);
        binop(op) : "(%^ BinaryOp %^ %^)" fmt(o.id, op tag toString, o inputsToLisp);
        compareop(op) : "(%^ CompareOp %^ %^)" fmt(o.id, op tag toString, o inputsToLisp);
        castop(op) : "(%^ CastOp %^ %^)" fmt(o.id, op numTypeInt, o inputsToLisp);
        random(op, rate, chans) : match (op) {
            frand(lo, hi) : "(%^ FRand %^ %^ %^ %^)" fmt(o.id, rate ordinal, chans asChans, lo, hi);
            irand(lo, hi) : "(%^ IRand %^ %^ %^ %^)" fmt(o.id, rate ordinal, chans asChans, lo, hi);
            unipolar : "(%^ URand %^ %^)" fmt(o.id, rate ordinal, chans asChans);
            bipolar : "(%^ BiRand %^ %^)" fmt(o.id, rate ordinal, chans asChans);
            bits : "(%^ Rand64 %^ %^)" fmt(o.id, rate ordinal, chans asChans);
        }
        vecop(op) : match (op) {
            at : "(%^ VecAt %^)" fmt(o.id, o inputsToLisp);
           	put : "(%^ VecPut %^)" fmt(o.id, o inputsToLisp);
           	take(n) : "(%^ VecTake %^ %^)" fmt(o.id, n, o inputsToLisp);
           	drop(n) : "(%^ VecDrop %^ %^)" fmt(o.id, n, o inputsToLisp);
           	stride(n) : "(%^ VecStride %^ %^)" fmt(o.id, n, o inputsToLisp);
           	stutter(n) : "(%^ VecStutter %^ %^)" fmt(o.id, n, o inputsToLisp);
           	ncyc(n) : "(%^ VecNCyc %^ %^)" fmt(o.id, n, o inputsToLisp);
           	rotate : "(%^ VecRotate %^)" fmt(o.id, o inputsToLisp);
           	reverse : "(%^ VecReverse %^)" fmt(o.id, o inputsToLisp);
           	transpose(n) : "(%^ VecTranspose %^ %^)" fmt(o.id, n, o inputsToLisp);
           	join : "(%^ VecJoin %^)" fmt(o.id, o inputsToLisp);
           	reduce(op, chans) : "(%^ VecReduce %^ %^ %^)"
                fmt(o.id, op tag toString, chans asChans, o inputsToLisp);
           	sum(chans) : "(%^ VecSum %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);
           	prod(chans) : "(%^ VecProd %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);
           	minOf(chans) : "(%^ VecMin %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);
           	maxOf(chans) : "(%^ VecMax %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);

        }
        inlet(typ, chans, name) : "(%^ Inlet \"%^\" %^ %^)" fmt(o.id, name, chans asChans, typ.0);
        outlet(name) : "(%^ Outlet \"%^\" %^)" fmt(o.id, name, o.ins.id separatedString);

        control(spec, chans, name) :
            "(%^ Control \"%^\" %^ %^)" fmt(o.id, name, chans asChans, spec toLisp);

        noteParam(spec, chans, name) :
            "(%^ NoteParam \"%^\" %^ %^)" fmt(o.id, name, chans asChans, spec toLisp);

        delay(delayVar, op) : match (op) {
            maxDelayTime : "(%^ MaxDelay %^ %^)" fmt(o.id, delayVar.id, o inputsToLisp);
            init(offset) : "(%^ DelayInit %^ %^ %^)" fmt(o.id, delayVar.id, offset, o inputsToLisp);
            read(offset) : "(%^ DelayFixRead %^ %^)" fmt(o.id, delayVar.id, offset);
            vread(interpolation) : "(%^ DelayVarRead %^ %^ %^)" fmt(o.id, delayVar.id, interpolation toLispString, o inputsToLisp);
            write :  "(%^ DelayWrite %^ %^)" fmt(o.id, delayVar.id, o inputsToLisp);
        }

        buffer(bufferVar, op) : match (op) {
            fixRead(index, chans, startChan) :
                "(%^ BufFixRead %^ %^ %^ %^)" fmt(o.id, bufferVar.id, index, chans, startChan);
            vread(interpolation, chans, startChan) :
                "(%^ BufVarRead %^ %^ %^ %^ %^)" fmt(o.id, bufferVar.id, interpolation toLispString, chans, startChan, o inputsToLisp);
            write(chans, startChan) :
                "(%^ BufWrite %^ %^ %^ %^)" fmt(o.id, bufferVar.id, chans, startChan, o inputsToLisp);
            length :
                "(%^ BufLength %^)" fmt(o.id, bufferVar.id);
        }

        if_(thenGraph, elseGraph) : {
            "(%^ IfExpr %^ %^ %^)" fmt(o.id, o inputsToLisp, thenGraph toLisp(indentLevel + 1), elseGraph toLisp(indentLevel + 1))
		}
        for_(count, bodyGraph) :
            "(%^ ForExpr %^ %^)" fmt(o.id, o inputsToLisp, bodyGraph toLisp(indentLevel + 1));

        switch_(cases) :
            "(%^ SwitchExpr %^ %^)" fmt(o.id, o inputsToLisp, cases toLisp(indentLevel + 1) separatedString(" "));

        select : "(%^ SelectExpr %^)" fmt(o.id, o inputsToLisp);
        select2 : "(%^ SelectExpr %^)" fmt(o.id, o inputsToLisp);
		varexpr(name) : "(%^ VarExpr \"%^\")" fmt(o.id, name);

        voicer(maxVoices, bodyGraph) : {
            "(%^ Voicer %^ %^)" fmt(o.id, maxVoices, bodyGraph toLisp(indentLevel + 1))
		}

		spectralFrame(fftSize) : "(%^ SpectralFrameInput %^)" fmt(o.id, fftSize);

		spectralChain(fftSize, hopSize, bodyGraph) : {
			"(%^ SpectralChainExpr %^ %^ %^ %^)" fmt(o.id, o inputsToLisp, fftSize, hopSize, bodyGraph toLisp(indentLevel + 1))
		}

		debug(label, period, consecutive) :
			"(%^ DebugExpr \"%^\" %^ %^ %^)" fmt(o.id, label, period, consecutive, o inputsToLisp);
    }
}


-- Build the in-memory SignalGraph for a synth function without compiling.
-- Used by the Tzopilotl-hosted compiler (synthc) and its tests.
fn makeGraph(synthFun GraphFn) SignalGraph = synthFun _makeTopGraph;

-- The (Synth <name> <graph>) s-expression for a graph, as sent to the C++
-- compiler by defSynth.
fn toSynthSexpr(graph SignalGraph, synthName String) String =
	"(Synth %^ %^)" fmt(synthName, graph toLisp(0));

fn defSynth(synthFun GraphFn, synthName String) String {

	let graph SignalGraph = synthFun _makeTopGraph;

	let sexprString = graph toSynthSexpr(synthName);

	let err = sexprString compileSynthDefAndLoad;
	if (err length > 0) {
		println("ERROR compiling " $ synthName $ ": " $ err);
	} else {
		println(synthName $ " compiled successfully.");
	}

	sexprString
}

---------------------------------------------------------------------------
-- Playing synthdefs

import audio_engine as ae;

var _nextNodeID = 1000;

fn play(defName String, nodeID Int) Int {
	ae.begin(0);
	ae.newNode(defName, nodeID);
	ae.connect(nodeID, 0, 0, 0);
	ae.sched();
	nodeID
}

fn play(defName String) Int {
	let nodeID = _nextNodeID;
	_nextNodeID = _nextNodeID + 1;
	play(defName, nodeID)
}

fn stop(nodeID Int) Int {
	ae.begin(0);
	ae.freeNode(nodeID);
	ae.sched();
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












