-- Tzopilotl
-- Copyright (C) 2026 James McCartney
--
-- synthc algebraic rewrite engine.
--
-- A faithful port of the C++ synthdef_rewrite.cpp term-rewriting system. Rules
-- are written with a small Tzopilotl DSL that overloads operators and math
-- functions over a `Pat` pattern AST, so a rule reads like the algebra it
-- expresses, e.g. `rule(x + zero, x)`.
--
-- The engine is invoked at node-construction time in importer.x, interleaved
-- with constant folding and hash-consing (each of the three can expose the
-- others), mirroring the C++ addExpr()/rewrite() fixpoint. This module is the
-- PURE part: pattern AST, DSL, matcher, and rule book. It reads a Ctx but never
-- creates nodes -- importer.x interprets a matched rule's RHS Pat to build the
-- replacement, re-entering its fold/rewrite/hash-cons constructor.

import synthdef.*;     -- UnaryOp, BinaryOp, CompareOp, CastOp, Rate
import synthc.ir.*;    -- Ctx, NodeKind, ConstVal

---------------------------------------------------------------------------
-- Pattern AST
--
-- A Pat is a pattern tree. Pattern variables bind to Ctx node indices during
-- matching. Var ids are partitioned for the rate check: I,J,K (ids 0..2) are
-- "low clock" vars and must be strictly lower rate than X,Y,Z (ids 3..5) for a
-- rule to fire (this is what makes the reassociation rules hoist constant/
-- control-rate work). Pos/Neg/MonoUp/MonoDn (6..9) are excluded from that check.

const VI = 0; const VJ = 1; const VK = 2;
const VX = 3; const VY = 4; const VZ = 5;
const VPOS = 6; const VNEG = 7; const VMONOUP = 8; const VMONODN = 9;
const NVARS = 10;

enum Pat {
	pvar(Int),                  -- variable id -> binds a node index
	pscalar(Float),             -- matches a scalar constant equal to this value
	pposnum,                    -- matches a scalar constant > 0
	pnegnum,                    -- matches a scalar constant < 0
	punop(UnaryOp, Pat),
	pbinop(BinaryOp, Pat, Pat),
	pcmpop(CompareOp, Pat, Pat),
	pmono(Int, Pat),            -- matches any unary op of monotonicity 0=up / 1=down
}

struct Rule { lhs Pat; rhs Pat; }
fn rule(a Pat, b Pat) Rule = Rule { lhs: a, rhs: b };

---------------------------------------------------------------------------
-- The DSL: operators and math functions build Pat trees.

fn +(a Pat, b Pat) Pat = Pat.pbinop(BinaryOp.add, a, b);
fn -(a Pat, b Pat) Pat = Pat.pbinop(BinaryOp.sub, a, b);
fn *(a Pat, b Pat) Pat = Pat.pbinop(BinaryOp.mul, a, b);
fn /(a Pat, b Pat) Pat = Pat.pbinop(BinaryOp.div, a, b);
fn %(a Pat, b Pat) Pat = Pat.pbinop(BinaryOp.mod, a, b);

fn -(a Pat) Pat = Pat.punop(UnaryOp.neg, a);
fn !(a Pat) Pat = Pat.punop(UnaryOp.not, a);

fn <(a Pat, b Pat) Pat  = Pat.pcmpop(CompareOp.lt, a, b);
fn <=(a Pat, b Pat) Pat = Pat.pcmpop(CompareOp.le, a, b);
fn >(a Pat, b Pat) Pat  = Pat.pcmpop(CompareOp.gt, a, b);
fn >=(a Pat, b Pat) Pat = Pat.pcmpop(CompareOp.ge, a, b);
fn ==(a Pat, b Pat) Pat = Pat.pcmpop(CompareOp.eq, a, b);
fn !=(a Pat, b Pat) Pat = Pat.pcmpop(CompareOp.ne, a, b);

