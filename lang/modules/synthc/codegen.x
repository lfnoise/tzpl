-- synthc/codegen.x
-- C++ source generation, ported from synthdef_cpp_codegen.cpp. M1 scope:
-- scalar and channel-loop emission (no SIMD), constants, unary/binary/compare/
-- cast/select, controls/inlets/outlets, and delays. Voicers, spectral chains,
-- control flow, vec ops, and SIMD are deferred to later milestones.
--
-- Output matches the C++ generator byte-for-byte (rewrites off) so it can be
-- differentially tested. Context (the Ctx, current loop channel count, current
-- tree root, init-vs-audio mode, indent) is threaded through dynamic-scope vars.

import synthdef.*;
import synthc.ir.*;
import synthc.opnames.*;

---------------------------------------------------------------------------
-- Small helpers

fn _tabs(n Int) String = n <= 0 ? "" : "\t" $ _tabs(n - 1);

fn cppType(t NumType) String = t numTypeStr;     -- i32/i64/f32/f64

---------------------------------------------------------------------------
-- Symbolic channel-index algebra (mirrors the C++ VarIndex + its operator
-- overloads in synthdef_cpp_codegen.cpp). Vec ops remap the loop channel index;
-- these constant simplifications are what make the emitted index expressions
-- byte-match. A VIdx is a constant, the opaque loop var ("i"), a negation, or a
-- binop. `synthc` only ever feeds "i" or "0" as the base index.

enum VIdx {
	vc(Int),
	vs(String),
	vneg(VIdx),
	vadd(VIdx, VIdx),
	vsub(VIdx, VIdx),
	vmul(VIdx, VIdx),
	vdiv(VIdx, VIdx),
	vrem(VIdx, VIdx),   -- C++ operator% -> infix `%`
	vmod(VIdx, VIdx),   -- C++ mod() -> synthdef::mod(a, b) (floored)
}

