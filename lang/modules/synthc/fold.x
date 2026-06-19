-- synthc/fold.x
-- Compile-time constant folding for unary/binary/compare/cast on constant
-- vectors, ported from synthdef_matrix.cpp (unary_op / binary_op / compare_op /
-- to_int / to_float). The C++ folds these at graph construction, so synthc must
-- fold identically for its analysis dump (and codegen) to match.
--
-- Storage mirrors the C++ Constant: ConstVal.ints (i64 vector) or
-- ConstVal.floats (f64 vector). The s-expression front-end the C++ consumes
-- always builds f64 storage, so importer.x imports every front-end constant as
-- floats; i64 storage only arises from a cast-to-int fold. Folding follows the
-- C++ std::visit rules: float ops promote int operands to float; integer ops
-- stay integer; compares always produce an i64 (-1 true / 0 false) result.

import synthdef.*;
import synthc.ir.*;

const PI = 3.141592653589793;

---------------------------------------------------------------------------
-- Cyclic element access (power-of-two broadcast: values[i & (size-1)]).

fn _atF(v ConstVal, i Int) Float = match (v) {
	ints(a):   a[i & (a length - 1)] toFloat;
	floats(a): a[i & (a length - 1)];
};

fn _atI(v ConstVal, i Int) Int = match (v) {
	ints(a):   a[i & (a length - 1)];
	floats(a): a[i & (a length - 1)] toInt;
};

fn _isInts(v ConstVal) Bool = match (v) { ints(_): true; floats(_): false; };

fn _broadcastSize(a ConstVal, b ConstVal) Int = max(a constSize, b constSize);

---------------------------------------------------------------------------
-- Casts (to_int / to_float)

fn toFloatStorage(v ConstVal) ConstVal = match (v) {
	ints(a):   ConstVal.floats(a toFloat);
	floats(_): v;
};

fn toIntStorage(v ConstVal) ConstVal = match (v) {
	ints(_):   v;
	floats(a): ConstVal.ints(a map(fn(x Float) Int { x toInt }));
};

-- foldCast returns the folded ConstVal AND the cast's init_type (= target type),
-- matching cast_op which sets b->init_type = type.
fn foldCast(v ConstVal, op CastOp) ConstVal = match (op) {
	i32: v toIntStorage;
	i64: v toIntStorage;
	f32: v toFloatStorage;
	f64: v toFloatStorage;
};

---------------------------------------------------------------------------
-- Float modulo (floored), matching synthdef::mod.

fn _fmodFloored(x Float, y Float) Float {
	let r = x - y * (x / y) floor;
	r
}

fn _imodFloored(x Int, y Int) Int {
	let r = x % y;
	r < 0 ? r + y : r
}

---------------------------------------------------------------------------
-- Unary

fn _applyFloatUnary(op UnaryOp, x Float) Float = match (op) {
	neg: 0.0 - x;  abs: x abs;
	floor: x floor;  ceil: x ceil;  trunc: x trunc;  round: x round;
	sqrt: x sqrt;  cbrt: x cbrt;
	log: x log;  log2: x log2;  log10: x log10;  log1p: x log1p;
	exp: x exp;  exp2: x exp2;  exp10: x exp10;  expm1: x expm1;
	sin: x sin;  cos: x cos;  tan: x tan;
	asin: x asin;  acos: x acos;  atan: x atan;
	sinh: x sinh;  cosh: x cosh;  tanh: x tanh;
	asinh: x asinh;  acosh: x acosh;  atanh: x atanh;
	sinpi: (PI * x) sin;  cospi: (PI * x) cos;  tanpi: (PI * x) tan;
	erf: x erf;  erfc: x erfc;
	_: x;            -- unreachable for supported ops
};

fn _applyIntUnary(op UnaryOp, x Int) Int = match (op) {
	neg: 0 - x;  abs: x abs;  not: x == 0 ? 1 : 0;  bitNot: ~x;
	_: x;
};

-- Returns the folded ConstVal. Float unary ops promote int storage to float.
fn foldUnary(v ConstVal, op UnaryOp) ConstVal {
	let n = v constSize;
	if (op isFloatUnop || !(v _isInts)) {
		var out [Float] = [];
		var i = 0;
		while (i < n) { out push!(_applyFloatUnary(op, _atF(v, i))); i = i + 1; }
		ConstVal.floats(out)
	} else {
		var out [Int] = [];
		var i = 0;
		while (i < n) { out push!(_applyIntUnary(op, _atI(v, i))); i = i + 1; }
		ConstVal.ints(out)
	}
}

