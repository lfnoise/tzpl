-- synthdef.x
-- The graph construction API for synths, plus playback helpers. Re-exports
-- `synthdef_ffi.*` (the synthc build and load FFIs) so `import synthdef.*;`
-- brings both. Synths are compiled by synthc: `defSynthX` in synthc/compile.x.

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

-- The NumType bit of a cast's target type.
fn numTypeInt(op CastOp) Int {
	match (op) {
		i32 : 1;
		i64 : 2;
		f32 : 4;
		f64 : 8;
	}
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

	int ([Int], NumType),
	float ([Float], NumType),

	unop UnaryOp,
	binop BinaryOp,
	compareop CompareOp,
	castop CastOp,
	eventToAudio,
	random (RandOp, Rate, Chans),
	vecop VecOp,

	inlet(NumType, Chans, String),
	outlet(String),

	control(ControlSpec, Chans, String, ControlKind),
	noteParam(ControlSpec, Chans, String),

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

-- Promote an event-rate signal to audio rate: a stepped audio signal that
-- re-reads the latched event value every sample. Restores one-sample trigger
-- semantics over event sources -- e.g. `eventToAudio(c) tr` fires a single-
-- sample impulse on the sample after a control or noteParam change. No
-- effect on signals of any other rate (constant/init/reset/audio pass
-- through unchanged).
fn eventToAudio(a AsSignal) S {
    SignalExprKind.eventToAudio _newSignalExpr([a asSignal])
}

fn inlet(typ NumType, chans Chans = 1, name String = "in") S {
    SignalExprKind.inlet(typ, chans asChans, name) _newSignalExpr
}

fn outlet(a S, name String = "out") S {
    SignalExprKind.outlet(name) _newSignalExpr([a])
}

fn control(name String, spec ControlSpec, chans Chans = 1) S {
    SignalExprKind.control(spec, chans asChans, name, ControlKind.continuous) _newSignalExpr
}

-- Momentary event control (0..1, init 0): the synth treats a nonzero
-- arrival as a one-shot edge (retrigger an envelope, reset a phase).
-- UIs render a button.
fn trigger(name String, chans Chans = 1) S {
    SignalExprKind.control(
        ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear },
        chans asChans, name, ControlKind.trigger) _newSignalExpr
}

-- Latched on/off control (0 or 1). UIs render a toggle.
fn toggle(name String, init Float = 0.0, chans Chans = 1) S {
    SignalExprKind.control(
        ControlSpec { lo: 0.0, hi: 1.0, init: init, warp: ControlWarp.linear },
        chans asChans, name, ControlKind.boolean) _newSignalExpr
}

-- Integer choice control: 0 .. numChoices-1 in unit steps. UIs render a
-- number box.
fn choice(name String, numChoices Int, init Int = 0, chans Chans = 1) S {
    SignalExprKind.control(
        ControlSpec { lo: 0.0, hi: (numChoices - 1) toFloat, init: init toFloat,
                      warp: ControlWarp.step(1.0) },
        chans asChans, name, ControlKind.select) _newSignalExpr
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

-- Expressed via urand rather than SignalExprKind.random(RandOp.frand(...))
-- directly: the synthc importer has no lowering for RandOp.frand/irand (see
-- lang/modules/synthc/importer.x, "FRand/IRand is not supported by the
-- synthdef compiler") and urand's lowering is already exercised end-to-end.
fn frand(lo Float, hi Float, chans Chans = 1, rate Rate = Rate.audio) S =
    lo + (hi - lo) * urand(chans, rate);
-- Uniform integer in [lo, hi] inclusive. Same urand-based construction as
-- frand above, for the same reason (no synthc lowering for RandOp.irand);
-- the min() guards the one-in-2^24 f32 case where u*(n) rounds up to n.
fn irand(lo Int, hi Int, chans Chans = 1, rate Rate = Rate.audio) S =
    (lo + min(hi - lo, (urand(chans, rate) * (hi - lo + 1)) floor)) i32;
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