-- unary math
fn abs(a Pat) Pat   = Pat.punop(UnaryOp.abs, a);
fn sqrt(a Pat) Pat  = Pat.punop(UnaryOp.sqrt, a);
fn cbrt(a Pat) Pat  = Pat.punop(UnaryOp.cbrt, a);
fn floor(a Pat) Pat = Pat.punop(UnaryOp.floor, a);
fn ceil(a Pat) Pat  = Pat.punop(UnaryOp.ceil, a);
fn round(a Pat) Pat = Pat.punop(UnaryOp.round, a);
fn trunc(a Pat) Pat = Pat.punop(UnaryOp.trunc, a);
fn log(a Pat) Pat   = Pat.punop(UnaryOp.log, a);
fn log2(a Pat) Pat  = Pat.punop(UnaryOp.log2, a);
fn log10(a Pat) Pat = Pat.punop(UnaryOp.log10, a);
fn exp(a Pat) Pat   = Pat.punop(UnaryOp.exp, a);
fn exp2(a Pat) Pat  = Pat.punop(UnaryOp.exp2, a);
fn exp10(a Pat) Pat = Pat.punop(UnaryOp.exp10, a);
fn tan(a Pat) Pat   = Pat.punop(UnaryOp.tan, a);
fn atan(a Pat) Pat  = Pat.punop(UnaryOp.atan, a);
fn sinh(a Pat) Pat  = Pat.punop(UnaryOp.sinh, a);
fn cosh(a Pat) Pat  = Pat.punop(UnaryOp.cosh, a);
fn tanh(a Pat) Pat  = Pat.punop(UnaryOp.tanh, a);
fn asinh(a Pat) Pat = Pat.punop(UnaryOp.asinh, a);
fn acosh(a Pat) Pat = Pat.punop(UnaryOp.acosh, a);
fn atanh(a Pat) Pat = Pat.punop(UnaryOp.atanh, a);

-- binary math
fn min(a Pat, b Pat) Pat   = Pat.pbinop(BinaryOp.min, a, b);
fn max(a Pat, b Pat) Pat   = Pat.pbinop(BinaryOp.max, a, b);
fn pow(a Pat, b Pat) Pat   = Pat.pbinop(BinaryOp.pow, a, b);
fn hypot(a Pat, b Pat) Pat = Pat.pbinop(BinaryOp.hypot, a, b);

-- monotonic wrappers (match any monotonically increasing / decreasing unary op)
fn monoup(a Pat) Pat = Pat.pmono(0, a);
fn monodn(a Pat) Pat = Pat.pmono(1, a);

---------------------------------------------------------------------------
-- Ported helpers (mirror synthdef_math_ops.cpp).

-- 0 = increasing, 1 = decreasing, 2 = non-monotonic.
fn monotonicity(op UnaryOp) Int = match (op) {
	neg: 1;  acosh: 1;  erfc: 1;
	floor: 0; ceil: 0; round: 0; trunc: 0;
	sqrt: 0; cbrt: 0;
	log: 0; log2: 0; log10: 0; log1p: 0;
	exp: 0; exp2: 0; exp10: 0; expm1: 0;
	asin: 0; atan: 0;
	sinh: 0; cosh: 0; tanh: 0;
	asinh: 0; atanh: 0;
	erf: 0;
	_: 2;
};

fn isCommutativeBin(op BinaryOp) Bool = match (op) {
	add: true; mul: true; min: true; max: true;
	bitAnd: true; bitOr: true; bitXor: true; hypot: true;
	_: false;
};

fn isCommutativeCmp(op CompareOp) Bool = match (op) {
	eq: true; ne: true;
	_: false;
};

---------------------------------------------------------------------------
-- Matching. Subs is a fixed array, var id -> node index (NONE = unbound).

const SUBS_NONE = 0 - 1;

fn _freshSubs() [Int] {
	var s [Int] = [];
	var i = 0;
	while (i < NVARS) { s push!(SUBS_NONE); i = i + 1; }
	s
}