fn _viC(x VIdx) Bool = match (x) { vc(_): true; _: false; };
fn _viVal(x VIdx) Int = match (x) { vc(k): k; _: 0; };
fn _viZero(x VIdx) Bool = match (x) { vc(k): k == 0; _: false; };
fn _viOne(x VIdx) Bool = match (x) { vc(k): k == 1; _: false; };
fn _imod(x Int, y Int) Int = x - (x // y) * y;   -- remainder; index operands are >= 0

fn viOf(cel String) VIdx = (cel == "0") ? VIdx.vc(0) : VIdx.vs(cel);

fn viNeg(x VIdx) VIdx = x _viC ? VIdx.vc(0 - x _viVal) : VIdx.vneg(x);

fn viAdd(a VIdx, b VIdx) VIdx {
	if (a _viC && b _viC) { return VIdx.vc(a _viVal + b _viVal); }
	if (a _viZero) { return b; }
	if (b _viZero) { return a; }
	var x = a; var y = b;
	if (b _viC) { x = b; y = a; }      -- normalize constant to the left
	if (x _viC) {
		match (y) {
			vadd(ya, yb): { if (ya _viC) { return VIdx.vadd(VIdx.vc(x _viVal + ya _viVal), yb); } }
			_: {}
		}
	}
	VIdx.vadd(x, y)
}

fn viSub(a VIdx, b VIdx) VIdx {
	if (a _viC && b _viC) { return VIdx.vc(a _viVal - b _viVal); }
	if (a _viZero) { return viNeg(b); }
	if (b _viZero) { return a; }
	VIdx.vsub(a, b)
}

fn viMul(a VIdx, b VIdx) VIdx {
	if (a _viC && b _viC) { return VIdx.vc(a _viVal * b _viVal); }
	if (a _viZero) { return VIdx.vc(0); }
	if (b _viZero) { return VIdx.vc(0); }
	if (a _viOne) { return b; }
	if (b _viOne) { return a; }
	var x = a; var y = b;
	if (b _viC) { x = b; y = a; }
	if (x _viC) {
		match (y) {
			vmul(ya, yb): { if (ya _viC) { return VIdx.vmul(VIdx.vc(x _viVal * ya _viVal), yb); } }
			_: {}
		}
	}
	VIdx.vmul(x, y)
}

fn viDiv(a VIdx, b VIdx) VIdx {
	if (a _viC && b _viC) { return VIdx.vc(a _viVal // b _viVal); }
	if (a _viZero) { return VIdx.vc(0); }
	if (a _viOne) { return b; }
	if (b _viC && b _viVal == 1) { return a; }
	VIdx.vdiv(a, b)
}

fn viRem(a VIdx, b VIdx) VIdx {     -- C++ operator%
	if (a _viC && b _viC) { return VIdx.vc(_imod(a _viVal, b _viVal)); }
	if (a _viZero) { return VIdx.vc(0); }
	if (b _viOne) { return VIdx.vc(0); }
	VIdx.vrem(a, b)
}

fn viMod(a VIdx, b VIdx) VIdx {     -- C++ mod()
	if (a _viC && b _viC) { return VIdx.vc(_imod(a _viVal, b _viVal)); }
	if (a _viZero) { return VIdx.vc(0); }
	if (b _viOne) { return VIdx.vc(0); }
	VIdx.vmod(a, b)
}

fn vxstr(x VIdx) String = match (x) {
	vc(k):      k toString;
	vs(s):      s;
	vneg(a):    genUnopStr(UnaryOp.neg, false, vxstr(a));
	vadd(a, b): genBinopStr(BinaryOp.add, false, vxstr(a), vxstr(b));
	vsub(a, b): genBinopStr(BinaryOp.sub, false, vxstr(a), vxstr(b));
	vmul(a, b): genBinopStr(BinaryOp.mul, false, vxstr(a), vxstr(b));
	vdiv(a, b): genBinopStr(BinaryOp.div, false, vxstr(a), vxstr(b));
	vrem(a, b): "(" $ vxstr(a) $ " % " $ vxstr(b) $ ")";
	vmod(a, b): "mod(" $ vxstr(a) $ ", " $ vxstr(b) $ ")";   -- i64 Mod op compile string (unqualified)
};

fn _shape(chans Int) String = chans > 1 ? "[%^]" fmt(chans) : "";

fn _typeTag(t NumType) String = match (t.0) {
	1: "tzpl_kI32";  2: "tzpl_kI64";  4: "tzpl_kF32";  8: "tzpl_kF64";  _: "tzpl_kF32";
};

fn _rateCode(r Rate) String = match (r) {
	constant: "tzpl_constRate";  init: "tzpl_initRate";  reset: "tzpl_resetRate";
	event: "tzpl_eventRate";  audio: "tzpl_audioRate";
};

---------------------------------------------------------------------------
-- Node classification used by codegen

fn _isRootNode(ctx Ctx, n NIdx) Bool {
	let t = ctx.treeOf[n];
	t != NONE && ctx.trees[t].root == n
}

fn _isInstVar(ctx Ctx, n NIdx) Bool = ctx.cut[n] isInstVarCut;

fn _numInlets(ctx Ctx) Int = ctx.inlets length;

-- genVarName: the C++ identifier (or accessor) for a node's value.
fn _varName(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(_, _):    "p->c%^" fmt(ctx.serial[n]);
	inletK(_, sn):     "((%^**)p->inlets)[%^]" fmt(cppType(ctx.typ[n]), sn);
	outletK(_, sn):    "p->outlets[%^]" fmt(sn + ctx.inlets length);
	-- Flat voice mode: a body instance var is per-voice (p->voice_v<serial>); a
	-- body temp is a local v<serial>[maxVoices] array.
	_:                 (`cgVoiceCount > 0 && ctx.graphOf[n] == `cgVoiceGraph)
	                       ? ((ctx _isInstVar(n)) ? "p->voice_v%^" fmt(ctx.serial[n]) : "v%^" fmt(ctx.serial[n]))
	                       : ((ctx _isInstVar(n)) ? "p->v%^" fmt(ctx.serial[n]) : "v%^" fmt(ctx.serial[n]));
};

fn _varDeclName(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(_, _):  "c%^" fmt(ctx.serial[n]);
	_:               "v%^" fmt(ctx.serial[n]);
};

fn _indexWrap(chans Int, loopChans Int, cel String) String =
	chans < loopChans
		-- chans is a power of two (or 0); the mask is (usize)(chans-1). For
		-- chans 0 that wraps to SIZE_MAX (the C++ usize formatting), not -1.
		? "[%^&%^]" fmt(cel, chans == 0 ? "18446744073709551615" : (chans - 1) toString)
		: "[%^]" fmt(cel);

-- genVarRef: a reference to a node's value at channel `cel` (scalar emits no index).
fn _varRef(ctx Ctx, n NIdx, cel String) String {
	let s = _varName(ctx, n);
	-- A root consumed only within its own loop is a scalar local (no index) --
	-- this holds in flat voice mode too, so check it BEFORE per-voice indexing.
	if (ctx _isRootNode(n)) {
		if (ctx.trees[ctx.treeOf[n]].loopOf != NONE && _consumedInLoop(ctx, ctx.treeOf[n])) {
			return s;
		}
	}
	-- Flat-voice SIMD reference (M5.3): mirrors the C++ inSimdMode && inFlatVoiceMode
	-- genVarRef. A voice-local SoA value loads/gathers by voice/chan; a non-voice
	-- value splats (mono) or loads/gathers with channel wrap. `*(Tx*)(...)` is a
	-- valid lvalue, so the store form works unchanged.
	if (`cgSimdW > 0 && `cgVoiceCount > 0) {
		let w = `cgSimdW;
		let t = ctx.typ[n];
		let C = ctx.chans[n];
		let lc = `cgLoopChans;
		let shift = `cgFlatChanShift;
		-- Voice-local = a voicer-subgraph inst-var or temp-var (mirror the C++
		-- isVoiceLocal). A constant/control in the voice graph is NOT voice-local --
		-- it is a non-voice value, wrapped by its own channel count.
		let isVoiceLocal = ctx.graphOf[n] == `cgVoiceGraph
			&& (ctx _isInstVar(n) || ctx.cut[n] isTempVarCut);
		if (isVoiceLocal) {
			-- voice-local SoA array (voice_v / inst-var / temp)
			if (C == lc || (C == 1 && shift == 0)) {
				return _simdLoad(t, w, "&%^[%^]" fmt(s, cel));
			}
			if (C == 1) {
				let log2Width = w == 4 ? 2 : 1;
				if (shift >= log2Width) {
					return _simdSplat(t, w, "%^[%^]" fmt(s, _flatSimdVoiceIdx(cel, 0, shift)));
				}
				var parts [String] = [];
				var j = 0;
				while (j < w) { parts push!("%^[%^]" fmt(s, _flatSimdVoiceIdx(cel, j, shift))); j = j + 1; }
				return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
			}
			-- multi-chan voice var, fewer chans than loop: gather with wrap
			var parts [String] = [];
			var j = 0;
			while (j < w) { parts push!("%^[%^]" fmt(s, _flatSimdVarWrapIdx(cel, j, shift, C))); j = j + 1; }
			return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
		}
		-- non-voice var in SIMD flat mode
		if (C == 1) { return _simdSplat(t, w, s); }
		if (C >= w) {
			if (cel == "0") { return _simdLoad(t, w, "&%^[%^]" fmt(s, 0 & (C - 1))); }
			return _simdLoad(t, w, "&%^[%^ & %^]" fmt(s, cel, C - 1));
		}
		var parts [String] = [];
		var j = 0;
		while (j < w) {
			let idx = cel == "0" ? (j & (C - 1)) toString : "(%^ + %^) & %^" fmt(cel, j, C - 1);
			parts push!("%^[%^]" fmt(s, idx));
			j = j + 1;
		}
		return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
	}
	-- SIMD reference (M5.1): a referenced value is a vector load `*(T x W*)(&acc[cel])`
	-- (or `*(T x W*)(&inlet[sn][cel])`), and `*(Tx*)(...)` is also a valid store
	-- lvalue, so genTree's `lhs = rhs` form works unchanged. Scalars splat.
	if (`cgSimdW > 0) {
		let w = `cgSimdW;
		let t = ctx.typ[n];
		match (ctx.kind[n]) {
			inletK(_, sn): {
				if (ctx.chans[n] == 1) { return _simdSplat(t, w, "((%^**)p->inlets)[%^]" fmt(cppType(t), sn)); }
				return _simdLoad(t, w, "&((%^**)p->inlets)[%^][%^]" fmt(cppType(t), sn, cel));
			}
			_: {}
		}
		if (ctx.chans[n] == 1) { return _simdSplat(t, w, s); }
		let C = ctx.chans[n];
		if (C == w) { return _simdLoad(t, w, "&%^[0]" fmt(s)); }
		if (C > w) {
			if (C < `cgLoopChans) {
				-- Narrower than the loop but wider than SIMD: cyclic broadcast.
				-- chans is a power of two, so wrap the load offset with & (C-1).
				if (cel == "0") { return _simdLoad(t, w, "&%^[0]" fmt(s)); }
				return _simdLoad(t, w, "&%^[%^ & %^]" fmt(s, cel, C - 1));
			}
			return _simdLoad(t, w, "&%^[%^]" fmt(s, cel));
		}
		-- Narrower than the SIMD width (e.g. 2-chan into 4-wide): expand element-wise.
		var parts [String] = [];
		var j = 0;
		while (j < w) { parts push!("%^[%^]" fmt(s, j % C)); j = j + 1; }
		return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
	}
	-- Flat voice mode: per-voice SoA indexing (mirrors the C++ flat-voice
	-- genVarRef). Voice values index by voice/chan; the voicer output (a non-voice
	-- temp read from the body) wraps normally.
	if (`cgVoiceCount > 0) {
		let isVoice = ctx.graphOf[n] == `cgVoiceGraph;
		let C = ctx.chans[n];
		if (ctx _isInstVar(n) && isVoice) {
			return s $ _flatVoiceIdx(ctx, C, cel);
		} else if (ctx _isInstVar(n)) {
			if (`cgFlatChanShift == 0) { return s; }
			let m = (C == `cgLoopChans) ? `cgFlatChanMask : C - 1;
			return "%^[%^ & %^]" fmt(s, cel, m);
		} else if (isVoice) {
			-- voice temp (local SoA array): mono -> by voice, else full-width [i]
			if (C == 1) { return `cgFlatChanShift == 0 ? "%^[%^]" fmt(s, cel) : "%^[%^ >> %^]" fmt(s, cel, `cgFlatChanShift); }
			return "%^[%^]" fmt(s, cel);
		} else {
			-- non-voice temp (e.g. the VoicerExpr output)
			if (C > 1) { return s $ _indexWrap(C, `cgLoopChans, cel); }
			return s;
		}
	}
	let isInlet = match (ctx.kind[n]) { inletK(_, _): true; _: false; };
	if (ctx.chans[n] == 1 && !isInlet) { return s; }
	s $ _indexWrap(ctx.chans[n], `cgLoopChans, cel)
}

-- A tree is "consumed in loop" if every consumer of its root is in the same
-- loop. Such roots are emitted as local temporaries, not struct members.
fn _consumedInLoop(ctx Ctx, treeIdx Int) Bool {
	let root = ctx.trees[treeIdx].root;
	-- A non-temp-var root (inst var, control, sink, etc.) is never a local temp
	-- consumed within its own loop -- it persists across loops. Mirrors the C++
	-- ExprTree::is_consumed_in_loop guard; without it a control read emitted as
	-- `p->vN` would be wrongly given a local-decl type prefix (`f32 p->vN`).
	if (!(ctx.cut[root] isTempVarCut)) { return false; }
	let loop = ctx.trees[treeIdx].loopOf;
	for (c : ctx.consumers[root]) {
		let ct = ctx.treeOf[c];
		if (ct == NONE) { return false; }
		if (ctx.trees[ct].loopOf != loop) { return false; }
	}
	true
}

---------------------------------------------------------------------------
-- Expression emission (genExpr + the inline visitor)

fn _isF32(t NumType) Bool = t.0 == 4;

-- A node's slot may be wider than the type its operands compute at -- e.g. an
-- init-rate `1/(t*fs())` whose result is f64 but whose folded operands are f32.
-- In scalar code C++ closes that gap with an implicit conversion on assignment
-- (`f64 v = (f32expr);`). SIMD vector types have no implicit conversion, so the
-- store `*(f64x4*)slot = (f32x4)expr` is a hard error; the cast must be explicit.
-- This wraps `e` in `convert<wantT>(...)` when, under SIMD, the value computes at
-- `haveT` but is stored/used as `wantT`. A no-op when they agree (the usual case)
-- or in scalar mode, so existing output is byte-for-byte unchanged.
fn _simdWiden(wantT NumType, haveT NumType, e String) String =
	(`cgSimdW > 0 && wantT != haveT) ? "convert<%^>(%^)" fmt(cppType(wantT), e) : e;

-- Inline a node's value expression at channel `cel`.
fn genExpr(ctx Ctx, n NIdx, cel String) String {
	-- A loop variable (VarExpr) always emits as the bare C++ loop-var name,
	-- never as `p->vN` -- even though it is cut Graph (cross-graph reference).
	match (ctx.kind[n]) { varK(name): { return name; } _: {} }
	let inInit Bool = `cgInInit;
	-- In reset loops, NoteParam/Control roots bypass root factoring: their
	-- materialized per-sample copies are zeroed/stale at note-on, so read the
	-- live sources directly (mirrors inResetMode in the C++ genExpr).
	let inReset Bool = `cgInResetMode;
	let resetLeaf = inReset && (match (ctx.kind[n]) {
		noteParamK(_, _, _): true;
		control(_, _, _, _): true;
		_: false;
	});
	if (!inInit && !resetLeaf && ctx _isRootNode(n) && n != `cgRoot) {
		return _varRef(ctx, n, cel);
	}
	_inlineExpr(ctx, n, cel)
}

fn _constLiteral(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(v, _): constStr(v, ctx.typ[n]);
	_:              "0";
};

-- The RNG state reference for a random node. In flat voice mode a voicer-body
-- RNG is per-voice: p->voice_rgen<g>[voice], voice = cel >> flatChanShift.
fn _rngStateRef(ctx Ctx, n NIdx, cel String) String {
	let g = ctx.graphOf[n];
	if (`cgVoiceCount > 0 && g == `cgVoiceGraph) {
		let vi = `cgFlatChanShift == 0 ? cel : "%^ >> %^" fmt(cel, `cgFlatChanShift);
		return "p->voice_rgen%^[%^]" fmt(g, vi);
	}
	"p->rgen%^" fmt(g)
}

fn _inlineExpr(ctx Ctx, n NIdx, cel String) String {
	let ins = ctx.ins[n];
	match (ctx.kind[n]) {
		constant(v, _): (v constSize == 1)
			? (`cgSimdW > 0 ? _simdSplat(ctx.typ[n], `cgSimdW, constStr(v, ctx.typ[n])) : constStr(v, ctx.typ[n]))
			: _varRef(ctx, n, cel);
		sampleRate:     (`cgSimdW > 0)
			? _simdSplat(ctx.typ[n], `cgSimdW, (ctx.typ[n].0 == 8) ? "p->fs" : "(%^)(p->fs)" fmt(cppType(ctx.typ[n])))
			: ((ctx.typ[n].0 == 8) ? "p->fs" : "(%^)(p->fs)" fmt(cppType(ctx.typ[n])));
		sampleDur:      (`cgSimdW > 0)
			? _simdSplat(ctx.typ[n], `cgSimdW, (ctx.typ[n].0 == 8) ? "p->sd" : "(%^)(p->sd)" fmt(cppType(ctx.typ[n])))
			: ((ctx.typ[n].0 == 8) ? "p->sd" : "(%^)(p->sd)" fmt(cppType(ctx.typ[n])));
		-- Shared input: the table stores volatile f32; cast to the inferred type.
		sharedInK(slot, _): (`cgSimdW > 0)
			? _simdSplat(ctx.typ[n], `cgSimdW, "(%^)(tzpl_sharedInput->vals[%^])" fmt(cppType(ctx.typ[n]), slot))
			: (ctx.typ[n] _isF32
				? "tzpl_sharedInput->vals[%^]" fmt(slot)
				: "(%^)(tzpl_sharedInput->vals[%^])" fmt(cppType(ctx.typ[n]), slot));
		control(_, _, sn, _): (ctx.chans[n] == 1)
			? "(*(%^*)p->controls[%^])" fmt(cppType(ctx.typ[n]), sn)
			: "((%^*)p->controls[%^])[%^]" fmt(cppType(ctx.typ[n]), sn, cel);
		-- NoteParam: gather the voice's parameter from the voicer_params matrix.
		-- cel is the voice index in flat voice mode. SIMD gathers per lane (M5.3).
		noteParamK(_, _, psn): (`cgSimdW > 0)
			? _noteParamSimd(ctx.typ[n], ctx.chans[n], psn, cel)
			: "p->voicer_params[%^][%^]" fmt(cel, psn);
		-- The packed FFT frame buffer the body reads/writes each sample.
		spectralFrameK(_): "p->spec%^_frame[%^]" fmt(ctx _frameChainSerial(n), cel);
		-- Inlet's own value: in SIMD this is the inlet VISITOR form (a bare-pointer
		-- vector load when chans==width, else &accessor[cel]); as a referenced
		-- value it routes through _varRef (always &accessor[cel]).
		inletK(_, sn): {
			if (`cgSimdW > 0) {
				let t = ctx.typ[n]; let w = `cgSimdW; let c = ctx.chans[n];
				if (c == w) { _simdLoad(t, w, "((%^**)p->inlets)[%^]" fmt(cppType(t), sn)) }
				else if (c > w) { _simdLoad(t, w, "&((%^**)p->inlets)[%^][%^]" fmt(cppType(t), sn, cel)) }
				else { _simdSplat(t, w, "((%^**)p->inlets)[%^][0]" fmt(cppType(t), sn)) }
			} else { _varRef(ctx, n, cel) }
		}
		unopK(op):      _simdWiden(ctx.typ[n], ctx.typ[ins[0]],
		                    genUnopStr(op, ctx.typ[n] _isF32, genExpr(ctx, ins[0], cel), `cgSimdW > 0));
		binopK(op):     _simdWiden(ctx.typ[n], commonType(ctx.typ[ins[0]], ctx.typ[ins[1]]),
		                    genBinopStr(op, ctx.typ[n] _isF32, genExpr(ctx, ins[0], cel), genExpr(ctx, ins[1], cel), `cgSimdW > 0));
		compareopK(op): genCompareStr(op, genExpr(ctx, ins[0], cel), genExpr(ctx, ins[1], cel));
		castopK(ct):    (`cgSimdW > 0)
			? "convert<%^>(%^)" fmt(cppType(ct), genExpr(ctx, ins[0], cel))
			: "%^(%^)" fmt(cppType(ct), genExpr(ctx, ins[0], cel));
		selectK:        _inlineSelect(ctx, n, cel);
		reduceK(op, cols): _genReduce(ctx, n, op, cols, cel);
		vecK(vop):      _genVec(ctx, n, vop, cel);
		urandK(_):      "urand%^(%^)" fmt(ctx.typ[n] _isF32 ? "f" : "", _rngStateRef(ctx, n, cel));
		birandK(_):     "birand%^(%^)" fmt(ctx.typ[n] _isF32 ? "f" : "", _rngStateRef(ctx, n, cel));
		rand64K(_):     "rand64(%^)" fmt(_rngStateRef(ctx, n, cel));
		bufFixReadK(bi, index, rc, sc): _bufFixReadExpr(ctx, n, bi, index, rc, sc, cel);
		bufVarReadK(bi, ip, rc, sc):    _bufVarReadExpr(ctx, n, bi, ip, rc, sc, cel);
		bufLengthK(bi):                 _bufLengthExpr(ctx, n, bi);
		bankFixReadK(index, rc, sc):    _bankFixReadExpr(ctx, n, index, rc, sc, cel);
		bankVarReadK(ip, rc, sc):       _bankVarReadExpr(ctx, n, ip, rc, sc, cel);
		bankRootKeyK:                   _bankSlot(ctx, ctx.ins[n][0], "rootKey", cel);
		bankSampleRateK:                _bankSlot(ctx, ctx.ins[n][0], "sr", cel);
		bankLengthK:                    _bankSlot(ctx, ctx.ins[n][0], "len", cel);
		bankLoopStartK:                 _bankSlot(ctx, ctx.ins[n][0], "loopStart", cel);
		bankLoopEndK:                   _bankSlot(ctx, ctx.ins[n][0], "loopEnd", cel);
		bankHasLoopK:                   _bankSlot(ctx, ctx.ins[n][0], "hasLoop", cel);
		delayFixReadK(d, k): _delayFixReadExpr(ctx, n, d, k, cel);
		delayVarReadK(d, ip): _delayVarReadExpr(ctx, n, d, ip, cel);
		_:              "/* FIXME %^ */" fmt(nodeStr(ctx, n));
	}
}

-- ReduceExpr codegen: reduce_rows<rows, cols>(cel, inputArray, lambda). The
-- input is materialized in a separate loop (reduceK forces a temp var), so we
-- pass its array name; rows = inputChans / cols.
fn _genReduce(ctx Ctx, n NIdx, op BinaryOp, cols Int, cel String) String {
	let inp = ctx.ins[n][0];
	let rows = ctx.chans[inp] // cols;
	let ty = cppType(ctx.typ[n]);
	let body = genBinopStr(op, ctx.typ[n] _isF32, "z", "x");
	let lambda = "[](%^ z, %^ x){ return %^; }" fmt(ty, ty, body);
	"reduce_rows<%^, %^>(%^, %^, %^)" fmt(rows, cols, cel, _varName(ctx, inp), lambda)
}

-- Vec op codegen: remap the channel index, then re-emit the (materialized)
-- input at that index. The index algebra (VIdx) replicates the C++ VarIndex
-- simplifications so the emitted subscript byte-matches.
fn _transposeIdx(c VIdx, inChans Int, cols Int) VIdx {
	let rows = inChans // cols;
	viAdd(viMul(viRem(c, VIdx.vc(rows)), VIdx.vc(cols)), viDiv(c, VIdx.vc(rows)))
}
fn _rotateIdx(ctx Ctx, n NIdx, c VIdx, inChans Int) VIdx {
	let nIn = ctx.ins[n][1];
	let r = "isize(" $ genExpr(ctx, nIn, "0") $ ")";
	viMod(viAdd(c, VIdx.vs(r)), VIdx.vc(inChans))
}
fn _genVec(ctx Ctx, n NIdx, vop VecOp, cel String) String {
	let inp = ctx.ins[n][0];
	let inChans = ctx.chans[inp];
	-- at(a, i): a dynamic gather from the materialized array a by the index signal i
	-- at channel cel, wrapped mod a.chans. Mirrors the C++ VecAt visitor.
	match (vop) {
		at: { return "%^[synthdef::mod(isize(%^), isize(%^))]"
			fmt(_varName(ctx, inp), genExpr(ctx, ctx.ins[n][1], cel), inChans); }
		_: {}
	}
	let c = viOf(cel);
	let idx = match (vop) {
		take(_):         c;
		drop(num):       viAdd(c, VIdx.vc(num));
		stride(num):     viMul(c, VIdx.vc(num));
		stutter(num):    viDiv(c, VIdx.vc(num));
		ncyc(_):         viMod(c, VIdx.vc(inChans));
		reverse:         viSub(VIdx.vc(inChans - 1), c);
		transpose(cols): _transposeIdx(c, inChans, cols);
		rotate:          _rotateIdx(ctx, n, c, inChans);
		_:               c;
	};
	genExpr(ctx, inp, idx vxstr)
}

-- put(a, i, v): copy a into the output array, then scatter v into it at the
-- (mod-wrapped) index positions. A gets_own_loop block (mirror the C++ VecPut).
-- The own-loop block forms end WITHOUT a trailing newline; genLoop's `$ "\n"`
-- supplies it (matching the control-flow convention, so byte-output lines up).
fn _genVecPut(ctx Ctx, root NIdx) String {
	let ins = ctx.ins[root];
	let ty = cppType(ctx.typ[root]);
	let outv = _varName(ctx, root);
	let a = ins[0]; let i = ins[1]; let v = ins[2];
	let aChans = ctx.chans[a];
	let putCount = min(ctx.chans[i], ctx.chans[v]);
	let t = _tabs(`cgIndent); let t1 = _tabs(`cgIndent + 1);
	var parts [String] = [];
	parts push!(t $ "memcpy(%^, %^, %^ * sizeof(%^));" fmt(outv, _varName(ctx, a), aChans, ty));
	parts push!(t $ "for (usize _j = 0; _j < %^; ++_j) {" fmt(putCount));
	parts push!(t1 $ "%^[synthdef::mod(isize(%^[_j]), isize(%^))] = %^[_j];" fmt(outv, _varName(ctx, i), aChans, _varName(ctx, v)));
	parts push!(t $ "}");
	parts join("\n")
}

-- join(inputs): concatenate inputs into the output array (scalar inputs are plain
-- assignments, multichannel use memcpy); zero-fill any power-of-two padding.
-- A gets_own_loop block (mirror the C++ VecJoin).
fn _genVecJoin(ctx Ctx, root NIdx) String {
	let ins = ctx.ins[root];
	let ty = cppType(ctx.typ[root]);
	let outv = _varName(ctx, root);
	let t = _tabs(`cgIndent);
	var parts [String] = [];
	var offset = 0;
	for (x : ins) {
		let xc = ctx.chans[x];
		if (xc == 1) {
			parts push!(t $ "%^[%^] = %^;" fmt(outv, offset, _varName(ctx, x)));
		} else {
			parts push!(t $ "memcpy(%^ + %^, %^, %^ * sizeof(%^));" fmt(outv, offset, _varName(ctx, x), xc, ty));
		}
		offset = offset + xc;
	}
	if (offset < ctx.chans[root]) {
		parts push!(t $ "memset(%^ + %^, 0, %^ * sizeof(%^));" fmt(outv, offset, ctx.chans[root] - offset, ty));
	}
	parts join("\n")
}

fn _inlineSelect(ctx Ctx, n NIdx, cel String) String {
	let ins = ctx.ins[n];
	if (ins length == 3) {
		"(%^ ? %^ : %^)" fmt(genExpr(ctx, ins[0], cel), genExpr(ctx, ins[2], cel), genExpr(ctx, ins[1], cel))
	} else {
		-- N-way select: sel(selector, std::array<T,N>{ (T)(v1), (T)(v2), ... }).
		-- Each value is cast to the array element type (the front end can hand us a
		-- bool/int/float mix that would trip -Wnarrowing). Mirrors the C++
		-- SelectExpr visitor's multi-line layout, indented by the current indent.
		let ty = cppType(ctx.typ[n]);
		var s = "sel(%^, std::array<%^,%^>{\n" fmt(genExpr(ctx, ins[0], cel), ty, ins length - 1);
		var i = 1;
		while (i < ins length) {
			if (i > 1) { s = s $ ",\n"; }
			s = s $ _tabs(`cgIndent + 1) $ "(%^)(%^)" fmt(ty, genExpr(ctx, ins[i], cel));
			i = i + 1;
		}
		s $ "})\n" $ _tabs(`cgIndent)
	}
}

---------------------------------------------------------------------------
-- Delays

fn _delayDIndex(ctx Ctx, d Int, n NIdx, cel String) String =
	ctx.delays[d].chans > 1 ? _indexWrap(ctx.chans[n], `cgLoopChans, cel) : "";

-- Ring-buffer index mask. A compile-time-sized (fixed) delay uses the constant
-- allocSize-1; a runtime-sized (maxDelay-bound) delay stores its mask in the
-- p->d{serial}_mask field. Mirrors the C++ `allocSize > 1 ? allocSize-1 : mask`.
fn _delayMask(info DelayInfo) String =
	info.allocSize > 1 ? "%^" fmt(info.allocSize - 1) : "p->d%^_mask" fmt(info.serial);

-- The voice index expression for a per-voice ring head/mask subscript
-- ((cel >> flatChanShift), parenthesized to match the C++); the buffer itself is
-- indexed by voice/chan via _flatVoiceIdx.
fn _voiceIdxStr(cel String) String = `cgFlatChanShift == 0 ? cel : "(%^ >> %^)" fmt(cel, `cgFlatChanShift);
fn _vDelayBuf(ctx Ctx, info DelayInfo, cel String) String =
	"p->voice_d%^%^" fmt(info.serial, _flatVoiceIdx(ctx, info.chans, cel));
fn _vDelayWrpos(info DelayInfo, cel String) String =
	"p->voice_d%^_wrpos[%^]" fmt(info.serial, _voiceIdxStr(cel));
fn _vDelayMask(info DelayInfo, cel String) String =
	info.allocSize > 1 ? "%^" fmt(info.allocSize - 1)
	                   : "p->voice_d%^_mask[%^]" fmt(info.serial, _voiceIdxStr(cel));

-- Per-voice 1-sample / ring delay read in flat-voice SIMD (M5.3). Mirrors the
-- flat-voice DelayFixRead visitor: a SoA voice delay loads/splats/gathers by
-- voice/chan exactly like a voice var; a ring gathers per lane with per-voice wrpos.
fn _voiceDelayFixReadSimd(ctx Ctx, n NIdx, info DelayInfo, k Int, cel String) String {
	let w = `cgSimdW;
	let t = ctx.typ[n];
	let ser = info.serial;
	let dchans = info.chans;
	let lc = `cgLoopChans;
	let shift = `cgFlatChanShift;
	if (info.allocSize == 1) {
		if (dchans == lc || (dchans == 1 && shift == 0)) {
			return _simdLoad(t, w, "&p->voice_d%^[%^]" fmt(ser, cel));
		}
		if (dchans == 1) {
			let log2Width = w == 4 ? 2 : 1;
			if (shift >= log2Width) {
				return _simdSplat(t, w, "p->voice_d%^[%^]" fmt(ser, _flatSimdVoiceIdx(cel, 0, shift)));
			}
			var parts [String] = [];
			var j = 0;
			while (j < w) { parts push!("p->voice_d%^[%^]" fmt(ser, _flatSimdVoiceIdx(cel, j, shift))); j = j + 1; }
			return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
		}
		var parts [String] = [];
		var j = 0;
		while (j < w) { parts push!("p->voice_d%^[%^]" fmt(ser, _flatSimdBufIdx(cel, j, shift, dchans))); j = j + 1; }
		return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
	}
	var parts [String] = [];
	var j = 0;
	while (j < w) {
		let vi = _flatSimdVoiceIdx(cel, j, shift);
		let bi = _flatSimdBufIdx(cel, j, shift, dchans);
		let mask = info.allocSize > 1 ? "%^" fmt(info.allocSize - 1) : "p->voice_d%^_mask[%^]" fmt(ser, vi);
		parts push!("p->voice_d%^[%^][(p->voice_d%^_wrpos[%^] - u64(%^)) & %^]" fmt(ser, bi, ser, vi, k, mask));
		j = j + 1;
	}
	"%^{%^}" fmt(_simdType(t, w), parts join(", "))
}

fn _delayFixReadExpr(ctx Ctx, n NIdx, d Int, k Int, cel String) String {
	let info = ctx.delays[d];
	if (ctx _isVoiceDelay(d)) {
		if (`cgSimdW > 0) { return _voiceDelayFixReadSimd(ctx, n, info, k, cel); }
		-- Per-voice 1-sample delay: a single value per voice/chan slot.
		if (info.allocSize == 1) { return _vDelayBuf(ctx, info, cel); }
		-- Per-voice ring buffer: index by the voice's write head.
		return "%^[(%^ - u64(%^)) & %^]" fmt(_vDelayBuf(ctx, info, cel), _vDelayWrpos(info, cel), k, _vDelayMask(info, cel));
	}
	-- SIMD delay read (M5.2). 1-sample (z1): a 1-chan delay splats; a delay at
	-- least as wide as the vector is a contiguous load; a narrower one gathers with
	-- channel wrap. A ring buffer gathers per lane from its per-channel ring (a
	-- 1-chan ring reads one value and splats). Mirrors the C++ DelayFixRead visitor.
	if (`cgSimdW > 0) {
		let w = `cgSimdW;
		let t = ctx.typ[n];
		let ser = info.serial;
		let dchans = info.chans;
		let lc = `cgLoopChans;
		if (info.allocSize == 1) {
			if (dchans == 1) { return _simdSplat(t, w, "p->d%^" fmt(ser)); }
			if (dchans >= w) { return _simdLoad(t, w, "&p->d%^[%^]" fmt(ser, cel)); }
			var parts [String] = [];
			var j = 0;
			while (j < w) { parts push!("p->d%^[%^]" fmt(ser, _simdLaneChanIdx(cel, j, dchans, lc))); j = j + 1; }
			return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
		}
		let mask = _delayMask(info);
		if (dchans == 1) {
			return _simdSplat(t, w, "p->d%^[(p->d%^_wrpos - u64(%^)) & %^]" fmt(ser, ser, k, mask));
		}
		var parts [String] = [];
		var j = 0;
		while (j < w) {
			let ci = _simdLaneChanIdx(cel, j, dchans, lc);
			parts push!("p->d%^[%^][(p->d%^_wrpos - u64(%^)) & %^]" fmt(ser, ci, ser, k, mask));
			j = j + 1;
		}
		return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
	}
	let di = _delayDIndex(ctx, d, n, cel);
	if (info.allocSize == 1) {
		"p->d%^%^" fmt(info.serial, di)
	} else {
		"p->d%^%^[(p->d%^_wrpos - u64(%^)) & %^]" fmt(info.serial, di, info.serial, k, _delayMask(info))
	}
}

-- A scalar (single-channel) delay-time offset is emitted in scalar mode at lane 0;
-- the dynvar shadow restores `cgSimdW on return. Mirrors the C++ inSimdMode toggle.
fn _genDelayOffScalar(ctx Ctx, e NIdx) String {
	var `cgSimdW Int = 0;
	genExpr(ctx, e, "0")
}

-- A single scalar delay read (the C++ genRead lambda): interpNone indexes the ring
-- directly; an interpolating mode calls the tzpl_delay_<kernel> helper.
fn _delayVarGenRead(ip Interpolation, buf String, wrpos String, mask String, off String) String = match (ip) {
	none: "%^[(%^ - u64(%^)) & %^]" fmt(buf, wrpos, off, mask);
	_:    "tzpl_delay_%^(%^, %^, %^, %^)" fmt(ip toLispString, buf, wrpos, mask, off);
};

-- Tap offsets (relative to floor(delay)) per interpolation kernel; mirrors the C++
-- `TI` table. The count is the tap array length.
fn _interpTaps(ip Interpolation) [Int] = match (ip) {
	linear:   [0, -1];
	cubic:    [1, 0, -1, -2];
	lagrange: [3, 2, 1, 0, -1, -2, -3, -4];
	sinc:     [3, 2, 1, 0, -1, -2, -3, -4];
	_:        [0];
};

fn _interpKernel(ip Interpolation) String = match (ip) {
	linear:   "tzpl_interp_linear";
	cubic:    "tzpl_interp_cubic";
	lagrange: "tzpl_interp_lagrange";
	_:        "";
};

-- The per-lane sinc coefficient block (after the tap-gather, before the return).
fn _genSimdSincCoeffs(w Int, scalarOff Bool, stype String, vtype String) String {
	var r = "";
	if (scalarOff) {
		r = r $ "auto _sc = tzpl_sinc_coeffs(double(%^(_off - %^(_di)))); " fmt(stype, stype);
		var k = 0;
		while (k < 8) { r = r $ "%^ _c%^ = (%^)(%^(_sc.c[%^])); " fmt(vtype, k, vtype, stype, k); k = k + 1; }
	} else {
		var j = 0;
		while (j < w) { r = r $ "auto _sc%^ = tzpl_sinc_coeffs(double(%^(_off[%^]) - %^(_di%^))); " fmt(j, stype, j, stype, j); j = j + 1; }
		var k = 0;
		while (k < 8) {
			r = r $ "%^ _c%^ = %^{" fmt(vtype, k, vtype);
			var j2 = 0;
			while (j2 < w) {
				if (j2 > 0) { r = r $ ", "; }
				r = r $ "%^(_sc%^.c[%^])" fmt(stype, j2, k);
				j2 = j2 + 1;
			}
			r = r $ "}; ";
			k = k + 1;
		}
	}
	r $ "return tzpl_interp_sinc(_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7, _c0, _c1, _c2, _c3, _c4, _c5, _c6, _c7); "
}

-- The SIMD interpolation lambda (mirror genSimdInterp). Gathers per-lane tap vectors
-- from the ring then calls the vector interp kernel. bufs/wrposs/masks are per-lane
-- expression strings (so this serves both the non-flat and flat-voice gathers). A
-- scalar offset uses one `_di` and casts frac; a vector offset uses per-lane `_di{j}`.
fn _genSimdInterp(t NumType, w Int, ip Interpolation,
                  bufs [String], wrposs [String], masks [String],
                  offExpr String, scalarOff Bool) String {
	let vtype = _simdType(t, w);
	let stype = cppType(t);
	let taps = _interpTaps(ip);
	var r = "[&]() -> %^ { " fmt(vtype);
	-- offset cache + per-lane integer delays
	if (scalarOff) {
		r = r $ "auto _off = %^; u64 _di = u64(_off); " fmt(offExpr);
	} else {
		r = r $ "auto _off = %^; " fmt(offExpr);
		var j = 0;
		while (j < w) { r = r $ "u64 _di%^ = u64(_off[%^]); " fmt(j, j); j = j + 1; }
	}
	-- per-tap gather vectors
	var ti = 0;
	while (ti < taps length) {
		let toff = taps[ti];
		r = r $ "%^ _s%^ = %^{" fmt(vtype, ti, vtype);
		var j = 0;
		while (j < w) {
			if (j > 0) { r = r $ ", "; }
			let di = scalarOff ? "_di" : "_di%^" fmt(j);
			let idx = toff > 0 ? "(%^ - %^ + %^) & %^" fmt(wrposs[j], di, toff, masks[j])
			        : (toff < 0 ? "(%^ - %^ - %^) & %^" fmt(wrposs[j], di, -toff, masks[j])
			        :             "(%^ - %^) & %^" fmt(wrposs[j], di, masks[j]));
			r = r $ "%^[%^]" fmt(bufs[j], idx);
			j = j + 1;
		}
		r = r $ "}; ";
		ti = ti + 1;
	}
	-- frac vector
	if (scalarOff) {
		r = r $ "%^ _frac = (%^)(%^(_off - %^(_di))); " fmt(vtype, vtype, stype, stype);
	} else {
		r = r $ "%^ _frac = _off - trunc(_off); " fmt(vtype);
	}
	-- kernel call (sinc needs the per-lane coefficient table)
	match (ip) {
		sinc: { r = r $ _genSimdSincCoeffs(w, scalarOff, stype, vtype); }
		_: {
			r = r $ "return %^(" fmt(_interpKernel(ip));
			var k = 0;
			while (k < taps length) {
				if (k > 0) { r = r $ ", "; }
				r = r $ "_s%^" fmt(k);
				k = k + 1;
			}
			r = r $ ", _frac); ";
		}
	}
	r $ "}()"
}

-- SIMD variable-rate read (M5.2). Non-flat-voice; the C++ SIMD DelayVarRead uses the
-- p->dN_wrpos/p->dN_mask fields literally. A scalar offset + 1-chan delay splats; an
-- interpolating mode builds the lambda; interpNone gathers per lane inline.
fn _delayVarReadSimd(ctx Ctx, n NIdx, info DelayInfo, ip Interpolation, cel String) String {
	let w = `cgSimdW;
	let t = ctx.typ[n];
	let ser = info.serial;
	let dchans = info.chans;
	let lc = `cgLoopChans;
	let offNode = ctx.ins[n][0];
	let scalarOff = ctx.chans[offNode] == 1;
	let offExpr = scalarOff ? _genDelayOffScalar(ctx, offNode) : genExpr(ctx, offNode, cel);
	let wrpos = "p->d%^_wrpos" fmt(ser);
	let mask = "p->d%^_mask" fmt(ser);
	-- scalar offset + 1-chan delay: every lane reads the same value, splat it.
	if (scalarOff && dchans == 1) {
		return _simdSplat(t, w, _delayVarGenRead(ip, "p->d%^" fmt(ser), wrpos, mask, offExpr));
	}
	match (ip) {
		none: {}
		_: {
			var bufs [String] = []; var wrposs [String] = []; var masks [String] = [];
			var jj = 0;
			while (jj < w) {
				let ci = dchans == 1 ? "" : "[%^]" fmt(_simdLaneChanIdx(cel, jj, dchans, lc));
				bufs push!("p->d%^%^" fmt(ser, ci));
				wrposs push!(wrpos);
				masks push!(mask);
				jj = jj + 1;
			}
			return _genSimdInterp(t, w, ip, bufs, wrposs, masks, offExpr, scalarOff);
		}
	}
	-- interpNone vector: a per-lane gather (off re-indexed per lane, no caching).
	var parts [String] = [];
	var j = 0;
	while (j < w) {
		let ci = dchans == 1 ? "" : "[%^]" fmt(_simdLaneChanIdx(cel, j, dchans, lc));
		let off = scalarOff ? offExpr : "(%^)[%^]" fmt(offExpr, j);
		parts push!(_delayVarGenRead(Interpolation.none, "p->d%^%^" fmt(ser, ci), wrpos, mask, off));
		j = j + 1;
	}
	"%^{%^}" fmt(_simdType(t, w), parts join(", "))
}

-- Per-voice variable-rate read in flat-voice SIMD (M5.3). Mirrors the flat-voice
-- DelayVarRead visitor: the offset is read per voice (scalar at lane 0 if it is a
-- global/non-voice scalar); each lane gathers from its own voice ring. An
-- interpolating mode builds the lambda; interpNone gathers genRead per lane.
fn _voiceDelayVarReadSimd(ctx Ctx, n NIdx, info DelayInfo, ip Interpolation, cel String) String {
	let w = `cgSimdW;
	let t = ctx.typ[n];
	let ser = info.serial;
	let dchans = info.chans;
	let shift = `cgFlatChanShift;
	let offNode = ctx.ins[n][0];
	-- globalScalar: a scalar offset that is NOT itself a voice value (read once,
	-- scalar). A voice-graph offset is per-lane even if mono.
	let globalScalar = ctx.chans[offNode] == 1 && ctx.graphOf[offNode] != `cgVoiceGraph;
	let offExpr = globalScalar ? _genDelayOffScalar(ctx, offNode) : genExpr(ctx, offNode, cel);
	match (ip) {
		none: {}
		_: {
			var bufs [String] = []; var wrposs [String] = []; var masks [String] = [];
			var j = 0;
			while (j < w) {
				let vi = _flatSimdVoiceIdx(cel, j, shift);
				let bi = _flatSimdBufIdx(cel, j, shift, dchans);
				bufs push!("p->voice_d%^[%^]" fmt(ser, bi));
				wrposs push!("p->voice_d%^_wrpos[%^]" fmt(ser, vi));
				masks push!(info.allocSize > 1 ? "%^" fmt(info.allocSize - 1) : "p->voice_d%^_mask[%^]" fmt(ser, vi));
				j = j + 1;
			}
			return _genSimdInterp(t, w, ip, bufs, wrposs, masks, offExpr, globalScalar);
		}
	}
	-- interpNone vector: a per-lane gather (off re-indexed per lane unless global).
	var parts [String] = [];
	var j = 0;
	while (j < w) {
		let vi = _flatSimdVoiceIdx(cel, j, shift);
		let bi = _flatSimdBufIdx(cel, j, shift, dchans);
		let mask = info.allocSize > 1 ? "%^" fmt(info.allocSize - 1) : "p->voice_d%^_mask[%^]" fmt(ser, vi);
		let off = globalScalar ? offExpr : "(%^)[%^]" fmt(offExpr, j);
		parts push!(_delayVarGenRead(Interpolation.none, "p->voice_d%^[%^]" fmt(ser, bi), "p->voice_d%^_wrpos[%^]" fmt(ser, vi), mask, off));
		j = j + 1;
	}
	"%^{%^}" fmt(_simdType(t, w), parts join(", "))
}

-- Variable (signal-rate) delay read with interpolation. interpNone reads the
-- ring buffer directly; the interpolating kernels call tzpl_delay_<kernel>
-- (declared in tzpl_delay_interp.hpp). off is the delay-time signal expression.
fn _delayVarReadExpr(ctx Ctx, n NIdx, d Int, ip Interpolation, cel String) String {
	let info = ctx.delays[d];
	if (ctx _isVoiceDelay(d)) {
		if (`cgSimdW > 0) { return _voiceDelayVarReadSimd(ctx, n, info, ip, cel); }
		let off = genExpr(ctx, ctx.ins[n][0], cel);
		let buf = _vDelayBuf(ctx, info, cel);
		let wrpos = _vDelayWrpos(info, cel);
		let vmask = _vDelayMask(info, cel);
		match (ip) {
			none: return "%^[(%^ - u64(%^)) & %^]" fmt(buf, wrpos, off, vmask);
			_:    return "tzpl_delay_%^(%^, %^, %^, %^)" fmt(ip toLispString, buf, wrpos, vmask, off);
		}
	}
	if (`cgSimdW > 0) { return _delayVarReadSimd(ctx, n, info, ip, cel); }
	let off = genExpr(ctx, ctx.ins[n][0], cel);
	let di = _delayDIndex(ctx, d, n, cel);
	let mask = _delayMask(info);
	match (ip) {
		none: "p->d%^%^[(p->d%^_wrpos - u64(%^)) & %^]" fmt(info.serial, di, info.serial, off, mask);
		_:    "tzpl_delay_%^(p->d%^%^, p->d%^_wrpos, %^, %^)"
			fmt(ip toLispString, info.serial, di, info.serial, mask, off);
	}
}

-- Per-voice delay write in flat-voice SIMD (M5.3). Mirrors the flat-voice
-- DelayWrite visitor: a contiguous voice SoA delay stores as a vector; everything
-- else (narrower z1, ring) evaluates once into `_dw<ser>` and scatters per lane.
fn _voiceDelayWriteSimd(ctx Ctx, info DelayInfo, value String, cel String, treeSerial Int) String {
	let w = `cgSimdW;
	let ser = info.serial;
	let dchans = info.chans;
	let lc = `cgLoopChans;
	let shift = `cgFlatChanShift;
	if (info.allocSize == 1 && (dchans == lc || (dchans == 1 && shift == 0))) {
		return "*(%^*)(&p->voice_d%^[%^]) = %^; // %^ DelayWrite\n" fmt(_simdType(info.typ, w), ser, cel, value, treeSerial);
	}
	var s = "%^ _dw%^ = %^; // %^ DelayWrite\n" fmt(_simdType(info.typ, w), ser, value, treeSerial);
	var j = 0;
	while (j < w) {
		let vi = _flatSimdVoiceIdx(cel, j, shift);
		let bi = _flatSimdBufIdx(cel, j, shift, dchans);
		if (info.allocSize == 1) {
			s = s $ _tabs(`cgIndent) $ "p->voice_d%^[%^] = _dw%^[%^];\n" fmt(ser, bi, ser, j);
		} else {
			let mask = info.allocSize > 1 ? "%^" fmt(info.allocSize - 1) : "p->voice_d%^_mask[%^]" fmt(ser, vi);
			s = s $ _tabs(`cgIndent) $ "p->voice_d%^[%^][p->voice_d%^_wrpos[%^] & %^] = _dw%^[%^];\n" fmt(ser, bi, ser, vi, mask, ser, j);
		}
		j = j + 1;
	}
	s
}

-- Emit a delay-write statement (a sink, called from genTree).
fn _delayWriteStmt(ctx Ctx, n NIdx, d Int, cel String, treeSerial Int) String {
	let info = ctx.delays[d];
	let value = genExpr(ctx, ctx.ins[n][0], cel);
	if (ctx _isVoiceDelay(d)) {
		if (`cgSimdW > 0) { return _voiceDelayWriteSimd(ctx, info, value, cel, treeSerial); }
		-- Per-voice 1-sample delay: write the voice's single slot.
		if (info.allocSize == 1) {
			return "%^ = %^; // %^\n" fmt(_vDelayBuf(ctx, info, cel), value, treeSerial);
		}
		-- Per-voice ring buffer: write at the voice's head & mask.
		return "%^[%^ & %^] = %^; // %^\n" fmt(_vDelayBuf(ctx, info, cel), _vDelayWrpos(info, cel), _vDelayMask(info, cel), value, treeSerial);
	}
	-- SIMD delay write (M5.2). A 1-sample delay at least as wide as the vector is a
	-- contiguous store; everything else (narrower z1, ring buffer) evaluates the
	-- value once into `_dw<serial>` then scatters per lane. The C++ SIMD DelayWrite
	-- visitor appends " DelayWrite" to the comment (the scalar form does not -- a
	-- C++ inconsistency we mirror). Per-lane lines carry their own indent.
	if (`cgSimdW > 0) {
		let w = `cgSimdW;
		let ser = info.serial;
		let dchans = info.chans;
		let lc = `cgLoopChans;
		if (info.allocSize == 1 && dchans >= w) {
			return "*(%^*)(&p->d%^[%^]) = %^; // %^ DelayWrite\n" fmt(_simdType(info.typ, w), ser, cel, value, treeSerial);
		}
		var s = "%^ _dw%^ = %^; // %^ DelayWrite\n" fmt(_simdType(info.typ, w), ser, value, treeSerial);
		let mask = _delayMask(info);
		var j = 0;
		while (j < w) {
			let ci = _simdLaneChanIdx(cel, j, dchans, lc);
			if (info.allocSize == 1) {
				s = s $ _tabs(`cgIndent) $ "p->d%^[%^] = _dw%^[%^];\n" fmt(ser, ci, ser, j);
			} else {
				s = s $ _tabs(`cgIndent) $ "p->d%^[%^][p->d%^_wrpos & %^] = _dw%^[%^];\n" fmt(ser, ci, ser, mask, ser, j);
			}
			j = j + 1;
		}
		return s;
	}
	let di = info.chans > 1 ? "[%^]" fmt(cel) : "";
	if (info.allocSize == 1) {
		"p->d%^%^ = %^; // %^\n" fmt(info.serial, di, value, treeSerial)
	} else {
		"p->d%^%^[p->d%^_wrpos & %^] = %^; // %^\n" fmt(info.serial, di, info.serial, _delayMask(info), value, treeSerial)
	}
}

---------------------------------------------------------------------------
-- Buffer ops (mirror the BufExpr visitors in synthdef_cpp_codegen.cpp).
-- Buffers are external, swappable memory held as a `tzpl_Buffer*` that may be
-- null, so every access is guarded with `buf ? ... : 0.0`.

fn _bufPtr(ctx Ctx, bi Int) String = "p->buf%^" fmt(ctx.bufSerials[bi]);

fn _bufInterpFunc(i Interpolation) String = match (i) {
	none:     "tzpl_buf_none";
	linear:   "tzpl_buf_linear";
	cubic:    "tzpl_buf_cubic";
	lagrange: "tzpl_buf_lagrange";
	sinc:     "tzpl_buf_sinc";
};

-- Channel index: single-channel reads/writes use startChan directly;
-- multichannel wrap (startChan + cel) & (buf->chans - 1).
fn _bufChanExpr(chans Int, startChan Int, cel String, bufPtr String) String =
	chans == 1 ? startChan toString
	: "(%^ + %^) & (%^->chans - 1)" fmt(startChan, cel, bufPtr);

-- The per-lane channel subscript for a SIMD buffer gather: single-channel reads use
-- startChan directly; multichannel wrap (startChan + laneChan) & (bufRef->chans-1).
-- bufRef is the buffer-pointer expression visible at the gather site (`p->bufN` for
-- fixed reads, the lambda-cached `_b` for var reads).
fn _bufSimdChanExpr(readChans Int, startChan Int, ci String, bufRef String) String =
	readChans == 1 ? startChan toString
	: "(%^ + %^) & (%^->chans - 1)" fmt(startChan, ci, bufRef);

-- BufFixRead (M5.4 SIMD): a 1-chan read splats the guarded scalar; a multichannel
-- read gathers per lane. Mirrors the C++ BufFixRead visitor.
fn _bufFixReadExpr(ctx Ctx, n NIdx, bi Int, index Int, readChans Int, startChan Int, cel String) String {
	let bp = ctx _bufPtr(bi);
	if (`cgSimdW > 0) {
		let w = `cgSimdW;
		let t = ctx.typ[n];
		let lc = `cgLoopChans;
		if (readChans == 1) {
			return _simdSplat(t, w, "(%^ ? %^->data[%^][tzpl_buf_tap(%^, %^->length)] : 0.0)" fmt(bp, bp, startChan, index, bp));
		}
		var parts [String] = [];
		var j = 0;
		while (j < w) {
			let chan = _bufSimdChanExpr(readChans, startChan, _simdLaneChanIdx(cel, j, readChans, lc), bp);
			parts push!("(%^ ? %^->data[%^][tzpl_buf_tap(%^, %^->length)] : 0.0)" fmt(bp, bp, chan, index, bp));
			j = j + 1;
		}
		return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
	}
	let chan = _bufChanExpr(readChans, startChan, cel, bp);
	"(%^ ? %^->data[%^][tzpl_buf_tap(%^, %^->length)] : 0.0)" fmt(bp, bp, chan, index, bp)
}

-- A single guarded buffer read (the C++ genRead form), against the buffer reference
-- `bref` (p->bufN or the cached _b).
fn _bufGenRead(interp Interpolation, bref String, chan String, off String) String = match (interp) {
	none: "(%^ ? %^->data[%^][tzpl_buf_tap(i64(%^), %^->length)] : 0.0)" fmt(bref, bref, chan, off, bref);
	_:    "(%^ ? %^(%^->data[%^], %^->length, %^) : 0.0)" fmt(bref, _bufInterpFunc(interp), bref, chan, bref, off);
};

-- BufVarRead (M5.4 SIMD): a scalar-offset single-channel read splats; otherwise a
-- per-lane gather wrapped in a lambda that caches the buffer pointer (`_b`). Mirrors
-- the C++ BufVarRead visitor.
fn _bufVarReadExpr(ctx Ctx, n NIdx, bi Int, interp Interpolation, readChans Int, startChan Int, cel String) String {
	let bp = ctx _bufPtr(bi);
	if (`cgSimdW > 0) {
		let w = `cgSimdW;
		let t = ctx.typ[n];
		let lc = `cgLoopChans;
		let offNode = ctx.ins[n][0];
		let scalarOff = ctx.chans[offNode] == 1;
		let offExpr = scalarOff ? _genDelayOffScalar(ctx, offNode) : genExpr(ctx, offNode, cel);
		if (readChans == 1 && scalarOff) {
			return _simdSplat(t, w, _bufGenRead(interp, bp, startChan toString, offExpr));
		}
		var parts [String] = [];
		var j = 0;
		while (j < w) {
			let chan = _bufSimdChanExpr(readChans, startChan, _simdLaneChanIdx(cel, j, readChans, lc), "_b");
			let off = scalarOff ? offExpr : "(%^)[%^]" fmt(offExpr, j);
			parts push!(_bufGenRead(interp, "_b", chan, off));
			j = j + 1;
		}
		return "[&]() -> %^ { auto _b = %^; return %^{%^}; }()" fmt(_simdType(t, w), bp, _simdType(t, w), parts join(", "));
	}
	let chan = _bufChanExpr(readChans, startChan, cel, bp);
	let idx = genExpr(ctx, ctx.ins[n][0], cel);
	match (interp) {
		none: "(%^ ? %^->data[%^][tzpl_buf_tap(i64(%^), %^->length)] : 0.0)" fmt(bp, bp, chan, idx, bp);
		_:    "(%^ ? %^(%^->data[%^], %^->length, %^) : 0.0)" fmt(bp, _bufInterpFunc(interp), bp, chan, bp, idx);
	}
}

fn _bufLengthExpr(ctx Ctx, n NIdx, bi Int) String {
	let bp = ctx _bufPtr(bi);
	let scalar = "(%^ ? (f64)(%^->length) : 0.0)" fmt(bp, bp);
	`cgSimdW > 0 ? _simdSplat(ctx.typ[n], `cgSimdW, scalar) : scalar
}

-- BufWrite is a sink: a guarded multi-line store, like _delayWriteStmt.
fn _bufWriteStmt(ctx Ctx, n NIdx, bi Int, writeChans Int, startChan Int, cel String, treeSerial Int) String {
	let bp = ctx _bufPtr(bi);
	let index = genExpr(ctx, ctx.ins[n][1], cel);
	let value = genExpr(ctx, ctx.ins[n][0], cel);
	-- writeChans 0 = auto: match the input channel count (BufWrite::calcShape).
	let wc = writeChans == 0 ? ctx.chans[n] : writeChans;
	let chan = _bufChanExpr(wc, startChan, cel, bp);
	let i1 = _tabs(`cgIndent + 1);
	let i0 = _tabs(`cgIndent);
	"if (%^) {\n" fmt(bp)
		$ i1 $ "u64 _idx = u64(tzpl_buf_tap(i64(%^), %^->length));\n" fmt(index, bp)
		$ i1 $ "%^->data[%^][_idx] = %^;\n" fmt(bp, chan, value)
		$ i0 $ "} // %^ BufWrite\n" fmt(treeSerial)
}

---------------------------------------------------------------------------
-- Sample bank ops (mirror the Bank* visitors + bankSlot in
-- synthdef_cpp_codegen.cpp). The lookup latches (pitch, velocity) into
-- per-voice slots at note-on and resolves a buffer pointer + metadata; reads
-- and accessors go through those slots. Scalar-only (bank kinds disqualify
-- SIMD on both sides).

-- Bank lookup nodes in deterministic (topological) order.
fn _bankLookupNodes(ctx Ctx) [Int] {
	var v [Int] = [];
	for (n : ctx.sorted) {
		match (ctx.kind[n]) { bankLookupK(_): { v push!(n); } _: {} }
	}
	v
}

-- The latched slot `field` of lookup `lu` in the current emission context.
-- synthc voicers are always flat (SoA), so a voicer-body lookup is a
-- per-voice array indexed by the voice derived from cel.
fn _bankSlot(ctx Ctx, lu NIdx, field String, cel String) String {
	if (ctx _isVoiceGraph(ctx.graphOf[lu])) {
		let vi = `cgFlatChanShift == 0 ? cel : "(%^ >> %^)" fmt(cel, `cgFlatChanShift);
		return "p->voice_lu%^_%^[%^]" fmt(ctx.serial[lu], field, vi);
	}
	"p->lu%^_%^" fmt(ctx.serial[lu], field)
}

fn _bankFixReadExpr(ctx Ctx, n NIdx, index Int, readChans Int, startChan Int, cel String) String {
	let bp = _bankSlot(ctx, ctx.ins[n][0], "buf", cel);
	let chan = _bufChanExpr(readChans, startChan, cel, bp);
	"(%^ ? %^->data[%^][tzpl_buf_tap(%^, %^->length)] : 0.0)" fmt(bp, bp, chan, index, bp)
}

fn _bankVarReadExpr(ctx Ctx, n NIdx, interp Interpolation, readChans Int, startChan Int, cel String) String {
	let bp = _bankSlot(ctx, ctx.ins[n][0], "buf", cel);
	let chan = _bufChanExpr(readChans, startChan, cel, bp);
	let idx = genExpr(ctx, ctx.ins[n][1], cel);
	match (interp) {
		none: "(%^ ? %^->data[%^][tzpl_buf_tap(i64(%^), %^->length)] : 0.0)" fmt(bp, bp, chan, idx, bp);
		_:    "(%^ ? %^(%^->data[%^], %^->length, %^) : 0.0)" fmt(bp, _bufInterpFunc(interp), bp, chan, bp, idx);
	}
}

-- BankLookup is a sink: store the clamped pitch/velocity, then resolve the
-- bank cell (mirrors the Rank1 BankLookup visitor). The caller prepends the
-- first line's indent.
fn _bankLookupStmt(ctx Ctx, n NIdx, cel String, treeSerial Int) String {
	let pitch = genExpr(ctx, ctx.ins[n][0], cel);
	let vel = genExpr(ctx, ctx.ins[n][1], cel);
	let pSlot = _bankSlot(ctx, n, "p", cel);
	let vSlot = _bankSlot(ctx, n, "v", cel);
	let i0 = _tabs(`cgIndent);
	var s = "%^ = std::min(std::max((i32)(%^ + 0.5f), 0), 127); // %^ BankLookup\n" fmt(pSlot, pitch, treeSerial);
	s = s $ i0 $ "%^ = std::min(std::max((i32)(%^ + 0.5f), 0), 127);\n" fmt(vSlot, vel);
	if (ctx _isVoiceGraph(ctx.graphOf[n])) {
		let vi = `cgFlatChanShift == 0 ? cel : "(%^ >> %^)" fmt(cel, `cgFlatChanShift);
		s = s $ i0 $ "%^_lu%^_resolve(p, %^);\n" fmt(ctx.name, ctx.serial[n], vi);
	} else {
		s = s $ i0 $ "%^_lu%^_resolve(p);\n" fmt(ctx.name, ctx.serial[n]);
	}
	s
}

---------------------------------------------------------------------------
-- Voicer emission (M4, flat voice mode -- non-branching body).
--
-- A VoicerExpr replicates its voice body across `maxVoices` voices. In flat
-- voice mode the body's audio loops run as `for (int i = 0; i < maxVoices; ++i)`
-- loops; body instance vars live in p->voice_v<serial>[maxVoices] arrays, body
-- temps in local v<serial>[maxVoices] arrays, and the body root's PhiNode writes
-- the voicer's own output var. NoteParams gather from p->voicer_params[i][serial].

-- The voicer nodes in this synth (M4.0: at most one).
fn _voicerNodes(ctx Ctx) [Int] {
	var out [Int] = [];
	for (n : ctx.sorted) { match (ctx.kind[n]) { voicerK(_): out push!(n); _: {} } }
	out
}

-- Does any node read the shared-input table? Gates the exported
-- tzpl_sharedInput pointer (mirrors the C++ genClass sorted-exprs scan).
fn _usesSharedInput(ctx Ctx) Bool {
	for (n : ctx.sorted) { match (ctx.kind[n]) { sharedInK(_, _): { return true; } _: {} } }
	false
}

fn _voicerMaxVoices(ctx Ctx, n NIdx) Int = match (ctx.kind[n]) { voicerK(mv): mv; _: 1; };

-- The body graph index of a voicer node (the graph its body PhiNode lives in).
fn _voicerBodyGraph(ctx Ctx, n NIdx) Int = ctx.graphOf[ctx.subs[n][0]];

-- Is graph g the body of some voicer? (so its inst vars are per-voice).
fn _isVoiceGraph(ctx Ctx, g Int) Bool {
	for (v : ctx _voicerNodes) { if (ctx _voicerBodyGraph(v) == g) { return true; } }
	false
}

-- Is delay d a per-voice delay (lives in a voicer body) being emitted in flat
-- voice mode? Its state is replicated per voice: voice_d<serial>[maxVoices].
fn _isVoiceDelay(ctx Ctx, d Int) Bool =
	`cgVoiceCount > 0 && ctx _delayGraph(ctx.delays[d]) == `cgVoiceGraph;

-- Does graph g (a voicer body) own any delays?
fn _graphHasDelays(ctx Ctx, g Int) Bool {
	for (dd : ctx.delays) { if (ctx _delayGraph(dd) == g) { return true; } }
	false
}

-- The NoteParam nodes of a voicer body, ordered by param serial (gate=0 first).
-- voicer_params has 1 + numUserParams columns; defaults[] holds the user params'
-- init values in serial order (excluding gate).
fn _voicerNoteParams(ctx Ctx, bg Int) [Int] {
	var out [Int] = [];
	-- collect, then order by serial
	var nodes [Int] = [];
	for (n : ctx.sorted) {
		if (ctx.graphOf[n] == bg) {
			match (ctx.kind[n]) { noteParamK(_, _, _): nodes push!(n); _: {} }
		}
	}
	-- selection sort by param serial (small N)
	var picked [Int] = [];
	var count = nodes length;
	while (out length < count) {
		var best = NONE;
		for (n : nodes) {
			var seen = false;
			for (p : picked) { if (p == n) { seen = true; } }
			if (!seen) {
				if (best == NONE || ctx _noteParamSerial(n) < ctx _noteParamSerial(best)) { best = n; }
			}
		}
		picked push!(best);
		out push!(best);
	}
	out
}

fn _noteParamSerial(ctx Ctx, n NIdx) Int = match (ctx.kind[n]) { noteParamK(_, _, sn): sn; _: 0; };

-- Number of user params (non-gate note params) of a voicer body.
fn _numUserParams(ctx Ctx, bg Int) Int {
	var k = 0;
	for (n : ctx _voicerNoteParams(bg)) { if (ctx _noteParamSerial(n) != 0) { k = k + 1; } }
	k
}

-- Emit the voicer's body loops in flat voice mode. The leading _tabs sits where
-- a control-flow statement would (matching the C++ control-flow block prefix);
-- the first body LOOP's own leading newline then closes the blank line.
fn _genVoicer(ctx Ctx, root NIdx, mv Int) String {
	let bg = ctx _voicerBodyGraph(root);
	var `cgVoiceCount Int = mv;
	var `cgVoiceGraph Int = bg;
	var `cgVoiceChans Int = ctx.chans[ctx.ins[ctx.subs[root][0]][0]];
	_tabs(`cgIndent) $ genLoops(ctx, ctx.graphs[bg].audioLoops) $ ctx genDelayAdvance(bg)
}

---------------------------------------------------------------------------
-- Spectral chain emission (M4.4). A SpectralChainExpr is a ControlFlowExpr whose
-- body subgraph operates on a packed split-complex FFT frame. Per sample it pumps
-- the input ring buffer and reads the overlap-add output; every hopSize samples it
-- windows + forward-FFTs each channel into the frame, runs the body subgraph, then
-- inverse-FFTs + overlap-adds. The frame (a cut-Graph source) is loaded into its
-- own inst var (v<frame>) each sample from spec<chain>_frame.

fn _spectralChains(ctx Ctx) [Int] {
	var out [Int] = [];
	for (n : ctx.sorted) { match (ctx.kind[n]) { spectralChainK(_, _): out push!(n); _: {} } }
	out
}

-- The chain serial owning a frame node (matched by fftSize: each chain builds one
-- frame with its own fftSize).
fn _frameChainSerial(ctx Ctx, frameNode NIdx) Int {
	let fft = match (ctx.kind[frameNode]) { spectralFrameK(f): f; _: 0; };
	for (c : ctx.sorted) {
		match (ctx.kind[c]) { spectralChainK(cf, _): { if (cf == fft) { return ctx.serial[c]; } } _: {} }
	}
	NONE
}

fn _genSpectral(ctx Ctx, root NIdx, fft Int, hop Int) String {
	let cs = ctx.serial[root];
	let ic = ctx.chans[root];           -- input/output channel count
	let mask = fft - 1;
	let bg = ctx.graphOf[ctx.subs[root][0]];
	let cur = `cgIndent;
	let t = _tabs(cur); let t1 = _tabs(cur + 1); let t2 = _tabs(cur + 2); let t3 = _tabs(cur + 3);
	var s = t $ "// SpectralChain: write input to ring buffer\n";
	-- per-channel: write the input sample into the ring
	var ch = 0;
	while (ch < ic) {
		s = s $ t $ "p->spec%^_inbuf[%^][p->spec%^_wrpos] = %^;\n" fmt(cs, ch, cs, genExpr(ctx, ctx.ins[root][0], ch toString));
		ch = ch + 1;
	}
	-- per-channel: read the overlap-add output and clear the slot
	ch = 0;
	while (ch < ic) {
		s = s $ t $ "%^ = p->spec%^_outbuf[%^][p->spec%^_rdpos];\n" fmt(_varRef(ctx, root, ch toString), cs, ch, cs);
		s = s $ t $ "p->spec%^_outbuf[%^][p->spec%^_rdpos] = 0.f;\n" fmt(cs, ch, cs);
		ch = ch + 1;
	}
	s = s $ t $ "p->spec%^_wrpos = (p->spec%^_wrpos + 1) & %^;\n" fmt(cs, cs, mask);
	s = s $ t $ "p->spec%^_rdpos = (p->spec%^_rdpos + 1) & %^;\n" fmt(cs, cs, mask);
	s = s $ t $ "if (++p->spec%^_hopcount >= %^) {\n" fmt(cs, hop);
	s = s $ t1 $ "p->spec%^_hopcount = 0;\n" fmt(cs);
	s = s $ t1 $ "for (int ch = 0; ch < %^; ++ch) {\n" fmt(ic);
	s = s $ t2 $ "for (int k = 0; k < %^; ++k) {\n" fmt(fft);
	s = s $ t3 $ "int idx = (p->spec%^_wrpos + k) & %^;\n" fmt(cs, mask);
	s = s $ t3 $ "p->spec%^_fftbuf[ch * %^ + k] = p->spec%^_inbuf[ch][idx] * p->spec%^_window[k];\n" fmt(cs, fft, cs, cs);
	s = s $ t2 $ "}\n";
	s = s $ t2 $ "tzpl_fft_forward(p->spec%^_fftsetup, p->spec%^_fftbuf + ch * %^, p->spec%^_frame + ch * %^);\n" fmt(cs, cs, fft, cs, fft);
	s = s $ t1 $ "}\n";
	s = s $ t1 $ "// spectral subgraph\n";
	-- body subgraph audio loops, indented one deeper
	s = s $ _genSpectralBody(ctx, bg, cur + 1);
	s = s $ t1 $ "for (int ch = 0; ch < %^; ++ch) {\n" fmt(ic);
	s = s $ t2 $ "tzpl_fft_inverse(p->spec%^_fftsetup, p->spec%^_frame + ch * %^, p->spec%^_fftbuf + ch * %^);\n" fmt(cs, cs, fft, cs, fft);
	s = s $ t2 $ "for (int k = 0; k < %^; ++k) {\n" fmt(fft);
	s = s $ t3 $ "int idx = (p->spec%^_wrpos + k) & %^;\n" fmt(cs, mask);
	s = s $ t3 $ "p->spec%^_outbuf[ch][idx] += p->spec%^_fftbuf[ch * %^ + k] * p->spec%^_window[k];\n" fmt(cs, cs, fft, cs);
	s = s $ t2 $ "}\n";
	s = s $ t1 $ "}\n";
	s = s $ t $ "}\n";
	s
}

-- The body subgraph's audio loops, emitted at the given indent.
fn _genSpectralBody(ctx Ctx, bg Int, indent Int) String {
	var `cgIndent Int = indent;
	genLoops(ctx, ctx.graphs[bg].audioLoops)
}

---------------------------------------------------------------------------
-- Control-flow emission (mirror the IfElse/Switch/ForLoop visitors).
-- A control-flow node is its own loop's single tree; its result variable is
-- declared by genLoop (it is a temp not consumed in its loop), and the branch
-- PhiNodes assign into it. Each branch body is the subgraph's own audio loops,
-- emitted inside the block at +1 indent.

-- Emit a branch/body subgraph's audio loops at the given indent.
fn _branchLoops(ctx Ctx, phi NIdx, indent Int) String {
	var `cgIndent Int = indent;
	let g = ctx.graphOf[phi];
	genLoops(ctx, ctx.graphs[g].audioLoops) $ ctx genDelayAdvance(g)
}

-- `<userial> <cut>  is_consumed_in_loop <bool>` -- matches the C++ comment.
fn _cfComment(ctx Ctx, root NIdx) String =
	"%^ %^  is_consumed_in_loop %^" fmt(ctx.serial[root], ctx.cut[root] cutStr,
		_consumedInLoop(ctx, ctx.treeOf[root]) ? "true" : "false");

fn _genIf(ctx Ctx, root NIdx) String {
	let cur = `cgIndent;
	let test = genExpr(ctx, ctx.ins[root][0], "0");
	var s = _tabs(cur) $ "if (%^) { // %^\n" fmt(test, ctx _cfComment(root));
	s = s $ ctx _branchLoops(ctx.subs[root][0], cur + 1);
	s = s $ _tabs(cur) $ "} else {\n";
	s = s $ ctx _branchLoops(ctx.subs[root][1], cur + 1);
	s = s $ _tabs(cur) $ "}\n";
	s
}

fn _genFor(ctx Ctx, root NIdx) String {
	let cur = `cgIndent;
	let count = genExpr(ctx, ctx.ins[root][0], "0");
	var s = _tabs(cur) $ "for (i32 i = 0; i < %^; ++i) {\n" fmt(count);
	s = s $ ctx _branchLoops(ctx.subs[root][0], cur + 1);
	s = s $ _tabs(cur) $ "}\n";
	s
}

-- Mirrors the C++ SwitchExpr visitor byte-for-byte, including its indentation
-- quirks: cases at cur+2, bodies at cur+3 (with a pre-genLoops tab), the closing
-- brace at cur+1 plus a trailing tab, and a net +1 indent LEAK -- everything
-- emitted after the switch is indented one deeper. The leak is reproduced by
-- assigning `cgIndent (the persistent binding from genTickFun / a branch).
fn _genSwitch(ctx Ctx, root NIdx, nc Int) String {
	let cur = `cgIndent;
	let sel = genExpr(ctx, ctx.ins[root][0], "0");
	var s = _tabs(cur) $ "switch(std::min(u32(%^), u32(%^))) {\n" fmt(sel, nc - 1);
	var i = 0;
	while (i < nc) {
		s = s $ _tabs(cur + 2) $ "case %^: {\n" fmt(i);
		s = s $ _tabs(cur + 3) $ ctx _branchLoops(ctx.subs[root][i], cur + 3);
		s = s $ _tabs(cur + 2) $ "} break;\n";
		i = i + 1;
	}
	s = s $ _tabs(cur + 1) $ "}\n" $ _tabs(cur + 1);
	`cgIndent = cur + 1;
	s
}

---------------------------------------------------------------------------
-- Tree emission

fn _isSink(ctx Ctx, n NIdx) Bool = ctx.kind[n] isSinkKind;

fn genTree(ctx Ctx, treeIdx Int, cel String) String {
	let tree = ctx.trees[treeIdx];
	let root = tree.root;
	var `cgRoot Int = root;

	match (ctx.cut[root]) {
		unused: { return _tabs(`cgIndent) $ "// Unused %^ %^\n" fmt(ctx.serial[root], nodeStr(ctx, root)); }
		_: {}
	}

	-- Special sink forms handled structurally.
	match (ctx.kind[root]) {
		delayWriteK(d): { return _tabs(`cgIndent) $ _delayWriteStmt(ctx, root, d, cel, tree.serial); }
		delayInitK(_, _): { return ""; }   -- emitted by genDelayInit
		maxDelayK(_): { return ""; }
		bufWriteK(bi, wc, sc): { return _tabs(`cgIndent) $ _bufWriteStmt(ctx, root, bi, wc, sc, cel, tree.serial); }
		bankLookupK(_): { return _tabs(`cgIndent) $ _bankLookupStmt(ctx, root, cel, tree.serial); }
		ifK:            { return ctx _genIf(root); }
		forK:           { return ctx _genFor(root); }
		switchK(nc):    { return ctx _genSwitch(root, nc); }
		voicerK(mv):    { return ctx _genVoicer(root, mv); }
		spectralChainK(fft, hop): { return ctx _genSpectral(root, fft, hop); }
		phiK(target):   { return _tabs(`cgIndent) $ "%^ = %^;\n" fmt(_varRef(ctx, target, cel), genExpr(ctx, ctx.ins[root][0], cel)); }
		-- put/join build their output array in a block (gets_own_loop); at is a
		-- normal inline gather and falls through.
		vecK(vop): { match (vop) {
			put:  { return ctx _genVecPut(root); }
			join: { return ctx _genVecJoin(root); }
			_:    {}
		} }
		_: {}
	}

	let indentStr = _tabs(`cgIndent);
	-- A root is emitted as a local temp declaration when it is consumed only
	-- within its loop, OR when it is a temp-var (non-inst) root in a scalar
	-- loop (mirrors genTree's second condition).
	let localDecl = _consumedInLoop(ctx, treeIdx)
		|| (ctx.cut[root] isTempVarCut && `cgLoopChans == 1 && `cgVoiceCount == 0);
	if (ctx _isSink(root)) {
		-- e.g. outlet: "<lhs> = <expr>; // <serial> Sink <str>" -- but outlet's
		-- genExpr is the lhs accessor; the assignment is built here.
		indentStr $ "%^; // %^ Sink %^\n" fmt(_sinkStmt(ctx, root, cel), tree.serial, nodeStr(ctx, root))
	} else if (localDecl) {
		let declT = `cgSimdW > 0 ? _simdType(ctx.typ[root], `cgSimdW) : cppType(ctx.typ[root]);
		indentStr $ "%^ %^ = %^; // %^ %^\n"
			fmt(declT, _varName(ctx, root), genExpr(ctx, root, cel),
			    tree.serial, ctx.cut[root] cutStr)
	} else {
		indentStr $ "%^ = %^; // %^ %^\n"
			fmt(_varRef(ctx, root, cel), genExpr(ctx, root, cel),
			    tree.serial, ctx.cut[root] cutStr)
	}
}

-- An outlet sink is `lhs = value`. The lhs is the outlet accessor; the value is
-- its input. genExpr on the outlet node itself yields the accessor.
fn _sinkStmt(ctx Ctx, n NIdx, cel String) String = match (ctx.kind[n]) {
	outletK(_, sn): {
		if (`cgSimdW > 0) {
			let t = ctx.typ[n]; let w = `cgSimdW;
			let val = genExpr(ctx, ctx.ins[n][0], cel);
			-- Case A (chans==width): the bare outlet pointer; Case B: &accessor[cel]
			-- (no parens around the &, mirroring the C++ Outlet visitor).
			if (ctx.chans[n] == w) {
				"*(%^*)((%^**)p->outlets)[%^] = %^" fmt(_simdType(t, w), cppType(t), sn, val)
			} else {
				"*(%^*)&((%^**)p->outlets)[%^][%^] = %^" fmt(_simdType(t, w), cppType(t), sn, cel, val)
			}
		} else {
			"((%^**)p->outlets)[%^]%^ = %^"
				fmt(cppType(ctx.typ[n]), sn, (ctx.chans[n] > 1 ? "[%^]" fmt(cel) : "[0]"),
				    genExpr(ctx, ctx.ins[n][0], cel))
		}
	}
	debugK(_, _, _, _): "/* FIXME debug */";
	_: genExpr(ctx, n, cel);
};

---------------------------------------------------------------------------
-- Loop emission (scalar + channel loop; no SIMD)

---------------------------------------------------------------------------
-- SIMD helpers (M5.1). Mirror simdTypeName/simdSplat/simdLoad/simdStore.

fn _simdType(t NumType, w Int) String = "%^x%^" fmt(cppType(t), w);
fn _simdSplat(t NumType, w Int, x String) String = "(%^)(%^)" fmt(_simdType(t, w), x);
fn _simdLoad(t NumType, w Int, ptr String) String = "*(%^*)(%^)" fmt(_simdType(t, w), ptr);

-- The per-lane channel subscript for a SIMD gather/scatter (mirror simdLaneChanIdx).
-- Lane j reads/writes channel cel+j, wrapping by (bufChans-1) when the delay/buffer
-- is narrower than the loop. cel is always a String here ("0" in Case A, "i" in a
-- stride loop); a numeric cel constant-folds the `+ j`.
fn _simdLaneChanIdx(cel String, j Int, bufChans Int, loopChans Int) String {
	let needWrap = bufChans < loopChans;
	if (cel == "0") {
		let idx = needWrap ? (j & (bufChans - 1)) : j;
		return idx toString;
	}
	let idx = j == 0 ? cel : "(%^ + %^)" fmt(cel, j);
	needWrap ? "(%^) & %^" fmt(idx, bufChans - 1) : idx
}

-- Flat-voice SIMD lane indices (mirror flatSimdVoiceIdx / flatSimdBufIdx). shift =
-- log2(perVoiceChans). A constant cel ("0", Case A) folds the arithmetic.
-- voiceIdx lane j = (cel + j) >> shift.
fn _flatSimdVoiceIdx(cel String, j Int, shift Int) String {
	if (cel == "0") { return (j >> shift) toString; }
	if (shift == 0) { return j == 0 ? cel : "(%^ + %^)" fmt(cel, j); }
	if (j == 0) { return "(%^ >> %^)" fmt(cel, shift); }
	"((%^ + %^) >> %^)" fmt(cel, j, shift)
}
-- bufIdx lane j: multi-chan buffers use the flat SoA index (loop iteration + j),
-- 1-chan buffers use the voice index.
fn _flatSimdBufIdx(cel String, j Int, shift Int, dchans Int) String {
	if (dchans > 1) {
		if (cel == "0") { return j toString; }
		return j == 0 ? cel : "(%^ + %^)" fmt(cel, j);
	}
	_flatSimdVoiceIdx(cel, j, shift)
}
-- Voice-local multi-chan var narrower than the loop: SoA element vi*chans + ci,
-- vi = (cel+j) >> shift, ci = (cel+j) & (chans-1).
fn _flatSimdVarWrapIdx(cel String, j Int, shift Int, chans Int) String {
	if (cel == "0") {
		let vi = j >> shift;
		let ci = j & (chans - 1);
		return (vi * chans + ci) toString;
	}
	"%^ * %^ + ((%^ + %^) & %^)" fmt(_flatSimdVoiceIdx(cel, j, shift), chans, cel, j, chans - 1)
}

-- NoteParam in flat-voice SIMD (mirror the NoteParam visitor): a per-lane gather
-- from voicer_params[voice][serial(+chan)]. A 1-chan param whose lanes all fall in
-- one voice splats. The column is serial for a mono param, serial + wrapped chan
-- for a multichannel one.
fn _noteParamSimd(t NumType, chans Int, psn Int, cel String) String {
	let w = `cgSimdW;
	let shift = `cgFlatChanShift;
	let log2Width = w == 4 ? 2 : 1;
	if (chans == 1 && shift >= log2Width) {
		return _simdSplat(t, w, "p->voicer_params[%^][%^]" fmt(_flatSimdVoiceIdx(cel, 0, shift), psn));
	}
	var parts [String] = [];
	var j = 0;
	while (j < w) {
		let vi = _flatSimdVoiceIdx(cel, j, shift);
		if (chans == 1) {
			parts push!("p->voicer_params[%^][%^]" fmt(vi, psn));
		} else if (cel == "0") {
			parts push!("p->voicer_params[%^][%^]" fmt(vi, psn + (j & (chans - 1))));
		} else {
			parts push!("p->voicer_params[%^][%^ + ((%^ + %^) & %^)]" fmt(vi, psn, cel, j, chans - 1));
		}
		j = j + 1;
	}
	"%^{%^}" fmt(_simdType(t, w), parts join(", "))
}

-- A loop's tree contains an expression that can't be vectorized (Apple simd has
-- no ternary/reduce; vec-reorder ops need per-element indices). Mirrors the
-- disqualifying-expr scan in simdWidth(). VecTake/VecDrop are NOT disqualified.
-- vec-reorder ops disqualify a loop from SIMD; take/drop do not.
fn _vecSimdDisqualified(vop VecOp) Bool = match (vop) {
	take(_): false;  drop(_): false;  _: true;
};
-- Whether an expression disqualifies its loop from SIMD. ctx-aware so a delay's
-- allocSize can gate it: M5.2 vectorizes 1-sample delays (allocSize==1); ring
-- buffers + the variable-read interpolation are still deferred, as are buffers
-- (M5.4). The C++ vectorizes all of these; disqualifying keeps synthc correct-
-- scalar for those loops until their SIMD paths land.
fn _exprSimdDisqualified(ctx Ctx, e NIdx) Bool = match (ctx.kind[e]) {
	vecK(vop):            vop _vecSimdDisqualified;
	compareopK(_):        true;
	selectK:              true;
	reduceK(_, _):        true;
	spectralChainK(_, _): true;
	spectralFrameK(_):    true;
	urandK(_):            true;
	birandK(_):           true;
	rand64K(_):           true;
	debugK(_, _, _, _):   true;
	delayFixReadK(_, _):  false;                           -- 1-sample + ring (M5.2)
	delayVarReadK(_, _):  false;                           -- interpolation (M5.2)
	delayWriteK(_):       false;                           -- 1-sample + ring (M5.2)
	bufFixReadK(_, _, _, _): false;                        -- gather (M5.4)
	bufVarReadK(_, _, _, _): false;                        -- gather (M5.4)
	bufWriteK(_, _, _):   false;                           -- scalar-form store, vector operands (M5.4)
	bufLengthK(_):        false;                           -- splat (M5.4)
	bankLookupK(_):       true;                            -- multi-statement latch, scalar only
	bankFixReadK(_, _, _): true;                           -- per-voice pointer, scalar only
	bankVarReadK(_, _, _): true;
	bankRootKeyK:         true;
	bankSampleRateK:      true;
	bankLengthK:          true;
	bankLoopStartK:       true;
	bankLoopEndK:         true;
	bankHasLoopK:         true;
	_:                    false;
};

-- Is graph g the body subgraph of a spectral chain? Loops inside a spectral frame
-- body must stay scalar: the C++ SIMD-vectorizes the per-bin frame loop into an
-- illegal lvalue-cast store (`(f32x4)(vN) = ...`) that never compiles, so its dylib
-- never builds (the diff oracle for spectral is therefore scalar-only). synthc is
-- the production compiler, so it must keep these loops scalar to actually render.
fn _isSpectralBodyGraph(ctx Ctx, g Int) Bool {
	for (n : ctx.sorted) {
		match (ctx.kind[n]) {
			spectralChainK(_, _): { if (ctx.graphOf[ctx.subs[n][0]] == g) { return true; } }
			_: {}
		}
	}
	false
}

-- True if any expr in the loop's trees disqualifies SIMD (vec-reorder, compare,
-- select, reduce, spectral, RNG, debug, buffers), or the loop lives in a spectral
-- frame body. Shared by the scalar and flat-voice width decisions.
fn _loopHasSimdDisqualifier(ctx Ctx, loop Loop) Bool {
	if (ctx _isSpectralBodyGraph(ctx.graphOf[ctx.trees[loop.trees[0]].root])) { return true; }
	for (t : loop.trees) {
		for (e : ctx.trees[t].exprs) {
			if (ctx _exprSimdDisqualified(e)) { return true; }
		}
	}
	false
}

-- Pick width 4 (the only reachable width at the oracle's max=min=4) or 0, from a
-- lane count. Mirrors the tail of simdWidth().
fn _simdWidthForCount(count Int) Int {
	let w = `cgSimdWidth;
	if (count < 2) { return 0; }
	if (count % 4 == 0 && w >= 4) { return 4; }
	if (count % 2 == 0 && w <= 2) { return 2; }
	0
}

-- The SIMD width for a plain (non-flat-voice) loop (0 = scalar). Mirrors simdWidth().
-- The diff oracle runs max=min=cgSimdWidth, so only width 4 (count % 4 == 0) is
-- reachable; the width-2 branch needs min<=2, never true at width 4.
fn _loopSimdWidth(ctx Ctx, loop Loop) Int {
	let w = `cgSimdWidth;
	if (w < 2) { return 0; }
	if (loop.isControlFlow) { return 0; }
	if (`cgVoiceCount > 0) { return 0; }
	if (loop.chans < 2) { return 0; }
	if (ctx _loopHasSimdDisqualifier(loop)) { return 0; }
	_simdWidthForCount(loop.chans)
}

