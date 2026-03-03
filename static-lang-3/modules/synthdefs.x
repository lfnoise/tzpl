
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
fn & (a NumType, b NumType) NumType = NumType(a.0 & b.0)
fn << (a NumType, b Int) NumType = NumType(a.0 << b)
fn >> (a NumType, b Int) NumType = NumType(a.0 >> b)

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
	if (a to64bits || b is64bits) {
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

fn numType(o Bool) NumType = INT32
fn numType(o Int)  NumType = ANY_NUM
fn numType(o Float) NumType = o isIntegerValued ? ANY_NUM : ANY_FLOAT

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
-- SynthContext

---------------------------------------------------------------------------
-- SignalGraph

struct SignalGraph {
    exprs [SignalExpr],
	delays [DelayVar],
	root SignalExpr,
}
---------------------------------------------------------------------------
-- Signal expressions

type ID = Int;

struct SignalExpr {
	id Ref<ID>,
	ins [SignalExpr],
	kind SignalExprKind,
}

var curGraphExprs Array<SignalExpr> = [];
var curGraphDelays Array<DelayVar> = [];
var exprIds = 0;
var delayVarIds = 0;

fn nextExprId() ID {
	let out = exprIds;
	exprIds = exprIds + 1;
	out
}

fn nextDelayVarId() ID {
	let out = delayVarIds;
	delayVarIds = delayVarIds + 1;
	out
}

fn addToGraph(expr SignalExpr) SignalExpr {
	expr.id <- nextExprId();
	curGraphExprs = curGraphExprs push(expr);
	expr
}

fn addToGraph(expr DelayVar) DelayVar {
	expr.id <- delayVarIds();
	curGraphExprs = curGraphDelays push(expr);
	expr
}

fn newSignalExpr(kind SignalExprKind, ins [SignalExpr]) SignalExpr {
	SignalExpr {
		id: &0,
		ins: ins,
		kind: kind,
	} addToGraph
}

fn newSignalExpr(kind SignalExprKind) SignalExpr {
	kind newSignalExpr([])
}

---------------------------------------------------------------------------
-- Constant scalars and matrices


fn scalar(x Bool) SignalExpr {
	SignalExprKind.int([x ? 1 : 0], ANY_NUM) newSignalExpr
}
fn scalar(x Int) SignalExpr {
	SignalExprKind.int([x], ANY_NUM) newSignalExpr
}
fn scalar(x Float) SignalExpr {
	SignalExprKind.float([x], x numType) newSignalExpr
}

fn fill(chans Int, x Int) SignalExpr {
	SignalExprKind.int(x repeat(chans asChans), ANY_NUM) newSignalExpr
}
fn fill(chans Int, x Float) SignalExpr {
	SignalExprKind.float(x repeat(chans asChans), x numType) newSignalExpr
}

fn vec(v [Int]) SignalExpr {
	SignalExprKind.int(x, ANY_NUM) newSignalExpr
}
fn vec(v [Float]) SignalExpr {
	SignalExprKind.float(x, ANY_FLOAT) newSignalExpr
}

fn asSignal(x SignalExpr) SignalExpr = x;
fn asSignal(x Int) SignalExpr = x scalar;
fn asSignal(x Float) SignalExpr = x scalar;
fn asSignal(x [Int]) SignalExpr = x vec;
fn asSignal(x [Float]) SignalExpr = x vec;


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

	bitAnd, bitOr, bitXOr,
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
	permute,
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
	maxDelay Optional<SignalExpr>,
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

	delay(DelayVar, DelayOp),

	if_(SignalGraph, SignalGraph),
	for_(Int, SignalGraph),
	switch_([SignalGraph]),
	select,
	select2,
}

struct SignalExpr {
	ins [SignalExpr],
	kind SignalExprKind,
}

fn fs() SignalExpr {
    SignalExprKind.sampleRate newSignalExpr
}

fn T() SignalExpr {
    SignalExprKind.sampleDur newSignalExpr
}

fn inlet(typ NumType, chans Chans = 1, name String = "in") SignalExpr {
    SignalExprKind.inlet(typ, chans asChans, name) newSignalExpr
}

fn outlet(a SignalExpr, name String = "out") SignalExpr {
    SignalExprKind.outlet(name) newSignalExpr([a])
}

fn control(spec ControlSpec, chans Chans = 1, name String) SignalExpr {
    SignalExprKind.control(spec, chans asChans, name) newSignalExpr
}

