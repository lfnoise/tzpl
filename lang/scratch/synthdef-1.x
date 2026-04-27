
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
	min Float,
	max Float,
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


--type ID = Int;

struct SignalExpr {
	id Ref<ID>,
	ins [SignalExpr],
	kind SignalExprKind,
}


var curGraphExprs [SignalExpr] = [];
var curGraphDelays [DelayVar] = [];
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
