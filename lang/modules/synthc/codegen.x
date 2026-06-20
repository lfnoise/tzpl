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
	_:                 (ctx _isInstVar(n)) ? "p->v%^" fmt(ctx.serial[n]) : "v%^" fmt(ctx.serial[n]);
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
	if (ctx _isRootNode(n)) {
		if (ctx.trees[ctx.treeOf[n]].loopOf != NONE && _consumedInLoop(ctx, ctx.treeOf[n])) {
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

-- Inline a node's value expression at channel `cel`.
fn genExpr(ctx Ctx, n NIdx, cel String) String {
	-- A loop variable (VarExpr) always emits as the bare C++ loop-var name,
	-- never as `p->vN` -- even though it is cut Graph (cross-graph reference).
	match (ctx.kind[n]) { varK(name): { return name; } _: {} }
	let inInit Bool = `cgInInit;
	if (!inInit && ctx _isRootNode(n) && n != `cgRoot) {
		return _varRef(ctx, n, cel);
	}
	_inlineExpr(ctx, n, cel)
}

fn _constLiteral(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(v, _): constStr(v, ctx.typ[n]);
	_:              "0";
};

fn _inlineExpr(ctx Ctx, n NIdx, cel String) String {
	let ins = ctx.ins[n];
	match (ctx.kind[n]) {
		constant(v, _): (v constSize == 1) ? constStr(v, ctx.typ[n]) : _varRef(ctx, n, cel);
		sampleRate:     (ctx.typ[n].0 == 8) ? "p->fs" : "(%^)(p->fs)" fmt(cppType(ctx.typ[n]));
		sampleDur:      (ctx.typ[n].0 == 8) ? "p->sd" : "(%^)(p->sd)" fmt(cppType(ctx.typ[n]));
		control(_, _, sn): (ctx.chans[n] == 1)
			? "(*(%^*)p->controls[%^])" fmt(cppType(ctx.typ[n]), sn)
			: "((%^*)p->controls[%^])[%^]" fmt(cppType(ctx.typ[n]), sn, cel);
		inletK(_, _):   _varRef(ctx, n, cel);
		unopK(op):      genUnopStr(op, ctx.typ[n] _isF32, genExpr(ctx, ins[0], cel));
		binopK(op):     genBinopStr(op, ctx.typ[n] _isF32, genExpr(ctx, ins[0], cel), genExpr(ctx, ins[1], cel));
		compareopK(op): genCompareStr(op, genExpr(ctx, ins[0], cel), genExpr(ctx, ins[1], cel));
		castopK(ct):    "%^(%^)" fmt(cppType(ct), genExpr(ctx, ins[0], cel));
		selectK:        _inlineSelect(ctx, n, cel);
		reduceK(op, cols): _genReduce(ctx, n, op, cols, cel);
		vecK(vop):      _genVec(ctx, n, vop, cel);
		urandK(_):      "urand%^(p->rgen%^)" fmt(ctx.typ[n] _isF32 ? "f" : "", ctx.graphOf[n]);
		birandK(_):     "birand%^(p->rgen%^)" fmt(ctx.typ[n] _isF32 ? "f" : "", ctx.graphOf[n]);
		rand64K(_):     "rand64(p->rgen%^)" fmt(ctx.graphOf[n]);
		bufFixReadK(bi, index, rc, sc): _bufFixReadExpr(ctx, bi, index, rc, sc, cel);
		bufVarReadK(bi, ip, rc, sc):    _bufVarReadExpr(ctx, n, bi, ip, rc, sc, cel);
		bufLengthK(bi):                 _bufLengthExpr(ctx, bi);
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

fn _inlineSelect(ctx Ctx, n NIdx, cel String) String {
	let ins = ctx.ins[n];
	if (ins length == 3) {
		"(%^ ? %^ : %^)" fmt(genExpr(ctx, ins[0], cel), genExpr(ctx, ins[2], cel), genExpr(ctx, ins[1], cel))
	} else {
		"/* FIXME select n-way */"
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

fn _delayFixReadExpr(ctx Ctx, n NIdx, d Int, k Int, cel String) String {
	let di = _delayDIndex(ctx, d, n, cel);
	let info = ctx.delays[d];
	if (info.allocSize == 1) {
		"p->d%^%^" fmt(info.serial, di)
	} else {
		"p->d%^%^[(p->d%^_wrpos - u64(%^)) & %^]" fmt(info.serial, di, info.serial, k, _delayMask(info))
	}
}

-- Variable (signal-rate) delay read with interpolation. interpNone reads the
-- ring buffer directly; the interpolating kernels call tzpl_delay_<kernel>
-- (declared in tzpl_delay_interp.hpp). off is the delay-time signal expression.
fn _delayVarReadExpr(ctx Ctx, n NIdx, d Int, ip Interpolation, cel String) String {
	let di = _delayDIndex(ctx, d, n, cel);
	let info = ctx.delays[d];
	let off = genExpr(ctx, ctx.ins[n][0], cel);
	let mask = _delayMask(info);
	match (ip) {
		none: "p->d%^%^[(p->d%^_wrpos - u64(%^)) & %^]" fmt(info.serial, di, info.serial, off, mask);
		_:    "tzpl_delay_%^(p->d%^%^, p->d%^_wrpos, %^, %^)"
			fmt(ip toLispString, info.serial, di, info.serial, mask, off);
	}
}

-- Emit a delay-write statement (a sink, called from genTree).
fn _delayWriteStmt(ctx Ctx, n NIdx, d Int, cel String, treeSerial Int) String {
	let info = ctx.delays[d];
	let di = info.chans > 1 ? "[%^]" fmt(cel) : "";
	let value = genExpr(ctx, ctx.ins[n][0], cel);
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

fn _bufFixReadExpr(ctx Ctx, bi Int, index Int, readChans Int, startChan Int, cel String) String {
	let bp = ctx _bufPtr(bi);
	let chan = _bufChanExpr(readChans, startChan, cel, bp);
	"%^ ? %^->data[%^][%^ & %^->mask] : 0.0" fmt(bp, bp, chan, index, bp)
}

fn _bufVarReadExpr(ctx Ctx, n NIdx, bi Int, interp Interpolation, readChans Int, startChan Int, cel String) String {
	let bp = ctx _bufPtr(bi);
	let chan = _bufChanExpr(readChans, startChan, cel, bp);
	let idx = genExpr(ctx, ctx.ins[n][0], cel);
	match (interp) {
		none: "%^ ? %^->data[%^][u64(%^) & %^->mask] : 0.0" fmt(bp, bp, chan, idx, bp);
		_:    "%^ ? %^(%^->data[%^], %^->mask, %^) : 0.0" fmt(bp, _bufInterpFunc(interp), bp, chan, bp, idx);
	}
}

fn _bufLengthExpr(ctx Ctx, bi Int) String {
	let bp = ctx _bufPtr(bi);
	"%^ ? (f64)(%^->length) : 0.0" fmt(bp, bp)
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
		$ i1 $ "u64 _idx = u64(%^) & %^->mask;\n" fmt(index, bp)
		$ i1 $ "%^->data[%^][_idx] = %^;\n" fmt(bp, chan, value)
		$ i0 $ "} // %^ BufWrite\n" fmt(treeSerial)
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
		ifK:            { return ctx _genIf(root); }
		forK:           { return ctx _genFor(root); }
		switchK(nc):    { return ctx _genSwitch(root, nc); }
		phiK(target):   { return _tabs(`cgIndent) $ "%^ = %^;\n" fmt(_varRef(ctx, target, cel), genExpr(ctx, ctx.ins[root][0], cel)); }
		_: {}
	}

	let indentStr = _tabs(`cgIndent);
	-- A root is emitted as a local temp declaration when it is consumed only
	-- within its loop, OR when it is a temp-var (non-inst) root in a scalar
	-- loop (mirrors genTree's second condition).
	let localDecl = _consumedInLoop(ctx, treeIdx)
		|| (ctx.cut[root] isTempVarCut && `cgLoopChans == 1);
	if (ctx _isSink(root)) {
		-- e.g. outlet: "<lhs> = <expr>; // <serial> Sink <str>" -- but outlet's
		-- genExpr is the lhs accessor; the assignment is built here.
		indentStr $ "%^; // %^ Sink %^\n" fmt(_sinkStmt(ctx, root, cel), tree.serial, nodeStr(ctx, root))
	} else if (localDecl) {
		indentStr $ "%^ %^ = %^; // %^ %^\n"
			fmt(cppType(ctx.typ[root]), _varName(ctx, root), genExpr(ctx, root, cel),
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
	outletK(_, sn): "((%^**)p->outlets)[%^]%^ = %^"
		fmt(cppType(ctx.typ[n]), sn, (ctx.chans[n] > 1 ? "[%^]" fmt(cel) : "[0]"),
		    genExpr(ctx, ctx.ins[n][0], cel));
	debugK(_, _, _, _): "/* FIXME debug */";
	_: genExpr(ctx, n, cel);
};

---------------------------------------------------------------------------
-- Loop emission (scalar + channel loop; no SIMD)

fn genLoop(ctx Ctx, loopIdx Int) String {
	let loop = ctx.loops[loopIdx];
	if (loop.chans == 0) { return ""; }
	var `cgLoopChans Int = loop.chans;

	-- Sort antecedent serials so the LOOP comment is deterministic (the C++
	-- loop_antecedents is an unordered_set; both sides sort ascending to match).
	var ante [String] = [];
	for (a : loop.antecedents sort) { ante push!(a toString); }
	var s = "\n" $ _tabs(`cgIndent) $ "// LOOP %^ [%^] %^ %^\n"
		fmt(padLeft(loop.serial toString, 2), ante join(" "), loop.rate rateStr, loop.chans);

	-- Declare temp-var roots not consumed in their loop: multichannel temps, and
	-- control-flow nodes whose value is assigned inside the block (so they cannot
	-- be declared inline like a scalar temp). The C++ hardcodes one tab here,
	-- regardless of nesting depth.
	if (loop.chans > 1 || loop.isControlFlow) {
		var decls = "";
		for (t : loop.trees) {
			let root = ctx.trees[t].root;
			if (ctx.cut[root] isTempVarCut && !_consumedInLoop(ctx, t)) {
				decls = decls $ "\t%^ %^%^;\n" fmt(cppType(ctx.typ[root]), _varName(ctx, root), _shape(ctx.chans[root]));
			}
		}
		if (decls length > 0) { s = s $ decls $ "\n"; }
	}

	var code = "";
	if (loop.isControlFlow) {
		-- A control-flow loop emits its block directly (no channel loop, even
		-- multichannel); the branches' own subgraph loops handle the channels.
		code = code $ genTree(ctx, loop.trees[0], "0");
	} else if (loop.chans == 1) {
		for (t : loop.trees) { code = code $ genTree(ctx, t, "0"); }
	} else {
		code = code $ _tabs(`cgIndent) $ "for (usize i = 0; i < %^; ++i) {\n" fmt(loop.chans);
		var `cgIndent Int = `cgIndent + 1;
		for (t : loop.trees) { code = code $ genTree(ctx, t, "i"); }
		code = code $ _tabs(`cgIndent - 1) $ "}\n";
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
	if (ctx.delays length == 0) { return ""; }
	var s = "\t// delays\n";
	for (d : ctx.delays) {
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
	for (d : ctx.delays) {
		if (d.allocSize != 1) { s = s $ "\tu64 d%^_wrpos;\n" fmt(d.serial); }
		if (d.hasMaxBound) { s = s $ "\tu64 d%^_mask;\n" fmt(d.serial); }
	}
	s
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
	-- match the C++ genDeclInstVars ordering.
	for (g : ctx _rngGraphs) { s = s $ "\tRandState1 rgen%^;\n" fmt(g); }
	-- inst-var roots, in loop/tree order.
	for (lp : ctx.loops) {
		for (t : lp.trees) {
			let root = ctx.trees[t].root;
			let isInletOutlet = match (ctx.kind[root]) { inletK(_, _): true; outletK(_, _): true; _: false; };
			if (ctx _isInstVar(root) && !isInletOutlet) {
				s = s $ "\t%^ %^%^;\n" fmt(cppType(ctx.typ[root]), _varDeclName(ctx, root), _shape(ctx.chans[root]));
			}
		}
	}
	-- control activation flags
	for (cn : ctx.controls) {
		match (ctx.kind[cn]) { control(_, _, sn): { s = s $ "\tbool ctrl%^_active;\n" fmt(sn); } _: {} }
	}
	if (s length > 0) { s = "\t// instance variables\n" $ s $ "\n"; }
	s = s $ genDelayDecls(ctx);
	s = s $ genBufDecls(ctx);
	s
}

-- Sample buffer pointers (one per SampleBuf), emitted after the delay decls to
-- match the C++ genClass ordering.
fn genBufDecls(ctx Ctx) String {
	var s = "";
	for (ser : ctx.bufSerials) { s = s $ "\ttzpl_Buffer* buf%^;\n" fmt(ser); }
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

fn genInitFun(ctx Ctx, name String) String {
	var s = "tzpl_SErr %^_init(%^* p) {\n\tf64 fs = p->fs;\n\tp->sd = 1./fs;\n" fmt(name, name);
	s = s $ genInitConstants(ctx);
	for (g : ctx _rngGraphs) { s = s $ "\tarc4seedrand(p->rgen%^);\n" fmt(g); }
	-- inInitMode is only for genDelayAlloc (runtime delay sizing); init LOOPS
	-- reference cross-tree roots as variables just like audio loops do.
	s = s $ genLoops(ctx, ctx.initLoops);
	s = s $ genDelayAlloc(ctx);
	s = s $ genDelayInit(ctx);
	-- Buffer pointers start null; the host swaps real buffers in at runtime.
	for (ser : ctx.bufSerials) { s = s $ "\tp->buf%^ = nullptr;\n" fmt(ser); }
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn genUninitFun(ctx Ctx, name String) String {
	-- M1: only allocSize<1 (runtime) delays need free; fixed delays need none.
	var s = "tzpl_SErr %^_uninit(%^* p) {\n" fmt(name, name);
	for (d : ctx.delays) {
		if (d.allocSize < 1 && d.hasMaxBound) {
			if (d.chans == 1) {
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
	var s = "tzpl_SErr %^_reset(%^* p) {\n\t// FIXME genResetFun\n" fmt(name, name);
	s = s $ genLoops(ctx, ctx.resetLoops);
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn genEventFun(ctx Ctx, name String) String {
	var s = "tzpl_SErr %^_event(%^* p, u64 id, tzpl_Slice dst, tzpl_Slice data) {\n" fmt(name, name);
	if (ctx.controls length > 0) {
		s = s $ "\tswitch (id) {\n";
		for (cn : ctx.controls) {
			match (ctx.kind[cn]) {
				control(_, _, sn): {
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

fn _ctrlSerialOf(ctx Ctx, n NIdx) Int = match (ctx.kind[n]) { control(_, _, sn): sn; _: NONE; };

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
	var s = "";
	for (d : ctx.delays) {
		if (d.allocSize != 1 && ctx _delayGraph(d) == graph) {
			let evtRate = d.writer != NONE && ctx.nrate[d.writer] == Rate.event;
			if (!evtRate) { s = s $ _tabs(`cgIndent) $ "++p->d%^_wrpos;\n" fmt(d.serial); }
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

fn _portLine(ctx Ctx, n NIdx, i Int, arr String, fallback String, sn Int) String {
	let nm = match (ctx.kind[n]) {
		inletK(name, _):  name length == 0 ? "%^%^" fmt(fallback, sn) : name;
		outletK(name, _): name length == 0 ? "%^%^" fmt(fallback, sn) : name;
		_: fallback;
	};
	"\tdef.%^[%^] = {\"%^\", {%^, %^, %^}};\n" fmt(arr, i, nm, _typeTag(ctx.typ[n]), _rateCode(ctx.nrate[n]), ctx.chans[n])
}

fn genLoad(ctx Ctx, name String) String {
	var s = "extern \"C\" tzpl_SynthDef load() {\n\ttzpl_SynthDef def;\n";
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
			control(spec, cname, sn): {
				-- Emit the control's spec (lo/hi/init) so the engine seeds the
				-- control buffer with its declared init value. Matches the C++
				-- emission; param/warp/kind are left value-initialized.
				s = s $ "\tdef.controls[%^] = {\"%^\", {%^, %^, %^}, %^, {.lo = %^, .hi = %^, .init = %^}};\n"
					fmt(i, cname, _typeTag(ctx.typ[n]), _rateCode(ctx.nrate[n]), ctx.chans[n], sn,
					    ftosF64(spec.lo), ftosF64(spec.hi), ftosF64(spec.init));
			}
			_: {}
		}
		i = i + 1;
	}
	s = s $ "\treturn def;\n}\n\n\n";
	s
}

fn _portSerial(ctx Ctx, n NIdx) Int = match (ctx.kind[n]) {
	inletK(_, sn):  sn;
	outletK(_, sn): sn;
	_:              0;
};

---------------------------------------------------------------------------
-- Top-level driver

fn genCpp(ctx Ctx, name String) String {
	-- Context dynamic vars (declared here so they exist before any gen call).
	var `cgIndent Int = 1;
	var `cgInInit Bool = false;
	var `cgRoot Int = 0;
	var `cgLoopChans Int = 1;

	var s = "\n";
	s = s $ "#include \"tzpl_plugin_abi.h\"\n";
	s = s $ "#include \"tzpl_matrix_transform.hpp\"\n";
	s = s $ "#include \"tzpl_random.hpp\"\n";
	s = s $ "#include \"tzpl_delay_interp.hpp\"\n";
	s = s $ "#include <cmath>\n#include <cstdio>\n#include <cstring>\n#include <cstdlib>\n#include <array>\n\n";
	s = s $ "using namespace synthdef;\nusing namespace simd;\n\n";
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
	s = s $ genInitFun(ctx, name);
	s = s $ genUninitFun(ctx, name);
	s = s $ genResetFun(ctx, name);
	s = s $ genEventFun(ctx, name);
	s = s $ genHandleEventsFun(ctx, name);
	s = s $ genTickFun(ctx, name);
	s = s $ genSwapBufferFun(ctx, name);
	s = s $ genFunPtrs(ctx, name);
	s = s $ genLoad(ctx, name);
	s
}