fn frand(lo Float, hi Float, chans Chans = 1, rate Rate = Rate.audio) SignalExpr {
    SignalExprKind.random(RandOp.frand(lo, hi), rate, chans asChans) newSignalExpr
}
fn irand(lo Int, hi Int, chans Chans = 1, rate Rate = Rate.audio) SignalExpr {
    SignalExprKind.random(RandOp.irand(lo, hi), rate, chans asChans) newSignalExpr
}
fn urand(chans Chans = 1, rate Rate = Rate.audio) SignalExpr {
    SignalExprKind.random(RandOp.unipolar, rate, chans asChans) newSignalExpr
}
fn brand(chans Chans = 1, rate Rate = Rate.audio) SignalExpr {
    SignalExprKind.random(RandOp.bipolar, rate, chans asChans) newSignalExpr
}
fn rand64(chans Chans = 1, rate Rate = Rate.audio) SignalExpr {
    SignalExprKind.random(RandOp.bits, rate, chans asChans) newSignalExpr
}

fn newUnaryOp(op UnaryOp, a) SignalExpr {
    SignalExprKind.unop(op) newSignalExpr([a asSignal])
}

fn newBinaryOp(op BinaryOp, a, b) SignalExpr {
    SignalExprKind.binop(op) newSignalExpr([a asSignal, b asSignal])
}

fn newCompareOp(op CompareOp, a, b) SignalExpr {
    SignalExprKind.compareop(op) newSignalExpr([a asSignal, b asSignal])
}

fn newCastOp(op CastOp, a) SignalExpr {
    SignalExprKind.castop(op) newSignalExpr([a asSignal])
}

---------------------------------------------------------------------------
-- Unary Signal Operators

fn - (a SignalExpr) SignalExpr = UnaryOp.neg newUnaryOp(a);
fn ! (a SignalExpr) SignalExpr = UnaryOp.not newUnaryOp(a);
fn ~ (a SignalExpr) SignalExpr = UnaryOp.bitNot newUnaryOp(a);

fn abs (a SignalExpr) SignalExpr = UnaryOp.abs newUnaryOp(a);

fn sign (a SignalExpr) SignalExpr = UnaryOp.sign newUnaryOp(a);
fn floor (a SignalExpr) SignalExpr = UnaryOp.floor newUnaryOp(a);
fn ceil (a SignalExpr) SignalExpr = UnaryOp.ceil newUnaryOp(a);
fn trunc (a SignalExpr) SignalExpr = UnaryOp.trunc newUnaryOp(a);
fn round (a SignalExpr) SignalExpr = UnaryOp.round newUnaryOp(a);

fn sqrt (a SignalExpr) SignalExpr = UnaryOp.sqrt newUnaryOp(a);
fn cbrt (a SignalExpr) SignalExpr = UnaryOp.cbrt newUnaryOp(a);

fn log (a SignalExpr) SignalExpr = UnaryOp.log newUnaryOp(a);
fn log2 (a SignalExpr) SignalExpr = UnaryOp.log2 newUnaryOp(a);
fn log10 (a SignalExpr) SignalExpr = UnaryOp.log10 newUnaryOp(a);
fn log1p (a SignalExpr) SignalExpr = UnaryOp.log1p newUnaryOp(a);

fn exp (a SignalExpr) SignalExpr = UnaryOp.exp newUnaryOp(a);
fn exp2 (a SignalExpr) SignalExpr = UnaryOp.exp2 newUnaryOp(a);
fn exp10 (a SignalExpr) SignalExpr = UnaryOp.exp10 newUnaryOp(a);
fn expm1 (a SignalExpr) SignalExpr = UnaryOp.expm1 newUnaryOp(a);

fn sin (a SignalExpr) SignalExpr = UnaryOp.sin newUnaryOp(a);
fn cos (a SignalExpr) SignalExpr = UnaryOp.cos newUnaryOp(a);
fn tan (a SignalExpr) SignalExpr = UnaryOp.tan newUnaryOp(a);
fn asin (a SignalExpr) SignalExpr = UnaryOp.asin newUnaryOp(a);
fn acos (a SignalExpr) SignalExpr = UnaryOp.acos newUnaryOp(a);
fn atan (a SignalExpr) SignalExpr = UnaryOp.atan newUnaryOp(a);