---------------------------------------------------------------------------
-- Binary

fn _applyFloatBinary(op BinaryOp, x Float, y Float) Float = match (op) {
	add: x + y;  sub: x - y;  mul: x * y;  div: x / y;
	mod: _fmodFloored(x, y);
	min: min(x, y);  max: max(x, y);
	pow: pow(x, y);  hypot: hypot(x, y);  atan2: atan2(x, y);  copysign: copysign(x, y);
	_: x;
};

fn _applyIntBinary(op BinaryOp, x Int, y Int) Int = match (op) {
	add: x + y;  sub: x - y;  mul: x * y;  div: x // y;
	mod: _imodFloored(x, y);
	bitAnd: x & y;  bitOr: x | y;  bitXor: x ^ y;
	shiftLeft: x << y;  shiftRight: x >> y;  unsignedShiftRight: x >> y;
	min: min(x, y);  max: max(x, y);
	_: x;
};

fn foldBinary(a ConstVal, b ConstVal, op BinaryOp) ConstVal {
	let n = _broadcastSize(a, b);
	let useFloat = op isFloatBinop || !(a _isInts) || !(b _isInts);
	if (useFloat) {
		var out [Float] = [];
		var i = 0;
		while (i < n) { out push!(_applyFloatBinary(op, _atF(a, i), _atF(b, i))); i = i + 1; }
		ConstVal.floats(out)
	} else {
		var out [Int] = [];
		var i = 0;
		while (i < n) { out push!(_applyIntBinary(op, _atI(a, i), _atI(b, i))); i = i + 1; }
		ConstVal.ints(out)
	}
}

---------------------------------------------------------------------------
-- Reduce a constant vector over `cols` columns (port of reduce_int/reduce_float
-- in synthdef_matrix.cpp). Input is reshaped as rows x cols; each output column
-- j is acc = a[j], then op-folded down the rows: acc = op(acc, a[row*cols + j]).

fn _reduceInts(a [Int], cols Int, op BinaryOp) [Int] {
	let rows = a length // cols;
	var out [Int] = [];
	var j = 0;
	while (j < cols) {
		var acc = a[j];
		var row = 1;
		while (row < rows) { acc = _applyIntBinary(op, acc, a[row * cols + j]); row = row + 1; }
		out push!(acc);
		j = j + 1;
	}
	out
}

fn _reduceFloats(a [Float], cols Int, op BinaryOp) [Float] {
	let rows = a length // cols;
	var out [Float] = [];
	var j = 0;
	while (j < cols) {
		var acc = a[j];
		var row = 1;
		while (row < rows) { acc = _applyFloatBinary(op, acc, a[row * cols + j]); row = row + 1; }
		out push!(acc);
		j = j + 1;
	}
	out
}

fn foldReduce(v ConstVal, cols Int, op BinaryOp) ConstVal = match (v) {
	ints(a):   ConstVal.ints(_reduceInts(a, cols, op));
	floats(a): ConstVal.floats(_reduceFloats(a, cols, op));
};

---------------------------------------------------------------------------
-- Compare (result is always an i64 vector: -1 true, 0 false)

fn _cmpF(op CompareOp, x Float, y Float) Int = match (op) {
	lt: x < y ? -1 : 0;  le: x <= y ? -1 : 0;
	eq: x == y ? -1 : 0;  ne: x != y ? -1 : 0;
	ge: x >= y ? -1 : 0;  gt: x > y ? -1 : 0;
};

fn _cmpI(op CompareOp, x Int, y Int) Int = match (op) {
	lt: x < y ? -1 : 0;  le: x <= y ? -1 : 0;
	eq: x == y ? -1 : 0;  ne: x != y ? -1 : 0;
	ge: x >= y ? -1 : 0;  gt: x > y ? -1 : 0;
};

fn foldCompare(a ConstVal, b ConstVal, op CompareOp) ConstVal {
	let n = _broadcastSize(a, b);
	let bothInt = a _isInts && b _isInts;
	var out [Int] = [];
	var i = 0;
	while (i < n) {
		if (bothInt) { out push!(_cmpI(op, _atI(a, i), _atI(b, i))); }
		else { out push!(_cmpF(op, _atF(a, i), _atF(b, i))); }
		i = i + 1;
	}
	ConstVal.ints(out)
}
