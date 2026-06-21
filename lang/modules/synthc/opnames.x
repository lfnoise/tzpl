-- synthc/opnames.x
-- Operator -> C++ spelling tables and scalar expression builders for codegen,
-- ported from synthdef_cpp_codegen.cpp (to_cpp_scalar_compile_string,
-- genUnopExprString, genBinopExprString) and synthdef_math_ops.cpp (op_syntax).
-- Scalar (non-SIMD) only for M1.

import synthdef.*;

---------------------------------------------------------------------------
-- Scalar C++ spellings

fn cppUnop(op UnaryOp, isF32 Bool) String = match (op) {
	neg: "-";  not: "!";  bitNot: "~";  abs: "std::abs";
	clz: "clz";  ctz: "ctz";  popCount: "popcount";  bitWidth: "numbits";
	floor: "std::floor";  ceil: "std::ceil";  round: "std::round";  trunc: "std::trunc";
	sqrt: "std::sqrt";  cbrt: "std::cbrt";
	log: "std::log";  log2: "std::log2";  log10: "std::log10";  log1p: "std::log1p";
	exp: "std::exp";  exp2: "std::exp2";  exp10: "std::exp10";  expm1: "std::expm1";
	sinpi: isF32 ? "__sinpif" : "__sinpi";
	cospi: isF32 ? "__cospif" : "__cospi";
	tanpi: isF32 ? "__tanpif" : "__tanpi";
	sin: "std::sin";  cos: "std::cos";  tan: "std::tan";
	asin: "std::asin";  acos: "std::acos";  atan: "std::atan";
	sinh: "std::sinh";  cosh: "std::cosh";  tanh: "std::tanh";
	asinh: "std::asinh";  acosh: "std::acosh";  atanh: "std::atanh";
	erf: "std::erf";  erfc: "std::erfc";
	_: "unknown";
};

fn cppBinop(op BinaryOp, isF32 Bool) String = match (op) {
	add: "+";  sub: "-";  mul: "*";  div: "/";
	mod: "mod";
	bitAnd: "&";  bitOr: "|";  bitXor: "^";
	shiftLeft: "<<";  shiftRight: ">>";  unsignedShiftRight: "ushr";
	min: "std::min";  max: "std::max";
	pow: "std::pow";  hypot: "std::hypot";  atan2: "std::atan2";  copysign: "std::copysign";
	_: "unknown";
};

fn cppCompare(op CompareOp) String = match (op) {
	lt: "<";  le: "<=";  eq: "==";  ne: "!=";  ge: ">=";  gt: ">";
};

---------------------------------------------------------------------------
-- SIMD C++ spellings (mirror to_cpp_simd_compile_string). The vector overloads
-- live in tzpl_simd.hpp's namespace (no std::/__ prefix; sinpi/cospi/tanpi have
-- no f32 variant suffix).

fn cppSimdUnop(op UnaryOp, isF32 Bool) String = match (op) {
	neg: "-";  not: "!";  bitNot: "~";  abs: "abs";
	clz: "clz";  ctz: "ctz";  popCount: "popcount";  bitWidth: "numbits";
	floor: "floor";  ceil: "ceil";  round: "round";  trunc: "trunc";
	sqrt: "sqrt";  cbrt: "cbrt";
	log: "log";  log2: "log2";  log10: "log10";  log1p: "log1p";
	exp: "exp";  exp2: "exp2";  exp10: "exp10";  expm1: "expm1";
	sinpi: "sinpi";  cospi: "cospi";  tanpi: "tanpi";
	sin: "sin";  cos: "cos";  tan: "tan";
	asin: "asin";  acos: "acos";  atan: "atan";
	sinh: "sinh";  cosh: "cosh";  tanh: "tanh";
	asinh: "asinh";  acosh: "acosh";  atanh: "atanh";
	_: "unknown";
};

fn cppSimdBinop(op BinaryOp, isF32 Bool) String = match (op) {
	add: "+";  sub: "-";  mul: "*";  div: "/";
	mod: "mod";
	bitAnd: "&";  bitOr: "|";  bitXor: "^";
	shiftLeft: "<<";  shiftRight: ">>";  unsignedShiftRight: "ushr";
	min: "min";  max: "max";
	pow: "pow";  hypot: "hypot";  atan2: "atan2";  copysign: "copysign";
	_: "unknown";
};

---------------------------------------------------------------------------
-- Op syntax

enum OpSyntax { prefix, infix, function }

fn unopSyntax(op UnaryOp) OpSyntax = match (op) {
	neg: OpSyntax.prefix;  not: OpSyntax.prefix;  bitNot: OpSyntax.prefix;
	_:   OpSyntax.function;
};

fn binopSyntax(op BinaryOp) OpSyntax = match (op) {
	add: OpSyntax.infix;  sub: OpSyntax.infix;  mul: OpSyntax.infix;  div: OpSyntax.infix;
	bitAnd: OpSyntax.infix;  bitOr: OpSyntax.infix;  bitXor: OpSyntax.infix;
	shiftLeft: OpSyntax.infix;  shiftRight: OpSyntax.infix;
	_: OpSyntax.function;   -- mod, min, max, pow, hypot, atan2, copysign, ushr
};

---------------------------------------------------------------------------
-- Expression builders (genUnopExprString / genBinopExprString)

fn genUnopStr(op UnaryOp, isF32 Bool, a String, simd Bool = false) String {
	let cs = simd ? cppSimdUnop(op, isF32) : cppUnop(op, isF32);
	match (op unopSyntax) {
		function: "%^(%^)" fmt(cs, a);
		prefix:   "%^%^" fmt(cs, a);
		infix:    "unknown";
	}
}

fn genBinopStr(op BinaryOp, isF32 Bool, a String, b String, simd Bool = false) String {
	let cs = simd ? cppSimdBinop(op, isF32) : cppBinop(op, isF32);
	match (op binopSyntax) {
		function: "%^(%^, %^)" fmt(cs, a, b);
		infix:    "(%^ %^ %^)" fmt(a, cs, b);
		prefix:   "unknown";
	}
}

-- Compare ops are emitted as `(a OP b)` (always infix).
fn genCompareStr(op CompareOp, a String, b String) String = "(%^ %^ %^)" fmt(a, cppCompare(op), b);