fn sinh (a SignalExpr) SignalExpr = UnaryOp.sinh newUnaryOp(a);
fn cosh (a SignalExpr) SignalExpr = UnaryOp.cosh newUnaryOp(a);
fn tanh (a SignalExpr) SignalExpr = UnaryOp.tanh newUnaryOp(a);
fn asinh (a SignalExpr) SignalExpr = UnaryOp.asinh newUnaryOp(a);
fn acosh (a SignalExpr) SignalExpr = UnaryOp.acosh newUnaryOp(a);
fn atanh (a SignalExpr) SignalExpr = UnaryOp.atanh newUnaryOp(a);

fn sinpi (a SignalExpr) SignalExpr = UnaryOp.sinpi newUnaryOp(a);
fn cospi (a SignalExpr) SignalExpr = UnaryOp.cospi newUnaryOp(a);
fn tanpi (a SignalExpr) SignalExpr = UnaryOp.tanpi newUnaryOp(a);

fn lgamma (a SignalExpr) SignalExpr = UnaryOp.lgamma newUnaryOp(a);
fn tgamma (a SignalExpr) SignalExpr = UnaryOp.tgamma newUnaryOp(a);

fn erf (a SignalExpr) SignalExpr = UnaryOp.erf newUnaryOp(a);
fn erfc (a SignalExpr) SignalExpr = UnaryOp.erfc newUnaryOp(a);

fn clz (a SignalExpr) SignalExpr = UnaryOp.clz newUnaryOp(a);
fn ctz (a SignalExpr) SignalExpr = UnaryOp.ctz newUnaryOp(a);
fn clo (a SignalExpr) SignalExpr = UnaryOp.clo newUnaryOp(a);
fn cto (a SignalExpr) SignalExpr = UnaryOp.cto newUnaryOp(a);
fn rotr (a SignalExpr) SignalExpr = UnaryOp.rotr newUnaryOp(a);
fn rtol (a SignalExpr) SignalExpr = UnaryOp.rotl newUnaryOp(a);
fn bitCeil (a SignalExpr) SignalExpr = UnaryOp.bitCeil newUnaryOp(a);
fn bitFloor (a SignalExpr) SignalExpr = UnaryOp.bitFloor newUnaryOp(a);
fn bitWidth (a SignalExpr) SignalExpr = UnaryOp.bitWidth newUnaryOp(a);
fn popCount (a SignalExpr) SignalExpr = UnaryOp.popCount newUnaryOp(a);
fn hasSingleBit (a SignalExpr) SignalExpr = UnaryOp.hasSingleBit newUnaryOp(a);

---------------------------------------------------------------------------
-- Binary Signal Operators