-- Option constructors wrapped in functions: inline `Option<T>.foo` does not
-- parse inside a ternary/match-arm body (the <> is ambiguous there), but it is
-- fine in an expression-bodied function, so route all construction through these.
fn _ok(subs [Int]) Option<[Int]> = Option<[Int]>.some(subs);
fn _no() Option<[Int]> = Option<[Int]>.none;
fn _someF(x Float) Option<Float> = Option<Float>.some(x);
fn _noF() Option<Float> = Option<Float>.none;
fn _bindOk(subs [Int], id Int, n Int) Option<[Int]> { subs[id] = n; _ok(subs) }

-- Scalar value of node n if it is a 1-element constant, else none.
fn _scalarOfVal(v ConstVal) Option<Float> = match (v) {
	ints(a):   (a length == 1) ? (a[0] toFloat) _someF : _noF();
	floats(a): (a length == 1) ? a[0] _someF : _noF();
};
fn _scalarOf(ctx Ctx, n Int) Option<Float> = match (ctx.kind[n]) {
	constant(v, _): _scalarOfVal(v);
	_: _noF();
};

fn _matchVar(id Int, n Int, subs [Int]) Option<[Int]> {
	if (subs[id] != SUBS_NONE) {
		if (subs[id] == n) { _ok(subs) } else { _no() }
	} else {
		subs[id] = n;
		_ok(subs)
	}
}

fn _matchScalar(ctx Ctx, c Float, n Int, subs [Int]) Option<[Int]> = match (_scalarOf(ctx, n)) {
	some(v): (v == c) ? subs _ok : _no();
	none: _no();
};

fn _matchPos(ctx Ctx, n Int, subs [Int]) Option<[Int]> = match (_scalarOf(ctx, n)) {
	some(v): (v > 0.0) ? _bindOk(subs, VPOS, n) : _no();
	none: _no();
};

fn _matchNeg(ctx Ctx, n Int, subs [Int]) Option<[Int]> = match (_scalarOf(ctx, n)) {
	some(v): (v < 0.0) ? _bindOk(subs, VNEG, n) : _no();
	none: _no();
};

fn _matchUnop(ctx Ctx, op UnaryOp, a Pat, kind NodeKind, ins [Int], subs [Int]) Option<[Int]> = match (kind) {
	unopK(op2): (op2 == op) ? _matchNode(ctx, a, ins[0], subs) : _no();
	_: _no();
};

fn _matchBinop(ctx Ctx, op BinaryOp, a Pat, b Pat, kind NodeKind, ins [Int], subs [Int]) Option<[Int]> = match (kind) {
	binopK(op2): (op2 == op) ? _matchArgs(ctx, a, b, ins[0], ins[1], subs, op isCommutativeBin) : _no();
	_: _no();
};

fn _matchCmpop(ctx Ctx, op CompareOp, a Pat, b Pat, kind NodeKind, ins [Int], subs [Int]) Option<[Int]> = match (kind) {
	compareopK(op2): (op2 == op) ? _matchArgs(ctx, a, b, ins[0], ins[1], subs, op isCommutativeCmp) : _no();
	_: _no();
};

-- Match a monotonic unary op of the requested direction, binding the matched
-- node so the RHS can rebuild with the SAME op. n is the matched node index
-- (>= 0 when matching an existing ctx node; -1 at the candidate top, where mono
-- never appears in a rule LHS).
fn _matchMono(ctx Ctx, m Int, a Pat, n Int, kind NodeKind, ins [Int], subs [Int]) Option<[Int]> = match (kind) {
	unopK(op2): (op2 monotonicity == m) ? _monoBind(ctx, m, a, n, ins, subs) : _no();
	_: _no();
};
fn _monoBind(ctx Ctx, m Int, a Pat, n Int, ins [Int], subs [Int]) Option<[Int]> {
	if (n >= 0) { subs[(m == 0) ? VMONOUP : VMONODN] = n; }
	_matchNode(ctx, a, ins[0], subs)
}