-- The SIMD width for a flat-voice loop (M5.3). The lane count is the flat
-- voices x channels trip count (or loop.chans for an already-voice-expanded phi
-- loop), mirroring simdWidth()'s flat-voice totalCount.
fn _flatLoopSimdWidth(ctx Ctx, loop Loop, trip Int) Int {
	let w = `cgSimdWidth;
	if (w < 2) { return 0; }
	if (loop.isControlFlow) { return 0; }
	if (`cgSingleVoice) { return 0; }   -- noteOn reset loops are scalar
	if (ctx _loopHasSimdDisqualifier(loop)) { return 0; }
	_simdWidthForCount(trip)
}

-- The maxVoices of the voicer whose body is graph g (0 if g is not a voice graph).
fn _voicerMaxForGraph(ctx Ctx, g Int) Int {
	for (v : ctx _voicerNodes) { if (ctx _voicerBodyGraph(v) == g) { return ctx _voicerMaxVoices(v); } }
	0
}

-- The per-voice channel count of a voicer body (= the body root's chans).
fn _voicerChansForGraph(ctx Ctx, g Int) Int {
	for (v : ctx _voicerNodes) {
		if (ctx _voicerBodyGraph(v) == g) { return ctx.chans[ctx.ins[ctx.subs[v][0]][0]]; }
	}
	1
}

fn _log2(n Int) Int { var s = 0; var c = n; while (c > 1) { c = c // 2; s = s + 1; } s }

-- Is loop a PhiNode loop (its chans are already voice-expanded)?
fn _isPhiLoop(ctx Ctx, loop Loop) Bool {
	for (t : loop.trees) { if (ctx.kind[ctx.trees[t].root] isPhiKind) { return true; } }
	false
}

-- Flat-voice index for a node/delay of `chans` channels at flat loop index `cel`.
-- voice = cel >> shift, chan = cel & (chans-1). Mirrors the C++ flat-voice
-- genVarRef SoA indexing: full-width -> [cel]; mono -> [voice]; partial -> wrap.
fn _flatVoiceIdx(ctx Ctx, chans Int, cel String) String {
	let shift = `cgFlatChanShift;
	if (chans == 1) { return shift == 0 ? "[%^]" fmt(cel) : "[%^ >> %^]" fmt(cel, shift); }
	if (chans == `cgLoopChans) { return "[%^]" fmt(cel); }
	"[(%^ >> %^) * %^ + (%^ & %^)]" fmt(cel, shift, chans, cel, chans - 1)
}

fn genLoop(ctx Ctx, loopIdx Int) String {
	let loop = ctx.loops[loopIdx];
	if (loop.chans == 0) { return ""; }
	-- Auto-enter flat voice mode for a voicer-body loop reached outside _genVoicer
	-- (e.g. the body's init-rate loop emitted from genInitFun).
	let rootGraph = ctx.graphOf[ctx.trees[loop.trees[0]].root];
	if (`cgVoiceCount == 0 && ctx _isVoiceGraph(rootGraph)) {
		var `cgVoiceCount Int = ctx _voicerMaxForGraph(rootGraph);
		var `cgVoiceGraph Int = rootGraph;
		var `cgVoiceChans Int = ctx _voicerChansForGraph(rootGraph);
		return genLoop(ctx, loopIdx);
	}
	-- In flat voice mode the loop iterates voices x channels. cgLoopChans stays
	-- the loop's per-voice chans; the flat shift/mask and trip count are derived
	-- from whether this is a phi loop (voice-expanded) or a compute loop.
	let flat = `cgVoiceCount > 0;
	var `cgLoopChans Int = loop.chans;
	let isPhi = flat && ctx _isPhiLoop(loop);
	-- per-voice channel count for shift/mask: bodyChans for a phi loop, the loop's
	-- own chans for a compute loop.
	let perVoiceChans = isPhi ? `cgVoiceChans : loop.chans;
	var `cgFlatChanShift Int = (flat && perVoiceChans > 1) ? _log2(perVoiceChans) : 0;
	var `cgFlatChanMask Int = (flat && perVoiceChans > 1) ? perVoiceChans - 1 : 0;
	let trip = flat ? (isPhi ? loop.chans : `cgVoiceCount * loop.chans) : loop.chans;

	-- Sort antecedent serials so the LOOP comment is deterministic (the C++
	-- loop_antecedents is an unordered_set; both sides sort ascending to match).
	var ante [String] = [];
	for (a : loop.antecedents sort) { ante push!(a toString); }
	var s = "\n" $ _tabs(`cgIndent) $ "// LOOP %^ [%^] %^ %^\n"
		fmt(padLeft(loop.serial toString, 2), ante join(" "), loop.rate rateStr, loop.chans);

	-- Declare temp-var roots not consumed in their loop: multichannel temps, and
	-- control-flow nodes whose value is assigned inside the block (so they cannot
	-- be declared inline like a scalar temp). The C++ hardcodes one tab here,
	-- regardless of nesting depth. In flat voice mode a body temp is a per-voice
	-- array sized [maxVoices * chans].
	if (flat || loop.chans > 1 || loop.isControlFlow) {
		var decls = "";
		for (t : loop.trees) {
			let root = ctx.trees[t].root;
			if (ctx.cut[root] isTempVarCut && !_consumedInLoop(ctx, t)) {
				let shp = flat ? "[%^]" fmt(`cgVoiceCount * ctx.chans[root]) : _shape(ctx.chans[root]);
				decls = decls $ "\t%^ %^%^;\n" fmt(cppType(ctx.typ[root]), _varName(ctx, root), shp);
			}
		}
		if (decls length > 0) { s = s $ decls $ "\n"; }
	}

	var code = "";
	if (flat) {
		-- Flat voice mode: a per-voice (x channel) loop, body indexed by voice/chan.
		-- The lane count is the flat trip (voices x chans); SIMD strides it (M5.3).
		let simdW = ctx _flatLoopSimdWidth(loop, trip);
		if (simdW > 0) {
			code = code $ _tabs(`cgIndent) $ "// SIMD %^x (flat voice)\n" fmt(simdW);
			var `cgSimdW Int = simdW;
			if (trip == simdW) {
				-- Case A: trip == width -> a single vector op, no loop (cel = 0).
				for (t : loop.trees) { code = code $ genTree(ctx, t, "0"); }
			} else {
				code = code $ _tabs(`cgIndent) $ "for (usize i = 0; i < %^; i += %^) {\n" fmt(trip, simdW);
				var `cgIndent Int = `cgIndent + 1;
				for (t : loop.trees) { code = code $ genTree(ctx, t, "i"); }
				code = code $ _tabs(`cgIndent - 1) $ "}\n";
			}
		} else {
			-- Single-voice mode (reset loops in _noteOn): emit for the voice
			-- `vi` in scope; the usual `i >> shift` derivation then yields vi.
			if (`cgSingleVoice) {
				if (loop.chans == 1) {
					code = code $ _tabs(`cgIndent) $ "{ int i = vi;\n";
				} else {
					code = code $ _tabs(`cgIndent) $ "for (int i = vi * %^; i < vi * %^ + %^; ++i) {\n" fmt(loop.chans, loop.chans, loop.chans);
				}
			} else {
				code = code $ _tabs(`cgIndent) $ "for (int i = 0; i < %^; ++i) {\n" fmt(trip);
			}
			var `cgIndent Int = `cgIndent + 1;
			for (t : loop.trees) { code = code $ genTree(ctx, t, "i"); }
			code = code $ _tabs(`cgIndent - 1) $ "}\n";
		}
	} else if (loop.isControlFlow) {
		-- A control-flow loop emits its block directly (no channel loop, even
		-- multichannel); the branches' own subgraph loops handle the channels.
		code = code $ genTree(ctx, loop.trees[0], "0");
	} else if (loop.trees length == 1 && getsOwnLoop(ctx.kind[ctx.trees[loop.trees[0]].root])) {
		-- put/join: the tree builds its output array in a block, no per-channel loop.
		code = code $ genTree(ctx, loop.trees[0], "0");
	} else if (loop.chans == 1) {
		for (t : loop.trees) { code = code $ genTree(ctx, t, "0"); }
	} else {
		let simdW = ctx _loopSimdWidth(loop);
		if (simdW > 0) {
			code = code $ _tabs(`cgIndent) $ "// SIMD %^x\n" fmt(simdW);
			var `cgSimdW Int = simdW;
			if (loop.chans == simdW) {
				-- Case A: chans == width -> a single vector op, no loop (cel = 0).
				for (t : loop.trees) { code = code $ genTree(ctx, t, "0"); }
			} else {
				-- Case B: stride loop over the channels.
				code = code $ _tabs(`cgIndent) $ "for (usize i = 0; i < %^; i += %^) {\n" fmt(loop.chans, simdW);
				var `cgIndent Int = `cgIndent + 1;
				for (t : loop.trees) { code = code $ genTree(ctx, t, "i"); }
				code = code $ _tabs(`cgIndent - 1) $ "}\n";
			}
		} else {
			code = code $ _tabs(`cgIndent) $ "for (usize i = 0; i < %^; ++i) {\n" fmt(loop.chans);
			var `cgIndent Int = `cgIndent + 1;
			for (t : loop.trees) { code = code $ genTree(ctx, t, "i"); }
			code = code $ _tabs(`cgIndent - 1) $ "}\n";
		}
	}
	if (code length > 0) { s = s $ code $ "\n"; }
	s
}

fn genLoops(ctx Ctx, loopIdxs [Int]) String {
	var s = "";
	for (li : loopIdxs) { s = s $ genLoop(ctx, li); }
	s
}

---------------------------------------------------------------------------
-- Struct member declarations

fn genDeclConstVars(ctx Ctx) String {
	var s = "";
	for (n : ctx.sorted) {
		match (ctx.kind[n]) {
			constant(v, _): {
				if (v constSize > 1) {
					s = s $ "\t%^ c%^%^;\n" fmt(cppType(ctx.typ[n]), ctx.serial[n], _shape(ctx.chans[n]));
				}
			}
			_: {}
		}
	}
	s length > 0 ? "\t// constants\n" $ s $ "\n" : ""
}

fn genDelayDecls(ctx Ctx) String {
	-- Root-graph delays only; voicer-body delays are per-voice (genVoicerDecls).
	var root [DelayInfo] = [];
	for (d : ctx.delays) { if (!(ctx _isVoiceGraph(ctx _delayGraph(d)))) { root push!(d); } }
	if (root length == 0) { return ""; }
	var s = "\t// delays\n";
	for (d : root) {
		let base = "\t%^" fmt(cppType(d.typ));
		if (d.allocSize == 1) {
			s = s $ base $ " d%^%^;\n" fmt(d.serial, _shape(d.chans));
		} else if (d.allocSize > 1) {
			s = s $ base $ " d%^%^[%^];\n" fmt(d.serial, _shape(d.chans), d.allocSize);
		} else {
			s = s $ base $ " *d%^%^;\n" fmt(d.serial, _shape(d.chans));
		}
	}
	s = s $ "\n";
	for (d : root) {
		if (d.allocSize != 1) { s = s $ "\tu64 d%^_wrpos;\n" fmt(d.serial); }
		if (d.hasMaxBound) { s = s $ "\tu64 d%^_mask;\n" fmt(d.serial); }
	}
	s
}

-- Per-voice delay struct fields for a voicer body (SoA): each delay's state is
-- replicated across maxVoices. M4.1 supports 1-sample delays (single value per
-- voice); ring-buffer delays in voices are M4.2.
fn genVoiceDelayDecls(ctx Ctx, bg Int, mv Int) String {
	var data = "";
	var extra = "";
	for (d : ctx.delays) {
		if (ctx _delayGraph(d) == bg) {
			let base = "\t%^" fmt(cppType(d.typ));
			let slots = mv * d.chans;
			if (d.allocSize == 1) {
				data = data $ base $ " voice_d%^[%^];\n" fmt(d.serial, slots);
			} else if (d.allocSize > 1) {
				data = data $ base $ " voice_d%^[%^][%^];\n" fmt(d.serial, slots, d.allocSize);
			} else {
				data = data $ base $ "* voice_d%^[%^];\n" fmt(d.serial, slots);
			}
			-- wrpos / mask are per-voice (one ring head shared by a voice's
			-- channels); only the delay data is per-voice-per-channel.
			if (d.allocSize != 1) { extra = extra $ "\tu64 voice_d%^_wrpos[%^];\n" fmt(d.serial, mv); }
			if (d.hasMaxBound) { extra = extra $ "\tu64 voice_d%^_mask[%^];\n" fmt(d.serial, mv); }
		}
	}
	if (data length == 0) { return ""; }
	"\t// per-voice delays (SoA)\n" $ data $ "\n" $ extra
}

-- Graphs that use RNG (contain a urand/birand/rand64 node), in graph order.
-- Each gets its own RandState (rgenN, N = graph serial), mirroring the C++ where
-- a branch with RNG has its RandState scoped to that subgraph.
fn _rngGraphs(ctx Ctx) [Int] {
	var uses [Bool] = false repeat(ctx.graphs length);
	for (n : ctx.sorted) { if (ctx.kind[n] isRngKind) { uses[ctx.graphOf[n]] = true; } }
	var out [Int] = [];
	var g = 0;
	while (g < ctx.graphs length) { if (uses[g]) { out push!(g); } g = g + 1; }
	out
}

fn genDeclInstVars(ctx Ctx) String {
	var s = "";
	-- RNG state (graph serial 0 in M2's single-graph synths), emitted first to
	-- match the C++ genDeclInstVars ordering. Voicer-body RNG is per-voice and
	-- emitted in the voicer block below, so skip it here.
	for (g : ctx _rngGraphs) { if (!(ctx _isVoiceGraph(g))) { s = s $ "\tRandState1 rgen%^;\n" fmt(g); } }
	-- inst-var roots, in loop/tree order. Voicer-body inst vars are per-voice and
	-- emitted in the voicer block below, so skip them here.
	for (lp : ctx.loops) {
		for (t : lp.trees) {
			let root = ctx.trees[t].root;
			let isInletOutlet = match (ctx.kind[root]) { inletK(_, _): true; outletK(_, _): true; _: false; };
			let inVoice = ctx _isVoiceGraph(ctx.graphOf[root]);
			if (ctx _isInstVar(root) && !isInletOutlet && !inVoice) {
				s = s $ "\t%^ %^%^;\n" fmt(cppType(ctx.typ[root]), _varDeclName(ctx, root), _shape(ctx.chans[root]));
			}
		}
	}
	-- control activation flags
	for (cn : ctx.controls) {
		match (ctx.kind[cn]) { control(_, _, sn, _): { s = s $ "\tbool ctrl%^_active;\n" fmt(sn); } _: {} }
	}
	if (s length > 0) { s = "\t// instance variables\n" $ s $ "\n"; }
	s = s $ genVoicerDecls(ctx);
	s = s $ genSpectralDecls(ctx);
	s = s $ genDelayDecls(ctx);
	s = s $ genBufDecls(ctx);
	s = s $ genBankDecls(ctx);
	s = s $ genBankSlotDeclsScalar(ctx);
	s
}

-- Per-spectral-chain struct fields (FFT setup, in/out ring buffers, fft/frame/
-- window buffers, ring positions, hop counter). Mirrors CppCodeGen::genClass.
fn genSpectralDecls(ctx Ctx) String {
	var s = "";
	for (sc : ctx _spectralChains) {
		let cs = ctx.serial[sc];
		let ic = ctx.chans[sc];
		match (ctx.kind[sc]) {
			spectralChainK(fft, hop): {
				s = s $ "\t// SpectralChain %^ (fftSize=%^, hopSize=%^)\n" fmt(cs, fft, hop);
				s = s $ "\tJscsFFTSetup* spec%^_fftsetup;\n" fmt(cs);
				s = s $ "\tf32* spec%^_inbuf[%^];\n" fmt(cs, ic);
				s = s $ "\tf32* spec%^_outbuf[%^];\n" fmt(cs, ic);
				s = s $ "\tf32 spec%^_fftbuf[%^];\n" fmt(cs, ic * fft);
				s = s $ "\tf32 spec%^_frame[%^];\n" fmt(cs, ic * fft);
				s = s $ "\tf32 spec%^_window[%^];\n" fmt(cs, fft);
				s = s $ "\tint spec%^_wrpos;\n" fmt(cs);
				s = s $ "\tint spec%^_rdpos;\n" fmt(cs);
				s = s $ "\tint spec%^_hopcount;\n" fmt(cs);
			}
			_: {}
		}
	}
	if (s length > 0) { s = s $ "\n"; }
	s
}

-- Per-voicer struct fields: the body's per-voice instance vars (in body
-- loop/tree order), then the RowVoicer and its parameter matrix. Mirrors the
-- voicer field emission in CppCodeGen::genClass.
fn genVoicerDecls(ctx Ctx) String {
	var s = "";
	for (v : ctx _voicerNodes) {
		let mv = ctx _voicerMaxVoices(v);
		let bg = ctx _voicerBodyGraph(v);
		-- per-voice RNG state (emitted first, mirroring the C++ ordering)
		for (g : ctx _rngGraphs) { if (g == bg) { s = s $ "\tRandState1 voice_rgen%^[%^];\n" fmt(g, mv); } }
		-- per-voice instance vars, in body loop/tree order
		for (lp : ctx.loops) {
			for (t : lp.trees) {
				let root = ctx.trees[t].root;
				if (ctx.graphOf[root] == bg && ctx _isInstVar(root)) {
					s = s $ "\t%^ voice_v%^[%^];\n" fmt(cppType(ctx.typ[root]), ctx.serial[root], mv * ctx.chans[root]);
				}
			}
		}
		-- per-voice delay state
		s = s $ genVoiceDelayDecls(ctx, bg, mv);
		-- per-voice bank lookup slots (SoA), mirroring genBankSlotDeclsFlat
		for (lu : ctx _bankLookupNodes) {
			if (ctx.graphOf[lu] == bg) {
				let u = ctx.serial[lu];
				s = s $ "\ti32 voice_lu%^_p[%^];\n" fmt(u, mv);
				s = s $ "\ti32 voice_lu%^_v[%^];\n" fmt(u, mv);
				s = s $ "\ttzpl_Buffer* voice_lu%^_buf[%^];\n" fmt(u, mv);
				s = s $ "\tf32 voice_lu%^_rootKey[%^];\n" fmt(u, mv);
				s = s $ "\tf64 voice_lu%^_sr[%^];\n" fmt(u, mv);
				s = s $ "\tf64 voice_lu%^_len[%^];\n" fmt(u, mv);
				s = s $ "\tf64 voice_lu%^_loopStart[%^];\n" fmt(u, mv);
				s = s $ "\tf64 voice_lu%^_loopEnd[%^];\n" fmt(u, mv);
				s = s $ "\ti32 voice_lu%^_hasLoop[%^];\n" fmt(u, mv);
			}
		}
		let nUser = ctx _numUserParams(bg);
		s = s $ "\tRowVoicer<%^, %^> voicer;\n" fmt(mv, nUser);
		s = s $ "\tf32 voicer_params[%^][%^];\n" fmt(mv, 1 + nUser);
	}
	if (s length > 0) { s = s $ "\n"; }
	s
}

-- Sample buffer pointers (one per SampleBuf), emitted after the delay decls to
-- match the C++ genClass ordering.
fn genBufDecls(ctx Ctx) String {
	var s = "";
	for (ser : ctx.bufSerials) { s = s $ "\ttzpl_Buffer* buf%^;\n" fmt(ser); }
	s
}

-- Sample bank pointers (one per SampleBank), after the buffer decls to match
-- the C++ genDeclInstVars ordering.
fn genBankDecls(ctx Ctx) String {
	var s = "";
	for (ser : ctx.bankSerials) { s = s $ "\ttzpl_SampleBank* bank%^;\n" fmt(ser); }
	s
}

-- Top-level (non-voicer) lookup slots, mirroring genBankSlotDeclsScalar.
fn genBankSlotDeclsScalar(ctx Ctx) String {
	var s = "";
	for (lu : ctx _bankLookupNodes) {
		if (!(ctx _isVoiceGraph(ctx.graphOf[lu]))) {
			let u = ctx.serial[lu];
			s = s $ "\ti32 lu%^_p;\n" fmt(u);
			s = s $ "\ti32 lu%^_v;\n" fmt(u);
			s = s $ "\ttzpl_Buffer* lu%^_buf;\n" fmt(u);
			s = s $ "\tf32 lu%^_rootKey;\n" fmt(u);
			s = s $ "\tf64 lu%^_sr;\n" fmt(u);
			s = s $ "\tf64 lu%^_len;\n" fmt(u);
			s = s $ "\tf64 lu%^_loopStart;\n" fmt(u);
			s = s $ "\tf64 lu%^_loopEnd;\n" fmt(u);
			s = s $ "\ti32 lu%^_hasLoop;\n" fmt(u);
		}
	}
	s
}

---------------------------------------------------------------------------
-- Lifecycle functions

fn genAllocFun(ctx Ctx, name String) String =
	"%^* %^_alloc() {\n\t%^* p = (%^*)calloc(1, sizeof(%^));\n\tp->funs = %^_funs;\n\treturn p;\n}\n\n"
		fmt(name, name, name, name, name, name);

fn genFreeFun(ctx Ctx, name String) String =
	"tzpl_SErr %^_free(%^* p) {\n\ttzpl_SErr %^_uninit(%^* p);\n\t%^_uninit(p);\n\tfree(p);\n\treturn tzpl_errNone;\n}\n\n"
		fmt(name, name, name, name, name);

fn genInitConstants(ctx Ctx) String {
	var s = "";
	for (n : ctx.sorted) {
		match (ctx.kind[n]) {
			constant(v, _): {
				if (v constSize > 1) {
					s = s $ "\tstatic %^ k%^%^ = %^;\n" fmt(cppType(ctx.typ[n]), ctx.serial[n], _shape(ctx.chans[n]), constStr(v, ctx.typ[n]));
					s = s $ "\tmemcpy(p->c%^, k%^, %^ * sizeof(%^));\n" fmt(ctx.serial[n], ctx.serial[n], ctx.chans[n], cppType(ctx.typ[n]));
				}
			}
			_: {}
		}
	}
	s
}

fn genDelayInit(ctx Ctx) String {
	var s = "";
	for (d : ctx.delays) {
		-- Per-voice delays are zeroed in noteOn, not initialized synth-wide.
		if (ctx _isVoiceGraph(ctx _delayGraph(d))) { continue; }
		for (n : d.initters) {
			match (ctx.kind[n]) {
				delayInitK(_, offset): {
					let value = genExpr(ctx, ctx.ins[n][0], "0");
					if (d.chans == 1) {
						if (d.allocSize == 1) {
							s = s $ "\tp->d%^ = %^;\n" fmt(d.serial, value);
						} else if (d.allocSize > 1) {
							s = s $ "\tp->d%^[(0ull - %^ull) & %^] = %^;\n" fmt(d.serial, offset, d.allocSize - 1, value);
						} else {
							s = s $ "\tp->d%^[(0ull - %^ull) & p->d%^_mask] = %^;\n" fmt(d.serial, offset, d.serial, value);
						}
					} else {
						var j = 0;
						while (j < d.chans) {
							if (d.allocSize == 1) {
								s = s $ "\tp->d%^[%^] = %^;\n" fmt(d.serial, j, value);
							} else if (d.allocSize > 1) {
								s = s $ "\tp->d%^[%^][(0ull - %^ull) & %^] = %^;\n" fmt(d.serial, j, offset, d.allocSize - 1, value);
							} else {
								s = s $ "\tp->d%^[%^][(0ull - %^ull) & p->d%^_mask] = %^;\n" fmt(d.serial, j, offset, d.serial, value);
							}
							j = j + 1;
						}
					}
				}
				_: {}
			}
		}
	}
	s
}

-- Per-delay buffer setup at init: write position, mask, and (for runtime-sized
-- delays) the calloc'd ring buffer. Mirrors C++ CppCodeGen::genDelayAlloc.
--   allocSize == 1 : scalar delay, nothing to set up here.
--   allocSize  > 1 : compile-time ring buffer, mask is the constant allocSize-1.
--   allocSize == 0 : runtime-sized -- size = nextPowerOfTwo(headroom + ceil(max
--                    delay)), mask = size-1, buffer calloc'd (freed in uninit).
-- The maxDelay bound is emitted in init mode so constants inline directly.
fn genDelayAlloc(ctx Ctx) String {
	var s = "";
	var `cgInInit Bool = true;
	for (d : ctx.delays) {
		-- Per-voice delays set up their state per voice (M4.2 for ring buffers).
		if (ctx _isVoiceGraph(ctx _delayGraph(d))) { continue; }
		if (d.allocSize != 1) { s = s $ "\tp->d%^_wrpos = 0;\n" fmt(d.serial); }
		if (d.allocSize >= 1) {
			if (d.allocSize > 1) { s = s $ "\tp->d%^_mask = %^;\n" fmt(d.serial, d.allocSize - 1); }
		} else if (d.maxDelay != NONE) {
			let headroom = max(4, d.maxOverread);
			let maxExpr = genExpr(ctx, ctx.ins[d.maxDelay][0], "0");
			let ty = cppType(d.typ);
			s = s $ "\tu64 d%^_size = nextPowerOfTwo(%^+u64(ceil(%^)));\n" fmt(d.serial, headroom, maxExpr);
			s = s $ "\tp->d%^_mask = d%^_size - 1;\n" fmt(d.serial, d.serial);
			if (d.chans == 1) {
				s = s $ "\tp->d%^ = (%^*)calloc(d%^_size, sizeof(%^));\n" fmt(d.serial, ty, d.serial, ty);
			} else {
				var j = 0;
				while (j < d.chans) {
					s = s $ "\tp->d%^[%^] = (%^*)calloc(d%^_size, sizeof(%^));\n" fmt(d.serial, j, ty, d.serial, ty);
					j = j + 1;
				}
			}
		}
	}
	s
}

-- Per-voice ring-buffer delay setup at init (M4.2). For each voicer-body delay
-- with allocSize != 1: reset every voice's write head; then set the per-voice
-- mask (constant for a fixed ring) or, for a runtime-sized ring, compute the
-- size once and calloc each voice's buffer. Mirrors the C++ flat-voice
-- genDelayAlloc.
fn genVoiceDelayAlloc(ctx Ctx) String {
	var s = "";
	var `cgInInit Bool = true;
	for (d : ctx.delays) {
		let g = ctx _delayGraph(d);
		if (!(ctx _isVoiceGraph(g)) || d.allocSize == 1) { continue; }
		let mv = ctx _voicerMaxForGraph(g);
		s = s $ "\tfor (int v = 0; v < %^; ++v) p->voice_d%^_wrpos[v] = 0;\n" fmt(mv, d.serial);
		if (d.allocSize > 1) {
			s = s $ "\tfor (int v = 0; v < %^; ++v) p->voice_d%^_mask[v] = %^;\n" fmt(mv, d.serial, d.allocSize - 1);
		} else if (d.maxDelay != NONE) {
			let headroom = max(4, d.maxOverread);
			let maxExpr = genExpr(ctx, ctx.ins[d.maxDelay][0], "0");
			let ty = cppType(d.typ);
			s = s $ "\t{\n";
			s = s $ "\t\tu64 d%^_size = nextPowerOfTwo(%^+u64(ceil(%^)));\n" fmt(d.serial, headroom, maxExpr);
			s = s $ "\t\tfor (int v = 0; v < %^; ++v) {\n" fmt(mv);
			s = s $ "\t\t\tp->voice_d%^_mask[v] = d%^_size - 1;\n" fmt(d.serial, d.serial);
			if (d.chans == 1) {
				s = s $ "\t\t\tp->voice_d%^[v] = (%^*)calloc(d%^_size, sizeof(%^));\n" fmt(d.serial, ty, d.serial, ty);
			} else {
				var j = 0;
				while (j < d.chans) {
					s = s $ "\t\t\tp->voice_d%^[v * %^ + %^] = (%^*)calloc(d%^_size, sizeof(%^));\n" fmt(d.serial, d.chans, j, ty, d.serial, ty);
					j = j + 1;
				}
			}
			s = s $ "\t\t}\n\t}\n";
		}
	}
	s
}

fn genInitFun(ctx Ctx, name String) String {
	var s = "tzpl_SErr %^_init(%^* p) {\n\tf64 fs = p->fs;\n\tp->sd = 1./fs;\n" fmt(name, name);
	s = s $ genInitConstants(ctx);
	-- Synth-wide RNG seeds up front; voicer-body RNG is seeded per voice below.
	for (g : ctx _rngGraphs) { if (!(ctx _isVoiceGraph(g))) { s = s $ "\tarc4seedrand(p->rgen%^);\n" fmt(g); } }
	-- inInitMode is only for genDelayAlloc (runtime delay sizing); init LOOPS
	-- reference cross-tree roots as variables just like audio loops do.
	s = s $ genLoops(ctx, ctx.initLoops);
	s = s $ genDelayAlloc(ctx);
	s = s $ genSpectralAlloc(ctx);
	s = s $ genDelayInit(ctx);
	s = s $ genVoiceDelayAlloc(ctx);
	-- Buffer pointers start null; the host swaps real buffers in at runtime.
	-- (Before the voicer block, matching the C++ genInitFun ordering.)
	for (ser : ctx.bufSerials) { s = s $ "\tp->buf%^ = nullptr;\n" fmt(ser); }
	-- Sample bank pointers start null; lookup slots fill in the reset loops.
	for (ser : ctx.bankSerials) { s = s $ "\tp->bank%^ = nullptr;\n" fmt(ser); }
	-- Voicer: point the voicer at its parameter matrix, zero it, and seed each
	-- voice's RNG.
	for (v : ctx _voicerNodes) {
		s = s $ "\tp->voicer.setParams((f32*)p->voicer_params);\n";
		s = s $ "\tmemset(p->voicer_params, 0, sizeof(p->voicer_params));\n";
		let mv = ctx _voicerMaxVoices(v);
		let bg = ctx _voicerBodyGraph(v);
		for (g : ctx _rngGraphs) {
			if (g == bg) {
				s = s $ "\tfor (int v = 0; v < %^; ++v) {\n\t\tarc4seedrand(p->voice_rgen%^[v]);\n\t}\n" fmt(mv, g);
			}
		}
	}
	-- Reset-rate loops run once at init so every slot holds a defined value
	-- before the first note-on. Non-voicer graphs latch here permanently
	-- (Rate.reset degenerates to init when there are no notes); voicer loops
	-- run over all voices (genLoop auto-enters flat voice mode).
	var `cgInResetMode Bool = true;
	for (li : ctx.resetLoops) {
		if (!(ctx _isVoiceGraph(_loopRootGraph(ctx, li)))) { s = s $ genLoop(ctx, li); }
	}
	for (li : ctx.resetLoops) {
		if (ctx _isVoiceGraph(_loopRootGraph(ctx, li))) { s = s $ genLoop(ctx, li); }
	}
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn _loopRootGraph(ctx Ctx, li Int) Int =
	ctx.graphOf[ctx.trees[ctx.loops[li].trees[0]].root];

-- The voice note-lifecycle functions (noteOn/noteOff/allNotesOff/setParams/
-- setParamRange). Mirrors CppCodeGen's note-function emission. noteOn allocates
-- a voice via the RowVoicer, fills user-param defaults, and zeros the per-voice
-- AUDIO-rate instance vars (init-rate vars are constants, left as init set them).
fn genVoicerNoteFuns(ctx Ctx, name String) String {
	if (ctx _voicerNodes length == 0) { return ""; }
	let v = ctx _voicerNodes [0];
	let bg = ctx _voicerBodyGraph(v);
	let nUser = ctx _numUserParams(bg);
	var s = "";
	-- noteOn
	s = s $ "tzpl_SErr %^_noteOn(%^* p, i64 now, int noteID, int n, f32* params) {\n" fmt(name, name);
	s = s $ "\tint vi;\n";
	s = s $ "\ttzpl_SErr err = p->voicer.noteOn(now, noteID, n, params, vi);\n";
	s = s $ "\tif (err != tzpl_errNone) return err;\n";
	if (nUser > 0) {
		var defs [String] = [];
		for (np : ctx _voicerNoteParams(bg)) {
			if (ctx _noteParamSerial(np) != 0) {
				match (ctx.kind[np]) { noteParamK(spec, _, _): { defs push!(ftosF64(spec.init) $ "f"); } _: {} }
			}
		}
		s = s $ "\tstatic const f32 defaults[%^] = {%^};\n" fmt(nUser, defs join(", "));
		s = s $ "\tf32* row = p->voicer.getRow(vi);\n";
		s = s $ "\tif (n < %^) {\n" fmt(nUser);
		s = s $ "\t\tmemcpy(row + n + 1, defaults + n, (%^ - n) * sizeof(f32));\n" fmt(nUser);
		s = s $ "\t}\n";
	}
	-- zero per-voice audio-rate inst vars (in body loop/tree order)
	for (lp : ctx.loops) {
		for (t : lp.trees) {
			let root = ctx.trees[t].root;
			if (ctx.graphOf[root] == bg && ctx _isInstVar(root) && ctx.nrate[root] != Rate.init) {
				s = s $ "\tp->voice_v%^[vi] = 0;\n" fmt(ctx.serial[root]);
			}
		}
	}
	-- zero per-voice delay state: 1-sample delays clear their single slot; ring
	-- buffers reset their write head, and a FIXED ring also clears its inline
	-- buffer (a runtime ring's calloc'd buffer is already clear from init).
	for (d : ctx.delays) {
		if (ctx _delayGraph(d) == bg) {
			if (d.allocSize == 1) {
				if (d.chans > 1) {
					s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c] = 0;\n" fmt(d.chans, d.serial, d.chans);
				} else {
					s = s $ "\tp->voice_d%^[vi] = 0;\n" fmt(d.serial);
				}
			} else {
				s = s $ "\tp->voice_d%^_wrpos[vi] = 0;\n" fmt(d.serial);
				if (d.allocSize > 1) {
					let ty = cppType(d.typ);
					if (d.chans > 1) {
						s = s $ "\tfor (int c = 0; c < %^; ++c) memset(p->voice_d%^[vi * %^ + c], 0, %^ * sizeof(%^));\n" fmt(d.chans, d.serial, d.chans, d.allocSize, ty);
					} else {
						s = s $ "\tmemset(p->voice_d%^[vi], 0, %^ * sizeof(%^));\n" fmt(d.serial, d.allocSize, ty);
					}
				}
			}
		}
	}
	-- re-seed this voice's RNG
	for (g : ctx _rngGraphs) {
		if (g == bg) { s = s $ "\tarc4seedrand(p->voice_rgen%^[vi]);\n" fmt(g); }
	}
	-- Reset-rate loops: per-note setup for the newly allocated voice, run
	-- after the zeroing above so reset-rate inst vars are recomputed for
	-- this note (mirrors the C++ genNoteFuns emission).
	var hasReset = false;
	for (li : ctx.resetLoops) {
		if (ctx _isVoiceGraph(_loopRootGraph(ctx, li))) { hasReset = true; }
	}
	if (hasReset) {
		var `cgInResetMode Bool = true;
		var `cgSingleVoice Bool = true;
		var `cgVoiceCount Int = ctx _voicerMaxVoices(v);
		var `cgVoiceGraph Int = bg;
		var `cgVoiceChans Int = ctx _voicerChansForGraph(bg);
		for (li : ctx.resetLoops) {
			if (ctx _isVoiceGraph(_loopRootGraph(ctx, li))) { s = s $ genLoop(ctx, li); }
		}
	}
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	-- noteOff / allNotesOff
	s = s $ "tzpl_SErr %^_noteOff(%^* p, i64 now, int noteID) {\n\treturn p->voicer.noteOff(now, noteID);\n}\n\n" fmt(name, name);
	s = s $ "tzpl_SErr %^_allNotesOff(%^* p, i64 now) {\n\tp->voicer.allOff(now);\n\treturn tzpl_errNone;\n}\n\n" fmt(name, name);
	-- noteSetParams / noteSetParamRange
	s = s $ "tzpl_SErr %^_noteSetParams(%^* p, int noteID, int n, tzpl_ParamPair* params) {\n" fmt(name, name);
	s = s $ "\tint vi = p->voicer.findVoice(noteID);\n\tif (vi < 0) return tzpl_errNoteNotFound;\n";
	s = s $ "\tp->voicer.setNoteParams(vi, n, params);\n\treturn tzpl_errNone;\n}\n\n";
	s = s $ "tzpl_SErr %^_noteSetParamRange(%^* p, int noteID, int first, int len, f32* vals) {\n" fmt(name, name);
	s = s $ "\tint vi = p->voicer.findVoice(noteID);\n\tif (vi < 0) return tzpl_errNoteNotFound;\n";
	s = s $ "\tp->voicer.setNoteParamRange(vi, first, len, vals);\n\treturn tzpl_errNone;\n}\n\n";
	s
}

-- Per-spectral-chain init: create the FFT setup, calloc per-channel ring buffers,
-- zero positions, and build the sqrt-Hann window. Mirrors CppCodeGen genInitFun.
fn genSpectralAlloc(ctx Ctx) String {
	var s = "";
	for (sc : ctx _spectralChains) {
		let cs = ctx.serial[sc];
		let ic = ctx.chans[sc];
		match (ctx.kind[sc]) {
			spectralChainK(fft, _): {
				s = s $ "\t// SpectralChain %^ alloc\n" fmt(cs);
				s = s $ "\tp->spec%^_fftsetup = tzpl_fft_create(%^);\n" fmt(cs, fft);
				var c = 0;
				while (c < ic) {
					s = s $ "\tp->spec%^_inbuf[%^] = (f32*)calloc(%^, sizeof(f32));\n" fmt(cs, c, fft);
					s = s $ "\tp->spec%^_outbuf[%^] = (f32*)calloc(%^, sizeof(f32));\n" fmt(cs, c, fft);
					c = c + 1;
				}
				s = s $ "\tp->spec%^_wrpos = 0;\n" fmt(cs);
				s = s $ "\tp->spec%^_rdpos = 0;\n" fmt(cs);
				s = s $ "\tp->spec%^_hopcount = 0;\n" fmt(cs);
				s = s $ "\ttzpl_window_sqrt_hann(p->spec%^_window, %^);\n" fmt(cs, fft);
			}
			_: {}
		}
	}
	s
}

-- Per-spectral-chain teardown (destroy FFT setup, free ring buffers).
fn genSpectralFree(ctx Ctx) String {
	var s = "";
	for (sc : ctx _spectralChains) {
		let cs = ctx.serial[sc];
		let ic = ctx.chans[sc];
		s = s $ "\t// SpectralChain %^ dealloc\n" fmt(cs);
		s = s $ "\ttzpl_fft_destroy(p->spec%^_fftsetup);\n" fmt(cs);
		var c = 0;
		while (c < ic) {
			s = s $ "\tfree(p->spec%^_inbuf[%^]);\n" fmt(cs, c);
			s = s $ "\tfree(p->spec%^_outbuf[%^]);\n" fmt(cs, c);
			c = c + 1;
		}
	}
	s
}

fn genUninitFun(ctx Ctx, name String) String {
	-- M1: only allocSize<1 (runtime) delays need free; fixed delays need none.
	var s = "tzpl_SErr %^_uninit(%^* p) {\n" fmt(name, name);
	s = s $ genSpectralFree(ctx);
	for (d : ctx.delays) {
		if (d.allocSize < 1 && d.hasMaxBound) {
			if (ctx _isVoiceGraph(ctx _delayGraph(d))) {
				-- per-voice runtime ring buffers: free each voice's (chan's) buffer
				let mv = ctx _voicerMaxForGraph(ctx _delayGraph(d));
				s = s $ "\tfor (int v = 0; v < %^; ++v) {\n" fmt(mv);
				if (d.chans == 1) {
					s = s $ "\t\tfree(p->voice_d%^[v]); p->voice_d%^[v] = nullptr;\n" fmt(d.serial, d.serial);
				} else {
					var j = 0;
					while (j < d.chans) { s = s $ "\t\tfree(p->voice_d%^[v * %^ + %^]); p->voice_d%^[v * %^ + %^] = nullptr;\n" fmt(d.serial, d.chans, j, d.serial, d.chans, j); j = j + 1; }
				}
				s = s $ "\t}\n";
			} else if (d.chans == 1) {
				s = s $ "\tfree(p->d%^); p->d%^ = nullptr;\n" fmt(d.serial, d.serial);
			} else {
				var j = 0;
				while (j < d.chans) { s = s $ "\tfree(p->d%^[%^]); p->d%^[%^] = nullptr;\n" fmt(d.serial, j, d.serial, j); j = j + 1; }
			}
		}
	}
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn genResetFun(ctx Ctx, name String) String {
	-- Rate.reset is per-note: reset loops are emitted into _noteOn (for the
	-- scheduled voice) and _init (initial values), never here. This whole-
	-- synth entry point is unused by the engine and left empty. (The FIXME
	-- comment matches the C++ generator verbatim.)
	var s = "tzpl_SErr %^_reset(%^* p) {\n\t// FIXME genResetFun\n" fmt(name, name);
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn genEventFun(ctx Ctx, name String) String {
	var s = "tzpl_SErr %^_event(%^* p, u64 id, tzpl_Slice dst, tzpl_Slice data) {\n" fmt(name, name);
	if (ctx.controls length > 0) {
		s = s $ "\tswitch (id) {\n";
		for (cn : ctx.controls) {
			match (ctx.kind[cn]) {
				control(_, _, sn, _): {
					s = s $ "\t\tcase %^:\n" fmt(sn);
					s = s $ "\t\t\tmemcpy(p->controls[%^], data.data, sizeof(%^) * %^);\n" fmt(sn, cppType(ctx.typ[cn]), ctx.chans[cn]);
					s = s $ "\t\t\tp->ctrl%^_active = true;\n" fmt(sn);
					s = s $ "\t\t\tbreak;\n";
				}
				_: {}
			}
		}
		s = s $ "\t}\n";
	}
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn _ctrlSerialOf(ctx Ctx, n NIdx) Int = match (ctx.kind[n]) { control(_, _, sn, _): sn; _: NONE; };

fn _isoHasControl(ctx Ctx, groupIdx Int, ctrlNode NIdx) Bool {
	for (c : ctx.isoGroups[groupIdx].controls) { if (c == ctrlNode) { return true; } }
	false
}

-- processEvents: without iso-groups, run the event loops directly (the codegen
-- fallback). With iso-groups, run only the groups whose controls changed, in
-- topological order, propagating activations downstream. Mirrors
-- genHandleEventsFun (synthdef_cpp_codegen.cpp:2790).
fn genHandleEventsFun(ctx Ctx, name String) String {
	var s = "void %^_processEvents(%^* p) {\n" fmt(name, name);
	if (ctx.isoGroups length == 0) {
		s = s $ genLoops(ctx, ctx.eventLoops);
		s = s $ "}\n\n";
		return s;
	}
	-- 1. Declare iso-group activation flags (topological order).
	for (g : ctx.isoOrder) { s = s $ "\tbool iso%^ = false;\n" fmt(ctx.isoGroups[g].serial); }
	s = s $ "\n";
	-- 2. Control activation -> iso-group activation.
	for (cn : ctx.controls) {
		let csn = _ctrlSerialOf(ctx, cn);
		s = s $ "\tif (p->ctrl%^_active) {\n" fmt(csn);
		for (g : ctx.isoOrder) {
			if (_isoHasControl(ctx, g, cn)) { s = s $ "\t\tiso%^ = true;\n" fmt(ctx.isoGroups[g].serial); }
		}
		s = s $ "\t\tp->ctrl%^_active = false;\n" fmt(csn);
		s = s $ "\t}\n";
	}
	s = s $ "\n";
	-- 3. Process iso-groups in topological order.
	for (g : ctx.isoOrder) {
		let grp = ctx.isoGroups[g];
		s = s $ "\tif (iso%^) {\n" fmt(grp.serial);
		var inner = "";
		var `cgIndent Int = 2;
		inner = inner $ genLoops(ctx, grp.loops);
		-- propagate activation to downstream groups
		for (d : grp.activates) { inner = inner $ _tabs(2) $ "iso%^ = true;\n" fmt(ctx.isoGroups[d].serial); }
		-- advance event-rate ring-buffer delays written in this group
		for (t : grp.trees) {
			for (e : ctx.trees[t].exprs) {
				match (ctx.kind[e]) {
					delayWriteK(dd): {
						if (ctx.delays[dd].allocSize != 1) {
							inner = inner $ _tabs(2) $ "++p->d%^_wrpos;\n" fmt(ctx.delays[dd].serial);
						}
					}
					_: {}
				}
			}
		}
		s = s $ inner $ "\t}\n";
	}
	s = s $ "}\n\n";
	s
}

-- The graph a delay belongs to (all its nodes share one graph).
fn _delayGraph(ctx Ctx, d DelayInfo) Int {
	if (d.writer != NONE) { ctx.graphOf[d.writer] }
	else if (d.initters length > 0) { ctx.graphOf[d.initters[0]] }
	else if (d.fixReaders length > 0) { ctx.graphOf[d.fixReaders[0]] }
	else if (d.varReaders length > 0) { ctx.graphOf[d.varReaders[0]] }
	else { 0 }
}

-- Advance the write positions of the audio-rate delays in graph `graph` (the C++
-- iterates graph->delayBufs). Called per graph: the root graph in processAudio,
-- each branch/body graph at the end of its control-flow block.
fn genDelayAdvance(ctx Ctx, graph Int) String {
	let voiceG = ctx _isVoiceGraph(graph);
	let mv = voiceG ? ctx _voicerMaxForGraph(graph) : 0;
	var s = "";
	for (d : ctx.delays) {
		if (d.allocSize != 1 && ctx _delayGraph(d) == graph) {
			let evtRate = d.writer != NONE && ctx.nrate[d.writer] == Rate.event;
			if (!evtRate) {
				if (voiceG) {
					s = s $ _tabs(`cgIndent) $ "for (int v = 0; v < %^; ++v) ++p->voice_d%^_wrpos[v];\n" fmt(mv, d.serial);
				} else {
					s = s $ _tabs(`cgIndent) $ "++p->d%^_wrpos;\n" fmt(d.serial);
				}
			}
		}
	}
	if (s length > 0) { s = s $ "\n"; }
	s
}

fn genTickFun(ctx Ctx, name String) String {
	var s = "void %^_processAudio(%^* p) {\n" fmt(name, name);
	-- Root graph's audio loops; subgraph loops are emitted inside their
	-- control-flow blocks (M3.4).
	s = s $ genLoops(ctx, ctx.graphs[0].audioLoops);
	s = s $ genDelayAdvance(ctx, 0);
	s = s $ "}\n\n";
	s
}

fn genFunPtrs(ctx Ctx, name String) String {
	var s = "tzpl_SynthFuns %^_funs = {\n" fmt(name);
	s = s $ "\t.alloc = (tzpl_SynthData* (*)())%^_alloc,\n" fmt(name);
	s = s $ "\t.free = (tzpl_SErr (*)(tzpl_SynthData*))%^_free,\n" fmt(name);
	s = s $ "\t.init = (tzpl_SErr (*)(tzpl_SynthData*))%^_init,\n" fmt(name);
	s = s $ "\t.uninit = (tzpl_SErr (*)(tzpl_SynthData*))%^_uninit,\n" fmt(name);
	s = s $ "\t.reset = (tzpl_SErr (*)(tzpl_SynthData*))%^_reset,\n" fmt(name);
	s = s $ "\t.event = (tzpl_SErr (*)(tzpl_SynthData*, u64, tzpl_Slice, tzpl_Slice))%^_event,\n" fmt(name);
	s = s $ "\t.processEvents = (void (*)(tzpl_SynthData*))%^_processEvents,\n" fmt(name);
	s = s $ "\t.processAudio = (void (*)(tzpl_SynthData*))%^_processAudio,\n" fmt(name);
	if (ctx _voicerNodes length > 0) {
		s = s $ "\t.noteOn = (tzpl_SErr (*)(tzpl_SynthData*, i64, int, int, f32*))%^_noteOn,\n" fmt(name);
		s = s $ "\t.noteOff = (tzpl_SErr (*)(tzpl_SynthData*, i64, int))%^_noteOff,\n" fmt(name);
		s = s $ "\t.allNotesOff = (tzpl_SErr (*)(tzpl_SynthData*, i64))%^_allNotesOff,\n" fmt(name);
		s = s $ "\t.noteSetParams = (tzpl_SErr (*)(tzpl_SynthData*, int, int, tzpl_ParamPair*))%^_noteSetParams,\n" fmt(name);
		s = s $ "\t.noteSetParamRange = (tzpl_SErr (*)(tzpl_SynthData*, int, int, int, f32*))%^_noteSetParamRange,\n" fmt(name);
	}
	if (ctx.bufSerials length > 0) {
		s = s $ "\t.swapBuffer = (tzpl_Buffer* (*)(tzpl_SynthData*, i64, tzpl_Buffer*))%^_swapBuffer,\n" fmt(name);
	}
	s = s $ "};\n\n";
	s
}

-- Runtime buffer swap: the host hands in a new tzpl_Buffer for a given buffer
-- serial and gets the old one back (for NRT cleanup). Emitted only when the
-- synth uses buffers.
fn genSwapBufferFun(ctx Ctx, name String) String {
	if (ctx.bufSerials length == 0) { return ""; }
	var s = "tzpl_Buffer* %^_swapBuffer(%^* p, i64 bufID, tzpl_Buffer* newBuf) {\n" fmt(name, name);
	s = s $ "\ttzpl_Buffer* old = nullptr;\n\tswitch (bufID) {\n";
	for (ser : ctx.bufSerials) {
		s = s $ "\t\tcase %^: old = p->buf%^; p->buf%^ = newBuf; break;\n" fmt(ser, ser, ser);
	}
	s = s $ "\t}\n\treturn old;\n}\n\n";
	s
}

-- One resolver per bank lookup: maps the latched (pitch, velocity) through
-- the bank's cell table to a buffer pointer + metadata. Called from the
-- reset loops (noteOn/init) and from swapSampleBank. Mirrors
-- genSampleBankResolvers byte-for-byte (synthc voicers are flat-only).
fn genSampleBankResolvers(ctx Ctx, name String) String {
	var s = "";
	for (lu : ctx _bankLookupNodes) {
		let bser = match (ctx.kind[lu]) { bankLookupK(bi): ctx.bankSerials[bi]; _: 0; };
		let u = ctx.serial[lu];
		if (ctx _isVoiceGraph(ctx.graphOf[lu])) {
			s = s $ "static void %^_lu%^_resolve(%^* p, int v) {\n" fmt(name, u, name);
			s = s $ "\ttzpl_SampleBank* bk = p->bank%^;\n" fmt(bser);
			s = s $ "\tint idx = bk ? bk->map[p->voice_lu%^_p[v] * 128 + p->voice_lu%^_v[v]] : TZPL_SAMPLEBANK_UNMAPPED;\n" fmt(u, u);
			s = s $ "\tif (idx == TZPL_SAMPLEBANK_UNMAPPED) {\n";
			s = s $ "\t\tp->voice_lu%^_buf[v] = nullptr;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_rootKey[v] = 0.0f;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_sr[v] = 0.0;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_len[v] = 0.0;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_loopStart[v] = 0.0;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_loopEnd[v] = 0.0;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_hasLoop[v] = 0;\n" fmt(u);
			s = s $ "\t} else {\n";
			s = s $ "\t\ttzpl_SampleBankEntry* e = &bk->samples[idx];\n";
			s = s $ "\t\tp->voice_lu%^_buf[v] = e->buf;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_rootKey[v] = e->rootKey;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_sr[v] = e->sampleRate;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_len[v] = e->buf ? (f64)e->buf->length : 0.0;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_loopStart[v] = e->loopStart;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_loopEnd[v] = e->loopEnd;\n" fmt(u);
			s = s $ "\t\tp->voice_lu%^_hasLoop[v] = e->hasLoop;\n" fmt(u);
			s = s $ "\t}\n";
			s = s $ "}\n\n";
		} else {
			s = s $ "static void %^_lu%^_resolve(%^* p) {\n" fmt(name, u, name);
			s = s $ "\ttzpl_SampleBank* bk = p->bank%^;\n" fmt(bser);
			s = s $ "\tint idx = bk ? bk->map[p->lu%^_p * 128 + p->lu%^_v] : TZPL_SAMPLEBANK_UNMAPPED;\n" fmt(u, u);
			s = s $ "\tif (idx == TZPL_SAMPLEBANK_UNMAPPED) {\n";
			s = s $ "\t\tp->lu%^_buf = nullptr;\n" fmt(u);
			s = s $ "\t\tp->lu%^_rootKey = 0.0f;\n" fmt(u);
			s = s $ "\t\tp->lu%^_sr = 0.0;\n" fmt(u);
			s = s $ "\t\tp->lu%^_len = 0.0;\n" fmt(u);
			s = s $ "\t\tp->lu%^_loopStart = 0.0;\n" fmt(u);
			s = s $ "\t\tp->lu%^_loopEnd = 0.0;\n" fmt(u);
			s = s $ "\t\tp->lu%^_hasLoop = 0;\n" fmt(u);
			s = s $ "\t} else {\n";
			s = s $ "\t\ttzpl_SampleBankEntry* e = &bk->samples[idx];\n";
			s = s $ "\t\tp->lu%^_buf = e->buf;\n" fmt(u);
			s = s $ "\t\tp->lu%^_rootKey = e->rootKey;\n" fmt(u);
			s = s $ "\t\tp->lu%^_sr = e->sampleRate;\n" fmt(u);
			s = s $ "\t\tp->lu%^_len = e->buf ? (f64)e->buf->length : 0.0;\n" fmt(u);
			s = s $ "\t\tp->lu%^_loopStart = e->loopStart;\n" fmt(u);
			s = s $ "\t\tp->lu%^_loopEnd = e->loopEnd;\n" fmt(u);
			s = s $ "\t\tp->lu%^_hasLoop = e->hasLoop;\n" fmt(u);
			s = s $ "\t}\n";
			s = s $ "}\n\n";
		}
	}
	s
}

-- The per-def swap entry point plus its exported "swapSampleBank" symbol.
-- After installing the new bank pointer it re-resolves every lookup on that
-- bank so cached per-voice buffer pointers never dangle across a swap.
fn genSwapSampleBankFun(ctx Ctx, name String) String {
	if (ctx.bankSerials length == 0) { return ""; }
	let lookups = ctx _bankLookupNodes;
	var s = "tzpl_SampleBank* %^_swapSampleBank(%^* p, i64 bankID, tzpl_SampleBank* newBank) {\n" fmt(name, name);
	s = s $ "\ttzpl_SampleBank* old = nullptr;\n\tswitch (bankID) {\n";
	var bi = 0;
	for (ser : ctx.bankSerials) {
		s = s $ "\t\tcase %^:\n" fmt(ser);
		s = s $ "\t\t\told = p->bank%^; p->bank%^ = newBank;\n" fmt(ser, ser);
		for (lu : lookups) {
			let lubi = match (ctx.kind[lu]) { bankLookupK(b): b; _: NONE; };
			if (lubi == bi) {
				if (ctx _isVoiceGraph(ctx.graphOf[lu])) {
					let mv = ctx _voicerMaxForGraph(ctx.graphOf[lu]);
					s = s $ "\t\t\tfor (int v = 0; v < %^; ++v) %^_lu%^_resolve(p, v);\n" fmt(mv, name, ctx.serial[lu]);
				} else {
					s = s $ "\t\t\t%^_lu%^_resolve(p);\n" fmt(name, ctx.serial[lu]);
				}
			}
		}
		s = s $ "\t\t\tbreak;\n";
		bi = bi + 1;
	}
	s = s $ "\t}\n\treturn old;\n}\n\n";
	s = s $ "extern \"C\" tzpl_SampleBank* swapSampleBank(tzpl_SynthData* p, int64_t bankID, tzpl_SampleBank* newBank) {\n";
	s = s $ "\treturn %^_swapSampleBank((%^*)p, bankID, newBank);\n" fmt(name, name);
	s = s $ "}\n\n";
	s
}

fn _portLine(ctx Ctx, n NIdx, i Int, arr String, fallback String, sn Int) String {
	let nm = match (ctx.kind[n]) {
		inletK(name, _):  name length == 0 ? "%^%^" fmt(fallback, sn) : name;
		outletK(name, _): name length == 0 ? "%^%^" fmt(fallback, sn) : name;
		_: fallback;
	};
	"\tdef.%^[%^] = {\"%^\", {%^, %^, %^}};\n" fmt(arr, i, nm, _typeTag(ctx.typ[n]), _rateCode(ctx.nrate[n]), ctx.chans[n])
}

-- Warp payload (step size for .step; 0.0 otherwise) for ControlDef emission.
fn _warpStepSize(w ControlWarp) Float {
	match (w) {
		ControlWarp.step(sz): sz;
		_: 0.0;
	}
}

fn genLoad(ctx Ctx, name String) String {
	-- ABI version stamp: the version of tzpl_plugin_abi.h the plugin was
	-- compiled against. Loaders refuse plugins newer than they understand.
	var s = "extern \"C\" int64_t tzpl_abi_version = TZPL_PLUGIN_ABI_VERSION;\n\n";
	s = s $ "extern \"C\" tzpl_SynthDef load() {\n\ttzpl_SynthDef def;\n";
	s = s $ "\tdef.name = \"%^\";\n" fmt(name);
	s = s $ "\tdef.funs = %^_funs;\n" fmt(name);
	s = s $ "\tdef.num_ins = %^;\n" fmt(ctx.inlets length);
	s = s $ "\tdef.num_outs = %^;\n" fmt(ctx.outlets length);
	s = s $ "\tdef.num_controls = %^;\n" fmt(ctx.controls length);
	s = s $ (ctx.inlets length > 0 ? "\tdef.ins = (tzpl_PortDef*)calloc(def.num_ins, sizeof(tzpl_PortDef));\n" : "\tdef.ins = nullptr;\n");
	s = s $ (ctx.outlets length > 0 ? "\tdef.outs = (tzpl_PortDef*)calloc(def.num_outs, sizeof(tzpl_PortDef));\n" : "\tdef.outs = nullptr;\n");
	s = s $ (ctx.controls length > 0 ? "\tdef.controls = (tzpl_ControlDef*)calloc(def.num_controls, sizeof(tzpl_ControlDef));\n" : "\tdef.controls = nullptr;\n");
	var i = 0;
	for (n : ctx.inlets) { s = s $ _portLine(ctx, n, i, "ins", "in", _portSerial(ctx, n)); i = i + 1; }
	i = 0;
	for (n : ctx.outlets) { s = s $ _portLine(ctx, n, i, "outs", "out", _portSerial(ctx, n)); i = i + 1; }
	i = 0;
	for (n : ctx.controls) {
		match (ctx.kind[n]) {
			control(spec, cname, sn, ckind): {
				-- Emit the full control spec: the engine seeds the control
				-- buffer from init; UI clients derive widgets from
				-- lo/hi/param/warp/kind. Matches the C++ emission: the ABI
				-- warp ordinal is the lang ControlWarp ordinal + 1 (0 is
				-- reserved for tzpl_cwNone), and `param` carries the warp
				-- payload (step size for the step warp).
				s = s $ "\tdef.controls[%^] = {\"%^\", {%^, %^, %^}, %^, {.lo = %^, .hi = %^, .init = %^, .param = %^, .warp = (tzpl_ControlWarp)%^, .kind = (tzpl_ControlKind)%^}};\n"
					fmt(i, cname, _typeTag(ctx.typ[n]), _rateCode(ctx.nrate[n]), ctx.chans[n], sn,
					    ftosF64(spec.lo), ftosF64(spec.hi), ftosF64(spec.init),
					    ftosF64(spec.warp _warpStepSize), spec.warp ordinal + 1, ckind ordinal);
			}
			_: {}
		}
		i = i + 1;
	}
	s = s $ "\treturn def;\n}\n\n\n";
	s
}

-- Companion symbol to load(): describes the synth's sample buffer slots.
-- Optional in the ABI (old plugins/engines simply lack it); emitted only when
-- the synth uses buffers. Matches the C++ compiler's emission byte-for-byte:
-- name "buf<serial>", f64 element type, and the widest channel span any
-- reader/writer touches.
fn genLoadBufferDefs(ctx Ctx) String {
	if (ctx.bufSerials length == 0) { return ""; }
	var spans = [Int]();
	for (ser : ctx.bufSerials) { spans push!(1); }
	var n = 0;
	for (k : ctx.kind) {
		match (k) {
			bufFixReadK(bi, _, rc, sc): { if (sc + rc > spans[bi]) { spans[bi] = sc + rc; } }
			bufVarReadK(bi, _, rc, sc): { if (sc + rc > spans[bi]) { spans[bi] = sc + rc; } }
			bufWriteK(bi, wc, sc): {
				-- writeChans 0 = auto: match the input channel count (BufWrite::calcShape).
				let w = wc == 0 ? ctx.chans[n] : wc;
				if (sc + w > spans[bi]) { spans[bi] = sc + w; }
			}
			_: {}
		}
		n = n + 1;
	}
	var s = "extern \"C\" tzpl_BufferDefList loadBufferDefs() {\n";
	s = s $ "\ttzpl_BufferDefList list;\n";
	s = s $ "\tlist.num_buffers = %^;\n" fmt(ctx.bufSerials length);
	s = s $ "\tlist.buffers = (tzpl_BufferDef*)calloc(list.num_buffers, sizeof(tzpl_BufferDef));\n";
	var i = 0;
	for (ser : ctx.bufSerials) {
		s = s $ "\tlist.buffers[%^] = {\"buf%^\", {tzpl_kF64, tzpl_constRate, %^}, %^};\n"
			fmt(i, ser, spans[i], ser);
		i = i + 1;
	}
	s = s $ "\treturn list;\n}\n\n\n";
	s
}

-- Companion symbol to load(): describes the synth's sample bank slots
-- (same additive pattern as loadBufferDefs). Matches the C++ emission
-- byte-for-byte.
fn genLoadSampleBankDefs(ctx Ctx) String {
	if (ctx.bankSerials length == 0) { return ""; }
	var s = "extern \"C\" tzpl_SampleBankDefList loadSampleBankDefs() {\n";
	s = s $ "\ttzpl_SampleBankDefList list;\n";
	s = s $ "\tlist.num_banks = %^;\n" fmt(ctx.bankSerials length);
	s = s $ "\tlist.banks = (tzpl_SampleBankDef*)calloc(list.num_banks, sizeof(tzpl_SampleBankDef));\n";
	var i = 0;
	for (ser : ctx.bankSerials) {
		s = s $ "\tlist.banks[%^] = {\"bank%^\", %^};\n" fmt(i, ser, ser);
		i = i + 1;
	}
	s = s $ "\treturn list;\n}\n\n\n";
	s
}

-- Companion symbol to load(): the synth's category tags. Optional in the ABI;
-- emitted only when tags are present. Matches the C++ compiler's emission
-- byte-for-byte.
fn genLoadTags(ctx Ctx) String {
	if (ctx.tags length == 0) { return ""; }
	var s = "extern \"C\" tzpl_TagList loadTags() {\n";
	s = s $ "\tstatic const char* tags[] = {";
	var i = 0;
	for (t : ctx.tags) {
		if (i > 0) { s = s $ ", "; }
		s = s $ "\"%^\"" fmt(t);
		i = i + 1;
	}
	s = s $ "};\n";
	s = s $ "\ttzpl_TagList list;\n\tlist.num_tags = %^;\n\tlist.tags = tags;\n" fmt(ctx.tags length);
	s = s $ "\treturn list;\n}\n\n\n";
	s
}

fn _portSerial(ctx Ctx, n NIdx) Int = match (ctx.kind[n]) {
	inletK(_, sn):  sn;
	outletK(_, sn): sn;
	_:              0;
};

---------------------------------------------------------------------------
-- Top-level driver

fn genCpp(ctx Ctx, name String, simdWidth Int = 0) String {
	-- Context dynamic vars (declared here so they exist before any gen call).
	var `cgIndent Int = 1;
	var `cgInInit Bool = false;
	var `cgRoot Int = 0;
	var `cgLoopChans Int = 1;
	-- Target SIMD width (0 = scalar; M5 enables 2/4). Threaded for the SIMD port;
	-- at width 0 every gen path stays scalar (M5.0 plumbing).
	var `cgSimdWidth Int = simdWidth;
	-- Active SIMD width while emitting inside a SIMD loop (0 = scalar). Set by
	-- genLoop; read by the var-ref / op / sink paths to pick vector forms.
	var `cgSimdW Int = 0;
	-- Flat-voice-mode state: when cgVoiceCount > 0 we are emitting a voicer body
	-- (the body's loops run per-voice over maxVoices). cgVoiceGraph is the body
	-- graph index, so var refs for body nodes get the per-voice `voice_`/[i] form.
	var `cgVoiceCount Int = 0;
	var `cgVoiceGraph Int = NONE;
	var `cgVoiceChans Int = 1;        -- per-voice channel count (body root chans)
	var `cgFlatChanShift Int = 0;     -- log2(perVoiceChans) for the current flat loop
	var `cgFlatChanMask Int = 0;      -- perVoiceChans - 1 for the current flat loop
	-- Reset-loop emission state (mirrors inResetMode / singleVoiceMode):
	-- cgInResetMode makes NoteParam/Control roots read their live sources;
	-- cgSingleVoice makes flat loops run for just the voice `vi` in scope.
	var `cgInResetMode Bool = false;
	var `cgSingleVoice Bool = false;

	var s = "\n";
	s = s $ "#include \"tzpl_plugin_abi.h\"\n";
	s = s $ "#include \"tzpl_matrix_transform.hpp\"\n";
	s = s $ "#include \"tzpl_random.hpp\"\n";
	s = s $ "#include \"tzpl_delay_interp.hpp\"\n";
	if (ctx _spectralChains length > 0) { s = s $ "#include \"tzpl_fft.hpp\"\n"; }
	s = s $ "#include <cmath>\n#include <cstdio>\n#include <cstring>\n#include <cstdlib>\n#include <array>\n\n";
	s = s $ "using namespace synthdef;\nusing namespace simd;\n";
	if (ctx _voicerNodes length > 0) {
		s = s $ "#define TZPL_VOICER_TYPES_DEFINED\n#include \"tzpl_voicer.hpp\"\n";
	}
	s = s $ "\n";

	-- Shared-input pointer (mirrors the C++ genClass emission): exported so
	-- the loader can repoint it at the engine's process-global table.
	if (ctx _usesSharedInput) {
		s = s $ "static tzpl_SharedInput tzpl_sharedInputFallback = {};\n";
		s = s $ "extern \"C\" tzpl_SharedInput const* tzpl_sharedInput = &tzpl_sharedInputFallback;\n\n";
	}

	s = s $ "extern tzpl_SynthFuns %^_funs;\n\n" fmt(name);
	s = s $ "typedef struct %^ {\n" fmt(name);
	s = s $ "\ttzpl_SynthFuns funs;\n\tstruct tzpl_Engine* engine;\n\tstruct tzpl_Node* node;\n";
	s = s $ "\tint num_ins;\n\tint num_outs;\n\tint num_controls;\n";
	s = s $ "\tvoid** inlets;\n\tvoid** outlets;\n\tvoid** controls;\n";
	s = s $ "\tdouble fs, sd; // sample rate, sample dur\n\n";
	s = s $ genDeclConstVars(ctx);
	s = s $ genDeclInstVars(ctx);
	s = s $ "} %^;\n\n" fmt(name);
	s = s $ genAllocFun(ctx, name);
	s = s $ genFreeFun(ctx, name);
	s = s $ genSampleBankResolvers(ctx, name);
	s = s $ genInitFun(ctx, name);
	s = s $ genUninitFun(ctx, name);
	s = s $ genResetFun(ctx, name);
	s = s $ genEventFun(ctx, name);
	s = s $ genHandleEventsFun(ctx, name);
	s = s $ genTickFun(ctx, name);
	s = s $ genVoicerNoteFuns(ctx, name);
	s = s $ genSwapBufferFun(ctx, name);
	s = s $ genSwapSampleBankFun(ctx, name);
	s = s $ genFunPtrs(ctx, name);
	s = s $ genLoad(ctx, name);
	s = s $ genLoadBufferDefs(ctx);
	s = s $ genLoadSampleBankDefs(ctx);
	s = s $ genLoadTags(ctx);
	s
}