fn + (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.add newBinaryOp(a, b);
fn + (a SignalExpr, b) SignalExpr = BinaryOp.add newBinaryOp(a, b);
fn + (a, b SignalExpr) SignalExpr = BinaryOp.add newBinaryOp(a, b);

fn - (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.sub newBinaryOp(a, b);
fn - (a SignalExpr, b) SignalExpr = BinaryOp.sub newBinaryOp(a, b);
fn - (a, b SignalExpr) SignalExpr = BinaryOp.sub newBinaryOp(a, b);

fn * (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.mul newBinaryOp(a, b);
fn * (a SignalExpr, b) SignalExpr = BinaryOp.mul newBinaryOp(a, b);
fn * (a, b SignalExpr) SignalExpr = BinaryOp.mul newBinaryOp(a, b);

fn / (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.div newBinaryOp(a, b);
fn / (a SignalExpr, b) SignalExpr = BinaryOp.div newBinaryOp(a, b);
fn / (a, b SignalExpr) SignalExpr = BinaryOp.div newBinaryOp(a, b);

fn // (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.idiv newBinaryOp(a, b);
fn // (a SignalExpr, b) SignalExpr = BinaryOp.idiv newBinaryOp(a, b);
fn // (a, b SignalExpr) SignalExpr = BinaryOp.idiv newBinaryOp(a, b);

fn % (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.mod newBinaryOp(a, b);
fn % (a SignalExpr, b) SignalExpr = BinaryOp.mod newBinaryOp(a, b);
fn % (a, b SignalExpr) SignalExpr = BinaryOp.mod newBinaryOp(a, b);

fn & (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.bitAnd newBinaryOp(a, b);
fn & (a SignalExpr, b) SignalExpr = BinaryOp.bitAnd newBinaryOp(a, b);
fn & (a, b SignalExpr) SignalExpr = BinaryOp.bitAnd newBinaryOp(a, b);

fn | (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.bitOr newBinaryOp(a, b);
fn | (a SignalExpr, b) SignalExpr = BinaryOp.bitOr newBinaryOp(a, b);
fn | (a, b SignalExpr) SignalExpr = BinaryOp.bitOr newBinaryOp(a, b);

fn ^ (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.bitXor newBinaryOp(a, b);
fn ^ (a SignalExpr, b) SignalExpr = BinaryOp.bitXor newBinaryOp(a, b);
fn ^ (a, b SignalExpr) SignalExpr = BinaryOp.bitXor newBinaryOp(a, b);

fn << (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.shiftLeft newBinaryOp(a, b);
fn << (a SignalExpr, b) SignalExpr = BinaryOp.shiftLeft newBinaryOp(a, b);
fn << (a, b SignalExpr) SignalExpr = BinaryOp.shiftLeft newBinaryOp(a, b);

fn >> (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.shiftRight newBinaryOp(a, b);
fn >> (a SignalExpr, b) SignalExpr = BinaryOp.shiftRight newBinaryOp(a, b);
fn >> (a, b SignalExpr) SignalExpr = BinaryOp.shiftRight newBinaryOp(a, b);

fn >>> (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.unsignedShiftRight newBinaryOp(a, b);
fn >>> (a SignalExpr, b) SignalExpr = BinaryOp.unsignedShiftRight newBinaryOp(a, b);
fn >>> (a, b SignalExpr) SignalExpr = BinaryOp.unsignedShiftRight newBinaryOp(a, b);

fn min (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.min newBinaryOp(a, b);
fn min (a SignalExpr, b) SignalExpr = BinaryOp.min newBinaryOp(a, b);
fn min (a, b SignalExpr) SignalExpr = BinaryOp.min newBinaryOp(a, b);

fn max (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.max newBinaryOp(a, b);
fn max (a SignalExpr, b) SignalExpr = BinaryOp.max newBinaryOp(a, b);
fn max (a, b SignalExpr) SignalExpr = BinaryOp.max newBinaryOp(a, b);

fn pow (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.pow newBinaryOp(a, b);
fn pow (a SignalExpr, b) SignalExpr = BinaryOp.pow newBinaryOp(a, b);
fn pow (a, b SignalExpr) SignalExpr = BinaryOp.pow newBinaryOp(a, b);

fn atan2 (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.atan2 newBinaryOp(a, b);
fn atan2 (a SignalExpr, b) SignalExpr = BinaryOp.atan2 newBinaryOp(a, b);
fn atan2 (a, b SignalExpr) SignalExpr = BinaryOp.atan2 newBinaryOp(a, b);

fn hypot (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.hypot newBinaryOp(a, b);
fn hypot (a SignalExpr, b) SignalExpr = BinaryOp.hypot newBinaryOp(a, b);
fn hypot (a, b SignalExpr) SignalExpr = BinaryOp.hypot newBinaryOp(a, b);

fn copysign (a SignalExpr, b SignalExpr) SignalExpr = BinaryOp.copysign newBinaryOp(a, b);
fn copysign (a SignalExpr, b) SignalExpr = BinaryOp.copysign newBinaryOp(a, b);
fn copysign (a, b SignalExpr) SignalExpr = BinaryOp.copysign newBinaryOp(a, b);

---------------------------------------------------------------------------
-- Signal Comparison Operators

fn < (a SignalExpr, b SignalExpr) SignalExpr = CompareOp.lt newCompareOp(a, b);
fn < (a SignalExpr, b) SignalExpr = CompareOp.lt newCompareOp(a, b);
fn < (a, b SignalExpr) SignalExpr = CompareOp.lt newCompareOp(a, b);

fn <= (a SignalExpr, b SignalExpr) SignalExpr = CompareOp.le newCompareOp(a, b);
fn <= (a SignalExpr, b) SignalExpr = CompareOp.le newCompareOp(a, b);
fn <= (a, b SignalExpr) SignalExpr = CompareOp.le newCompareOp(a, b);

fn == (a SignalExpr, b SignalExpr) SignalExpr = CompareOp.eq newCompareOp(a, b);
fn == (a SignalExpr, b) SignalExpr = CompareOp.eq newCompareOp(a, b);
fn == (a, b SignalExpr) SignalExpr = CompareOp.eq newCompareOp(a, b);

fn != (a SignalExpr, b SignalExpr) SignalExpr = CompareOp.ne newCompareOp(a, b);
fn != (a SignalExpr, b) SignalExpr = CompareOp.ne newCompareOp(a, b);
fn != (a, b SignalExpr) SignalExpr = CompareOp.ne newCompareOp(a, b);

fn >= (a SignalExpr, b SignalExpr) SignalExpr = CompareOp.ge newCompareOp(a, b);
fn >= (a SignalExpr, b) SignalExpr = CompareOp.ge newCompareOp(a, b);
fn >= (a, b SignalExpr) SignalExpr = CompareOp.ge newCompareOp(a, b);

fn > (a SignalExpr, b SignalExpr) SignalExpr = CompareOp.gt newCompareOp(a, b);
fn > (a SignalExpr, b) SignalExpr = CompareOp.gt newCompareOp(a, b);
fn > (a, b SignalExpr) SignalExpr = CompareOp.gt newCompareOp(a, b);

---------------------------------------------------------------------------
-- Signal Cast Operators

fn i32 (a SignalExpr) SignalExpr = CastOp.i32 newCastOp(a);
fn i64 (a SignalExpr) SignalExpr = CastOp.i64 newCastOp(a);
fn f32 (a SignalExpr) SignalExpr = CastOp.f32 newCastOp(a);
fn f64 (a SignalExpr) SignalExpr = CastOp.f64 newCastOp(a);

---------------------------------------------------------------------------
-- Delay Operators

fn init(d DelayVar, index Int, s) SignalExpr {
    SignalExprKind.delay(d, DelayOp.init(index)) newSignalExpr([s asSignal])
}

fn read(d DelayVar, index Int) SignalExpr {
    SignalExprKind.delay(d, DelayOp.read(index)) newSignalExpr
}

fn vread(d DelayVar, index, interp Interpolation) SignalExpr {
    SignalExprKind.delay(d, DelayOp.vread(interp)) newSignalExpr([index asSignal])
}

fn write(d DelayVar, s) SignalExpr {
    SignalExprKind.delay(d, DelayOp.write) newSignalExpr([s asSignal])
}
fn write(s, d DelayVar) SignalExpr {
    SignalExprKind.delay(d, DelayOp.write) newSignalExpr([s asSignal])
}

fn <- (d DelayVar, s) SignalExpr = d write(s);
fn -> (s, d DelayVar) SignalExpr = s write(d);

---------------------------------------------------------------------------
--- Vector operations

fn at(a SignalExpr, i) SignalExpr {
    SignalExprKind.vecop(VecOp.at) newSignalExpr([a, i asSignal])
}
fn put(a SignalExpr, i, v) SignalExpr {
    SignalExprKind.vecop(VecOp.put) newSignalExpr([a, i asSignal, v asSignal])
}
fn matmul(a SignalExpr, b SignalExpr) SignalExpr {
    SignalExprKind.vecop(VecOp.put) newSignalExpr([a, b])
}
fn take(m SignalExpr, n Int) SignalExpr {
    SignalExprKind.vecop(VecOp.take(n)) newSignalExpr([a])
}
fn drop(m SignalExpr, n Int) SignalExpr {
    SignalExprKind.vecop(VecOp.drop(n)) newSignalExpr([a])
}
fn stride(m SignalExpr, n Int) SignalExpr {
    SignalExprKind.vecop(VecOp.stride(n)) newSignalExpr([a])
}
fn stutter(m SignalExpr, n Int) SignalExpr {
    SignalExprKind.vecop(VecOp.stutter(n)) newSignalExpr([a])
}
fn ncyc(m SignalExpr, n Int) SignalExpr {
    SignalExprKind.vecop(VecOp.ncyc(n)) newSignalExpr([a])
}
fn rotate(m SignalExpr, b) SignalExpr {
    SignalExprKind.vecop(VecOp.rotate) newSignalExpr([a, b asSignal])
}
fn reverse(m SignalExpr) SignalExpr {
    SignalExprKind.vecop(VecOp.reverse) newSignalExpr([a])
}
fn reduce(m SignalExpr, op BinaryOp, chans Chans = 1) SignalExpr {
    SignalExprKind.vecop(VecOp.reduce(op, chans asChans)) newSignalExpr([a])
}
fn sum(m SignalExpr, chans Chans = 1) SignalExpr {
    m reduce(BinaryOp.add, chans asChans)
}
fn product(m SignalExpr, chans Chans = 1) SignalExpr {
    m reduce(BinaryOp.mul, chans asChans)
}
fn minOf(m SignalExpr, chans Chans = 1) SignalExpr {
    m reduce(BinaryOp.min, chans asChans)
}
fn maxOf(m SignalExpr, chans Chans = 1) SignalExpr {
    m reduce(BinaryOp.max, chans asChans)
}

---------------------------------------------------------------------------
--- Subgraphs

type GraphFn = fn() SignalExpr;
type GraphFn1 = fn(SignalExpr) SignalExpr;

fn callSubGraphFn(f GraphFn) SignalGraph {
    -- save graph state
	let savedExprs = curGraphExprs;
	let savedDelays = curGraphDelays;

	-- fresh graph state
	curGraphExprs = [];
	curGraphDelays = [];

	let root SignalExpr = f();

	let graph = SignalGraph {
	    exprs: curGraphExprs,
		delays: curGraphDelays,
		root: root,
	};

	-- restore graph state
	curGraphExprs = savedExprs;
	curGraphDelays = savedDelays;

	graph
}

fn callForBodyFn (i SignalExpr, f GraphFn1) SignalExpr {
-- save graph state
	let savedExprs = curGraphExprs;
	let savedDelays = curGraphDelays;

	-- fresh graph state
	curGraphExprs = [];
	curGraphDelays = [];

	let root = f(i);

	let graph = SignalGraph {
	    exprs: curGraphExprs,
		delays: curGraphDelays,
		root: root,
	};

	-- restore graph state
	curGraphExprs = savedExprs;
	curGraphDelays = savedDelays;

	graph
}

---------------------------------------------------------------------------
--- Control Flow Operators

fn if_(test SignalExpr, thenFn GraphFn, elseFn GraphFn) SignalExpr {
    let thenGraph = thenFn callSubGraphFn;
    let elseGraph = elseFn callSubGraphFn;
    SignalExprKind.if_(thenGraph, elseGraph) newSignalExpr([test])
}

fn if_(test SignalExpr, thenFn GraphFn) SignalExpr {
    let thenGraph = thenFn callSubGraphFn;
    let elseGraph = fn() { 0 asSignal } callSubGraphFn
    SignalExprKind.if_(thenGraph, elseGraph) newSignalExpr([test])
}

fn if_(test Bool, thenFn GraphFn, elseFn GraphFn) SignalExpr = test ? thenFn() : elseFn();
fn if_(test Bool, thenFn GraphFn) SignalExpr = test ? thenFn() : 0 asSignal;

fn for_(count Int, bodyFn GraphFn) SignalExpr {
    let bodyGraph = bodyFn callSubGraphFn;
    SignalExprKind.for_(count, bodyGraph) newSignalExpr
}

fn switch(test, funs [GraphFn]) SignalExpr {
    let graphs = funs callSubGraphFn;
    SignalExprKind.switch_(graphs) newSignalExpr([test])
}

fn select(test SignalExpr, exprs [SignalExpr]) SignalExpr {
    let ins = [test] $ exprs;
    SignalExprKind.select newSignalExpr(ins)
}

fn select2(test SignalExpr, ifOne SignalExpr, ifZero SignalExpr) SignalExpr {
    test select([ifZero, ifOne])
}

---------------------------------------------------------------------------
-- To S-Expressions

enum Braces { round, square, curly, quotes }

fn parens(s String) String = "(%^)" fmt(s);
fn braces(s String, Braces b) String {
    match (b) {
        Braces.round  : "(%^)" fmt(s);
        Braces.square : "[%^]" fmt(s);
        Braces.curly  : "{%^}" fmt(s);
        Braces.quotes : "\"%^\"" fmt(s);
    }

}

fn separatedString(strings [String], separator String = " ") String {
    var out = "";
    var between = false;
    for (s : strings) {
        if (between) {
            out = out + separator;
        } else {
            between = true;
        }
        out = out + s;
    }
    out
}

fn inputsToLisp(o SignalExpr) String = o.ins.id separatedString parens;

fn idsToLisp(o [SignalExpr]) String = o.id separatedString parens;
fn numbersToLisp(o [Int]) String = o.id separatedString parens;

fn toLisp(o ControlSpec) String {
    "(ControlSpec %^ %^ %^ %^)" fmt(o.lo, o.hi, o.init, o.warp)
}

fn numTypeInt(op CastOp) Int {
	match (op) {
		i32 : 1;
		i64 : 2;
		f32 : 4;
		f64 : 8;
	}
}

fn toLisp(g SignalGraph) String {
	let exprsStr = g.exprs toLisp separatedString(" ");
	"(%^ %^)" fmt(g.root.id, exprsStr)
}

fn toLisp(o SignalExpr) String {
    match (o.kind) {
        sampleRate : "(%^ SampleRate)" fmt(o.id);
        sampleDur : "(%^ SampleDur)" fmt(o.id);
        int(a) : "(%^ Constant %^ %^ %^)" fmt(o.id, a length, ANY_NUM, a separatedString parens);
        float(a) : "(%^ Constant %^ %^ %^)" fmt(o.id, a length, ANY_FLOAT, a separatedString parens);
        unop(op) : "(%^ UnaryOp %^ %^)" fmt(o.id, op name, o inputsToLisp);
        binop(op) : "(%^ BinaryOp %^ %^)" fmt(o.id, op name, o inputsToLisp);
        compareop(op) : "(%^ BinaryOp %^ %^)" fmt(o.id, op name, o inputsToLisp);
        castop(op) : "(%^ CastOp %^ %^)" fmt(o.id, op numTypeInt, o inputsToLisp);
        random(op, rate, chans) : match (op) {
            frand(lo, hi) : "(%^ FRand %^ %^ %^ %^)" fmt(o.id, rate, chans asChans, lo, hi);
            irand(lo, hi) : "(%^ IRand %^ %^ %^ %^)" fmt(o.id, rate, chans asChans, lo, hi);
            unipolar : "(%^ URand %^ %^)" fmt(o.id, rate, chans asChans);
            bipolar : "(%^ BiRand %^ %^)" fmt(o.id, rate, chans asChans);
            bits : "(%^ Rand64 %^ %^)" fmt(o.id, rate, chans asChans);
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
           	permute : "(%^ VecAt %^)" fmt(o.id, o inputsToLisp);
           	reduce(op, chans) : "(%^ VecReduce %^ %^ %^)"
                fmt(o.id, op name, chans asChans, o inputsToLisp);
           	sum(chans) : "(%^ VecSum %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);
           	prod(chans) : "(%^ VecProd %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);
           	minOf(chans) : "(%^ VecMin %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);
           	maxOf(chans) : "(%^ VecMax %^ %^)" fmt(o.id, chans asChans, o inputsToLisp);

        }
        inlet(typ, chans, name) : "(%^ Inlet \"%^\" %^ %^)" fmt(o.id, name, chans asChans, typ.0);
        outlet(name) : "(%^ Outlet \"%^\" %^)" fmt(o.id, name, o.ins.id separatedString);

        control(spec, chans, name) :
            "(%^ Control \"%^\" %^ %^)" fmt(o.id, name, chans asChans, spec toLisp);

        delay(delayVar, op) : match (op) {
            maxDelayTime : "(%^ MaxDelay %^ %^)" fmt(o.id, delayVar.id, o inputsToLisp);
            init(offset) : "(%^ DelayInit %^ %^ %^)" fmt(o.id, delayVar.id, offset, o inputsToLisp);
            read(offset) : "(%^ DelayFixRead %^ %^)" fmt(o.id, delayVar.id, offset);
            vread(interpolation) : "(%^ DelayVarRead %^ %^)" fmt(o.id, delayVar.id, o inputsToLisp);
            write :  "(%^ DelayWrite %^ %^)" fmt(o.id, delayVar.id, o inputsToLisp);
        }

        if_(thenGraph, elseGraph) :
            "(%^ IfExpr %^ %^ %^)" fmt(o.id, o inputsToLisp, thenGraph toLisp, elseGraph toLisp);

        for_(count, bodyGraph) :
            "(%^ ForExpr %^ %^)" fmt(o.id, count, bodyGraph toLisp);

        switch_(cases) :
            "(%^ SwitchExpr %^ %^)" fmt(o.id, o inputsToLisp, cases toLisp separatedString(" "));

        select : "(%^ SelectExpr %^)" fmt(o.id, o inputsToLisp);
        select2 : "(%^ SelectExpr %^)" fmt(o.id, o inputsToLisp);
	}
}
---------------------------------------------------------------------------
---------------------------------------------------------------------------
---------------------------------------------------------------------------