-- Match a pattern against an EXISTING ctx node n. subs is mutated in place;
-- callers that branch copy it first.
fn _matchNode(ctx Ctx, p Pat, n Int, subs [Int]) Option<[Int]> = match (p) {
	pvar(id):   _matchVar(id, n, subs);
	pscalar(c): _matchScalar(ctx, c, n, subs);
	pposnum:    _matchPos(ctx, n, subs);
	pnegnum:    _matchNeg(ctx, n, subs);
	punop(op, a):     _matchUnop(ctx, op, a, ctx.kind[n], ctx.ins[n], subs);
	pbinop(op, a, b): _matchBinop(ctx, op, a, b, ctx.kind[n], ctx.ins[n], subs);
	pcmpop(op, a, b): _matchCmpop(ctx, op, a, b, ctx.kind[n], ctx.ins[n], subs);
	pmono(m, a):      _matchMono(ctx, m, a, n, ctx.kind[n], ctx.ins[n], subs);
};

-- Match (pa,pb) against (n0,n1), trying the swapped order for commutative ops.
fn _matchArgs(ctx Ctx, pa Pat, pb Pat, n0 Int, n1 Int, subs [Int], commut Bool) Option<[Int]> = match (_tryPair(ctx, pa, pb, n0, n1, subs copy)) {
	some(s): s _ok;
	none: commut ? _tryPair(ctx, pa, pb, n1, n0, subs copy) : _no();
};

fn _tryPair(ctx Ctx, pa Pat, pb Pat, na Int, nb Int, subs [Int]) Option<[Int]> = match (_matchNode(ctx, pa, na, subs)) {
	some(s): _matchNode(ctx, pb, nb, s);
	none: _no();
};

-- Match a rule's LHS (always rooted at an op) against the CANDIDATE node being
-- constructed (kind + already-imported input indices), before it exists in ctx.
fn _matchTop(ctx Ctx, lhs Pat, kind NodeKind, ins [Int], subs [Int]) Option<[Int]> = match (lhs) {
	punop(op, a):     _matchUnop(ctx, op, a, kind, ins, subs);
	pbinop(op, a, b): _matchBinop(ctx, op, a, b, kind, ins, subs);
	pcmpop(op, a, b): _matchCmpop(ctx, op, a, b, kind, ins, subs);
	pmono(m, a):      _matchMono(ctx, m, a, 0 - 1, kind, ins, subs);
	_: _no();   -- a rule LHS is never a bare var / scalar
};

-- Rate constraint: max rate of low vars (I,J,K) < min rate of high vars (X,Y,Z).
fn _checkVarRates(ctx Ctx, subs [Int]) Bool {
	var maxLow = SUBS_NONE;     -- below any real rate ordinal (0..4)
	var minHigh = 999;
	var id = 0;
	while (id < 6) {
		let n = subs[id];
		if (n != SUBS_NONE) {
			let ri = ctx.nrate[n] ordinal;
			if (id <= 2) { if (ri > maxLow) { maxLow = ri; } }
			else { if (ri < minHigh) { minHigh = ri; } }
		}
		id = id + 1;
	}
	maxLow < minHigh
}

---------------------------------------------------------------------------
-- The rule book. (First batch: identities, degenerate/reflexive cases, power
-- laws, min/max, redundant rounding. Reassociation and the rest follow.)

fn _buildRules() [Rule] {
	let x = Pat.pvar(VX); let y = Pat.pvar(VY); let z = Pat.pvar(VZ);
	let i = Pat.pvar(VI); let j = Pat.pvar(VJ); let k = Pat.pvar(VK);
	let pos = Pat.pposnum; let neg = Pat.pnegnum;
	let zero = Pat.pscalar(0.0); let one = Pat.pscalar(1.0); let two = Pat.pscalar(2.0);
	let three = Pat.pscalar(3.0);
	let negone = Pat.pscalar(0.0 - 1.0); let negtwo = Pat.pscalar(0.0 - 2.0);
	let negthree = Pat.pscalar(0.0 - 3.0);

	[
		-- inverses
		rule(-(-(x)), x),
		rule(!(!(x)), x),
		rule(-(x - y), y - x),
		rule(one / (x / y), y / x),

		rule(tan(atan(x)), x),
		rule(sinh(asinh(x)), x),
		rule(asinh(sinh(x)), x),
		rule(atanh(tanh(x)), x),
		rule(acosh(cosh(x)), abs(x)),

		rule(log(exp(x)), x),
		rule(log2(exp2(x)), x),
		rule(log10(exp10(x)), x),
		rule(exp(log(x)), x),
		rule(exp2(log2(x)), x),
		rule(exp10(log10(x)), x),

		rule(sqrt(x * x), abs(x)),
		rule(sqrt(x) * sqrt(x), x),
		rule(sqrt(x) * sqrt(y), sqrt(x * y)),
		rule(sqrt(x) / sqrt(y), sqrt(x / y)),
		rule(cbrt(x) * cbrt(y), cbrt(x * y)),
		rule(cbrt(x) / cbrt(y), cbrt(x / y)),

		rule(log(pow(x, y)),   y * log(abs(x))),
		rule(log2(pow(x, y)),  y * log2(abs(x))),
		rule(log10(pow(x, y)), y * log10(abs(x))),

		-- identity functions
		rule(x / one, x),
		rule(x / negone, -x),
		rule(one * x, x),
		rule(negone * x, -x),
		rule(x - zero, x),
		rule(zero + x, x),
		rule(-x + -y, -(x + y)),
		rule(-x - -y, y - x),
		rule(-x * -y, x * y),
		rule(-x / -y, x / y),

		rule(pow(x, zero), one),
		rule(pow(x, one), x),
		rule(pow(x, two), x * x),
		rule(pow(x, three), x * x * x),
		rule(pow(x, negone), one / x),
		rule(pow(x, negtwo), one / (x * x)),
		rule(pow(x, negthree), one / (x * x * x)),

		rule(pow(x, y) * pow(x, z), pow(x, y + z)),
		rule(pow(x, y) / pow(x, z), pow(x, y - z)),
		rule(pow(x, z) * pow(y, z), pow(x * y, z)),
		rule(pow(pow(x, y), z), pow(x, y * z)),

		rule(hypot(zero, x), abs(x)),

		-- degenerate and reflexive cases
		rule(zero * x, zero),
		rule(zero / x, zero),
		rule(x - x, zero),
		rule(x - k, (-k) + x),
		rule(x / k, (one / k) * x),
		rule(y / (y / x), x),
		rule(zero - x, -x),
		rule(x / x, one),
		rule((-x) / x, negone),
		rule(x / (-x), negone),

		rule(min(x, x), x),
		rule(max(x, x), x),

		rule(k * abs(x), abs(k * x)),

		-- rate-aware reassociation: group low-clock (I,J,K) work together so it
		-- factors out of the high-clock signal (X,Y,Z) and feeds constant
		-- folding. Each fires only when checkVarRates confirms I,J,K are strictly
		-- lower rate than X,Y,Z.
		rule((x + j) + k, (j + k) + x),
		rule((x + j) - k, (j - k) + x),
		rule((x - j) + k, (k - j) + x),
		rule((x - j) - k, (-(j + k)) + x),
		rule((i * x + j) * k, ((k * i) * x) + (k * j)),
		rule((i * x - j) * k, ((k * i) * x) - (k * j)),
		rule((x - j) + (y - k), (x + y) - (j + k)),

		rule(k * ((i * x) + (j * y)), ((k * i) * x) + ((k * j) * y)),

		rule(j / (k * x), (j / k) / x),
		rule((j * x) / k, (j / k) * x),

		rule(j * (k / x), (j * k) / x),
		rule((j * x) * k, (j * k) * x),
		rule(k * (x / j), (k / j) * x),
		rule((x / j) / k, (one / (j * k)) * x),
		rule(z / (y / x), (z * x) / y),

		rule(j * x + k * (x * y), x * (j + k * y)),
		rule(j * x - k * (x * y), x * (j - k * y)),

		-- redundant applications
		rule(abs(-x), abs(x)),
		rule(abs(abs(x)), abs(x)),

		rule(floor(floor(x)), floor(x)),
		rule(ceil(floor(x)),  floor(x)),
		rule(round(floor(x)), floor(x)),
		rule(trunc(floor(x)), floor(x)),

		rule(floor(ceil(x)), ceil(x)),
		rule(ceil(ceil(x)),  ceil(x)),
		rule(round(ceil(x)), ceil(x)),
		rule(trunc(ceil(x)), ceil(x)),

		rule(floor(round(x)), round(x)),
		rule(ceil(round(x)),  round(x)),
		rule(round(round(x)), round(x)),
		rule(trunc(round(x)), round(x)),

		rule(floor(trunc(x)), trunc(x)),
		rule(ceil(trunc(x)),  trunc(x)),
		rule(round(trunc(x)), trunc(x)),
		rule(trunc(trunc(x)), trunc(x)),

		rule(max(max(x, y), y), max(x, y)),
		rule(max(max(x, y), x), max(x, y)),
		rule(min(min(x, y), y), min(x, y)),
		rule(min(min(x, y), x), min(x, y)),
		rule(max(min(x, y), max(x, y)), max(x, y)),
		rule(min(min(x, y), max(x, y)), min(x, y)),
		rule(max(x + pos, x), x + pos),
		rule(max(x - pos, x), x),
		rule(max(x + neg, x), x),
		rule(max(x - neg, x), x - neg),
		rule(max(k + min(x, y), k + max(x, y)), k + max(x, y)),
		rule(min(k + min(x, y), k + max(x, y)), k + min(x, y)),

		-- monotonic-function factoring: min/max of the same monotonic op pull
		-- the op out. The RHS rebuilds with the matched op (bound during match).
		rule(min(monoup(x), monoup(y)), monoup(min(x, y))),
		rule(max(monoup(x), monoup(y)), monoup(max(x, y))),
		rule(min(monodn(x), monodn(y)), monodn(max(x, y))),
		rule(max(monodn(x), monodn(y)), monodn(min(x, y))),
	]
}

let _rules = _buildRules();

-- Result of a successful match: the rule's RHS pattern + the variable bindings.
struct RewriteHit { rhs Pat; subs [Int]; }
fn _hit(rhs Pat, subs [Int]) Option<RewriteHit> = Option<RewriteHit>.some(RewriteHit { rhs: rhs, subs: subs });
fn _miss() Option<RewriteHit> = Option<RewriteHit>.none;

-- Public entry point. Try each rule against the candidate (kind, ins). Return
-- the matched rule's RHS pattern plus bindings; importer.x interprets the RHS.
fn tryRewriteMatch(ctx Ctx, kind NodeKind, ins [Int]) Option<RewriteHit> {
	var i = 0;
	let n = _rules length;
	while (i < n) {
		let r = _rules[i];
		match (_matchTop(ctx, r.lhs, kind, ins, _freshSubs())) {
			some(s): if (ctx _checkVarRates(s)) { return _hit(r.rhs, s); }
			none: {}
		}
		i = i + 1;
	}
	_miss()
}

-- Accessors for importer.x's RHS interpreter.
fn subsNode(subs [Int], id Int) Int = subs[id];
fn subsMonoUpOp(subs [Int]) Int = subs[VMONOUP];
fn subsMonoDnOp(subs [Int]) Int = subs[VMONODN];
