-- synthc/codegen.x
-- C++ source generation for plugin ABI v3 (block engine), M4: serial loops.
-- process(p, off, n) runs the per-sample body (ported from the C++
-- synthdef-compiler generator) once per sample: each sample it gathers every
-- input port's channels into the struct arrays p->in<k>[], runs the loops,
-- and scatters p->out<k>[] into the output ports. Controls are inputs; a
-- voicer's note parameters read their columns of the `notes` input, and the
-- engine's voicer calls resetVoice(p, v, offset) at each onset.
--
-- Context (the Ctx, current loop channel count, current tree root,
-- init-vs-audio mode, indent) is threaded through dynamic-scope vars.

import synthdef.*;
import synthc.ir.*;
import synthc.opnames.*;
import synthc.recurrence.*;

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
	block: "tzpl_audioRate";  audio: "tzpl_audioRate";
};

---------------------------------------------------------------------------
-- Node classification used by codegen

fn _isRootNode(ctx Ctx, n NIdx) Bool {
	let t = ctx.treeOf[n];
	t != NONE && ctx.trees[t].root == n
}

fn _isInstVar(ctx Ctx, n NIdx) Bool = ctx.cut[n] isInstVarCut;

-- A voice-local materialized value in flat voice mode: stored as a SoA array
-- [maxVoices * chans], so accesses need a voice base. Mirrors the C++
-- CppCodeGen::isVoiceLocalStorage.
fn _isVoiceLocalStorage(ctx Ctx, n NIdx) Bool =
	`cgVoiceCount > 0 && ctx.graphOf[n] == `cgVoiceGraph
	&& match (ctx.kind[n]) {
		constant(_, _): false;
		inletK(_, _, _): false;
		outletK(_, _): false;
		_: true;
	};

fn _numInlets(ctx Ctx) Int = ctx.inlets length;

-- genVarName: the C++ identifier (or accessor) for a node's value.
fn _varName(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(_, _):    "p->c%^" fmt(ctx.serial[n]);
	inletK(_, sn, _):  "in%^" fmt(sn);
	outletK(_, sn):    "out%^" fmt(sn);
	-- Flat voice mode: a body instance var is per-voice (p->voice_v<serial>); a
	-- body temp is a local v<serial>[maxVoices] array. In an AoS per-voice
	-- block (cgInVoiceLoop) an instance var is a VoiceState member (vs.),
	-- mirroring the C++ genVarName's inVoiceLoop branch.
	_:                 ctx _isRootBuf(n) ? "bv%^" fmt(ctx.serial[n]) :
	                   (`cgVoiceCount > 0 && ctx.graphOf[n] == `cgVoiceGraph)
	                       ? ((ctx _isInstVar(n)) ? "p->voice_v%^" fmt(ctx.serial[n]) : "v%^" fmt(ctx.serial[n]))
	                       : ((ctx _isInstVar(n))
	                           ? (`cgInVoiceLoop ? "vs.v%^" fmt(ctx.serial[n]) : "p->v%^" fmt(ctx.serial[n]))
	                           : "v%^" fmt(ctx.serial[n]));
};

fn _varDeclName(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(_, _):  "c%^" fmt(ctx.serial[n]);
	_:               "v%^" fmt(ctx.serial[n]);
};

-- The channel subscript of an operand of shape `src` read by a consumer of
-- shape `dst` at loop channel `cel`. Broadcasting is cyclic per dimension
-- (tzpl broadcastChannel): when that is a plain wrap of the flat index it is
-- a mask (power of two, the old form) or a remainder; otherwise a static
-- channel map table.
fn _indexWrapShape(src Shape, dst Shape, loopChans Int, cel String) String {
	let C = src chans;
	if (C == 0 || C >= loopChans) { return _indexWrap(C, loopChans, cel); }
	var plain = true;
	var map [Int] = [];
	if (dst chans == loopChans) {
		var w = 0;
		while (w < loopChans) {
			let j = broadcastChannel(src, dst, w);
			map push!(j);
			if (j != w % C) { plain = false; }
			w = w + 1;
		}
	}
	if (plain) {
		if (C hasSingleBit) { return _indexWrap(C, loopChans, cel); }
		return "[(%^) %% %^]" fmt(cel, C);
	}
	var tables [String] = `cgTables;
	let id = tables length;
	var vals [String] = [];
	for (j : map) { vals push!(j toString); }
	tables push!("static const int bmap%^[%^] = {%^};\n" fmt(id, loopChans, vals join(", ")));
	"[bmap%^[%^]]" fmt(id, cel)
}

fn _indexWrap(chans Int, loopChans Int, cel String) String =
	chans < loopChans
		-- chans is a power of two (or 0); the mask is (usize)(chans-1). For
		-- chans 0 that wraps to SIZE_MAX (the C++ usize formatting), not -1.
		? "[%^&%^]" fmt(cel, chans == 0 ? "18446744073709551615" : (chans - 1) toString)
		: "[%^]" fmt(cel);

-- genVarRef: a reference to a node's value at channel `cel` (scalar emits no index).
fn _varRef(ctx Ctx, n NIdx, cel String) String {
	match (ctx.kind[n]) { inletK(_, sn, _): { return _inletRef(ctx, n, sn, cel); } _: {} }
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
		if (C == `cgFlatTrip) {
			-- Spans the loop's full flat element range (a voice-expanded temp,
			-- e.g. the phi copy loop's source/dest): direct load. The index
			-- never wraps, and the voice total need not be a power of two,
			-- so no mask (`& (C-1)` is wrong for e.g. 12 voices).
			return _simdLoad(t, w, "&%^[%^]" fmt(s, cel));
		}
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
			inletK(_, sn, _): {
				if (ctx.chans[n] == 1) { return _simdSplat(t, w, "in%^[0]" fmt(sn)); }
				return _simdLoad(t, w, "&in%^[%^]" fmt(sn, cel));
			}
			_: {}
		}
		if (ctx.chans[n] == 1) {
			if (`cgSampleVec && ctx.nrate[n] == Rate.audio) {
				-- a block buffer row: 4 samples from the row pointer
				if (ctx _isBlockTemp(n) || ctx _isRootBuf(n)) { return _simdLoad(t, w, s); }
				-- a temp of this region: a vector local
				if (ctx.cut[n] isTempVarCut) { return s; }
			}
			if (`cgSampleVec && ctx _isRootBuf(n) && ctx.nrate[n] == Rate.block) {
				-- a parameter group's value: per sample while it runs every sample
				let g = ctx.groupOf[n];
				return "(pgS%^ ? %^ : %^)" fmt(g, _simdLoad(t, w, "&%^" fmt(s)), _simdSplat(t, w, s));
			}
			return _simdSplat(t, w, s);
		}
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
		} else if (ctx.cut[n] isTempVarCut && isVoice) {
			-- voice temp (local SoA array): mono -> by voice, full width ->
			-- [i], narrower -> voice base + wrapped channel
			return s $ _flatVoiceIdx(ctx, C, cel);
		} else if (ctx.cut[n] isTempVarCut) {
			-- non-voice temp (e.g. the VoicerExpr output)
			if (C > 1) { return s $ _indexWrap(C, `cgLoopChans, cel); }
			return s;
		} else {
			-- Other non-voice values (e.g. a multichannel constant): index
			-- by CHANNEL, like the non-voice inst var above. The raw element
			-- index spans voices x channels in a flat-voice loop, which
			-- would run off the end of a node-level array -- p->cN has C
			-- elements (hit by the single-voice event loops in noteOn).
			if (C > 1 && `cgFlatChanShift != 0) {
				let m = (C == `cgLoopChans) ? `cgFlatChanMask : C - 1;
				return "%^[%^ & %^]" fmt(s, cel, m);
			}
			return "%^[%^]" fmt(s, cel);
		}
	}
	let isPort = match (ctx.kind[n]) { inletK(_, _, _): true; outletK(_, _): true; _: false; };
	if (ctx.chans[n] == 1 && !isPort) { return s; }
	s $ _indexWrapShape(ctx.shape[n], `cgDstShape, `cgLoopChans, cel)
}

-- An input port's gathered value p->in<sn>[] at loop element `cel`. Ports
-- are node-level (never per voice): in a flat voice loop the element index
-- spans voices x channels, so the channel is cel wrapped by the port's
-- width; in SIMD a mono port splats and wider ones load or gather.
-- A multichannel port read or written only by the root graph's audio loops
-- is accessed through the engine's channel-pointer table, r_in<k>[chan][bi]
-- (r_out<k>), with no interleaved copy (block engine plan M8, D4). The notes
-- input, ports read at other rates, and inputs a consumer takes as a whole
-- row (reduce, channel ops) keep their gathered block buffer.
fn _directPort(ctx Ctx, n NIdx) Bool {
	if (ctx.chans[n] < 2) { return false; }
	match (ctx.kind[n]) {
		inletK(_, sn, _): {
			if (ctx.notesInlets contains(n)) { return false; }
			if (ctx.consumers[n] length == 0) { return false; }
			for (c : ctx.consumers[n]) {
				if (ctx.nrate[c] != Rate.audio || ctx.graphOf[c] != 0) { return false; }
				-- elementwise consumers only: a reduce, channel op or buffer
				-- access takes the whole row
				let elementwise = match (ctx.kind[c]) {
					unopK(_): true; binopK(_): true; compareopK(_): true; castopK(_): true;
					selectK: true; delayWriteK(_): true; outletK(_, _): true;
					_: false;
				};
				if (!elementwise) { return false; }
			}
			true
		}
		outletK(_, _): ctx.graphOf[n] == 0;
		_: false;
	}
}

fn _inletRef(ctx Ctx, n NIdx, sn Int, cel String) String {
	let C = ctx.chans[n];
	let a = "in%^" fmt(sn);
	let flat = `cgVoiceCount > 0 && `cgFlatChanShift >= 0 && `cgFlatTrip > 0;
	if (ctx _directPort(n) && `cgPortRows) {
		-- row k of the transposed port
		if (`cgSimdW > 0) {
			let w = `cgSimdW;
			if (C >= `cgLoopChans) { return _simdLoad(ctx.typ[n], w, "&t_in%^[k * %^ + %^]" fmt(sn, C, cel)); }
			var parts [String] = [];
			var j = 0;
			while (j < w) {
				let idx = cel == "0" ? (j % C) toString : "((%^ + %^) %% %^)" fmt(cel, j, C);
				parts push!("t_in%^[k * %^ + %^]" fmt(sn, C, idx));
				j = j + 1;
			}
			return "%^{%^}" fmt(_simdType(ctx.typ[n], w), parts join(", "));
		}
		let ix = _indexWrapShape(ctx.shape[n], `cgDstShape, `cgLoopChans, cel);
		return "t_in%^[k * %^ + %^]" fmt(sn, C, ix substring(1, ix length - 2));
	}
	if (ctx _directPort(n)) {
		-- channel idx at sample bi, through the pointer table
		let ra = "r_in%^" fmt(sn);
		if (`cgSimdW > 0) {
			let w = `cgSimdW;
			var parts [String] = [];
			var j = 0;
			while (j < w) {
				let idx = cel == "0" ? (j % C) toString
				          : (C >= `cgLoopChans ? "%^ + %^" fmt(cel, j) : "((%^ + %^) %% %^)" fmt(cel, j, C));
				parts push!("%^[%^][bi]" fmt(ra, idx));
				j = j + 1;
			}
			return "%^{%^}" fmt(_simdType(ctx.typ[n], w), parts join(", "));
		}
		let ix = _indexWrapShape(ctx.shape[n], `cgDstShape, `cgLoopChans, cel);
		return "%^%^[bi]" fmt(ra, ix);
	}
	if (`cgSimdW > 0) {
		let w = `cgSimdW;
		let t = ctx.typ[n];
		if (C == 1) { return `cgSampleVec ? _simdLoad(t, w, a) : _simdSplat(t, w, "%^[0]" fmt(a)); }
		if (!flat && C >= `cgLoopChans) { return _simdLoad(t, w, "&%^[%^]" fmt(a, cel)); }
		var parts [String] = [];
		var j = 0;
		while (j < w) {
			let idx = cel == "0" ? (j % C) toString : "((%^ + %^) %% %^)" fmt(cel, j, C);
			parts push!("%^[%^]" fmt(a, idx));
			j = j + 1;
		}
		return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
	}
	if (C == 1) { return "%^[0]" fmt(a); }
	if (flat) { return "%^[(%^) %% %^]" fmt(a, cel, C); }
	a $ _indexWrapShape(ctx.shape[n], `cgDstShape, `cgLoopChans, cel)
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
	if (ctx.consumers[root] length == 0) {
		-- Vacuous truth over an empty consumer set used to be harmless: a
		-- zero-consumer temp-var-cut root was always Unused and removed by
		-- removeDeadCode, so this case was unreachable. hasSideEffectSubgraph
		-- (ir.x) now keeps a side-effect-guarding ControlFlow node alive with
		-- zero consumers; treating it as "trivially consumed in its own loop"
		-- would wrongly skip its up-front declaration (below) and make
		-- _varRef emit an unindexed scalar reference for a multichannel phi
		-- write. Require the ordinary declaration instead.
		return false;
	}
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
		inletK(_, _, _): true;
		_: false;
	});
	if (!inInit && !resetLeaf && ctx _isRootNode(n) && n != `cgRoot) {
		return _varRef(ctx, n, cel);
	}
	-- Operand references inside this node map their channels to its shape.
	var `cgDstShape Shape = ctx.shape[n];
	_inlineExpr(ctx, n, cel)
}

fn _constLiteral(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(v, _): constStr(v, ctx.typ[n]);
	_:              "0";
};

-- Whether an operand will emit as an inline C++ comparison (bool): it is a
-- compareop that genExpr will NOT read from a materialized root slot.
-- Mirrors the inlineCompare lambda in the C++ BinaryOpExpr visitor.
fn _inlineCompare(ctx Ctx, n NIdx) Bool {
	let isCmp = match (ctx.kind[n]) { compareopK(_): true; _: false; };
	if (!isCmp) { return false; }
	let inInit Bool = `cgInInit;
	!(!inInit && ctx _isRootNode(n) && n != `cgRoot)
}

-- The RNG state reference for a random node. In flat voice mode a voicer-body
-- RNG is per-voice: p->voice_rgen<g>[voice], voice = cel >> flatChanShift.
fn _rngStateRef(ctx Ctx, n NIdx, cel String) String {
	let g = ctx.graphOf[n];
	if (`cgVoiceCount > 0 && g == `cgVoiceGraph) {
		let vi = `cgFlatChanShift == 0 ? cel : "%^ >> %^" fmt(cel, `cgFlatChanShift);
		return "p->voice_rgen%^[%^]" fmt(g, vi);
	}
	if (`cgInVoiceLoop && ctx _isVoiceGraphDeep(g)) { return "vs.rgen%^" fmt(g); }
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
		-- NoteParam: the voice's column(s) of the `notes` input. In flat voice
		-- mode the voice index is extracted from cel via the channel shift;
		-- SIMD gathers per lane. In an AoS per-voice block the row pointer
		-- `vparams` is in scope.
		noteParamK(_, _, psn): `cgInVoiceLoop
			? ((ctx.chans[n] == 1) ? "vparams[%^]" fmt(psn) : "vparams[%^ + (%^)]" fmt(psn, cel))
			: ((`cgSimdW > 0)
				? _noteParamSimd(ctx.typ[n], ctx.chans[n], psn, cel)
				: _noteParamFlatScalar(ctx, n, psn, cel));
		-- The packed FFT frame buffer the body reads/writes each sample.
		spectralFrameK(_): "p->spec%^_frame[%^]" fmt(ctx _frameChainSerial(n), cel);
		-- An input port's gathered value (controls included). In SIMD a
		-- width-matched port is a vector load, a mono one splats.
		inletK(_, sn, _): {
			if (`cgSimdW > 0) {
				let t = ctx.typ[n]; let w = `cgSimdW; let c = ctx.chans[n];
				if (c == w) { _simdLoad(t, w, "in%^" fmt(sn)) }
				else if (c > w) { _simdLoad(t, w, "&in%^[%^]" fmt(sn, cel)) }
				else { _simdSplat(t, w, "in%^[0]" fmt(sn)) }
			} else { _varRef(ctx, n, cel) }
		}
		unopK(op):      _simdWiden(ctx.typ[n], ctx.typ[ins[0]],
		                    genUnopStr(op, ctx.typ[n] _isF32, genExpr(ctx, ins[0], cel), `cgSimdW > 0));
		-- Operands may compute at different types (e.g. a rewriter-made f32
		-- constant against an f64 signal). Scalar C promotes implicitly; SIMD
		-- vector ops don't, so widen each operand to the common type.
		binopK(op):     {
			let ct = commonType(ctx.typ[ins[0]], ctx.typ[ins[1]]);
			var a = _simdWiden(ct, ctx.typ[ins[0]], genExpr(ctx, ins[0], cel));
			var b = _simdWiden(ct, ctx.typ[ins[1]], genExpr(ctx, ins[1], cel));
			-- An inline comparison emits as C++ bool; function-syntax ops
			-- (std::min/std::max/...) are templates and refuse mixed
			-- bool/float deduction, so cast such an operand to the promotion
			-- type. A materialized compare root reads its typed slot and
			-- needs no cast. Mirrors the C++ BinaryOpExpr visitor.
			let fnSyntax = match (op binopSyntax) { OpSyntax.function: true; _: false; };
			if (`cgSimdW == 0 && fnSyntax) {
				if (_inlineCompare(ctx, ins[0])) { a = "%^(%^)" fmt(cppType(ct), a); }
				if (_inlineCompare(ctx, ins[1])) { b = "%^(%^)" fmt(cppType(ct), b); }
			}
			_simdWiden(ctx.typ[n], ct,
			    genBinopStr(op, ctx.typ[n] _isF32, a, b, `cgSimdW > 0))
		}
		compareopK(op): genCompareStr(op, genExpr(ctx, ins[0], cel), genExpr(ctx, ins[1], cel));
		castopK(ct):    (`cgSimdW > 0)
			? "convert<%^>(%^)" fmt(cppType(ct), genExpr(ctx, ins[0], cel))
			: "%^(%^)" fmt(cppType(ct), genExpr(ctx, ins[0], cel));
		selectK:        _inlineSelect(ctx, n, cel);
		reduceK(op, cols): _genReduce(ctx, n, op, cols, cel);
		vecK(vop):      _genVec(ctx, n, vop, cel);
		urandK(_):      "urand%^(%^)" fmt(ctx.typ[n] _isF32 ? "f" : "", _rngStateRef(ctx, n, cel));
		birandK(_):     "birand%^(%^)" fmt(ctx.typ[n] _isF32 ? "f" : "", _rngStateRef(ctx, n, cel));
		-- runtime rand64() returns u64; cast to the node's signed type so an
		-- inlined draw cannot turn an additive chain into unsigned arithmetic
		rand64K(_):     "i64(rand64(%^))" fmt(_rngStateRef(ctx, n, cel));
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
		delayFixReadK(d, k): n == `cgReaderSub ? `cgReaderExpr : _delayFixReadExpr(ctx, n, d, k, cel);
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
	-- One row (input chans == output chans, e.g. `sum` of a 1-channel
	-- signal) is the identity: re-emit the input at this channel, as the vec
	-- ops do. reduce_rows wants a pointer, and a 1-channel materialized
	-- input is scalar storage (`vs.vN`), so the call would not compile.
	if (rows == 1) { return genExpr(ctx, inp, cel); }
	let ty = cppType(ctx.typ[n]);
	let body = genBinopStr(op, ctx.typ[n] _isF32, "z", "x");
	let lambda = "[](%^ z, %^ x){ return %^; }" fmt(ty, ty, body);
	-- A voice-local input in flat voice mode is laid out [voice * chans +
	-- chan]: reduce the voice's own rows, not a window starting at the flat
	-- index (which summed into the next voice's slots).
	if (ctx _isVoiceLocalStorage(inp)) {
		let shift = `cgFlatChanShift;
		let voice = shift == 0 ? cel : "(%^ >> %^)" fmt(cel, shift);
		let col = cols == 1 ? "0" : "(%^ & %^)" fmt(cel, cols - 1);
		return "reduce_rows<%^, %^>(%^, %^ + %^ * %^, %^)"
			fmt(rows, cols, col, _varName(ctx, inp), voice, ctx.chans[inp], lambda);
	}
	"reduce_rows<%^, %^>(%^, %^, %^)" fmt(rows, cols, cel, _varName(ctx, inp), lambda)
}

-- Vec op codegen: remap the channel index, then re-emit the (materialized)
-- input at that index. The index algebra (VIdx) replicates the C++ VarIndex
-- simplifications so the emitted subscript byte-matches.
fn _transposeIdx(c VIdx, inChans Int, cols Int) VIdx {
	let rows = inChans // cols;
	viAdd(viMul(viRem(c, VIdx.vc(rows)), VIdx.vc(cols)), viDiv(c, VIdx.vc(rows)))
}
-- The (mono) amount is read at the loop element `cel`, not "0": in a flat
-- voice loop that resolves to the current voice's slot.
fn _rotateIdx(ctx Ctx, n NIdx, c VIdx, inChans Int, cel String) VIdx {
	let nIn = ctx.ins[n][1];
	let r = "isize(" $ genExpr(ctx, nIn, cel) $ ")";
	viMod(viAdd(c, VIdx.vs(r)), VIdx.vc(inChans))
}
fn _genVec(ctx Ctx, n NIdx, vop VecOp, cel String) String {
	let inp = ctx.ins[n][0];
	let inChans = ctx.chans[inp];
	-- at(a, i): a dynamic gather from the materialized array a by the index signal i
	-- at channel cel, wrapped mod a.chans. Mirrors the C++ VecAt visitor.
	match (vop) {
		at: {
			-- In-place delay element read (inplaceDelayOps): the delay
			-- read's materialization is suppressed, so load the state
			-- directly. The fixReaders antecedents order this before the
			-- in-place store.
			if (ctx.suppressed[inp]) {
				match (ctx.kind[inp]) {
					delayFixReadK(d, _): {
						return "p->d%^[synthdef::mod(isize(%^), isize(%^))]"
							fmt(ctx.delays[d].serial, genExpr(ctx, ctx.ins[n][1], cel), ctx.delays[d].chans);
					}
					_: {}
				}
			}
			-- A voice-local materialized vector in flat voice mode is laid
			-- out [voice * chans + chan]; the dynamic index only picks the
			-- channel, so the voice base must be added -- without it every
			-- voice reads voice 0's slots (e.g. a per-note seq table).
			-- Mirrors the C++ VecAt visitor's flat-voice branch.
			if (ctx _isVoiceLocalStorage(inp)) {
				let voice = `cgFlatChanShift == 0 ? cel : "(%^ >> %^)" fmt(cel, `cgFlatChanShift);
				return "%^[%^ * %^ + synthdef::mod(isize(%^), isize(%^))]"
				fmt(_varName(ctx, inp), voice, inChans, genExpr(ctx, ctx.ins[n][1], cel), inChans);
			}
			return "%^[synthdef::mod(isize(%^), isize(%^))]"
			fmt(_varName(ctx, inp), genExpr(ctx, ctx.ins[n][1], cel), inChans); }
		_: {}
	}
	-- A voice-local materialized input in a scalar flat voice loop is laid
	-- out [voice * chans + chan]: remap the channel within the voice and add
	-- the voice base. Remapping the flat index instead mixes voices (an
	-- ncyc read voice 0's slots for every voice).
	let flatVoice = `cgSimdW == 0 && `cgVoiceCount > 0 && ctx _isVoiceLocalStorage(inp)
		&& (ctx _isInstVar(inp) || ctx.cut[inp] isTempVarCut);
	let shift = `cgFlatChanShift;
	let c = viOf(!flatVoice ? cel : (shift == 0 ? "0" : "(%^ & %^)" fmt(cel, `cgFlatChanMask)));
	let idx = match (vop) {
		take(_):         c;
		drop(num):       viAdd(c, VIdx.vc(num));
		stride(num):     viMul(c, VIdx.vc(num));
		stutter(num):    viDiv(c, VIdx.vc(num));
		ncyc(_):         viMod(c, VIdx.vc(inChans));
		reverse:         viSub(VIdx.vc(inChans - 1), c);
		transpose(cols): _transposeIdx(c, inChans, cols);
		rotate:          _rotateIdx(ctx, n, c, inChans, cel);
		_:               c;
	};
	if (flatVoice) {
		let voice = shift == 0 ? cel : "(%^ >> %^)" fmt(cel, shift);
		return "%^[%^ * %^ + %^]" fmt(_varName(ctx, inp), voice, inChans, idx vxstr);
	}
	genExpr(ctx, inp, idx vxstr)
}

-- put(a, i, v): copy a into the output array, then scatter v into it at the
-- (mod-wrapped) index positions. A gets_own_loop block (mirror the C++ VecPut).
-- The own-loop block forms end WITHOUT a trailing newline; genLoop's `$ "\n"`
-- supplies it (matching the control-flow convention, so byte-output lines up).
-- A put/join operand reference inside a flat-voice own-loop block (voice
-- index `i` in scope): voice-local operands get an `i * chans` base; shared
-- operands are referenced as usual. Mirrors the C++ voiceArr/voiceElem.
fn _voiceArr(ctx Ctx, n NIdx) String =
	ctx _isVoiceLocalStorage(n)
		? "%^ + i * %^" fmt(_varName(ctx, n), ctx.chans[n])
		: _varName(ctx, n);

-- A scalar constant is inlined everywhere and has no declared storage (no
-- p->cN); reference it by its literal (mirrors the C++ is_scalar_constant
-- checks in scalarRef/voiceElem).
fn _scalarConstLit(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	constant(v, _): (v constSize == 1) ? constStr(v, ctx.typ[n]) : "";
	_: "";
};

-- A scalar (1-chan) put/join operand outside any voice context: a scalar
-- constant emits its literal; an input port is its gathered array's element
-- 0; anything else is a declared value.
fn _scalarRef(ctx Ctx, n NIdx) String {
	let lit = ctx _scalarConstLit(n);
	if (lit length > 0) { return lit; }
	ctx _scalarVar(n)
}

-- A 1-channel value's storage as a scalar expression. Gathered input ports
-- are arrays (p->in<sn>[]), so their scalar is element 0.
fn _scalarVar(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	inletK(_, _, _): _varName(ctx, n) $ "[0]";
	_:               _varName(ctx, n);
};

fn _voiceElem(ctx Ctx, n NIdx) String {
	let lit = ctx _scalarConstLit(n);
	if (lit length > 0) { return lit; }
	if (ctx.chans[n] == 1) {
		return ctx _isVoiceLocalStorage(n) ? "%^[i]" fmt(_varName(ctx, n)) : ctx _scalarVar(n);
	}
	ctx _isVoiceLocalStorage(n)
		? "%^[i * %^ + _j]" fmt(_varName(ctx, n), ctx.chans[n])
		: _varName(ctx, n) $ "[_j]"
}

fn _genVecPut(ctx Ctx, root NIdx) String {
	let ins = ctx.ins[root];
	let ty = cppType(ctx.typ[root]);
	let outv = _varName(ctx, root);
	let a = ins[0]; let i = ins[1]; let v = ins[2];
	let aChans = ctx.chans[a];
	let putCount = min(ctx.chans[i], ctx.chans[v]);
	let t = _tabs(`cgIndent); let t1 = _tabs(`cgIndent + 1);
	if (`cgFlatOwnLoop) {
		-- Per-voice block (voice index `i` in scope): the output row is
		-- outv + i * chans; voice-local operands get their own voice base.
		var parts [String] = [];
		parts push!(t $ "memcpy(%^ + i * %^, %^, %^ * sizeof(%^));" fmt(outv, aChans, ctx _voiceArr(a), aChans, ty));
		parts push!(t $ "for (usize _j = 0; _j < %^; ++_j) {" fmt(putCount));
		parts push!(t1 $ "%^[i * %^ + synthdef::mod(isize(%^), isize(%^))] = %^;" fmt(outv, aChans, ctx _voiceElem(i), aChans, ctx _voiceElem(v)));
		parts push!(t $ "}");
		return parts join("\n");
	}
	-- Scalar index / value inputs (chans == 1) are declared as values, not
	-- arrays, so they are referenced without the per-put subscript.
	let iref = ctx.chans[i] == 1 ? ctx _scalarRef(i) : _varName(ctx, i) $ "[_j]";
	let vref = ctx.chans[v] == 1 ? ctx _scalarRef(v) : _varName(ctx, v) $ "[_j]";
	var parts [String] = [];
	parts push!(t $ "memcpy(%^, %^, %^ * sizeof(%^));" fmt(outv, _varName(ctx, a), aChans, ty));
	parts push!(t $ "for (usize _j = 0; _j < %^; ++_j) {" fmt(putCount));
	parts push!(t1 $ "%^[synthdef::mod(isize(%^), isize(%^))] = %^;" fmt(outv, iref, aChans, vref));
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
	if (`cgFlatOwnLoop) {
		-- Per-voice block (voice index `i` in scope): the output row is
		-- outv + i * chans; voice-local inputs get their own voice base.
		let outChans = ctx.chans[root];
		for (x : ins) {
			let xc = ctx.chans[x];
			if (xc == 1) {
				parts push!(t $ "%^[i * %^ + %^] = %^;" fmt(outv, outChans, offset, ctx _voiceElem(x)));
			} else {
				parts push!(t $ "memcpy(%^ + i * %^ + %^, %^, %^ * sizeof(%^));" fmt(outv, outChans, offset, ctx _voiceArr(x), xc, ty));
			}
			offset = offset + xc;
		}
		if (offset < outChans) {
			parts push!(t $ "memset(%^ + i * %^ + %^, 0, %^ * sizeof(%^));" fmt(outv, outChans, offset, outChans - offset, ty));
		}
		return parts join("\n");
	}
	for (x : ins) {
		let xc = ctx.chans[x];
		if (xc == 1) {
			parts push!(t $ "%^[%^] = %^;" fmt(outv, offset, ctx _scalarRef(x)));
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

-- Delay state prefix: vs. inside an AoS per-voice block (the delay is a
-- VoiceState member there), p-> otherwise. Mirrors the C++
-- `dp = g.inVoiceLoop ? "vs." : "p->"` in the delay visitors.
fn _dpre() String = `cgInVoiceLoop ? "vs." : "p->";

-- Ring reads on a delay with no declared init values are ZERO-GUARDED:
-- reading further back than has been written since the write head was reset
-- returns silence instead of the buffer contents (note-on / engine reset
-- then only zero the head; the ring is never cleared, and a reused voice
-- cannot bleed the previous note's tail). Mirrors the C++ delayZeroGuarded.
fn _delayZeroGuarded(d DelayInfo) Bool = d.allocSize != 1 && d.initters length == 0;

-- A multichannel inline ring of the root graph is stored tap-major,
-- d[tap][chan] (block engine plan 4.2 step 8: a buffer's layout follows its
-- consumer): a channel-vector loop reads 4 channels of one tap as one vector.
-- Variable-time reads need each channel's ring contiguous, so a delay with
-- any keeps d[chan][tap].
fn _tapMajor(ctx Ctx, d DelayInfo) Bool =
	d.allocSize > 1 && d.chans > 1 && d.varReaders length == 0 && !(ctx _isVoiceGraphDeep(ctx _delayGraph(d)));

-- A typed zero literal for guarded ring reads (mirrors the C++ zeroLiteral).
fn _zeroLit(t NumType) String {
	if (t _isF32) { return "0.0f"; }
	if (cppType(t) == "f64") { return "0.0"; }
	"0"
}

-- Kernel pre-extent: the oldest tap each interpolation kernel touches,
-- relative to the integer delay (matches the _z kernels and the C++
-- interpPreExtent).
fn _interpPreExtent(ip Interpolation) Int = match (ip) {
	none:     0;
	linear:   1;
	cubic:    2;
	lagrange: 4;
	sinc:     4;
};

-- Ring-buffer index mask. A compile-time-sized (fixed) delay uses the constant
-- allocSize-1; a runtime-sized (maxDelay-bound) delay stores its mask in the
-- p->d{serial}_mask field. Mirrors the C++ `allocSize > 1 ? allocSize-1 : mask`.
fn _delayMask(info DelayInfo) String =
	info.allocSize > 1 ? "%^" fmt(info.allocSize - 1) : "%^d%^_mask" fmt(_dpre(), info.serial);

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
	let zg = _delayZeroGuarded(info);
	var parts [String] = [];
	var j = 0;
	while (j < w) {
		let vi = _flatSimdVoiceIdx(cel, j, shift);
		let bi = _flatSimdBufIdx(cel, j, shift, dchans);
		let mask = info.allocSize > 1 ? "%^" fmt(info.allocSize - 1) : "p->voice_d%^_mask[%^]" fmt(ser, vi);
		var rd = "p->voice_d%^[%^][(p->voice_d%^_wrpos[%^] - u64(%^)) & %^]" fmt(ser, bi, ser, vi, k, mask);
		if (zg) {
			rd = "(p->voice_d%^_wrpos[%^] >= u64(%^) ? %^ : %^)" fmt(ser, vi, k, rd, _zeroLit(t));
		}
		parts push!(rd);
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
		var rd = "%^[(%^ - u64(%^)) & %^]" fmt(_vDelayBuf(ctx, info, cel), _vDelayWrpos(info, cel), k, _vDelayMask(info, cel));
		if (_delayZeroGuarded(info)) {
			rd = "(%^ >= u64(%^) ? %^ : %^)" fmt(_vDelayWrpos(info, cel), k, rd, _zeroLit(ctx.typ[n]));
		}
		return rd;
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
		let dp = _dpre();
		if (info.allocSize == 1) {
			if (dchans == 1) { return _simdSplat(t, w, "%^d%^" fmt(dp, ser)); }
			if (dchans >= w) { return _simdLoad(t, w, "&%^d%^[%^]" fmt(dp, ser, cel)); }
			var parts [String] = [];
			var j = 0;
			while (j < w) { parts push!("%^d%^[%^]" fmt(dp, ser, _simdLaneChanIdx(cel, j, dchans, lc))); j = j + 1; }
			return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
		}
		let mask = _delayMask(info);
		let zg = _delayZeroGuarded(info);
		let guard = "%^d%^_wrpos >= u64(%^)" fmt(dp, ser, k);
		if (dchans == 1) {
			var rd = "%^d%^[(%^d%^_wrpos - u64(%^)) & %^]" fmt(dp, ser, dp, ser, k, mask);
			if (zg) { rd = "(%^ ? %^ : %^)" fmt(guard, rd, _zeroLit(t)); }
			return _simdSplat(t, w, rd);
		}
		let tm = ctx _tapMajor(info);
		let tap = "(%^d%^_wrpos - u64(%^)) & %^" fmt(dp, ser, k, mask);
		if (tm && dchans == lc) {
			-- tap-major: the tap's channels are one vector
			let ld = _simdLoad(t, w, "&%^d%^[%^][%^]" fmt(dp, ser, tap, cel));
			return zg ? "(%^ ? %^ : %^)" fmt(guard, ld, _simdSplat(t, w, _zeroLit(t))) : ld;
		}
		var parts [String] = [];
		var j = 0;
		while (j < w) {
			let ci = _simdLaneChanIdx(cel, j, dchans, lc);
			var rd = tm ? "%^d%^[%^][%^]" fmt(dp, ser, tap, ci) : "%^d%^[%^][%^]" fmt(dp, ser, ci, tap);
			if (zg) { rd = "(%^ ? %^ : %^)" fmt(guard, rd, _zeroLit(t)); }
			parts push!(rd);
			j = j + 1;
		}
		return "%^{%^}" fmt(_simdType(t, w), parts join(", "));
	}
	let di = _delayDIndex(ctx, d, n, cel);
	if (info.allocSize == 1) {
		return "%^d%^%^" fmt(_dpre(), info.serial, di);
	}
	let tapIx = "[(%^d%^_wrpos - u64(%^)) & %^]" fmt(_dpre(), info.serial, k, _delayMask(info));
	var rd = ctx _tapMajor(info) ? "%^d%^%^%^" fmt(_dpre(), info.serial, tapIx, di)
	                             : "%^d%^%^%^" fmt(_dpre(), info.serial, di, tapIx);
	if (_delayZeroGuarded(info)) {
		rd = "(%^d%^_wrpos >= u64(%^) ? %^ : %^)" fmt(_dpre(), info.serial, k, rd, _zeroLit(ctx.typ[n]));
	}
	rd
}

-- A scalar (single-channel) delay-time offset is emitted in scalar mode at lane 0;
-- the dynvar shadow restores `cgSimdW on return. Mirrors the C++ inSimdMode toggle.
fn _genDelayOffScalar(ctx Ctx, e NIdx) String {
	var `cgSimdW Int = 0;
	genExpr(ctx, e, "0")
}

-- A single scalar delay read (the C++ genRead lambda): interpNone indexes the ring
-- directly; an interpolating mode calls the tzpl_delay_<kernel> helper.
-- Guarded (zg) reads go through the _z kernels: interp-none must not
-- open-code a ternary that would evaluate the delay-time expression twice
-- (it can contain rand draws). Mirrors the C++ genRead.
fn _delayVarGenRead(ip Interpolation, buf String, wrpos String, mask String, off String, zg Bool) String = match (ip) {
	none: zg ? "tzpl_delay_none_z(%^, %^, %^, %^)" fmt(buf, wrpos, mask, off)
	         : "%^[(%^ - u64(tzpl_delay_time(%^))) & %^]" fmt(buf, wrpos, off, mask);
	_:    "tzpl_delay_%^%^(%^, %^, %^, %^)" fmt(ip toLispString, zg ? "_z" : "", buf, wrpos, mask, off);
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
	r
}

let _kSincKernelCall = "tzpl_interp_sinc(_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7, _c0, _c1, _c2, _c3, _c4, _c5, _c6, _c7)";

-- The SIMD interpolation lambda (mirror genSimdInterp). Gathers per-lane tap vectors
-- from the ring then calls the vector interp kernel. bufs/wrposs/masks are per-lane
-- expression strings (so this serves both the non-flat and flat-voice gathers). A
-- scalar offset uses one `_di` and casts frac; a vector offset uses per-lane `_di{j}`.
fn _genSimdInterp(t NumType, w Int, ip Interpolation,
                  bufs [String], wrposs [String], masks [String],
                  offExpr String, scalarOff Bool, zg Bool) String {
	let vtype = _simdType(t, w);
	let stype = cppType(t);
	let taps = _interpTaps(ip);
	let pre = _interpPreExtent(ip);
	var r = "[&]() -> %^ { " fmt(vtype);
	-- offset cache + per-lane integer delays
	if (scalarOff) {
		r = r $ "auto _off = tzpl_delay_time(%^); u64 _di = u64(_off); " fmt(offExpr);
	} else {
		-- every lane's delay time clamped at 0 (tzpl_delay_time) before the
		-- integer conversion, which is undefined for negative values
		r = r $ "auto _off = %^; " fmt(offExpr);
		var j = 0;
		while (j < w) { r = r $ "_off[%^] = tzpl_delay_time(_off[%^]); u64 _di%^ = u64(_off[%^]); " fmt(j, j, j, j); j = j + 1; }
	}
	-- Zero-history guard (delays with no init values): a uniform write head
	-- takes one early return; per-lane heads/offsets zero the invalid lanes
	-- after the kernel below. Mirrors the C++ genSimdInterp guard.
	var uniform = scalarOff;
	var ju = 1;
	while (ju < w) { if (wrposs[ju] != wrposs[0]) { uniform = false; } ju = ju + 1; }
	if (zg && uniform) {
		r = r $ "if (%^ < _di + %^) return (%^)(0); " fmt(wrposs[0], pre, vtype);
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
	var kcall = "";
	match (ip) {
		sinc: {
			r = r $ _genSimdSincCoeffs(w, scalarOff, stype, vtype);
			kcall = _kSincKernelCall;
		}
		_: {
			kcall = "%^(" fmt(_interpKernel(ip));
			var k = 0;
			while (k < taps length) {
				if (k > 0) { kcall = kcall $ ", "; }
				kcall = kcall $ "_s%^" fmt(k);
				k = k + 1;
			}
			kcall = kcall $ ", _frac)";
		}
	}
	if (zg && !uniform) {
		r = r $ "%^ _r = %^; " fmt(vtype, kcall);
		var jz = 0;
		while (jz < w) {
			let di = scalarOff ? "_di" : "_di%^" fmt(jz);
			r = r $ "if (%^ < %^ + %^) _r[%^] = 0; " fmt(wrposs[jz], di, pre, jz);
			jz = jz + 1;
		}
		r = r $ "return _r; ";
	} else {
		r = r $ "return " $ kcall $ "; ";
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
	let dp = _dpre();
	let wrpos = "%^d%^_wrpos" fmt(dp, ser);
	let mask = "%^d%^_mask" fmt(dp, ser);
	-- scalar offset + 1-chan delay: every lane reads the same value, splat it.
	if (scalarOff && dchans == 1) {
		return _simdSplat(t, w, _delayVarGenRead(ip, "%^d%^" fmt(dp, ser), wrpos, mask, offExpr, _delayZeroGuarded(info)));
	}
	match (ip) {
		none: {}
		_: {
			var bufs [String] = []; var wrposs [String] = []; var masks [String] = [];
			var jj = 0;
			while (jj < w) {
				let ci = dchans == 1 ? "" : "[%^]" fmt(_simdLaneChanIdx(cel, jj, dchans, lc));
				bufs push!("%^d%^%^" fmt(dp, ser, ci));
				wrposs push!(wrpos);
				masks push!(mask);
				jj = jj + 1;
			}
			return _genSimdInterp(t, w, ip, bufs, wrposs, masks, offExpr, scalarOff, _delayZeroGuarded(info));
		}
	}
	-- interpNone vector: a per-lane gather (off re-indexed per lane, no caching).
	var parts [String] = [];
	var j = 0;
	while (j < w) {
		let ci = dchans == 1 ? "" : "[%^]" fmt(_simdLaneChanIdx(cel, j, dchans, lc));
		let off = scalarOff ? offExpr : "(%^)[%^]" fmt(offExpr, j);
		parts push!(_delayVarGenRead(Interpolation.none, "%^d%^%^" fmt(dp, ser, ci), wrpos, mask, off, _delayZeroGuarded(info)));
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
			return _genSimdInterp(t, w, ip, bufs, wrposs, masks, offExpr, globalScalar, _delayZeroGuarded(info));
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
		parts push!(_delayVarGenRead(Interpolation.none, "p->voice_d%^[%^]" fmt(ser, bi), "p->voice_d%^_wrpos[%^]" fmt(ser, vi), mask, off, _delayZeroGuarded(info)));
		j = j + 1;
	}
	"%^{%^}" fmt(_simdType(t, w), parts join(", "))
}

-- Variable (signal-rate) delay read with interpolation. interpNone reads the
-- ring buffer directly; the interpolating kernels call tzpl_delay_<kernel>
-- (declared in tzpl_delay_interp.hpp). off is the delay-time signal expression.
fn _delayVarReadExpr(ctx Ctx, n NIdx, d Int, ip Interpolation, cel String) String {
	let info = ctx.delays[d];
	let zg = _delayZeroGuarded(info);
	if (ctx _isVoiceDelay(d)) {
		if (`cgSimdW > 0) { return _voiceDelayVarReadSimd(ctx, n, info, ip, cel); }
		let off = genExpr(ctx, ctx.ins[n][0], cel);
		return _delayVarGenRead(ip, _vDelayBuf(ctx, info, cel), _vDelayWrpos(info, cel), _vDelayMask(info, cel), off, zg);
	}
	if (`cgSimdW > 0) { return _delayVarReadSimd(ctx, n, info, ip, cel); }
	let off = genExpr(ctx, ctx.ins[n][0], cel);
	let di = _delayDIndex(ctx, d, n, cel);
	_delayVarGenRead(ip, "%^d%^%^" fmt(_dpre(), info.serial, di), "%^d%^_wrpos" fmt(_dpre(), info.serial), _delayMask(info), off, zg)
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
	-- In-place put (inplaceDelayOps): replace one element of the delay
	-- state with a single indexed store.
	let vin = ctx.ins[n][0];
	if (ctx.suppressed[vin]) {
		return "p->d%^[synthdef::mod(isize(%^), isize(%^))] = %^; // %^ DelayWrite (in-place put)\n"
			fmt(info.serial, genExpr(ctx, ctx.ins[vin][1], cel), info.chans,
			    genExpr(ctx, ctx.ins[vin][2], cel), treeSerial);
	}
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
		let dp = _dpre();
		if (info.allocSize == 1 && dchans >= w) {
			return "*(%^*)(&%^d%^[%^]) = %^; // %^ DelayWrite\n" fmt(_simdType(info.typ, w), dp, ser, cel, value, treeSerial);
		}
		let tm = ctx _tapMajor(info);
		if (tm && dchans == lc) {
			return "*(%^*)(&%^d%^[%^d%^_wrpos & %^][%^]) = %^; // %^ DelayWrite\n"
				fmt(_simdType(info.typ, w), dp, ser, dp, ser, _delayMask(info), cel, value, treeSerial);
		}
		var s = "%^ _dw%^ = %^; // %^ DelayWrite\n" fmt(_simdType(info.typ, w), ser, value, treeSerial);
		let mask = _delayMask(info);
		var j = 0;
		while (j < w) {
			let ci = _simdLaneChanIdx(cel, j, dchans, lc);
			if (info.allocSize == 1) {
				s = s $ _tabs(`cgIndent) $ "%^d%^[%^] = _dw%^[%^];\n" fmt(dp, ser, ci, ser, j);
			} else if (tm) {
				s = s $ _tabs(`cgIndent) $ "%^d%^[%^d%^_wrpos & %^][%^] = _dw%^[%^];\n" fmt(dp, ser, dp, ser, mask, ci, ser, j);
			} else {
				s = s $ _tabs(`cgIndent) $ "%^d%^[%^][%^d%^_wrpos & %^] = _dw%^[%^];\n" fmt(dp, ser, ci, dp, ser, mask, ser, j);
			}
			j = j + 1;
		}
		return s;
	}
	let di = info.chans > 1 ? "[%^]" fmt(cel) : "";
	if (info.allocSize == 1) {
		"%^d%^%^ = %^; // %^\n" fmt(_dpre(), info.serial, di, value, treeSerial)
	} else if (ctx _tapMajor(info)) {
		"%^d%^[%^d%^_wrpos & %^]%^ = %^; // %^\n" fmt(_dpre(), info.serial, _dpre(), info.serial, _delayMask(info), di, value, treeSerial)
	} else {
		"%^d%^%^[%^d%^_wrpos & %^] = %^; // %^\n" fmt(_dpre(), info.serial, di, _dpre(), info.serial, _delayMask(info), value, treeSerial)
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
	-- Buffers hold f64: the read position is f64 whatever the index computes in.
	_:    "(%^ ? %^(%^->data[%^], %^->length, f64(%^)) : 0.0)" fmt(bref, _bufInterpFunc(interp), bref, chan, bref, off);
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
		_:    "(%^ ? %^(%^->data[%^], %^->length, f64(%^)) : 0.0)" fmt(bp, _bufInterpFunc(interp), bp, chan, bp, idx);
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
	if (ctx _isVoiceGraphDeep(ctx.graphOf[lu])) {
		-- AoS: a VoiceState member (vs. inside a per-voice block, else
		-- indexed by the loop's voice name). Mirrors the C++ bankSlot.
		if (!`cgFlatVoice) {
			if (`cgInVoiceLoop) { return "vs.lu%^_%^" fmt(ctx.serial[lu], field); }
			return "p->voice_state[%^].lu%^_%^" fmt(`cgVoiceIdx, ctx.serial[lu], field);
		}
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
		_:    "(%^ ? %^(%^->data[%^], %^->length, f64(%^)) : 0.0)" fmt(bp, _bufInterpFunc(interp), bp, chan, bp, idx);
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
	if (ctx _isVoiceGraphDeep(ctx.graphOf[n])) {
		-- AoS uses the enclosing per-voice block's index name; flat derives
		-- the voice from cel. Mirrors the C++ BankLookup visitor.
		let vi = !`cgFlatVoice ? `cgVoiceIdx
			: (`cgFlatChanShift == 0 ? cel : "(%^ >> %^)" fmt(cel, `cgFlatChanShift));
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

-- Transitive version: is g the voicer body OR nested inside it (e.g. a pause
-- branch subgraph of the voice body)? Mirrors the C++ isVoicerSubgraph, which
-- walks the parent chain. Flat bodies have no nesting (control flow is what
-- forces AoS mode), so the flat paths keep using the direct check.
fn _isVoiceGraphDeep(ctx Ctx, g Int) Bool {
	var cur = g;
	while (cur != NONE) {
		if (ctx _isVoiceGraph(cur)) { return true; }
		cur = ctx.graphs[cur].parent;
	}
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
	-- Collect from the declaration list (ctx.noteParams), NOT ctx.sorted: a
	-- declared-but-unused param is sink-unreachable, so the topo sort leaves
	-- it out of sorted -- but it still owns a column of the params matrix
	-- (noteOn params follow declaration order, used or not). Sizing from
	-- sorted shrank the matrix while surviving params kept their original
	-- column serials -- an out-of-bounds row read that silenced the synth.
	-- Mirrors the C++, which sizes from the synth->noteParams declaration
	-- list; then order by serial.
	var nodes [Int] = [];
	for (n : ctx.noteParams) {
		if (ctx.graphOf[n] == bg) { nodes push!(n); }
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
	if (!`cgFlatVoice) {
		-- AoS (non-flat): a per-voice loop; the body loops emit plain scalar
		-- code whose voice state resolves to vs. members. Mirrors the C++
		-- VoicerExpr visitor's AoS branch.
		var s = _tabs(`cgIndent) $ "for (int v = 0; v < %^; ++v) {\n" fmt(mv);
		var `cgIndent Int = `cgIndent + 1;
		s = s $ _tabs(`cgIndent) $ "f32* vparams = &in%^[v * %^];\n" fmt(`cgNotesSn, `cgNotesCols);
		s = s $ _tabs(`cgIndent) $ "auto& vs = p->voice_state[v];\n";
		var `cgInVoiceLoop Bool = true;
		var `cgVoiceIdx String = "v";
		s = s $ genLoops(ctx, ctx.graphs[bg].audioLoops) $ ctx genDelayAdvance(bg);
		s = s $ _tabs(`cgIndent - 1) $ "}\n";
		return s;
	}
	var `cgVoiceCount Int = mv;
	var `cgVoiceGraph Int = bg;
	var `cgVoiceChans Int = ctx.chans[ctx.ins[ctx.subs[root][0]][0]];
	ctx genVoiceBlockLoops(bg) $ _tabs(`cgIndent) $ genLoops(ctx, ctx.graphs[bg].audioLoops)
		$ ctx genDelayAdvance(bg)
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
	-- The loop variable's own name (for_("k", ...)), as its uses in the body
	-- emit it. (The variable node itself is built in the enclosing graph.)
	let bg = ctx.graphOf[ctx.subs[root][0]];
	var vname = "i";
	var n = 0;
	while (n < ctx numNodes) {
		if (ctx.graphOf[n] == bg) {
			for (x : ctx.ins[n]) {
				match (ctx.kind[x]) { varK(nm): { vname = nm; } _: {} }
			}
		}
		n = n + 1;
	}
	var s = _tabs(cur) $ "for (i32 %^ = 0; %^ < %^; ++%^) {\n" fmt(vname, vname, count, vname);
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
	var `cgDstShape Shape = ctx.shape[root];

	match (ctx.cut[root]) {
		unused: { return _tabs(`cgIndent) $ "// Unused %^ %^\n" fmt(ctx.serial[root], nodeStr(ctx, root)); }
		_: {}
	}

	-- Special sink forms handled structurally.
	match (ctx.kind[root]) {
		inletK(_, _, _): { return ""; }   -- gathered by process()
		delayWriteK(d): {
			if (d == `cgSuppressDelay) { return ""; }   -- a block-form cycle sets its state itself
			return _tabs(`cgIndent) $ _delayWriteStmt(ctx, root, d, cel, tree.serial);
		}
		delayInitK(_, _): { return ""; }   -- emitted by genDelayInit
		maxDelayK(_): { return ""; }
		bufWriteK(bi, wc, sc): { return _tabs(`cgIndent) $ _bufWriteStmt(ctx, root, bi, wc, sc, cel, tree.serial); }
		bankLookupK(_): { return _tabs(`cgIndent) $ _bankLookupStmt(ctx, root, cel, tree.serial); }
		ifK:            { return ctx _genIf(root); }
		forK:           { return ctx _genFor(root); }
		switchK(nc):    { return ctx _genSwitch(root, nc); }
		voicerK(mv):    { return ctx _genVoicer(root, mv); }
		spectralChainK(fft, hop): { return ctx _genSpectral(root, fft, hop); }
		phiK(target):   {
			-- AoS per-voice loop: a phi writing the voicer's output indexes
			-- it (v * voiceChans + cel) -- without the per-voice offset every
			-- voice iteration would overwrite the same output slots. Mirrors
			-- the C++ PhiNodeExpr visitor.
			let isVoicerTarget = match (ctx.kind[target]) { voicerK(_): true; _: false; };
			if (`cgInVoiceLoop && isVoicerTarget) {
				let vChans = ctx.chans[ctx.ins[root][0]];
				let voiceCel = vChans == 1 ? `cgVoiceIdx
					: "(%^ * %^ + (%^))" fmt(`cgVoiceIdx, vChans, cel);
				return _tabs(`cgIndent) $ "%^ = %^;\n" fmt(_varRef(ctx, target, voiceCel), genExpr(ctx, ctx.ins[root][0], cel));
			}
			return _tabs(`cgIndent) $ "%^ = %^;\n" fmt(_varRef(ctx, target, cel), genExpr(ctx, ctx.ins[root][0], cel));
		}
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
	let localDecl = !(ctx _isBlockTemp(root)) && (_consumedInLoop(ctx, treeIdx)
		|| (ctx.cut[root] isTempVarCut && `cgLoopChans == 1 && `cgVoiceCount == 0));
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
		if (ctx _directPort(n) && `cgPortRows) {
			let val = genExpr(ctx, ctx.ins[n][0], cel);
			let C = ctx.chans[n];
			if (`cgSimdW > 0) {
				"*(%^*)&t_out%^[k * %^ + %^] = %^" fmt(_simdType(ctx.typ[n], `cgSimdW), sn, C, cel, val)
			} else {
				"t_out%^[k * %^ + %^] = %^" fmt(sn, C, cel, val)
			}
		} else if (ctx _directPort(n)) {
			let val = genExpr(ctx, ctx.ins[n][0], cel);
			if (`cgSimdW > 0) {
				-- the vector's lanes to their channels at sample bi
				let w = `cgSimdW;
				var st = "{ %^ _o = %^;" fmt(_simdType(ctx.typ[n], w), val);
				var j = 0;
				while (j < w) {
					let idx = cel == "0" ? j toString : "%^ + %^" fmt(cel, j);
					st = st $ " r_out%^[%^][bi] = _o[%^];" fmt(sn, idx, j);
					j = j + 1;
				}
				st $ " }"
			} else {
				"r_out%^[%^][bi] = %^" fmt(sn, cel, val)
			}
		} else if (`cgSimdW > 0) {
			let t = ctx.typ[n]; let w = `cgSimdW;
			let val = genExpr(ctx, ctx.ins[n][0], cel);
			-- Case A (chans==width): the bare outlet pointer; Case B: &accessor[cel]
			-- (no parens around the &, mirroring the C++ Outlet visitor).
			if (`cgSampleVec) {
				"*(%^u*)out%^ = %^" fmt(_simdType(t, w), sn, val)
			} else if (ctx.chans[n] == w) {
				"*(%^*)out%^ = %^" fmt(_simdType(t, w), sn, val)
			} else {
				"*(%^*)&out%^[%^] = %^" fmt(_simdType(t, w), sn, cel, val)
			}
		} else {
			"out%^%^ = %^"
				fmt(sn, (ctx.chans[n] > 1 ? "[%^]" fmt(cel) : "[0]"),
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
fn _simdLoad(t NumType, w Int, ptr String) String =
	"*(%^%^*)(%^)" fmt(_simdType(t, w), `cgSampleVec ? "u" : "", ptr);

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

-- A voice's notes column: element (voice, col) of the gathered `notes` input
-- {maxVoices, cols}.
fn _np(vi String, col String) String =
	"in%^[(%^) * %^ + %^]" fmt(`cgNotesSn, vi, `cgNotesCols, col);

-- NoteParam in flat-voice SIMD: a per-lane gather of the voice's column(s).
-- A 1-chan param whose lanes all fall in one voice splats. The column is the
-- param's first column, plus the wrapped chan for a multichannel one.
fn _noteParamSimd(t NumType, chans Int, psn Int, cel String) String {
	let w = `cgSimdW;
	let shift = `cgFlatChanShift;
	let log2Width = w == 4 ? 2 : 1;
	if (chans == 1 && shift >= log2Width) {
		return _simdSplat(t, w, _np(_flatSimdVoiceIdx(cel, 0, shift), psn toString));
	}
	var parts [String] = [];
	var j = 0;
	while (j < w) {
		let vi = _flatSimdVoiceIdx(cel, j, shift);
		if (chans == 1) {
			parts push!(_np(vi, psn toString));
		} else if (cel == "0") {
			parts push!(_np(vi, (psn + (j & (chans - 1))) toString));
		} else {
			parts push!(_np(vi, "%^ + ((%^ + %^) & %^)" fmt(psn, cel, j, chans - 1)));
		}
		j = j + 1;
	}
	"%^{%^}" fmt(_simdType(t, w), parts join(", "))
}

-- NoteParam in flat-voice scalar mode: voice = cel >> flatChanShift; a
-- multichannel param adds the wrapped channel to the column.
fn _noteParamFlatScalar(ctx Ctx, n NIdx, psn Int, cel String) String {
	let shift = `cgFlatChanShift;
	let vi = shift == 0 ? cel : "%^ >> %^" fmt(cel, shift);
	if (ctx.chans[n] == 1) { return _np(vi, psn toString); }
	_np(vi, "%^ + (%^ %% %^)" fmt(psn, cel, ctx.chans[n]))
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
			-- The SIMD forms index with power-of-two masks: any other channel
			-- count (an exact tensor shape) runs the scalar loop.
			if (!(ctx _pow2Chans(e))) { return true; }
			for (x : ctx.ins[e]) { if (!(ctx _pow2Chans(x))) { return true; } }
		}
	}
	false
}

fn _pow2Chans(ctx Ctx, n NIdx) Bool = ctx.chans[n] == 0 || ctx.chans[n] hasSingleBit;

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
	-- Trees folded into in-place delay element accesses (inplaceDelayOps)
	-- are not emitted; drop them, and drop the loop if nothing remains.
	var live [Int] = [];
	for (t : loop.trees) {
		if (!ctx.suppressed[ctx.trees[t].root]) { live push!(t); }
	}
	if (live length < loop.trees length) {
		if (live length == 0) { return ""; }
		ctx.loops[loopIdx] = Loop { ...loop, trees: live };
		return genLoop(ctx, loopIdx);
	}
	-- Auto-enter flat voice mode for a voicer-body loop reached outside _genVoicer
	-- (e.g. the body's init-rate loop emitted from genInitFun).
	let rootGraph = ctx.graphOf[ctx.trees[loop.trees[0]].root];
	if (`cgFlatVoice && `cgVoiceCount == 0 && ctx _isVoiceGraph(rootGraph)) {
		var `cgVoiceCount Int = ctx _voicerMaxForGraph(rootGraph);
		var `cgVoiceGraph Int = rootGraph;
		var `cgVoiceChans Int = ctx _voicerChansForGraph(rootGraph);
		return genLoop(ctx, loopIdx);
	}
	-- The AoS (non-flat) counterpart: wrap a voicer-body loop reached outside
	-- the voicer in a per-voice block (e.g. an event-rate loop emitted from
	-- genHandleEventsFun). Cross-loop event values are instance vars (vs.
	-- members), so per-loop wrapping is safe here; init loops, whose
	-- cross-loop temps are stack locals, are wrapped as a single block by
	-- genInitFun instead. Mirrors the C++ AoS auto-enter.
	if (!`cgFlatVoice && !`cgInVoiceLoop && ctx _voicerNodes length > 0
			&& ctx _isVoiceGraphDeep(rootGraph)) {
		let mv = ctx _voicerMaxVoices(ctx _voicerNodes[0]);
		var s2 = _tabs(`cgIndent) $ "for (int v = 0; v < %^; ++v) {\n" fmt(mv);
		var `cgIndent Int = `cgIndent + 1;
		s2 = s2 $ _tabs(`cgIndent) $ "f32* vparams = &in%^[v * %^];\n" fmt(`cgNotesSn, `cgNotesCols);
		s2 = s2 $ _tabs(`cgIndent) $ "auto& vs = p->voice_state[v];\n";
		var `cgInVoiceLoop Bool = true;
		var `cgVoiceIdx String = "v";
		s2 = s2 $ genLoop(ctx, loopIdx);
		s2 = s2 $ _tabs(`cgIndent - 1) $ "}\n";
		return s2;
	}
	-- In flat voice mode every voicer-subgraph loop (including PhiNode output
	-- loops) iterates voices x channels: loop.chans is the per-voice channel
	-- count, and the voice fan-out is in the trip count and the shift/mask
	-- voice/channel decomposition of `i`. Mirrors the C++ genLoop/simdWidth.
	let flat = `cgVoiceCount > 0;
	var `cgLoopChans Int = loop.chans;
	var `cgFlatChanShift Int = (flat && loop.chans > 1) ? _log2(loop.chans) : 0;
	var `cgFlatChanMask Int = (flat && loop.chans > 1) ? loop.chans - 1 : 0;
	let trip = flat ? `cgVoiceCount * loop.chans : loop.chans;
	var `cgFlatTrip Int = flat ? trip : 0;

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
			if (ctx.cut[root] isTempVarCut && !_consumedInLoop(ctx, t) && !(ctx _isBlockTemp(root))) {
				let shp = flat ? "[%^]" fmt(`cgVoiceCount * ctx.chans[root]) : _shape(ctx.chans[root]);
				decls = decls $ "\t%^ %^%^;\n" fmt(cppType(ctx.typ[root]), _varName(ctx, root), shp);
			}
		}
		if (decls length > 0) { s = s $ decls $ "\n"; }
	}

	var code = "";
	if (flat && loop.trees length == 1 && getsOwnLoop(ctx.kind[ctx.trees[loop.trees[0]].root])
			&& ctx.graphOf[ctx.trees[loop.trees[0]].root] == `cgVoiceGraph) {
		-- put/join with a per-voice output array: run the block once per voice
		-- with `i` as the voice index (or the single voice `vi` in noteOn
		-- reset loops); the emitters add `i * chans` bases to voice-local
		-- operands. Mirrors the C++ flat own-loop wrapper.
		if (`cgSingleVoice) {
			code = code $ _tabs(`cgIndent) $ "{ int i = vi;\n";
		} else {
			code = code $ _tabs(`cgIndent) $ "for (int i = 0; i < %^; ++i) {\n" fmt(`cgVoiceCount);
		}
		var `cgIndent Int = `cgIndent + 1;
		var `cgFlatOwnLoop Bool = true;
		-- The own-loop block forms end without a trailing newline; supply it
		-- before the closing brace (the C++ visitor lines each end in \n).
		code = code $ genTree(ctx, loop.trees[0], "i") $ "\n";
		code = code $ _tabs(`cgIndent - 1) $ "}\n";
	} else if (flat) {
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
	for (d : ctx.delays) { if (!(ctx _isVoiceGraphDeep(ctx _delayGraph(d)))) { root push!(d); } }
	if (root length == 0) { return ""; }
	var s = "\t// delays\n";
	for (d : root) {
		let base = "\t%^" fmt(cppType(d.typ));
		if (d.allocSize == 1) {
			s = s $ base $ " d%^%^;\n" fmt(d.serial, _shape(d.chans));
		} else if (d.allocSize > 1) {
			s = s $ base $ (ctx _tapMajor(d) ? " d%^[%^]%^;\n" fmt(d.serial, d.allocSize, _shape(d.chans))
			                                 : " d%^%^[%^];\n" fmt(d.serial, _shape(d.chans), d.allocSize));
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
	for (g : ctx _rngGraphs) { if (!(ctx _isVoiceGraphDeep(g))) { s = s $ "\tRandState1 rgen%^;\n" fmt(g); } }
	-- inst-var roots, in loop/tree order. Voicer-body inst vars are per-voice and
	-- emitted in the voicer block below, so skip them here (deep: nested
	-- control-flow subgraph state is per-voice too).
	for (lp : ctx.loops) {
		for (t : lp.trees) {
			let root = ctx.trees[t].root;
			let isInletOutlet = match (ctx.kind[root]) { inletK(_, _, _): true; outletK(_, _): true; _: false; };
			let inVoice = ctx _isVoiceGraphDeep(ctx.graphOf[root]);
			if (ctx _isInstVar(root) && !isInletOutlet && !inVoice && !(ctx _isRootBuf(root))) {
				s = s $ "\t%^ %^%^;\n" fmt(cppType(ctx.typ[root]), _varDeclName(ctx, root), _shape(ctx.chans[root]));
			}
		}
	}
	if (s length > 0) { s = "\t// instance variables\n" $ s $ "\n"; }
	s = genPortDecls(ctx) $ s $ ctx genBlockBufDecls;
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
-- AoS voice delay member decls (inside struct VoiceState, indent "\t\t").
-- Mirrors the C++ genDelayDecls(voiceDelays, "\t\t").
fn _genVoiceDelayDeclsAoS(ctx Ctx) String {
	var vd [DelayInfo] = [];
	for (d : ctx.delays) { if (ctx _isVoiceGraphDeep(ctx _delayGraph(d))) { vd push!(d); } }
	if (vd length == 0) { return ""; }
	var s = "\t\t// delays\n";
	for (d : vd) {
		let base = "\t\t%^" fmt(cppType(d.typ));
		if (d.allocSize == 1) {
			s = s $ base $ " d%^%^;\n" fmt(d.serial, _shape(d.chans));
		} else if (d.allocSize > 1) {
			s = s $ base $ " d%^%^[%^];\n" fmt(d.serial, _shape(d.chans), d.allocSize);
		} else {
			s = s $ base $ " *d%^%^;\n" fmt(d.serial, _shape(d.chans));
		}
	}
	s = s $ "\n";
	for (d : vd) {
		if (d.allocSize != 1) { s = s $ "\t\tu64 d%^_wrpos;\n" fmt(d.serial); }
		if (d.hasMaxBound) { s = s $ "\t\tu64 d%^_mask;\n" fmt(d.serial); }
	}
	s
}

fn genVoicerDecls(ctx Ctx) String {
	var s = "";
	if (!`cgFlatVoice) {
		-- AoS layout: a VoiceState struct with per-voice rgens, instance
		-- vars, delays, and bank lookup slots as plain members. The deep
		-- graph check includes nested control-flow subgraphs (their rgens
		-- and state are per-voice too). Mirrors the C++ genDeclVoiceState
		-- AoS branch.
		for (v : ctx _voicerNodes) {
			let mv = ctx _voicerMaxVoices(v);
			s = s $ "\tstruct VoiceState {\n";
			for (g : ctx _rngGraphs) {
				if (ctx _isVoiceGraphDeep(g)) { s = s $ "\t\tRandState1 rgen%^;\n" fmt(g); }
			}
			for (lp : ctx.loops) {
				for (t : lp.trees) {
					let root = ctx.trees[t].root;
					if (ctx _isVoiceGraphDeep(ctx.graphOf[root]) && ctx _isInstVar(root)) {
						s = s $ "\t\t%^ v%^%^;\n" fmt(cppType(ctx.typ[root]), ctx.serial[root], _shape(ctx.chans[root]));
					}
				}
			}
			s = s $ ctx _genVoiceDelayDeclsAoS;
			for (lu : ctx _bankLookupNodes) {
				if (ctx _isVoiceGraphDeep(ctx.graphOf[lu])) {
					let u = ctx.serial[lu];
					s = s $ "\t\ti32 lu%^_p;\n" fmt(u);
					s = s $ "\t\ti32 lu%^_v;\n" fmt(u);
					s = s $ "\t\ttzpl_Buffer* lu%^_buf;\n" fmt(u);
					s = s $ "\t\tf32 lu%^_rootKey;\n" fmt(u);
					s = s $ "\t\tf64 lu%^_sr;\n" fmt(u);
					s = s $ "\t\tf64 lu%^_len;\n" fmt(u);
					s = s $ "\t\tf64 lu%^_loopStart;\n" fmt(u);
					s = s $ "\t\tf64 lu%^_loopEnd;\n" fmt(u);
					s = s $ "\t\ti32 lu%^_hasLoop;\n" fmt(u);
				}
			}
			s = s $ "\t};\n\n";
			s = s $ "\tVoiceState voice_state[%^];\n" fmt(mv);
		}
		if (s length > 0) { s = s $ "\n"; }
		return s;
	}
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

fn genAllocFun(ctx Ctx, name String) String {
	var s = "%^* %^_alloc(int32_t maxBlock) {\n\t%^* p = (%^*)calloc(1, sizeof(%^));\n\tp->funs = %^_funs;\n"
		fmt(name, name, name, name, name, name);
	s = s $ "\tif (maxBlock < 1) maxBlock = 1;\n";
	let bufs = ctx _blockBuffers;
	var k = 0;
	while (k < bufs.names length) {
		if (bufs.alias[k] length == 0) {
			s = s $ "\tp->%^ = (%^*)calloc((size_t)maxBlock * %^%^, sizeof(%^));\n"
				fmt(bufs.names[k], bufs.types[k], bufs.elems[k],
				    bufs.pad[k] > 0 ? " + %^" fmt(bufs.pad[k]) : "", bufs.types[k]);
		}
		k = k + 1;
	}
	-- arena: temps whose live ranges don't overlap share memory
	k = 0;
	while (k < bufs.names length) {
		if (bufs.alias[k] length > 0) { s = s $ "\tp->%^ = p->%^;\n" fmt(bufs.names[k], bufs.alias[k]); }
		k = k + 1;
	}
	s $ "\treturn p;\n}\n\n"
}

-- Every block buffer: member names, element types, row elements (parallel
-- arrays; an array of tuples here tripped a VM GC bug).
struct BlockBufs {
	names [String],
	types [String],
	elems [Int],
	pad [Int],      -- extra leading elements (a scan's w holds the prior state at [-1])
	alias [String], -- "" or the buffer whose memory this one reuses (arena)
}

fn _addBuf(b BlockBufs, name String, ty String, e Int, pad Int = 0, alias String = "") Void {
	b.names push!(name);
	b.types push!(ty);
	b.elems push!(e);
	b.pad push!(pad);
	b.alias push!(alias);
}

fn _blockBuffers(ctx Ctx) BlockBufs {
	let out = BlockBufs { names: [String](), types: [String](), elems: [Int](), pad: [Int](), alias: [String]() };
	-- One-channel ports are read and written in place (planar is interleaved).
	for (x : ctx.inlets) {
		match (ctx.kind[x]) { inletK(_, sn, _): { if (ctx.chans[x] > 1 && !(ctx _directPort(x))) { out _addBuf("bin%^" fmt(sn), cppType(ctx.typ[x]), ctx _portElems(x)); } } _: {} }
	}
	for (x : ctx.outlets) {
		match (ctx.kind[x]) { outletK(_, sn): { if (ctx.chans[x] > 1 && !(ctx _directPort(x))) { out _addBuf("bout%^" fmt(sn), cppType(ctx.typ[x]), ctx _portElems(x)); } } _: {} }
	}
	for (x : `cgRootBuf) { out _addBuf("bk%^" fmt(ctx.serial[x]), cppType(ctx.typ[x]), ctx _rowElems(x)); }
	let owners = ctx _blockTempArena;
	var bi = 0;
	while (bi < `cgBlockTemps length) {
		let x = `cgBlockTemps[bi];
		let o = owners[bi];
		out _addBuf("bt%^" fmt(ctx.serial[x]), cppType(ctx.typ[x]), ctx _rowElems(x), 0,
		            o == x ? "" : "bt%^" fmt(ctx.serial[o]));
		bi = bi + 1;
	}
	for (k : ctx.blockCycles) {
		let ty = ctx _blockCycleType(k);
		for (part : ["x", "a", "b"]) { out _addBuf("rc%^_%^" fmt(k, part), ty, 1); }
		out _addBuf("rc%^_w" fmt(k), ty, 1, 1);
	}
	out
}

-- The buffer arena (plan 4.2 step 8): a block temp lives from the region
-- that writes it to the last region that reads it. One whose live range
-- (closed, in region order) overlaps no other's of the same element type and
-- row width reuses that buffer's memory. Overlapping temps never share, so
-- the `__restrict` bases of a region stay distinct. Returns, parallel to
-- `cgBlockTemps, the temp whose buffer each one uses (itself for an owner).
fn _blockTempArena(ctx Ctx) [NIdx] {
	let regions = ctx rootRegions;
	let temps = `cgBlockTemps;
	var lo [Int] = [];
	var hi [Int] = [];
	for (x : temps) {
		var a = regions length;
		var b = -1;
		var members = ctx.consumers[x] copy;
		members push!(x);
		for (u : members) {
			let li = ctx _rootLoopOfNode(u);
			let r = li == NONE ? NONE : _regionOfLoop(regions, li);
			if (r == NONE) { a = 0; b = regions length; } else { a = min(a, r); b = max(b, r); }
		}
		lo push!(a);
		hi push!(b);
	}
	var owner [NIdx] = [];
	var slotEnd [Int] = [];     -- per owner index into temps: the last region of its current tenant
	var i = 0;
	while (i < temps length) {
		let x = temps[i];
		var o = x;
		var j = 0;
		while (j < i) {
			let y = temps[j];
			if (owner[j] == y && slotEnd[j] < lo[i] && ctx.typ[y] == ctx.typ[x] && ctx _rowElems(y) == ctx _rowElems(x)) {
				o = y;
				slotEnd[j] = hi[i];
				break;
			}
			j = j + 1;
		}
		owner push!(o);
		slotEnd push!(hi[i]);
		i = i + 1;
	}
	owner
}

fn genFreeFun(ctx Ctx, name String) String {
	var s = "tzpl_SErr %^_free(%^* p) {\n\ttzpl_SErr %^_uninit(%^* p);\n\t%^_uninit(p);\n"
		fmt(name, name, name, name, name);
	let bufs = ctx _blockBuffers;
	var k = 0;
	while (k < bufs.names length) {
		if (bufs.alias[k] length == 0) { s = s $ "\tfree(p->%^);\n" fmt(bufs.names[k]); }
		k = k + 1;
	}
	s $ "\tfree(p);\n\treturn tzpl_errNone;\n}\n\n"
}

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

-- The init value for one flat element `i` of a per-voice delay (i spans
-- voices x chans, voice-major). Constants inline (scalar) or index their
-- channel; shared values broadcast; voice-local materialized values index
-- their aligned flat slot ([i], or [i / chans] for a per-voice scalar
-- broadcast into a multichannel delay). Mirrors the C++
-- genVoicerDelayInitValue; sets the flat-voice dynvars so _varName yields
-- the voice_ forms.
fn _genVoicerDelayInitValue(ctx Ctx, in0 NIdx, chans Int) String {
	let isConst = match (ctx.kind[in0]) { constant(_, _): true; _: false; };
	if (isConst) {
		if (ctx.chans[in0] == 1) { return genExpr(ctx, in0, "0"); }
		return "%^[i %% %^]" fmt(_varName(ctx, in0), chans);
	}
	let bg = ctx _voicerBodyGraph(ctx _voicerNodes[0]);
	var `cgVoiceCount Int = ctx _voicerMaxForGraph(bg);
	var `cgVoiceGraph Int = bg;
	if (ctx.cut[in0] isInstVarCut || ctx.cut[in0] isTempVarCut) {
		let name = _varName(ctx, in0);
		if (ctx.graphOf[in0] != bg) {
			if (ctx.chans[in0] == 1) { return name; }
			return "%^[i %% %^]" fmt(name, chans);
		}
		if (ctx.chans[in0] == chans) { return "%^[i]" fmt(name); }
		if (ctx.chans[in0] == 1 && chans == 1) { return "%^[i]" fmt(name); }
		if (ctx.chans[in0] == 1) { return "%^[i / %^]" fmt(name, chans); }
		panic("voicer delay init: unsupported init value shape");
		return "";
	}
	-- Not materialized (the DelayInit is its only consumer): inline the
	-- expression per element, like the non-voicer path's genExpr
	-- re-inlining, under a flat-voice context so per-voice leaves (e.g. an
	-- init-rate rand64 draw) decompose `i` into voice and channel.
	var `cgFlatChanShift Int = chans > 1 ? _log2(chans) : 0;
	var `cgFlatChanMask Int = chans > 1 ? chans - 1 : 0;
	var `cgFlatTrip Int = `cgVoiceCount * chans;
	genExpr(ctx, in0, "i")
}

-- AoS (non-flat) per-voice delay init writes. Emitted INSIDE genInitFun's
-- per-voice init block (`vs` and the voicer init loops' temps in scope,
-- cgInVoiceLoop set), so genExpr resolves per-voice values to their vs./local
-- forms. Mirrors the C++ genVoicerDelayInitAoS.
-- The AoS per-voice init block: voicer-subgraph init loops + per-voice delay
-- init writes, sharing one voice iteration (cross-loop init temps are voice
-- locals). Own function so the dynvar overrides (which persist to function
-- exit) don't leak into genInitFun's later shared emissions.
fn _genAoSInitVoiceBlock(ctx Ctx) String {
	let mv = ctx _voicerMaxVoices(ctx _voicerNodes[0]);
	var s = "\tfor (int v = 0; v < %^; ++v) {\n" fmt(mv);
	var `cgIndent Int = 2;
	s = s $ _tabs(`cgIndent) $ "f32* vparams = &in%^[v * %^];\n" fmt(`cgNotesSn, `cgNotesCols);
	s = s $ _tabs(`cgIndent) $ "auto& vs = p->voice_state[v];\n";
	var `cgInVoiceLoop Bool = true;
	var `cgVoiceIdx String = "v";
	for (li : ctx.initLoops) {
		if (ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li))) { s = s $ genLoop(ctx, li); }
	}
	s = s $ ctx _genVoicerDelayInitAoS;
	s = s $ _tabs(`cgIndent - 1) $ "}\n";
	s
}

-- The AoS per-voice reset block (all voicer reset loops in one voice
-- iteration, mirroring the C++ AoS reset wrapper). Own function for the same
-- dynvar-scoping reason as _genAoSInitVoiceBlock.
fn _genAoSResetVoiceBlock(ctx Ctx) String {
	let mv = ctx _voicerMaxVoices(ctx _voicerNodes[0]);
	var s = "\tfor (int v = 0; v < %^; ++v) {\n" fmt(mv);
	var `cgIndent Int = 2;
	s = s $ _tabs(`cgIndent) $ "f32* vparams = &in%^[v * %^];\n" fmt(`cgNotesSn, `cgNotesCols);
	s = s $ _tabs(`cgIndent) $ "auto& vs = p->voice_state[v];\n";
	var `cgInVoiceLoop Bool = true;
	var `cgVoiceIdx String = "v";
	var `cgInResetMode Bool = true;
	for (li : ctx.resetLoops) {
		if (ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li))) { s = s $ genLoop(ctx, li); }
	}
	s = s $ _tabs(`cgIndent - 1) $ "}\n";
	s
}

fn _genVoicerDelayInitAoS(ctx Ctx) String {
	var s = "";
	for (d : ctx.delays) {
		if (!(ctx _isVoiceGraphDeep(ctx _delayGraph(d)))) { continue; }
		for (n : d.initters) {
			match (ctx.kind[n]) {
				delayInitK(_, offset): {
					let value = genExpr(ctx, ctx.ins[n][0], "0");
					if (d.chans == 1) {
						if (d.allocSize == 1) {
							s = s $ "\t\tvs.d%^ = %^;\n" fmt(d.serial, value);
						} else if (d.allocSize > 1) {
							s = s $ "\t\tvs.d%^[(0ull - %^ull) & %^] = %^;\n" fmt(d.serial, offset, d.allocSize - 1, value);
						} else {
							s = s $ "\t\tvs.d%^[(0ull - %^ull) & vs.d%^_mask] = %^;\n" fmt(d.serial, offset, d.serial, value);
						}
					} else {
						var j = 0;
						while (j < d.chans) {
							-- Re-generate per channel: a multichannel init
							-- value must index its own channel, not channel 0.
							let valueJ = genExpr(ctx, ctx.ins[n][0], "%^" fmt(j));
							if (d.allocSize == 1) {
								s = s $ "\t\tvs.d%^[%^] = %^;\n" fmt(d.serial, j, valueJ);
							} else if (d.allocSize > 1) {
								s = s $ "\t\tvs.d%^[%^][(0ull - %^ull) & %^] = %^;\n" fmt(d.serial, j, offset, d.allocSize - 1, valueJ);
							} else {
								s = s $ "\t\tvs.d%^[%^][(0ull - %^ull) & vs.d%^_mask] = %^;\n" fmt(d.serial, j, offset, d.serial, valueJ);
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

fn genDelayInit(ctx Ctx) String {
	var s = "";
	for (d : ctx.delays) {
		-- Per-voice delay state: write every voice's initial value (the old
		-- code skipped these entirely, so e.g. a seq index declared
		-- init(1, -1) started at 0 in every voice). AoS mode emits these
		-- inside genInitFun's per-voice block (_genVoicerDelayInitAoS).
		if (!`cgFlatVoice && ctx _isVoiceGraphDeep(ctx _delayGraph(d))) { continue; }
		if (ctx _isVoiceGraph(ctx _delayGraph(d))) {
			if (d.initters length == 0) { continue; }
			let total = ctx _voicerMaxForGraph(ctx _delayGraph(d)) * d.chans;
			for (n : d.initters) {
				match (ctx.kind[n]) {
					delayInitK(_, offset): {
						let value = ctx _genVoicerDelayInitValue(ctx.ins[n][0], d.chans);
						if (d.allocSize == 1) {
							s = s $ "\tfor (int i = 0; i < %^; ++i) p->voice_d%^[i] = %^;\n" fmt(total, d.serial, value);
						} else if (d.allocSize > 1) {
							s = s $ "\tfor (int i = 0; i < %^; ++i) p->voice_d%^[i][(0ull - %^ull) & %^] = %^;\n" fmt(total, d.serial, offset, d.allocSize - 1, value);
						} else {
							s = s $ "\tfor (int i = 0; i < %^; ++i) p->voice_d%^[i][(0ull - %^ull) & p->voice_d%^_mask[i / %^]] = %^;\n" fmt(total, d.serial, offset, d.serial, d.chans, value);
						}
					}
					_: {}
				}
			}
			continue;
		}
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
							-- Re-generate per channel: a multichannel init
							-- value must index its own channel, not channel 0.
							let valueJ = genExpr(ctx, ctx.ins[n][0], "%^" fmt(j));
							if (d.allocSize == 1) {
								s = s $ "\tp->d%^[%^] = %^;\n" fmt(d.serial, j, valueJ);
							} else if (d.allocSize > 1 && ctx _tapMajor(d)) {
								s = s $ "\tp->d%^[(0ull - %^ull) & %^][%^] = %^;\n" fmt(d.serial, offset, d.allocSize - 1, j, valueJ);
							} else if (d.allocSize > 1) {
								s = s $ "\tp->d%^[%^][(0ull - %^ull) & %^] = %^;\n" fmt(d.serial, j, offset, d.allocSize - 1, valueJ);
							} else {
								s = s $ "\tp->d%^[%^][(0ull - %^ull) & p->d%^_mask] = %^;\n" fmt(d.serial, j, offset, d.serial, valueJ);
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
		-- In AoS mode that includes delays nested in a control-flow subgraph of
		-- the voice body (e.g. a delayVar written inside an if_ branch), which
		-- are declared as vs. members: the deep check mirrors genDelayInit and
		-- _genVoicerDelayInitAoS. Without it such a delay was also emitted here
		-- as p->dN, which the struct never declares.
		if (!`cgFlatVoice && ctx _isVoiceGraphDeep(ctx _delayGraph(d))) { continue; }
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
	if (!`cgFlatVoice) {
		-- AoS: one per-voice block sets up every voice ring's head/mask and
		-- callocs runtime rings as vs. members. Mirrors the C++
		-- genDelayAlloc AoS branch (single-slot delays need no setup).
		var has = false;
		for (d : ctx.delays) {
			let g = ctx _delayGraph(d);
			if (!(ctx _isVoiceGraphDeep(g)) || d.allocSize == 1) { continue; }
			if (!has) {
				has = true;
				let mv = ctx _voicerMaxVoices(ctx _voicerNodes[0]);
				s = s $ "\tfor (int v = 0; v < %^; ++v) {\n" fmt(mv);
				s = s $ "\t\tauto& vs = p->voice_state[v];\n";
			}
			s = s $ "\t\tvs.d%^_wrpos = 0;\n" fmt(d.serial);
			if (d.allocSize > 1) {
				s = s $ "\t\tvs.d%^_mask = %^;\n" fmt(d.serial, d.allocSize - 1);
			} else if (d.maxDelay != NONE) {
				let headroom = max(4, d.maxOverread);
				let maxExpr = genExpr(ctx, ctx.ins[d.maxDelay][0], "0");
				let ty = cppType(d.typ);
				s = s $ "\t\tu64 d%^_size = nextPowerOfTwo(%^+u64(ceil(%^)));\n" fmt(d.serial, headroom, maxExpr);
				s = s $ "\t\tvs.d%^_mask = d%^_size - 1;\n" fmt(d.serial, d.serial);
				if (d.chans == 1) {
					s = s $ "\t\tvs.d%^ = (%^*)calloc(d%^_size, sizeof(%^));\n" fmt(d.serial, ty, d.serial, ty);
				} else {
					var j = 0;
					while (j < d.chans) {
						s = s $ "\t\tvs.d%^[%^] = (%^*)calloc(d%^_size, sizeof(%^));\n" fmt(d.serial, j, ty, d.serial, ty);
						j = j + 1;
					}
				}
			}
		}
		if (has) { s = s $ "\t}\n"; }
		return s;
	}
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
	-- Init code that names an input port (an AoS voice row) sees row 0.
	for (x : ctx.inlets) {
		match (ctx.kind[x]) {
			inletK(_, sn, _): {
				let ty = cppType(ctx.typ[x]);
				if (ctx _directPort(x)) {}
				else if (ctx.chans[x] > 1) { s = s $ "\t%^* in%^ = p->bin%^; (void)in%^;\n" fmt(ty, sn, sn, sn); }
				else { s = s $ "\tstatic %^ z%^[1] = {}; %^* in%^ = z%^; (void)in%^;\n" fmt(ty, sn, ty, sn, sn, sn); }
			}
			_: {}
		}
	}
	s = s $ genInitConstants(ctx);
	-- Synth-wide RNG seeds up front; per-voice RNGs are seeded next, BEFORE
	-- the init loops, so per-voice init-rate draws (e.g. a voicer pink's
	-- dice) come from seeded, per-voice distinct streams instead of the
	-- calloc'd zero state (which also ignored TZPL_RNG_SEED and gave every
	-- voice identical draws). Mirrors the C++ genInitFun.
	for (g : ctx _rngGraphs) { if (!(ctx _isVoiceGraphDeep(g))) { s = s $ "\tarc4seedrand(p->rgen%^);\n" fmt(g); } }
	if (`cgFlatVoice) {
		for (v : ctx _voicerNodes) {
			let mv = ctx _voicerMaxVoices(v);
			let bg = ctx _voicerBodyGraph(v);
			for (g : ctx _rngGraphs) {
				if (g == bg) {
					s = s $ "\tfor (int v = 0; v < %^; ++v) {\n\t\tarc4seedrand(p->voice_rgen%^[v]);\n\t}\n" fmt(mv, g);
				}
			}
		}
	} else {
		-- AoS: bind the voicer matrix, wipe all voice state, and seed the
		-- per-voice RNGs before the init loops draw from them (the rgen
		-- lives inside voice_state, so the wipe must come first).
		for (v : ctx _voicerNodes) {
			let mv = ctx _voicerMaxVoices(v);
			s = s $ "\tmemset(p->voice_state, 0, sizeof(p->voice_state));\n";
			for (g : ctx _rngGraphs) {
				if (ctx _isVoiceGraphDeep(g)) {
					s = s $ "\tfor (int v = 0; v < %^; ++v) {\n\t\tarc4seedrand(p->voice_state[v].rgen%^);\n\t}\n" fmt(mv, g);
				}
			}
		}
	}
	-- inInitMode is only for genDelayAlloc (runtime delay sizing); init LOOPS
	-- reference cross-tree roots as variables just like audio loops do.
	if (!`cgFlatVoice && ctx _voicerNodes length > 0) {
		-- AoS: shared init loops first, then ONE per-voice block running the
		-- voicer-subgraph init loops and the per-voice delay init writes
		-- (cross-loop init temps are per-voice locals, so the loops and the
		-- writes that read them must share the voice iteration). Delay
		-- allocation comes before the block so a runtime ring's calloc
		-- precedes its init write. Mirrors the C++ genInitFun AoS path.
		for (li : ctx.initLoops) {
			if (!(ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li)))) { s = s $ genLoop(ctx, li); }
		}
		s = s $ genDelayAlloc(ctx);
		s = s $ genVoiceDelayAlloc(ctx);
		s = s $ genSpectralAlloc(ctx);
		var anyVoicerInit = false;
		for (li : ctx.initLoops) {
			if (ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li))) { anyVoicerInit = true; }
		}
		for (d : ctx.delays) {
			if (ctx _isVoiceGraphDeep(ctx _delayGraph(d)) && d.initters length > 0) { anyVoicerInit = true; }
		}
		if (anyVoicerInit) { s = s $ ctx _genAoSInitVoiceBlock; }
		s = s $ genDelayInit(ctx);
		-- Buffer pointers start null; the host swaps real buffers in at runtime.
		for (ser : ctx.bufSerials) { s = s $ "\tp->buf%^ = nullptr;\n" fmt(ser); }
		for (ser : ctx.bankSerials) { s = s $ "\tp->bank%^ = nullptr;\n" fmt(ser); }
		-- Reset-rate loops: shared first, then voicer loops in one per-voice
		-- block (mirrors the C++ AoS reset wrapper).
		var `cgInResetMode Bool = true;
		for (li : ctx.resetLoops) {
			if (!(ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li)))) { s = s $ genLoop(ctx, li); }
		}
		var anyVoicerReset = false;
		for (li : ctx.resetLoops) {
			if (ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li))) { anyVoicerReset = true; }
		}
		if (anyVoicerReset) { s = s $ ctx _genAoSResetVoiceBlock; }
		s = s $ "\treturn tzpl_errNone;\n}\n\n";
		return s;
	}
	s = s $ genLoops(ctx, ctx.initLoops);
	-- Alloc order mirrors the C++ genDelayAlloc: shared delays, per-voice
	-- delays, spectral chains -- then the delay init writes (which for a
	-- runtime-sized per-voice ring must land AFTER its calloc).
	s = s $ genDelayAlloc(ctx);
	s = s $ genVoiceDelayAlloc(ctx);
	s = s $ genSpectralAlloc(ctx);
	s = s $ genDelayInit(ctx);
	-- Buffer pointers start null; the host swaps real buffers in at runtime.
	-- (Before the voicer block, matching the C++ genInitFun ordering.)
	for (ser : ctx.bufSerials) { s = s $ "\tp->buf%^ = nullptr;\n" fmt(ser); }
	-- Sample bank pointers start null; lookup slots fill in the reset loops.
	for (ser : ctx.bankSerials) { s = s $ "\tp->bank%^ = nullptr;\n" fmt(ser); }
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
	s = s $ ctx genParamGroupInit;
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn _loopRootGraph(ctx Ctx, li Int) Int =
	ctx.graphOf[ctx.trees[ctx.loops[li].trees[0]].root];

-- resetVoice(p, vi, off): the engine's voicer gave voice vi a new note at
-- block offset off. Gather the inputs at off (so per-note reset loops read
-- the new row of `notes`), zero the voice's audio-rate state and restore its
-- delays' init values, re-seed its RNG, then run the voice's reset-rate
-- loops (per-note setup such as sample bank lookups). The engine writes the
-- note's parameters into `notes` and splits the node at the onset.
fn genVoicerNoteFuns(ctx Ctx, name String) String {
	if (ctx _voicerNodes length == 0) { return ""; }
	let v = ctx _voicerNodes [0];
	let bg = ctx _voicerBodyGraph(v);
	var s = "";
	s = s $ "tzpl_SErr %^_resetVoice(%^* p, i32 vi, i32 off) {\n" fmt(name, name);
	s = s $ "\tif (vi < 0 || vi >= %^) return tzpl_errNoteNotFound;\n" fmt(ctx _voicerMaxVoices(v));
	s = s $ "\t%^_gather(p, off, 1);\n" fmt(name);
	for (x : ctx.inlets) {
		match (ctx.kind[x]) {
			inletK(_, sn, _): {
				let ty = cppType(ctx.typ[x]);
				if (ctx _directPort(x)) {}
				else if (ctx.chans[x] > 1) { s = s $ "\t%^* in%^ = p->bin%^ + off * %^;\n" fmt(ty, sn, sn, ctx _portElems(x)); }
				else { s = s $ "\t%^* in%^ = (%^*)p->ins[%^].chans[0] + off;\n" fmt(ty, sn, ty, sn); }
			}
			_: {}
		}
	}
	-- The voice's parameter-group values are recomputed for the new note.
	if (ctx _pgActive) { s = s $ "\tp->pg_vforce[vi] = 1;\n"; }
	if (!`cgFlatVoice) {
		-- AoS: per-field reset (p->voice_state[vi]. members), preserving
		-- init-rate state. Mirrors the C++ genNoteFuns AoS branch.
		for (lp : ctx.loops) {
			for (t : lp.trees) {
				let root = ctx.trees[t].root;
				if (ctx _isVoiceGraphDeep(ctx.graphOf[root]) && ctx _isInstVar(root) && ctx.nrate[root] != Rate.init) {
					if (ctx.chans[root] == 1) {
						s = s $ "\tp->voice_state[vi].v%^ = 0;\n" fmt(ctx.serial[root]);
					} else {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].v%^[c] = 0;\n"
							fmt(ctx.chans[root], ctx.serial[root]);
					}
				}
			}
		}
		for (d : ctx.delays) {
			if (ctx _isVoiceGraphDeep(ctx _delayGraph(d))) {
				var hasNonConstInit = false;
				var constInits [(Int, Int)] = [];   -- (in0, offset)
				for (n : d.initters) {
					match (ctx.kind[n]) {
						delayInitK(_, offset): {
							let in0 = ctx.ins[n][0];
							match (ctx.kind[in0]) {
								constant(_, _): { constInits push!((in0, offset)); }
								_: { hasNonConstInit = true; }
							}
						}
						_: {}
					}
				}
				if (hasNonConstInit) { continue; }
				if (d.allocSize != 1) {
					s = s $ "\tp->voice_state[vi].d%^_wrpos = 0;\n" fmt(d.serial);
				}
				if (d.allocSize == 1 && constInits length > 0) {
					for (ci : constInits) {
						let in0 = ci.0;
						if (d.chans == 1) {
							s = s $ "\tp->voice_state[vi].d%^ = %^;\n" fmt(d.serial, genExpr(ctx, in0, "0"));
						} else if (ctx.chans[in0] == 1) {
							s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].d%^[c] = %^;\n" fmt(d.chans, d.serial, genExpr(ctx, in0, "0"));
						} else {
							s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].d%^[c] = %^[c];\n" fmt(d.chans, d.serial, _varName(ctx, in0));
						}
					}
				} else if (d.allocSize == 1) {
					if (d.chans == 1) {
						s = s $ "\tp->voice_state[vi].d%^ = 0;\n" fmt(d.serial);
					} else {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].d%^[c] = 0;\n" fmt(d.chans, d.serial);
					}
				} else if (d.allocSize > 1) {
					-- Zero-guarded rings (no init values) need no clearing
					-- (see the flat-mode branch).
					let ty = cppType(d.typ);
					if (d.initters length > 0) {
						if (d.chans == 1) {
							s = s $ "\tmemset(p->voice_state[vi].d%^, 0, %^ * sizeof(%^));\n" fmt(d.serial, d.allocSize, ty);
						} else {
							s = s $ "\tfor (int c = 0; c < %^; ++c) memset(p->voice_state[vi].d%^[c], 0, %^ * sizeof(%^));\n" fmt(d.chans, d.serial, d.allocSize, ty);
						}
					}
					for (ci : constInits) {
						let in0 = ci.0;
						let slot = "(0ull - %^ull) & %^" fmt(ci.1, d.allocSize - 1);
						if (d.chans == 1) {
							s = s $ "\tp->voice_state[vi].d%^[%^] = %^;\n" fmt(d.serial, slot, genExpr(ctx, in0, "0"));
						} else if (ctx.chans[in0] == 1) {
							s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].d%^[c][%^] = %^;\n" fmt(d.chans, d.serial, slot, genExpr(ctx, in0, "0"));
						} else {
							s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].d%^[c][%^] = %^[c];\n" fmt(d.chans, d.serial, slot, _varName(ctx, in0));
						}
					}
				} else {
					-- dynamic delays: the ring keeps the reused voice's tail
					-- (matching flat mode); restore constant init slots only.
					for (ci : constInits) {
						let in0 = ci.0;
						let slot = "(0ull - %^ull) & p->voice_state[vi].d%^_mask" fmt(ci.1, d.serial);
						if (d.chans == 1) {
							s = s $ "\tp->voice_state[vi].d%^[%^] = %^;\n" fmt(d.serial, slot, genExpr(ctx, in0, "0"));
						} else if (ctx.chans[in0] == 1) {
							s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].d%^[c][%^] = %^;\n" fmt(d.chans, d.serial, slot, genExpr(ctx, in0, "0"));
						} else {
							s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_state[vi].d%^[c][%^] = %^[c];\n" fmt(d.chans, d.serial, slot, _varName(ctx, in0));
						}
					}
				}
			}
		}
		for (g : ctx _rngGraphs) {
			if (ctx _isVoiceGraphDeep(g)) { s = s $ "\tarc4seedrand(p->voice_state[vi].rgen%^);\n" fmt(g); }
		}
	} else {
	-- zero per-voice audio-rate inst vars (in body loop/tree order); a
	-- multichannel var (e.g. a ringing-filter bank's per-mode state) clears
	-- all of its channels, or a reused voice starts with the previous
	-- note's state in channels 1..N-1
	for (lp : ctx.loops) {
		for (t : lp.trees) {
			let root = ctx.trees[t].root;
			if (ctx.graphOf[root] == bg && ctx _isInstVar(root) && ctx.nrate[root] != Rate.init) {
				if (ctx.chans[root] == 1) {
					s = s $ "\tp->voice_v%^[vi] = 0;\n" fmt(ctx.serial[root]);
				} else {
					s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_v%^[vi * %^ + c] = 0;\n"
						fmt(ctx.chans[root], ctx.serial[root], ctx.chans[root]);
				}
			}
		}
	}
	-- Reset per-voice delay state: 1-sample delays clear their single slot;
	-- ring buffers reset their write head, and a FIXED ring also clears its
	-- inline buffer (a runtime ring's calloc'd buffer is already clear from
	-- init). A delay declared with init values is restored to them, not
	-- zeroed: constant inits are re-emitted here (e.g. a seq index's
	-- init(1, -1)); a delay with any NON-constant init (e.g. pink's
	-- per-voice dice) keeps the reused voice's state. Mirrors the C++
	-- genNoteFuns delay reset.
	for (d : ctx.delays) {
		if (ctx _delayGraph(d) == bg) {
			var hasNonConstInit = false;
			var constInits [(Int, Int)] = [];   -- (in0, offset)
			for (n : d.initters) {
				match (ctx.kind[n]) {
					delayInitK(_, offset): {
						let in0 = ctx.ins[n][0];
						match (ctx.kind[in0]) {
							constant(_, _): { constInits push!((in0, offset)); }
							_: { hasNonConstInit = true; }
						}
					}
					_: {}
				}
			}
			if (hasNonConstInit) { continue; }
			if (d.allocSize != 1) {
				s = s $ "\tp->voice_d%^_wrpos[vi] = 0;\n" fmt(d.serial);
			}
			if (d.allocSize == 1 && constInits length > 0) {
				for (ci : constInits) {
					let in0 = ci.0;
					if (d.chans == 1) {
						s = s $ "\tp->voice_d%^[vi] = %^;\n" fmt(d.serial, genExpr(ctx, in0, "0"));
					} else if (ctx.chans[in0] == 1) {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c] = %^;\n" fmt(d.chans, d.serial, d.chans, genExpr(ctx, in0, "0"));
					} else {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c] = %^[c];\n" fmt(d.chans, d.serial, d.chans, _varName(ctx, in0));
					}
				}
			} else if (d.allocSize == 1) {
				if (d.chans > 1) {
					s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c] = 0;\n" fmt(d.chans, d.serial, d.chans);
				} else {
					s = s $ "\tp->voice_d%^[vi] = 0;\n" fmt(d.serial);
				}
			} else if (d.allocSize > 1) {
				-- Zero-guarded rings (no init values) need no clearing: the
				-- wrpos reset above plus the read guard silences the old
				-- contents. Init-carrying rings keep the clear + restore.
				let ty = cppType(d.typ);
				if (d.initters length > 0) {
					if (d.chans > 1) {
						s = s $ "\tfor (int c = 0; c < %^; ++c) memset(p->voice_d%^[vi * %^ + c], 0, %^ * sizeof(%^));\n" fmt(d.chans, d.serial, d.chans, d.allocSize, ty);
					} else {
						s = s $ "\tmemset(p->voice_d%^[vi], 0, %^ * sizeof(%^));\n" fmt(d.serial, d.allocSize, ty);
					}
				}
				for (ci : constInits) {
					let in0 = ci.0;
					let slot = "(0ull - %^ull) & %^" fmt(ci.1, d.allocSize - 1);
					if (d.chans == 1) {
						s = s $ "\tp->voice_d%^[vi][%^] = %^;\n" fmt(d.serial, slot, genExpr(ctx, in0, "0"));
					} else if (ctx.chans[in0] == 1) {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c][%^] = %^;\n" fmt(d.chans, d.serial, d.chans, slot, genExpr(ctx, in0, "0"));
					} else {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c][%^] = %^[c];\n" fmt(d.chans, d.serial, d.chans, slot, _varName(ctx, in0));
					}
				}
			} else {
				-- dynamic delays: already zeroed by calloc; restore constant
				-- init slots (the ring itself keeps the reused voice's tail,
				-- matching the pre-init-fix behavior).
				for (ci : constInits) {
					let in0 = ci.0;
					let slot = "(0ull - %^ull) & p->voice_d%^_mask[vi]" fmt(ci.1, d.serial);
					if (d.chans == 1) {
						s = s $ "\tp->voice_d%^[vi][%^] = %^;\n" fmt(d.serial, slot, genExpr(ctx, in0, "0"));
					} else if (ctx.chans[in0] == 1) {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c][%^] = %^;\n" fmt(d.chans, d.serial, d.chans, slot, genExpr(ctx, in0, "0"));
					} else {
						s = s $ "\tfor (int c = 0; c < %^; ++c) p->voice_d%^[vi * %^ + c][%^] = %^[c];\n" fmt(d.chans, d.serial, d.chans, slot, _varName(ctx, in0));
					}
				}
			}
		}
	}
	-- re-seed this voice's RNG
	for (g : ctx _rngGraphs) {
		if (g == bg) { s = s $ "\tarc4seedrand(p->voice_rgen%^[vi]);\n" fmt(g); }
	}
	}
	-- Reset-rate loops: per-note setup for the newly allocated voice, run
	-- after the zeroing above so reset-rate inst vars are recomputed for
	-- this note (mirrors the C++ genNoteFuns emission).
	var hasReset = false;
	for (li : ctx.resetLoops) {
		if (ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li))) { hasReset = true; }
	}
	if (hasReset && !`cgFlatVoice) {
		-- AoS: bind the new voice's row and state, then run the loops as
		-- plain per-voice code (mirrors the C++ AoS reset-loop emission).
		s = s $ "\tf32* vparams = &in%^[vi * %^];\n" fmt(`cgNotesSn, `cgNotesCols);
		s = s $ "\tauto& vs = p->voice_state[vi];\n";
		var `cgInResetMode Bool = true;
		var `cgInVoiceLoop Bool = true;
		var `cgVoiceIdx String = "vi";
		for (li : ctx.resetLoops) {
			if (ctx _isVoiceGraphDeep(_loopRootGraph(ctx, li))) { s = s $ genLoop(ctx, li); }
		}
	} else if (hasReset) {
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
			-- AoS voice rings are freed in one per-voice block below.
			if (!`cgFlatVoice && ctx _isVoiceGraphDeep(ctx _delayGraph(d))) { continue; }
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
	if (!`cgFlatVoice) {
		-- AoS: free every voice's runtime rings as vs. members in one
		-- per-voice block. Mirrors the C++ genDelayDealloc AoS branch.
		var has = false;
		for (d : ctx.delays) {
			if (!(d.allocSize < 1 && d.hasMaxBound)) { continue; }
			if (!(ctx _isVoiceGraphDeep(ctx _delayGraph(d)))) { continue; }
			if (!has) {
				has = true;
				let mv = ctx _voicerMaxVoices(ctx _voicerNodes[0]);
				s = s $ "\tfor (int v = 0; v < %^; ++v) {\n" fmt(mv);
				s = s $ "\t\tauto& vs = p->voice_state[v];\n";
			}
			if (d.chans == 1) {
				s = s $ "\t\tfree(vs.d%^); vs.d%^ = nullptr;\n" fmt(d.serial, d.serial);
			} else {
				var j = 0;
				while (j < d.chans) { s = s $ "\t\tfree(vs.d%^[%^]); vs.d%^[%^] = nullptr;\n" fmt(d.serial, j, d.serial, j); j = j + 1; }
			}
		}
		if (has) { s = s $ "\t}\n"; }
	}
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
	s
}

fn genResetFun(ctx Ctx, name String) String {
	-- Whole-synth reset: the engine calls this when audio stops so delay
	-- lines, filter memories, and voice state don't replay their tail on the
	-- next start. Tear down and re-init, preserving the engine-installed
	-- buffer and sample-bank pointers (init nulls them; the engine only
	-- installs them once via swapBuffer/swapSampleBank).
	var s = "tzpl_SErr %^_reset(%^* p) {\n" fmt(name, name);
	for (ser : ctx.bufSerials) { s = s $ "\ttzpl_Buffer* save_buf%^ = p->buf%^;\n" fmt(ser, ser); }
	for (ser : ctx.bankSerials) { s = s $ "\ttzpl_SampleBank* save_bank%^ = p->bank%^;\n" fmt(ser, ser); }
	s = s $ "\t%^_uninit(p);\n" fmt(name);
	s = s $ "\t%^_init(p);\n" fmt(name);
	for (ser : ctx.bufSerials) { s = s $ "\tp->buf%^ = save_buf%^;\n" fmt(ser, ser); }
	for (ser : ctx.bankSerials) { s = s $ "\tp->bank%^ = save_bank%^;\n" fmt(ser, ser); }
	s = s $ "\treturn tzpl_errNone;\n}\n\n";
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
			if (voiceG && `cgFlatVoice) {
				s = s $ _tabs(`cgIndent) $ "for (int v = 0; v < %^; ++v) ++p->voice_d%^_wrpos[v];\n" fmt(mv, d.serial);
			} else {
				-- AoS voice delays advance inside the per-voice loop as
				-- vs. members.
				s = s $ _tabs(`cgIndent) $ "++%^d%^_wrpos;\n" fmt(_dpre(), d.serial);
			}
		}
	}
	if (s length > 0) { s = s $ "\n"; }
	s
}

-- Struct arrays the per-sample body reads and writes: one per input port
-- (gathered each sample) and per output port (scattered each sample),
-- padded to whole SIMD vectors.
fn _portElems(ctx Ctx, n NIdx) Int {
	let c = ctx.chans[n] max(1);
	c == 1 ? 1 : (c + 3) // 4 * 4
}

fn genPortDecls(ctx Ctx) String {
	var s = "";
	for (n : ctx.inlets) {
		match (ctx.kind[n]) {
			inletK(_, sn, _): { if (ctx.chans[n] > 1 && !(ctx _directPort(n))) { s = s $ "\t%^* bin%^;\n" fmt(cppType(ctx.typ[n]), sn); } }
			_: {}
		}
	}
	for (n : ctx.outlets) {
		match (ctx.kind[n]) {
			outletK(_, sn): { if (ctx.chans[n] > 1 && !(ctx _directPort(n))) { s = s $ "\t%^* bout%^;\n" fmt(cppType(ctx.typ[n]), sn); } }
			_: {}
		}
	}
	if (s length > 0) { s = "\t// ports: block buffers (maxBlock rows, interleaved)\n" $ s $ "\n"; }
	s
}

-- gather(p, from, count): input ports' planar channels into the interleaved
-- block buffers, rows [from, from + count); scatter: the reverse for outputs.
fn genGatherFun(ctx Ctx, name String) String {
	var s = "static inline void %^_gather(%^* p, i32 from, i32 count) {\n" fmt(name, name);
	for (n : ctx.inlets) {
		match (ctx.kind[n]) {
			inletK(_, sn, _): {
				if (ctx.chans[n] > 1 && !(ctx _directPort(n))) {
					let t = cppType(ctx.typ[n]);
					let e = ctx _portElems(n);
					s = s $ "\tfor (int c = 0; c < %^; ++c) {\n" fmt(ctx.chans[n]);
					s = s $ "\t\tconst %^* src = (const %^*)p->ins[%^].chans[c];\n" fmt(t, t, sn);
					s = s $ "\t\tfor (i32 i = from; i < from + count; ++i) p->bin%^[i * %^ + c] = src[i];\n\t}\n" fmt(sn, e);
				}
			}
			_: {}
		}
	}
	s = s $ "}\n\n";
	s = s $ "static inline void %^_scatter(%^* p, i32 from, i32 count) {\n" fmt(name, name);
	for (n : ctx.outlets) {
		match (ctx.kind[n]) {
			outletK(_, sn): {
				if (ctx.chans[n] > 1 && !(ctx _directPort(n))) {
					let t = cppType(ctx.typ[n]);
					let e = ctx _portElems(n);
					s = s $ "\tfor (int c = 0; c < %^; ++c) {\n" fmt(ctx.chans[n]);
					s = s $ "\t\t%^* dst = (%^*)p->outs[%^].chans[c];\n" fmt(t, t, sn);
					s = s $ "\t\tfor (i32 i = from; i < from + count; ++i) dst[i] = p->bout%^[i * %^ + c];\n\t}\n" fmt(sn, e);
				}
			}
			_: {}
		}
	}
	s $ "}\n\n"
}

---------------------------------------------------------------------------
-- Parameter groups (block-engine M6, plan 4.5). Block-placed loops hold one
-- group each and run only when the group's sources change. At the top of
-- process, each source input channel is classified against the version
-- cached from the last call: 0 = constant and unchanged, 1 = constant but a
-- new value (a step), 2 = not constant this block. A group's mode is the
-- largest over its sources (per voice for groups fed by `notes` columns):
-- 0 keeps the cached values, 1 computes once at the first sample, 2 computes
-- every sample. A non-constant channel caches an invalid version, so the
-- group recomputes once when it settles.

fn _pgLoops(ctx Ctx, g Int) [Int] {
	var out [Int] = [];
	for (li : ctx.blockLoops) { if (_loopRootGraph(ctx, li) == g) { out push!(li); } }
	out
}

fn _pgInlet(ctx Ctx, sn Int) NIdx {
	for (x : ctx.inlets) { match (ctx.kind[x]) { inletK(_, s2, _): { if (s2 == sn) { return x; } } _: {} } }
	NONE
}

-- The `notes` input's port serial, or -1 without a voicer (cgNotesSn
-- defaults to 0, a real input's serial).
fn _pgNotesSn(ctx Ctx) Int = ctx.notesInlets length > 0 ? `cgNotesSn : -1;

-- The input port serials whose versions the groups watch (not `notes`).
fn _pgInputs(ctx Ctx) [Int] {
	let ns = ctx _pgNotesSn;
	var out [Int] = [];
	for (pg : ctx.paramGroups) {
		for (sn : pg.inputs) { if (sn != ns && !(out contains(sn))) { out push!(sn); } }
	}
	out
}

fn _pgUsesNotes(ctx Ctx) Bool {
	for (pg : ctx.paramGroups) { if (pg.noteCols length > 0) { return true; } }
	false
}

fn _pgVoices(ctx Ctx) Int {
	let vs = ctx _voicerNodes;
	vs length > 0 ? ctx _voicerMaxVoices(vs[0]) : 0
}

fn _pgActive(ctx Ctx) Bool = ctx.blockLoops length > 0;

fn genParamGroupDecls(ctx Ctx) String {
	if (!(ctx _pgActive)) { return ""; }
	var s = "\t// parameter groups: cached source versions\n";
	for (sn : ctx _pgInputs) {
		s = s $ "\tu32 pgv%^[%^];\n" fmt(sn, ctx.chans[ctx _pgInlet(sn)]);
	}
	if (ctx _pgUsesNotes) {
		s = s $ "\tu32 pgv%^[%^];\n" fmt(`cgNotesSn, ctx.chans[ctx _pgInlet(`cgNotesSn)]);
	}
	s = s $ "\tu8 pg_first;\n";
	let mv = ctx _pgVoices;
	if (mv > 0) { s = s $ "\tu8 pg_vforce[%^];\n" fmt(mv); }
	s $ "\n"
}

-- In init: every cached version invalid and every group due once.
fn genParamGroupInit(ctx Ctx) String {
	if (!(ctx _pgActive)) { return ""; }
	var s = "";
	for (sn : ctx _pgInputs) { s = s $ "\tmemset(p->pgv%^, 0xff, sizeof(p->pgv%^));\n" fmt(sn, sn); }
	if (ctx _pgUsesNotes) {
		s = s $ "\tmemset(p->pgv%^, 0xff, sizeof(p->pgv%^));\n" fmt(`cgNotesSn, `cgNotesSn);
	}
	s = s $ "\tp->pg_first = 1;\n";
	if (ctx _pgVoices > 0) { s = s $ "\tmemset(p->pg_vforce, 1, sizeof(p->pg_vforce));\n"; }
	s
}

-- Classify one input's channels (into a per-input u8, or with `perChan` a
-- per-channel array) and refresh the cache.
fn _pgClassify(ctx Ctx, sn Int, chans Int, perChan Bool) String {
	var s = "";
	if (perChan) { s = s $ "\tu8 pgd%^[%^];\n\tu8 pgd%^_any = 0;\n" fmt(sn, chans, sn); } else { s = s $ "\tu8 pgd%^ = 0;\n" fmt(sn); }
	s = s $ "\tfor (int c = 0; c < %^; ++c) {\n" fmt(chans);
	s = s $ "\t\tbool k = p->ins[%^].isConst[c];\n" fmt(sn);
	s = s $ "\t\tu32 v = p->ins[%^].version[c];\n" fmt(sn);
	s = s $ "\t\tu8 d = !k ? 2 : (v != p->pgv%^[c] ? 1 : 0);\n" fmt(sn);
	if (perChan) { s = s $ "\t\tpgd%^[c] = d;\n\t\tif (d > pgd%^_any) pgd%^_any = d;\n" fmt(sn, sn, sn); } else { s = s $ "\t\tif (d > pgd%^) pgd%^ = d;\n" fmt(sn, sn); }
	s = s $ "\t\tp->pgv%^[c] = k ? v : 0xffffffffu;\n" fmt(sn);
	s $ "\t}\n"
}

-- Input transposition at the top of process. A multichannel input read only
-- by parameter groups (the `notes` matrix, a control tensor) is gathered
-- only when some channel changed.
fn _genProcessGather(ctx Ctx) String {
	var s = "";
	for (x : ctx.inlets) {
		match (ctx.kind[x]) {
			inletK(_, sn, _): {
				if (ctx.chans[x] > 1 && !(ctx _directPort(x))) {
					var blockOnly = ctx _pgActive;
					for (c : ctx.consumers[x]) { if (ctx.nrate[c] != Rate.block) { blockOnly = false; } }
					let watched = (ctx _pgInputs) contains(sn) || (sn == ctx _pgNotesSn && ctx _pgUsesNotes);
					let t = cppType(ctx.typ[x]);
					let e = ctx _portElems(x);
					var cond = "";
					if (blockOnly && watched) {
						cond = sn == ctx _pgNotesSn ? "if (p->pg_first || pgd%^_any) " fmt(sn) : "if (p->pg_first || pgd%^) " fmt(sn);
					}
					s = s $ "\t%^for (int c = 0; c < %^; ++c) {\n" fmt(cond, ctx.chans[x]);
					s = s $ "\t\tconst %^* src = (const %^*)p->ins[%^].chans[c];\n" fmt(t, t, sn);
					s = s $ "\t\tfor (i32 i = off; i < off + n; ++i) p->bin%^[i * %^ + c] = src[i];\n\t}\n" fmt(sn, e);
				}
			}
			_: {}
		}
	}
	s
}

-- The top of process: classify the sources, then each group's mode.
fn genParamGroupModes(ctx Ctx) String {
	if (!(ctx _pgActive)) { return ""; }
	var s = "\t// parameter groups: 0 cached, 1 compute once, 2 every sample\n";
	for (sn : ctx _pgInputs) { s = s $ ctx _pgClassify(sn, ctx.chans[ctx _pgInlet(sn)], false); }
	if (ctx _pgUsesNotes) {
		s = s $ ctx _pgClassify(`cgNotesSn, ctx.chans[ctx _pgInlet(`cgNotesSn)], true);
	}
	let mv = ctx _pgVoices;
	var g = 0;
	while (g < ctx.paramGroups length) {
		let pg = ctx.paramGroups[g];
		var terms = ["p->pg_first"];
		for (sn : pg.inputs) { if (sn != ctx _pgNotesSn) { terms push!("pgd%^" fmt(sn)); } }
		if (_pgGroupIsVoiced(ctx, g) && mv > 0) {
			terms push!("p->pg_vforce[v]");
			s = s $ "\tu8 pg%^[%^];\n\tu8 pg%^_any = 0;\n" fmt(g, mv, g);
			s = s $ "\tfor (int v = 0; v < %^; ++v) {\n" fmt(mv);
			s = s $ "\t\tu8 m = std::max<u8>({%^});\n" fmt(terms join(", "));
			for (c : pg.noteCols) {
				s = s $ "\t\tm = std::max(m, pgd%^[v * %^ + %^]);\n" fmt(`cgNotesSn, `cgNotesCols, c);
			}
			s = s $ "\t\tpg%^[v] = m;\n" fmt(g);
			s = s $ "\t\tif (m > pg%^_any) pg%^_any = m;\n\t}\n" fmt(g, g);
		} else {
			s = s $ "\tu8 pg%^ = std::max<u8>({%^});\n" fmt(g, terms join(", "));
		}
		g = g + 1;
	}
	s
}

-- A group whose nodes live in the voice body runs per voice.
fn _pgGroupIsVoiced(ctx Ctx, g Int) Bool {
	let nodes = ctx.paramGroups[g].nodes;
	nodes length > 0 && ctx _isVoiceGraph(ctx.graphOf[nodes[0]])
}

fn _pgDue(m String) String = "(%^ == 2 || (%^ == 1 && bi == off))" fmt(m, m);

-- The root graph's block loops, each guarded by its group's mode.
fn genRootBlockLoops(ctx Ctx) String {
	var s = "";
	for (li : ctx _pgLoops(0)) { s = s $ ctx _guardedRootLoop(li); }
	s
}

-- A loop of only input trees (the gathered ports themselves) emits nothing.
fn _pgLoopCode(ctx Ctx, li Int) String {
	let code = genLoop(ctx, li);
	var body = false;
	for (t : ctx.loops[li].trees) {
		match (ctx.kind[ctx.trees[t].root]) { inletK(_, _, _): {} _: { body = true; } }
	}
	body ? code : ""
}

fn _guardedRootLoop(ctx Ctx, li Int) String {
	let g = ctx.loops[li].group;
	var `cgIndent Int = 2;
	let code = ctx _pgLoopCode(li);
	if (code length == 0) { return ""; }
	let rl = ctx _rowLocalsFor("bi", code);
	"\tif (pg%^ == 2) {\n" fmt(g) $ rl.bases $ "\t\tfor (i32 bi = off; bi < off + n; ++bi) {\n"
		$ rl.rows $ code
		$ "\t\t}\n\t} else if (pg%^ == 1) {\n\t\ti32 bi = off;\n" fmt(g)
		$ rl.bases $ rl.rows $ code $ "\t}\n"
}

-- A flat voice body's block loops: per voice, for the voices whose group is
-- due (single-voice loop forms, voice `vi`).
fn genVoiceBlockLoops(ctx Ctx, bg Int) String {
	var s = "";
	for (li : ctx _pgLoops(bg)) { s = s $ ctx _guardedVoiceLoop(li); }
	s
}

fn _guardedVoiceLoop(ctx Ctx, li Int) String {
	let g = ctx.loops[li].group;
	-- Skip the whole voice scan when no voice's group is due.
	let head = _tabs(`cgIndent) $ "if %^\n" fmt(_pgDue("pg%^_any" fmt(g)))
		$ _tabs(`cgIndent) $ "for (int vi = 0; vi < %^; ++vi) {\n" fmt(`cgVoiceCount);
	var `cgIndent Int = `cgIndent + 1;
	var `cgSingleVoice Bool = true;
	let code = ctx _pgLoopCode(li);
	if (code length == 0) { return ""; }
	head $ _tabs(`cgIndent) $ "if (!%^) continue;\n" fmt(_pgDue("pg%^[vi]" fmt(g)))
		$ code $ _tabs(`cgIndent - 1) $ "}\n"
}

---------------------------------------------------------------------------
-- Block layout (block-engine M7). process() runs each root-graph loop over
-- the block's samples in turn. Loops that share a delay (its writer and
-- readers, and with them every feedback cycle) form one region that runs
-- sample by sample, so delay code is unchanged; every other loop is a region
-- of its own. A temp consumed in a later region becomes a block buffer
-- (bt<serial>, maxBlock rows of its channels, interleaved), and so does a
-- root-graph instance value at block or audio rate (bk<serial>): a
-- parameter group's value is read at row bi * stride, stride 1 while the
-- group runs every sample, 0 when it is cached or computed once. Ports are
-- interleaved block buffers too (bin/bout), gathered and scattered once per
-- call; p->in<k> / p->out<k> point at the current row.

fn _graphOwnerOf(ctx Ctx, g Int) NIdx {
	for (x : ctx.sorted) {
		for (sub : ctx.subs[x]) { if (ctx.graphOf[sub] == g) { return x; } }
	}
	NONE
}

-- The root-graph audio loop holding node x (through any enclosing
-- control-flow or voicer node), or NONE.
fn _rootLoopOfNode(ctx Ctx, x NIdx) Int {
	var cur = x;
	while (cur != NONE && ctx.graphOf[cur] != 0) { cur = ctx _graphOwnerOf(ctx.graphOf[cur]); }
	if (cur == NONE) { return NONE; }
	let t = ctx.treeOf[cur];
	t == NONE ? NONE : ctx.trees[t].loopOf
}

fn _delayMembers(ctx Ctx, d DelayInfo) [NIdx] {
	var out [NIdx] = [];
	for (r : d.fixReaders) { out push!(r); }
	for (r : d.varReaders) { out push!(r); }
	if (d.writer != NONE) { out push!(d.writer); }
	out
}

-- The root regions: runs of root audio loops, as lists of loop indices.
fn rootRegions(ctx Ctx) [[Int]] {
	let loops = ctx.graphs[0].audioLoops;
	let m = loops length;
	var hi [Int] = [];
	var i = 0;
	while (i < m) { hi push!(i); i = i + 1; }
	-- Every delay, wherever its writer lives: one written inside a branch
	-- and read in a root loop still ties those loops together.
	for (d : ctx.delays) {
		var lo = m;
		var top = -1;
		for (x : ctx _delayMembers(d)) {
			let li = ctx _rootLoopOfNode(x);
			let pos = _posOf(loops, li);
			if (pos >= 0) { lo = min(lo, pos); top = max(top, pos); }
		}
		if (top > lo) { hi[lo] = max(hi[lo], top); }
	}
	-- Other state shared between loops: a graph's random generator (its
	-- draw order is sample order), and a buffer that is written.
	var rngLoops [Int] = [];
	var bufLoops [[Int]] = [];
	var bufWritten [Bool] = [];
	for (x : ctx.sorted) {
		let li = ctx _rootLoopOfNode(x);
		let pos = li == NONE ? -1 : _posOf(loops, li);
		if (pos < 0) { continue; }
		match (ctx.kind[x]) {
			urandK(_): { rngLoops push!(pos); }
			birandK(_): { rngLoops push!(pos); }
			rand64K(_): { rngLoops push!(pos); }
			bufWriteK(b, _, _): { _noteBufLoop(bufLoops, bufWritten, b, pos, true); }
			bufFixReadK(b, _, _, _): { _noteBufLoop(bufLoops, bufWritten, b, pos, false); }
			bufVarReadK(b, _, _, _): { _noteBufLoop(bufLoops, bufWritten, b, pos, false); }
			_: {}
		}
	}
	_spanAll(hi, rngLoops);
	var bix = 0;
	while (bix < bufLoops length) {
		if (bufWritten[bix]) { _spanAll(hi, bufLoops[bix]); }
		bix = bix + 1;
	}
	var regions [[Int]] = [];
	i = 0;
	while (i < m) {
		var end = hi[i];
		var j = i;
		while (j <= end) { end = max(end, hi[j]); j = j + 1; }
		var r [Int] = [];
		j = i;
		while (j <= end) { r push!(loops[j]); j = j + 1; }
		regions push!(r);
		i = end + 1;
	}
	ctx _fuseRegions(regions)
}

-- Fusion (plan 4.2 step 7): consecutive plain regions run as one sample loop,
-- so a value one computes and the next reads stays a local instead of going
-- through a block buffer. Block-cycle regions stay apart, and so do regions
-- of different forms: one-channel elementwise regions (sample-vector) merge
-- only with each other.
fn _fuseRegions(ctx Ctx, regions [[Int]]) [[Int]] {
	if (serialForms()) { return regions; }
	-- each region's kind: 0 block cycle, 1 sample-vector, 2 other plain
	var kind [Int] = [];
	for (r : regions) {
		var block = false;
		for (li : r) { if (ctx.loops[li].group <= -10) { block = true; } }
		kind push!(block ? 0 : (ctx _sampleVecOK(r) ? 1 : 2));
	}
	var out [[Int]] = [];
	var i = 0;
	while (i < regions length) {
		var merged [Int] = [];
		merged append!(regions[i]);
		var j = i + 1;
		while (kind[i] != 0 && j < regions length && kind[j] == kind[i]) {
			merged append!(regions[j]);
			j = j + 1;
		}
		out push!(merged);
		i = j;
	}
	out
}

-- Join the loops at positions `ps` into one span.
fn _spanAll(hi [Int], ps [Int]) Void {
	if (ps length < 2) { return; }
	var lo = ps[0];
	var top = ps[0];
	for (q : ps) { lo = min(lo, q); top = max(top, q); }
	hi[lo] = max(hi[lo], top);
}

fn _noteBufLoop(bufLoops [[Int]], written [Bool], b Int, pos Int, isWrite Bool) Void {
	while (bufLoops length <= b) { bufLoops push!([Int]()); written push!(false); }
	bufLoops[b] push!(pos);
	if (isWrite) { written[b] = true; }
}

fn _posOf(xs [Int], x Int) Int {
	var k = 0;
	while (k < xs length) { if (xs[k] == x) { return k; } k = k + 1; }
	-1
}

fn _regionOfLoop(regions [[Int]], li Int) Int {
	var k = 0;
	while (k < regions length) {
		if (regions[k] contains(li)) { return k; }
		k = k + 1;
	}
	NONE
}

fn _isPortKind(k NodeKind) Bool = match (k) { inletK(_, _, _): true; outletK(_, _): true; _: false; };

-- Root-graph instance values at block or audio rate: block-buffered.
fn rootBufNodes(ctx Ctx) [NIdx] {
	var out [NIdx] = [];
	for (lp : ctx.loops) {
		for (t : lp.trees) {
			let x = ctx.trees[t].root;
			if (ctx.graphOf[x] == 0 && ctx _isInstVar(x) && !(ctx.kind[x] _isPortKind)
				&& (ctx.nrate[x] == Rate.block || ctx.nrate[x] == Rate.audio)
				&& !(out contains(x))) {
				out push!(x);
			}
		}
	}
	out
}

-- Root temps consumed in a later region: block-buffered.
fn blockTempNodes(ctx Ctx) [NIdx] {
	let regions = ctx rootRegions;
	var out [NIdx] = [];
	-- a state space's outputs: the block form writes them for the block
	for (k : ctx.blockCycles) {
		if (ctx _blockCycleForm(k) != "statespace") { continue; }
		for (o : ctx stateSpaceModel(ctx.cycles[k]).outputs) {
			if (ctx.cut[o] isTempVarCut && ctx.graphOf[o] == 0 && !(out contains(o))) { out push!(o); }
		}
	}
	for (li : ctx.graphs[0].audioLoops) {
		let own = _regionOfLoop(regions, li);
		for (t : ctx.loops[li].trees) {
			let x = ctx.trees[t].root;
			if (!(ctx.cut[x] isTempVarCut) || ctx.graphOf[x] != 0) { continue; }
			var cross = false;
			for (c : ctx.consumers[x]) {
				let cl = ctx _rootLoopOfNode(c);
				if (cl != NONE && _regionOfLoop(regions, cl) != own) { cross = true; }
			}
			if (cross && !(out contains(x))) { out push!(x); }
		}
	}
	out
}

fn _isBlockTemp(ctx Ctx, x NIdx) Bool = `cgBlockTemps contains(x);
fn _isRootBuf(ctx Ctx, x NIdx) Bool = `cgRootBuf contains(x);

-- Elements per buffer row: the channels, padded to whole SIMD vectors.
fn _rowElems(ctx Ctx, x NIdx) Int {
	let c = ctx.chans[x] max(1);
	c == 1 ? 1 : (c + 3) // 4 * 4
}

fn genBlockBufDecls(ctx Ctx) String {
	var s = "";
	for (k : ctx.blockCycles) {
		let ty = ctx _blockCycleType(k);
		for (part : ["x", "a", "b", "w"]) { s = s $ "\t%^* rc%^_%^;\n" fmt(ty, k, part); }
		if (ctx _blockCycleForm(k) == "statespace") {
			let m = ctx stateSpaceModel(ctx.cycles[k]);
			s = s $ "\ttzpl::dsp::BlockStateSpaceIO<%^, %^, %^, %^> ss%^[%^];\n\tu8 ssv%^;\n"
				fmt(ty, m.nstates, m.inputs length, m.outputs length, k, m.chans, k);
		}
	}
	for (x : `cgRootBuf) { s = s $ "\t%^* bk%^;\n" fmt(cppType(ctx.typ[x]), ctx.serial[x]); }
	for (x : `cgBlockTemps) { s = s $ "\t%^* bt%^;\n" fmt(cppType(ctx.typ[x]), ctx.serial[x]); }
	s length > 0 ? "\t// block buffers (maxBlock rows)\n" $ s $ "\n" : ""
}

-- The per-row locals of one sample that `code` uses, and the hoisted bases
-- they index: `__restrict` locals set before the sample loop, so the rows
-- don't reload through `p` (which every state store may alias).
struct RowLocals {
	bases String,
	rows String,
}

-- One row local: the name it defines, its base (type, initializer), and
-- its definition from the base `r_<name>` at row `row`.
struct RowEntry {
	name String,
	baseType String,
	baseInit String,
	rowDef String,
}

fn _rowEntries(ctx Ctx, row String) [RowEntry] {
	var out [RowEntry] = [];
	for (x : ctx.inlets) {
		match (ctx.kind[x]) {
			inletK(_, sn, _): {
				let ty = cppType(ctx.typ[x]);
				let nm = "in%^" fmt(sn);
				if (ctx _directPort(x)) {
					-- the channel-pointer table; indexed [chan][bi] where used
					out push!(RowEntry { name: nm, baseType: "%^* const" fmt(ty), baseInit: "(%^* const*)p->ins[%^].chans" fmt(ty, sn), rowDef: "" });
				} else {
					let init = ctx.chans[x] == 1 ? "(%^*)p->ins[%^].chans[0]" fmt(ty, sn) : "p->bin%^" fmt(sn);
					out push!(RowEntry { name: nm, baseType: ty, baseInit: init,
					                     rowDef: "%^* %^ = r_%^ + (%^) * %^;" fmt(ty, nm, nm, row, ctx _portElems(x)) });
				}
			}
			_: {}
		}
	}
	for (x : ctx.outlets) {
		match (ctx.kind[x]) {
			outletK(_, sn): {
				let ty = cppType(ctx.typ[x]);
				let nm = "out%^" fmt(sn);
				if (ctx _directPort(x)) {
					out push!(RowEntry { name: nm, baseType: "%^* const" fmt(ty), baseInit: "(%^* const*)p->outs[%^].chans" fmt(ty, sn), rowDef: "" });
				} else {
					let init = ctx.chans[x] == 1 ? "(%^*)p->outs[%^].chans[0]" fmt(ty, sn) : "p->bout%^" fmt(sn);
					out push!(RowEntry { name: nm, baseType: ty, baseInit: init,
					                     rowDef: "%^* %^ = r_%^ + (%^) * %^;" fmt(ty, nm, nm, row, ctx _portElems(x)) });
				}
			}
			_: {}
		}
	}
	for (x : `cgRootBuf) {
		let ty = cppType(ctx.typ[x]);
		let nm = "bv%^" fmt(ctx.serial[x]);
		let stride = ctx.nrate[x] == Rate.block ? "pgS%^" fmt(ctx.groupOf[x]) : "1";
		let def = ctx.chans[x] == 1
			? (`cgSampleVec && stride == "1" ? "%^* %^ = r_%^ + (%^);" fmt(ty, nm, nm, row)
			                                  : "%^& %^ = r_%^[(%^) * %^];" fmt(ty, nm, nm, row, stride))
			: "%^* %^ = r_%^ + (%^) * %^ * %^;" fmt(ty, nm, nm, row, stride, ctx _rowElems(x));
		out push!(RowEntry { name: nm, baseType: ty, baseInit: "p->bk%^" fmt(ctx.serial[x]), rowDef: def });
	}
	for (x : `cgBlockTemps) {
		let ty = cppType(ctx.typ[x]);
		let nm = "v%^" fmt(ctx.serial[x]);
		let def = ctx.chans[x] == 1
			? (`cgSampleVec ? "%^* %^ = r_%^ + (%^);" fmt(ty, nm, nm, row) : "%^& %^ = r_%^[%^];" fmt(ty, nm, nm, row))
			: "%^* %^ = r_%^ + (%^) * %^;" fmt(ty, nm, nm, row, ctx _rowElems(x));
		out push!(RowEntry { name: nm, baseType: ty, baseInit: "p->bt%^" fmt(ctx.serial[x]), rowDef: def });
	}
	out
}

-- Does `code` use identifier `nm` (not just as a prefix of a longer one)?
fn _usesName(code String, nm String) Bool {
	for (tail : code split(nm) drop(1)) {
		if (tail length == 0) { return true; }
		let cp = tail codePoints head;
		let ident = (cp >= 48 && cp <= 57) || (cp >= 65 && cp <= 90) || (cp >= 97 && cp <= 122) || cp == 95;
		if (!ident) { return true; }
	}
	false
}

fn _rowLocalsFor(ctx Ctx, row String, code String) RowLocals {
	var bases = "";
	var rows = "";
	for (e : ctx _rowEntries(row)) {
		if (!(_usesName(code, e.name))) { continue; }
		bases = bases $ "\t%^* __restrict r_%^ = %^;\n" fmt(e.baseType, e.name, e.baseInit);
		if (e.rowDef length > 0) { rows = rows $ "\t\t" $ e.rowDef $ "\n"; }
	}
	RowLocals { bases: bases, rows: rows }
}

fn _advanceDelay(ctx Ctx, d DelayInfo) String =
	d.allocSize != 1 ? _tabs(`cgIndent) $ "++p->d%^_wrpos;\n" fmt(d.serial) : "";

---------------------------------------------------------------------------
-- Block-form recurrences (block-engine M7, plan 4.7). A first-order cycle
-- chosen by recurrence.x (selectBlockCycles) runs over the block in two
-- passes around a library scan:
--   pass 1  per sample: the scan input x (scans), or the affine map's a and
--           b from the cycle's linear form (s' = a s + b), into rc<k>_x/_a/_b
--   scan    tzpl::dsp::scanSumWrapBlock / scanSumBlock / scanAffineBlock:
--           the written value of every sample, into rc<k>_w
--   pass 2  per sample: the cycle's trees with the state read replaced by
--           the previous written value, so values used outside the cycle
--           are computed as before; the delay write is left out and the
--           state is set from the scan's carry.

fn _blockCycleType(ctx Ctx, k Int) String = cppType(ctx.typ[ctx.dsrc[ctx.cycles[k].readers[0]]]);

fn _blockCycleForm(ctx Ctx, k Int) String = ctx blockFormOf(ctx.cycles[k], (ctx classifyRecurrences)[k]);

-- The block cycle a region consists of, or NONE. Its loops are the cycle's
-- (group key -(10 + k)); a state-space cycle's region may also hold other
-- loops that share no state with them (regionExtras).
fn _regionBlockCycle(ctx Ctx, region [Int]) Int {
	var k = NONE;
	for (li : region) {
		if (ctx _codelessLoop(li)) { continue; }
		let g = ctx.loops[li].group;
		if (g <= -10) {
			let kk = -10 - g;
			if (k != NONE && kk != k) { return NONE; }
			k = kk;
		}
	}
	if (k == NONE) { return NONE; }
	var extras = false;
	for (li : region) { if (ctx.loops[li].group != -(10 + k) && !(ctx _codelessLoop(li))) { extras = true; } }
	if (!extras) { return k; }
	if (ctx _blockCycleForm(k) != "statespace") { return NONE; }
	ctx _regionExtras(region, k).ok ? k : NONE
}

-- A loop of input-port trees only: it emits no code (a port is read where
-- it is used), so it belongs to whichever region it lands in.
fn _codelessLoop(ctx Ctx, li Int) Bool {
	for (t : ctx.loops[li].trees) {
		match (ctx.kind[ctx.trees[t].root]) { inletK(_, _, _): {} _: { return false; } }
	}
	true
}

struct RegionExtras {
	ok Bool,
	pre [Int],     -- loops that run before the state space (no dependence on it)
	post [Int],    -- loops that run after it (they depend on it)
}

-- The loops of a state-space cycle's region that are not the cycle's: each
-- shares no state with the cycle's loops; one that depends on them (through
-- the region) runs after the block, one that doesn't before it, and none
-- may do both (feed the cycle and depend on it).
fn _regionExtras(ctx Ctx, region [Int], k Int) RegionExtras {
	let g = -(10 + k);
	let keys = ctx loopStateKeys;
	var mine [String] = [];
	for (li : region) { if (ctx.loops[li].group == g) { for (key : keys[li]) { if (!(mine contains(key))) { mine push!(key); } } } }
	let n = region length;
	var dep [Bool] = false repeat(n);     -- depends on the cycle's loops
	var feeds [Bool] = false repeat(n);   -- the cycle's loops depend on it
	var i = 0;
	while (i < n) {
		let li = region[i];
		var j = 0;
		while (j < i) {
			let lj = region[j];
			if (ctx.loops[li].antecedents contains(ctx.loops[lj].serial) && (ctx.loops[lj].group == g || dep[j])) { dep[i] = true; }
			j = j + 1;
		}
		i = i + 1;
	}
	i = n - 1;
	while (i >= 0) {
		let li = region[i];
		var j = i + 1;
		while (j < n) {
			let lj = region[j];
			if (ctx.loops[lj].antecedents contains(ctx.loops[li].serial) && (ctx.loops[lj].group == g || feeds[j])) { feeds[i] = true; }
			j = j + 1;
		}
		i = i - 1;
	}
	var pre [Int] = [];
	var post [Int] = [];
	i = 0;
	while (i < n) {
		let li = region[i];
		if (ctx.loops[li].group != g && !(ctx _codelessLoop(li))) {
			for (key : keys[li]) { if (mine contains(key)) { return RegionExtras { ok: false, pre: pre, post: post }; } }
			if (dep[i] && feeds[i]) { return RegionExtras { ok: false, pre: pre, post: post }; }
			if (dep[i]) { post push!(li); } else { pre push!(li); }
		}
		i = i + 1;
	}
	RegionExtras { ok: true, pre: pre, post: post }
}

fn _mulS(a String, x String) String = a == "0" ? "0" : (a == "1" ? x : "(%^ * %^)" fmt(a, x));
fn _divS(a String, x String) String = a == "0" ? "0" : "(%^ / %^)" fmt(a, x);
fn _addS(a String, b String) String = a == "0" ? b : (b == "0" ? a : "(%^ + %^)" fmt(a, b));
fn _subS(a String, b String) String = b == "0" ? a : (a == "0" ? "(-%^)" fmt(b) : "(%^ - %^)" fmt(a, b));
fn _negS(a String) String = a == "0" ? "0" : "(-%^)" fmt(a);

-- The affine map of cycle node n over the state read r: (a, b) with
-- n = a * r + b, as C++ expressions.
fn _linAB(ctx Ctx, cy CycleInfo, r NIdx, n NIdx) (String, String) {
	if (n == r) { return ("1", "0"); }
	if (!(cy.nodes contains(n))) {
		-- a materialized value is read, not recomputed (it may draw random
		-- numbers, or be shared)
		let e = ctx _isRootNode(n) ? _varRef(ctx, n, "0") : genExpr(ctx, n, "0");
		return ("0", "(%^)" fmt(e));
	}
	let ins = ctx.ins[n];
	match (ctx.kind[n]) {
		binopK(op): {
			let (a1, b1) = _linAB(ctx, cy, r, ins[0]);
			let (a2, b2) = _linAB(ctx, cy, r, ins[1]);
			match (op) {
				add: { return (_addS(a1, a2), _addS(b1, b2)); }
				sub: { return (_subS(a1, a2), _subS(b1, b2)); }
				mul: {
					if (cy.nodes contains(ins[0])) { return (_mulS(a1, b2), _mulS(b1, b2)); }
					return (_mulS(a2, b1), _mulS(b2, b1));
				}
				div: { return (_divS(a1, b2), _divS(b1, b2)); }
				_: {}
			}
		}
		unopK(op): {
			let (a1, b1) = _linAB(ctx, cy, r, ins[0]);
			match (op) { neg: { return (_negS(a1), _negS(b1)); } _: {} }
		}
		castopK(t): {
			let (a1, b1) = _linAB(ctx, cy, r, ins[0]);
			let ty = cppType(t);
			return ("%^(%^)" fmt(ty, a1), "%^(%^)" fmt(ty, b1));
		}
		_: {}
	}
	("0", "0")   -- not reached: the cycle classified affine
}

fn _genBlockCycle(ctx Ctx, k Int, region [Int]) String {
	let cy = ctx.cycles[k];
	let r = cy.readers[0];
	let d = match (ctx.kind[r]) { delayFixReadK(di, _): di; _: 0; };
	let ser = ctx.delays[d].serial;
	let v = ctx.dsrc[r];
	let ty = ctx _blockCycleType(k);
	-- a state space's varying form is the affine scan
	let form0 = ctx blockFormOf(cy, (ctx classifyRecurrences)[k]);
	let form = form0 == "statespace" ? "affine" : form0;
	var `cgIndent Int = 2;
	var `cgRoot Int = NONE;    -- no tree is being emitted: every root is a reference
	-- pass 1
	var p1 = "";
	var scanCall = "";
	if (form == "scan+" || form == "scan+wrap") {
		-- the accumulation r + x (under the wrap, a - floor(a))
		let acc = form == "scan+" ? v : ctx.ins[v][0];
		let x = ctx.ins[acc][0] == r ? ctx.ins[acc][1] : ctx.ins[acc][0];
		p1 = "\t\tr_x[bi] = %^;\n" fmt(ctx _isRootNode(x) ? _varRef(ctx, x, "0") : genExpr(ctx, x, "0"));
		let fun = form == "scan+" ? "scanSumBlock" : "scanSumWrapBlock";
		scanCall = "\ttzpl::dsp::%^(r_x + off, r_w + off, n, s);\n" fmt(fun);
	} else {
		let (a, b) = _linAB(ctx, cy, r, v);
		p1 = "\t\tr_a[bi] = %^;\n\t\tr_b[bi] = %^;\n" fmt(a, b);
		scanCall = "\ttzpl::dsp::scanAffineBlock(r_a + off, 1, r_b + off, 1, r_w + off, n, s);\n";
	}
	-- pass 2
	var `cgReaderSub Int = r;
	var `cgReaderExpr String = "rv";
	var `cgSuppressDelay Int = d;
	let body = genLoops(ctx, region);
	-- Both passes run sample-vector when their nodes are elementwise (pass 1
	-- of a scan; the affine form's a and b stay scalar).
	let isScan = form == "scan+" || form == "scan+wrap";
	let sv = `cgSimdWidth >= 4 && ctx _sampleVecOK(region, r, d);
	let sv1 = sv && isScan && ctx _exprSampleVecOK(ctx _scanInput(cy, r, v, form));
	var s = "\t{ // recurrence %^: block form (%^)\n" fmt(k, form);
	s = s $ "\t%^* __restrict r_x = p->rc%^_x;\n\t%^* __restrict r_a = p->rc%^_a;\n" fmt(ty, k, ty, k);
	-- w has a leading element: w[off - 1] is the state before the block, so
	-- pass 2 reads every sample's prior state as w[bi - 1]
	s = s $ "\t%^* __restrict r_b = p->rc%^_b;\n\t%^* __restrict r_w = p->rc%^_w + 1;\n" fmt(ty, k, ty, k);
	s = s $ ctx _sampleLoop(p1, sv1 ? ctx _scanPass1SV(cy, r, v, form) : "");
	s = s $ "\t%^ s = p->d%^;\n\tr_w[off - 1] = s;\n" fmt(ty, ser);
	s = s $ scanCall;
	let rvS = "\t\t%^ rv = r_w[bi - 1];\n" fmt(ty);
	let rvV = "\t\t%^x4 rv = *(%^x4u*)(r_w + bi - 1);\n" fmt(ty, ty);
	var vbody = "";
	if (sv) {
		let (vb, vr) = ctx _sampleVecBody(region);
		vbody = vb;
	}
	s = s $ ctx _sampleLoop(rvS $ body, sv ? rvV $ vbody : "");
	s = s $ "\tp->d%^ = s;\n\t}\n" fmt(ser);
	s
}

-- A sample loop over [off, off+n) running `scalar`, or, when `vec` is given,
-- `vec` 4 samples a step and `scalar` for the tail; with their row locals.
fn _sampleLoop(ctx Ctx, scalar String, vec String) String {
	let rl = ctx _rowLocalsFor("bi", scalar);
	if (vec length == 0) {
		return "\t{\n" $ rl.bases $ "\tfor (i32 bi = off; bi < off + n; ++bi) {\n" $ rl.rows $ scalar $ "\t}\n\t}\n";
	}
	let vrows = ctx _sampleVecRows(vec);
	let bases = (ctx _rowLocalsFor("bi", vec $ scalar)).bases;
	"\t{\n" $ bases $ "\ti32 bi = off;\n"
		$ "\tfor (; bi + 4 <= off + n; bi += 4) {\n" $ vrows $ vec $ "\t}\n"
		$ "\tfor (; bi < off + n; ++bi) {\n" $ rl.rows $ scalar $ "\t}\n\t}\n"
}

fn _sampleVecRows(ctx Ctx, code String) String {
	var `cgSampleVec Bool = true;
	(ctx _rowLocalsFor("bi", code)).rows
}

-- A scan's input: the x of the accumulation r + x (under a wrap, a - floor(a)).
fn _scanInput(ctx Ctx, cy CycleInfo, r NIdx, v NIdx, form String) NIdx {
	let acc = form == "scan+" ? v : ctx.ins[v][0];
	ctx.ins[acc][0] == r ? ctx.ins[acc][1] : ctx.ins[acc][0]
}

fn _scanPass1SV(ctx Ctx, cy CycleInfo, r NIdx, v NIdx, form String) String {
	var `cgSampleVec Bool = true;
	var `cgSimdW Int = 4;
	let x = ctx _scanInput(cy, r, v, form);
	let t = ctx.typ[x];
	let e = ctx _isRootNode(x) ? _varRef(ctx, x, "0") : genExpr(ctx, x, "0");
	"\t\t*(%^x4u*)(r_x + bi) = %^;\n" fmt(cppType(t), e)
}

-- Is the expression at n, through its inlined nodes, elementwise (no state,
-- draws or channel ops)?
fn _exprSampleVecOK(ctx Ctx, n NIdx) Bool {
	if (ctx.chans[n] > 1 || ctx.graphOf[n] != 0) { return false; }
	if (ctx _isRootNode(n)) { return ctx.nrate[n] != Rate.audio || ctx _isBlockTemp(n) || ctx _isRootBuf(n); }
	if (!(ctx _nodeSampleVecOK(n))) { return false; }
	for (i : ctx.ins[n]) { if (!(ctx _exprSampleVecOK(i))) { return false; } }
	true
}

-- Block state space (plan 4.7, "Affine, block-constant coefficients"; the
-- model is recurrence.x stateSpaceModel): the cycle as a step function of
-- its state taps s[] and its inputs x[], writing its outputs y[]. An
-- expression over them, rebuilding the cycle's nodes and inlined inputs:
fn _ssExpr(ctx Ctx, cy CycleInfo, m SSModel, n NIdx, cel String) String {
	let inside = cy.nodes contains(n) || m.chain contains(n);
	let d = inside ? ctx readDelayOf(n) : NONE;
	if (d != NONE) {
		var j = 0;
		while (j < m.delays length) {
			if (m.delays[j] == d) { return "s[%^]" fmt(m.base[j] + ctx.dweight[n] - 1); }
			j = j + 1;
		}
	}
	var i = 0;
	while (i < m.inputs length) {
		if (m.inputs[i] == n) { return "x[%^]" fmt(i); }
		i = i + 1;
	}
	-- An outside value (a coefficient at block rate or lower) is read as is;
	-- the cycle's nodes and the inlined input expressions are rebuilt.
	if (!inside) {
		return "(%^)" fmt(ctx _isRootNode(n) ? _varRef(ctx, n, cel) : genExpr(ctx, n, cel));
	}
	let ins = ctx.ins[n];
	match (ctx.kind[n]) {
		binopK(op): {
			let a = ctx _ssExpr(cy, m, ins[0], cel);
			let b = ctx _ssExpr(cy, m, ins[1], cel);
			match (op) {
				add: { return "(%^ + %^)" fmt(a, b); }
				sub: { return "(%^ - %^)" fmt(a, b); }
				mul: { return "(%^ * %^)" fmt(a, b); }
				div: { return "(%^ / %^)" fmt(a, b); }
				_: {}
			}
		}
		unopK(op): { match (op) { neg: { return "(-%^)" fmt(ctx _ssExpr(cy, m, ins[0], cel)); } _: {} } }
		castopK(t): { return "W(%^)" fmt(ctx _ssExpr(cy, m, ins[0], cel)); }
		_: {}
	}
	"W(0)"   -- not reached: the model admits only these
}

-- The parameter groups of the block-rate values a cycle (and its inlined
-- inputs, nodes) reads.
fn _cycleGroups(ctx Ctx, nodes [NIdx]) [Int] {
	var out [Int] = [];
	for (x : nodes) {
		for (i : ctx.ins[x]) {
			if (!(nodes contains(i)) && ctx.nrate[i] == Rate.block) {
				let g = ctx.groupOf[i];
				if (g != NONE && !(out contains(g))) { out push!(g); }
			}
		}
	}
	out
}

-- A delay tap as a C++ lvalue/rvalue: tap w (1..) of delay d, channel c
-- ("c", or "" for a one-channel delay), with the write position advanced by
-- `adv` ("" or " + u64(n)").
fn _tapRef(ctx Ctx, d Int, w Int, c String, adv String) String {
	let di = ctx.delays[d];
	let ci = c length > 0 ? "[%^]" fmt(c) : "";
	if (di.allocSize == 1) { return "p->d%^%^" fmt(di.serial, ci); }
	let tap = "[(p->d%^_wrpos%^ - u64(%^)) & %^]" fmt(di.serial, adv, w, _delayMask(di));
	ctx _tapMajor(di) ? "p->d%^%^%^" fmt(di.serial, tap, ci) : "p->d%^%^%^" fmt(di.serial, ci, tap)
}

-- A block buffer of node n for the state space: (pointer, row stride in
-- elements), or ("", 0). Channel c of a multichannel buffer is at element c
-- of each row.
fn _ssBuf(ctx Ctx, n NIdx) (String, Int) {
	let c = ctx.chans[n];
	match (ctx.kind[n]) {
		inletK(_, sn, _): {
			return c == 1 ? ("(%^*)p->ins[%^].chans[0]" fmt(cppType(ctx.typ[n]), sn), 1)
			              : ("p->bin%^" fmt(sn), ctx _portElems(n));
		}
		_: {}
	}
	if (ctx _isBlockTemp(n)) { return ("p->bt%^" fmt(ctx.serial[n]), ctx _rowElems(n)); }
	if (ctx _isRootBuf(n) && ctx.nrate[n] == Rate.audio) { return ("p->bk%^" fmt(ctx.serial[n]), ctx _rowElems(n)); }
	("", 0)
}

-- A block state space (stateSpaceModel): the region runs as the plain
-- recurrence (or, first order, the affine scan) while a coefficient group
-- recomputes every sample, else over the block's samples through matrices
-- rebuilt when a group changed, one state space per channel.
fn _genStateSpace(ctx Ctx, k Int, region [Int], delays [DelayInfo]) String {
	let cy = ctx.cycles[k];
	let m = ctx stateSpaceModel(cy);
	let ty = ctx _blockCycleType(k);
	let C = m.chans;
	let serial = ctx _genRegion(region, delays);
	-- each input's and output's buffer pointer and row stride (parallel
	-- arrays: arrays of tuples trip a VM GC bug, #19)
	var xPtr [String] = [];
	var xStr [Int] = [];
	for (e : m.inputs) {
		let (bp, bs) = ctx _ssBuf(e);
		xPtr push!(bp);
		xStr push!(bs);
		if (cppType(ctx.typ[e]) != ty) { return serial; }
	}
	var yPtr [String] = [];
	var yStr [Int] = [];
	for (o : m.outputs) {
		let (bp, bs) = ctx _ssBuf(o);
		yPtr push!(bp);
		yStr push!(bs);
		if (cppType(ctx.typ[o]) != ty) { return serial; }
	}
	-- no block buffers to run over
	if ((xPtr $ yPtr) contains("")) { return serial; }
	-- the region's other loops run around the block form (regionExtras),
	-- each with the ring delays it owns
	let g = -(10 + k);
	var hasExtras = false;
	for (li : region) { if (ctx.loops[li].group != g && !(ctx _codelessLoop(li))) { hasExtras = true; } }
	let ex = hasExtras ? ctx _regionExtras(region, k) : RegionExtras { ok: true, pre: [Int](), post: [Int]() };
	if (!ex.ok) { return serial; }
	var preDelays [DelayInfo] = [];
	var postDelays [DelayInfo] = [];
	for (di : delays) {
		var inModel = false;
		for (d : m.delays) { if (ctx.delays[d].serial == di.serial) { inModel = true; } }
		if (inModel) { continue; }
		var inPost = false;
		var inPre = false;
		for (x : ctx _delayMembers(di)) {
			let li = ctx _rootLoopOfNode(x);
			if (ex.post contains(li)) { inPost = true; }
			if (ex.pre contains(li)) { inPre = true; }
		}
		if (inPost) { postDelays push!(di); } else if (inPre) { preDelays push!(di); } else { return serial; }
	}
	let nst = m.nstates;
	let cv = C == 1 ? "" : "c";       -- the channel index in C++
	let cel = C == 1 ? "0" : "c";
	var `cgIndent Int = 2;
	var `cgRoot Int = NONE;
	var `cgSimdW Int = 0;
	var `cgVoiceCount Int = 0;
	var `cgLoopChans Int = C;
	var `cgDstShape Shape = ctx.shape[cy.nodes[0]];
	-- the step: every delay's new value and the outputs from the old state,
	-- then the taps shift
	var stepBody = "";
	var j = 0;
	while (j < m.delays length) {
		let d = m.delays[j];
		let src = ctx.dsrc[ctx.delays[d].fixReaders[0]];
		stepBody = stepBody $ "\t\t\tW n%^ = W(%^);\n" fmt(j, ctx _ssExpr(cy, m, src, cel));
		j = j + 1;
	}
	var o = 0;
	while (o < m.outputs length) {
		stepBody = stepBody $ "\t\t\ty[%^] = W(%^);\n" fmt(o, ctx _ssExpr(cy, m, m.outputs[o], cel));
		o = o + 1;
	}
	j = 0;
	while (j < m.delays length) {
		var w = m.depth[j] - 1;
		while (w >= 1) { stepBody = stepBody $ "\t\t\ts[%^] = s[%^];\n" fmt(m.base[j] + w, m.base[j] + w - 1); w = w - 1; }
		stepBody = stepBody $ "\t\t\ts[%^] = n%^;\n" fmt(m.base[j], j);
		j = j + 1;
	}
	let rl = ctx _rowLocalsFor("bi", stepBody);
	var gnodes = cy.nodes copy;
	gnodes append!(m.chain);
	let groups = ctx _cycleGroups(gnodes);
	var varying [String] = [];
	var changed [String] = ["!p->ssv%^" fmt(k)];
	for (g : groups) { varying push!("pgS%^" fmt(g)); changed push!("pg%^ == 1" fmt(g)); }
	let ni = m.inputs length;
	let no = m.outputs length;
	var s = "\t{ // recurrence %^: block state space (%^ states, %^ in, %^ out, %^ chans)\n" fmt(k, nst, ni, no, C);
	s = s $ "\tif (%^) {\n" fmt(varying length > 0 ? varying join(" || ") : "false");
	-- the varying form: a first-order cycle's affine scan, else the recurrence
	let cls = (ctx classifyRecurrences)[k];
	let order1 = match (cls) { affineBlock(o1): o1 == 1; _: false; };
	s = s $ (varying length > 0 && order1 && !hasExtras && ctx firstOrderScanOK(cy) ? ctx _genBlockCycle(k, region) : serial);
	s = s $ "\t} else {\n";
	if (ex.pre length > 0) { s = s $ ctx _genRegion(ex.pre, preDelays); }
	s = s $ "\t{\n" $ rl.bases $ "\t\ti32 bi = off;\n" $ rl.rows;
	s = s $ "\t\tbool rebuild = %^;\n" fmt(changed join(" || "));
	let ind = C == 1 ? "\t\t" : "\t\t\t";
	if (C > 1) { s = s $ "\t\tfor (int c = 0; c < %^; ++c) {\n" fmt(C); }
	let ssRef = C == 1 ? "p->ss%^[0]" fmt(k) : "p->ss%^[c]" fmt(k);
	s = s $ ind $ "auto step = [&](auto* s, auto const* x, auto* y) {\n";
	s = s $ "\t\t\tusing W = std::remove_pointer_t<decltype(s)>;\n\t\t\t(void)x;\n" $ stepBody $ ind $ "};\n";
	s = s $ ind $ "if (rebuild) %^ = tzpl::dsp::BlockStateSpaceIO<%^, %^, %^, %^>::build(step);\n"
		fmt(ssRef, ty, nst, ni, no);
	-- the state from the delays: tap w of a ring is its read at weight w
	var loads [String] = [];
	j = 0;
	while (j < m.delays length) {
		let d = m.delays[j];
		let di = ctx.delays[d];
		var w = 1;
		while (w <= m.depth[j]) {
			var rd = ctx _tapRef(d, w, cv, "");
			if (di.allocSize != 1 && _delayZeroGuarded(di)) {
				rd = "(p->d%^_wrpos >= u64(%^) ? %^ : %^)" fmt(di.serial, w, rd, _zeroLit(di.typ));
			}
			loads push!(rd);
			w = w + 1;
		}
		j = j + 1;
	}
	s = s $ ind $ "%^ s[%^] = {%^};\n" fmt(ty, nst, loads join(", "));
	var xs [String] = [];
	var incs [String] = [];
	var i = 0;
	while (i < ni) {
		let b = (xPtr[i], xStr[i]);
		let e = m.inputs[i];
		let portSn = match (ctx.kind[e]) { inletK(_, sn, _): sn; _: NONE; };
		if (C > 1 && portSn != NONE && ctx.chans[e] > 1) {
			-- a multichannel port: its channel straight from the engine's pointer table
			xs push!("(%^*)p->ins[%^].chans[c] + off" fmt(ty, portSn));
			incs push!("1");
		} else {
			let chOff = C > 1 && ctx.chans[e] > 1 ? " + c" : "";
			xs push!("%^ + off * %^%^" fmt(b.0, b.1, chOff));
			incs push!(b.1 toString);
		}
		i = i + 1;
	}
	var ys [String] = [];
	var yincs [String] = [];
	o = 0;
	while (o < no) {
		let b = (yPtr[o], yStr[o]);
		ys push!("%^ + off * %^%^" fmt(b.0, b.1, C > 1 ? " + c" : ""));
		yincs push!(b.1 toString);
		o = o + 1;
	}
	s = s $ ind $ "%^ const* xin[%^] = {%^};\n" fmt(ty, ni, xs join(", "));
	s = s $ ind $ "int xinc[%^] = {%^};\n" fmt(ni, incs join(", "));
	s = s $ ind $ "%^* yout[%^] = {%^};\n" fmt(ty, no, ys join(", "));
	s = s $ ind $ "int yinc[%^] = {%^};\n" fmt(no, yincs join(", "));
	s = s $ ind $ "tzpl::dsp::runStateSpaceIO(%^, step, s, xin, xinc, yout, yinc, n);\n" fmt(ssRef);
	-- back to the delays: a ring holds the taps at its position after the
	-- block, then advances (once, for all channels)
	j = 0;
	while (j < m.delays length) {
		let d = m.delays[j];
		var w = 1;
		while (w <= m.depth[j]) {
			s = s $ ind $ "%^ = s[%^];\n" fmt(ctx _tapRef(d, w, cv, " + u64(n)"), m.base[j] + w - 1);
			w = w + 1;
		}
		j = j + 1;
	}
	if (C > 1) { s = s $ "\t\t}\n"; }
	for (d : m.delays) {
		let di = ctx.delays[d];
		if (di.allocSize != 1) { s = s $ "\t\tp->d%^_wrpos += u64(n);\n" fmt(di.serial); }
	}
	s = s $ "\t\tp->ssv%^ = 1;\n\t}\n" fmt(k);
	if (ex.post length > 0) { s = s $ ctx _genRegion(ex.post, postDelays); }
	s = s $ "\t}\n\t}\n";
	s
}

-- One region: its loops sample by sample, and the root delays it owns.
-- Serial regions keep their state and coefficients in locals: through p, the
-- compiler can't rule out that the output stores alias them, so they would go
-- through memory every sample. In each plain region (not a block form):
--  - a root-graph, one-channel delay used in that region alone (and nowhere
--    else in process, given as `rest`) is loaded before the sample loop and
--    stored after it: a single value (an array of them for C channels), or a
--    small inline ring (2..8 samples, up to 16 channels) with its write
--    position;
--  - an instance value p->v<k> the region only reads is loaded before it.
-- Rewrites texts in place.
fn _localizeDelays(ctx Ctx, texts [String], plain [Bool], rest String) Void {
	let head = "\t{\n";
	let tail = "\t}\n";
	var pre [String] = [];
	var post [String] = [];
	var subFrom [[String]] = [];   -- names to replace per text, and their locals
	var subTo [[String]] = [];
	for (t : texts) { pre push!(""); post push!(""); subFrom push!([String]()); subTo push!([String]()); }
	for (d : ctx.delays) {
		if (ctx _isVoiceGraphDeep(ctx _delayGraph(d))) { continue; }
		let ring = d.allocSize >= 2 && d.allocSize <= 8 && d.chans <= 16;
		let multi = d.chans > 1 && d.allocSize == 1 && d.chans <= 64;
		if (d.allocSize != 1 && !ring) { continue; }
		let nm = "p->d%^" fmt(d.serial);
		let wp = nm $ "_wrpos";
		if (_usesName(rest, nm) || _usesName(rest, wp)) { continue; }
		var at = NONE;
		var uses = 0;
		var i = 0;
		while (i < texts length) {
			if (_usesName(texts[i], nm) || _usesName(texts[i], wp)) { uses = uses + 1; at = i; }
			i = i + 1;
		}
		if (uses != 1 || !plain[at]) { continue; }
		let loc = "l_d%^" fmt(d.serial);
		let ty = cppType(d.typ);
		if (multi) {
			-- one value per channel, read and written a vector at a time
			pre[at] = pre[at] $ "\talignas(32) %^ %^[%^]; std::memcpy(%^, %^, sizeof %^);\n" fmt(ty, loc, d.chans, loc, nm, loc);
			post[at] = post[at] $ "\tstd::memcpy(%^, %^, sizeof %^);\n" fmt(nm, loc, loc);
		} else if (ring) {
			let dims = d.chans == 1 ? "[%^]" fmt(d.allocSize)
			         : (ctx _tapMajor(d) ? "[%^][%^]" fmt(d.allocSize, d.chans) : "[%^][%^]" fmt(d.chans, d.allocSize));
			pre[at] = pre[at] $ "\talignas(32) %^ %^%^; std::memcpy(%^, %^, sizeof %^);\n" fmt(ty, loc, dims, loc, nm, loc)
			          $ "\tu64 %^_wrpos = %^;\n" fmt(loc, wp);
			post[at] = post[at] $ "\tstd::memcpy(%^, %^, sizeof %^);\n" fmt(nm, loc, loc)
			           $ "\t%^ = %^_wrpos;\n" fmt(wp, loc);
			subFrom[at] push!(wp);
			subTo[at] push!(loc $ "_wrpos");
		} else {
			pre[at] = pre[at] $ "\t%^ %^ = %^;\n" fmt(ty, loc, nm);
			post[at] = post[at] $ "\t%^ = %^;\n" fmt(nm, loc);
		}
		subFrom[at] push!(nm);
		subTo[at] push!(loc);
	}
	var i = 0;
	while (i < texts length) {
		let t = texts[i];
		-- A plain region is "\t{\n" ... "\t}\n\t}\n": the sample loop sits in a scope.
		if (plain[i] && t startsWith(head) && t endsWith(tail)) {
			var body = t substring(head length, t length - head length - tail length);
			var si = 0;
			while (si < subFrom[i] length) { body = body _replaceName(subFrom[i][si], subTo[i][si]); si = si + 1; }
			var loads = pre[i];
			for (nm : _readOnlyValues(body)) {
				let loc = "l_" $ nm substring(3, nm length - 3);
				loads = loads $ "\tauto const %^ = %^;\n" fmt(loc, nm);
				body = body _replaceName(nm, loc);
			}
			texts[i] = head $ loads $ body $ post[i] $ tail;
		}
		i = i + 1;
	}
}

fn _isIdentCp(cp Int) Bool =
	(cp >= 48 && cp <= 57) || (cp >= 65 && cp <= 90) || (cp >= 97 && cp <= 122) || cp == 95;

-- The instance values "p->v<k>" that code only reads as scalars: never
-- assigned, incremented, indexed or taken the address of.
fn _readOnlyValues(code String) [String] {
	var names [String] = [];
	var bad [String] = [];
	let parts = code split("p->v");
	var i = 1;
	while (i < parts length) {
		var cps [Int] = [];
		for (c : parts[i] codePoints) { cps push!(c); }
		let ncp = cps length;
		var k = 0;
		while (k < ncp && cps[k] >= 48 && cps[k] <= 57) { k = k + 1; }
		if (k > 0 && (k == ncp || !_isIdentCp(cps[k]))) {
			let nm = "p->v" $ parts[i] substring(0, k);
			var j = k;
			while (j < ncp && cps[j] == 32) { j = j + 1; }
			let c0 = j < ncp ? cps[j] : 0;
			let c1 = j + 1 < ncp ? cps[j + 1] : 0;
			let prevPart = parts[i - 1];
			let prevAmp = prevPart length > 0 && prevPart endsWith("&") && !(prevPart endsWith("&&"));
			let prevInc = prevPart endsWith("++") || prevPart endsWith("--");
			let assign = (c0 == 61 && c1 != 61)                            -- =, not ==
				|| ((c0 == 43 || c0 == 45 || c0 == 42 || c0 == 47 || c0 == 124 || c0 == 38 || c0 == 94) && c1 == 61)
				|| (c0 == 43 && c1 == 43) || (c0 == 45 && c1 == 45);
			if (assign || c0 == 91 || prevAmp || prevInc) {
				if (!(bad contains(nm))) { bad push!(nm); }
			} else if (!(names contains(nm))) {
				names push!(nm);
			}
		}
		i = i + 1;
	}
	names filter(fn(nm String) Bool { !(bad contains(nm)) })
}

-- code with every use of the name nm (not followed by an identifier
-- character, as _usesName) replaced by repl.
fn _replaceName(code String, nm String, repl String) String {
	let parts = code split(nm);
	var out = parts[0];
	var i = 1;
	while (i < parts length) {
		let tail = parts[i];
		let ident = tail length > 0 && _isIdentCp(tail codePoints head);
		out = out $ (ident ? nm : repl) $ tail;
		i = i + 1;
	}
	out
}

fn _genRegion(ctx Ctx, region [Int], delays [DelayInfo]) String {
	var `cgIndent Int = 2;
	var body = genLoops(ctx, region);
	for (d : delays) { body = body $ ctx _advanceDelay(d); }
	if (body trim length == 0) { return ""; }
	-- sample-vector (4 samples a step, then the scalar tail) when elementwise
	var vbody = "";
	if (`cgSimdWidth >= 4 && delays length == 0 && ctx _sampleVecOK(region)) {
		let (vb, vr) = ctx _sampleVecBody(region);
		vbody = vb;
	}
	if (vbody length == 0 && `cgSimdWidth >= 4) {
		let pr = ctx _portRowsLoop(region, delays, body);
		if (pr length > 0) { return pr; }
	}
	ctx _sampleLoop(body, vbody)
}

-- A region on direct ports, 4 samples a step: its input ports transposed
-- into 4 rows of channels, the body run for each row, its output rows
-- transposed back; then the per-sample tail. "" if it uses no direct port.
fn _portRowsLoop(ctx Ctx, region [Int], delays [DelayInfo], body String) String {
	var pre = "";
	var post = "";
	for (x : ctx.inlets) {
		match (ctx.kind[x]) {
			inletK(_, sn, _): {
				if (ctx _directPort(x) && _usesName(body, "r_in%^" fmt(sn))) {
					let t = cppType(ctx.typ[x]);
					let C = ctx.chans[x];
					pre = pre $ "\t\talignas(32) %^ t_in%^[%^];\n\t\ttzpl::dsp::portToRows4(r_in%^, %^, bi, t_in%^);\n"
						fmt(t, sn, 4 * C, sn, C, sn);
				}
			}
			_: {}
		}
	}
	for (x : ctx.outlets) {
		match (ctx.kind[x]) {
			outletK(_, sn): {
				if (ctx _directPort(x) && _usesName(body, "r_out%^" fmt(sn))) {
					let t = cppType(ctx.typ[x]);
					let C = ctx.chans[x];
					pre = pre $ "\t\talignas(32) %^ t_out%^[%^];\n" fmt(t, sn, 4 * C);
					post = post $ "\t\ttzpl::dsp::rowsToPort4(t_out%^, %^, r_out%^, bi);\n" fmt(sn, C, sn);
				}
			}
			_: {}
		}
	}
	if (pre length == 0) { return ""; }
	let rbody = ctx _portRowsBody(region, delays);
	let rrows = (ctx _rowLocalsFor("bi + k", rbody)).rows;
	let rl = ctx _rowLocalsFor("bi", body);
	let bases = (ctx _rowLocalsFor("bi", rbody $ body)).bases;
	"\t{\n" $ bases $ "\ti32 bi = off;\n"
		$ "\tfor (; bi + 4 <= off + n; bi += 4) {\n" $ pre
		$ "\t\tfor (i32 k = 0; k < 4; ++k) {\n" $ rrows $ rbody $ "\t\t}\n" $ post $ "\t}\n"
		$ "\tfor (; bi < off + n; ++bi) {\n" $ rl.rows $ body $ "\t}\n\t}\n"
}

fn _portRowsBody(ctx Ctx, region [Int], delays [DelayInfo]) String {
	var `cgPortRows Bool = true;
	var `cgIndent Int = 3;
	var b = genLoops(ctx, region);
	for (d : delays) { b = b $ ctx _advanceDelay(d); }
	b
}

fn _nodeSampleVecOK(ctx Ctx, e NIdx) Bool = match (ctx.kind[e]) {
	delayFixReadK(_, _): false; delayVarReadK(_, _): false; delayWriteK(_): false;
	urandK(_): false; birandK(_): false; rand64K(_): false;
	bufWriteK(_, _, _): false; bufFixReadK(_, _, _, _): false; bufVarReadK(_, _, _, _): false;
	bankLookupK(_): false; vecK(_): false; phiK(_): false; voicerK(_): false;
	ifK: false; forK: false; switchK(_): false; spectralChainK(_, _): false;
	_: true;
};

-- A region's loops in sample-vector form, and the rows they use.
fn _sampleVecBody(ctx Ctx, region [Int]) (String, String) {
	var `cgSampleVec Bool = true;
	var `cgSimdW Int = 4;
	let body = genLoops(ctx, region);
	(body, (ctx _rowLocalsFor("bi", body)).rows)
}

-- Can a region run sample-vector (4 samples a step)? Its loops are one
-- channel, audio rate and SIMD-able, with no state (delays, random draws,
-- buffers), control flow or channel ops: every node is elementwise over
-- samples.
fn _sampleVecOK(ctx Ctx, region [Int], subRead NIdx = NONE, subDelay Int = NONE) Bool {
	for (li : region) {
		let loop = ctx.loops[li];
		if (loop.chans != 1 || loop.rate != Rate.audio || loop.isControlFlow) { return false; }
		if (ctx _loopHasSimdDisqualifier(loop)) { return false; }
		for (t : loop.trees) {
			if (getsOwnLoop(ctx.kind[ctx.trees[t].root])) { return false; }
			for (e : ctx.trees[t].exprs) {
				if (ctx.graphOf[e] != 0) { return false; }
				if (ctx.chans[e] > 1) { return false; }
				-- a block cycle's substituted state read and suppressed write
				if (e == subRead) { continue; }
				let sup = match (ctx.kind[e]) { delayWriteK(dd): dd == subDelay; _: false; };
				if (sup) { continue; }
				if (!(ctx _nodeSampleVecOK(e))) { return false; }
			}
		}
	}
	true
}

-- process(p, off, n): the root graph's loops, region by region, over the
-- samples of [off, off+n) (block layout above).
fn genTickFun(ctx Ctx, name String) String {
	var s = "void %^_process(%^* p, i32 off, i32 n) {\n" fmt(name, name);
	s = s $ ctx genParamGroupModes;
	var g = 0;
	while (ctx _pgActive && g < ctx.paramGroups length) {
		if (!(_pgGroupIsVoiced(ctx, g))) { s = s $ "\ti32 pgS%^ = pg%^ == 2;\n" fmt(g, g); }
		g = g + 1;
	}
	s = s $ ctx _genProcessGather;
	-- Root graph's block loops (parameter groups), then its regions; subgraph
	-- loops are emitted inside their control-flow blocks.
	s = s $ ctx genRootBlockLoops;
	let regions = ctx rootRegions;
	var owned [[DelayInfo]] = [];
	for (r : regions) { owned push!([DelayInfo]()); }
	for (d : ctx.delays) {
		if (ctx _delayGraph(d) != 0 || d.allocSize == 1) { continue; }
		var k = regions length - 1;
		for (x : ctx _delayMembers(d)) {
			let li = ctx _rootLoopOfNode(x);
			if (li != NONE) { let rk = _regionOfLoop(regions, li); if (rk != NONE) { k = rk; } }
		}
		if (k >= 0) { owned[k] push!(d); }
	}
	var texts [String] = [];
	var plain [Bool] = [];
	-- A block cycle runs once per call: should its loops ever span two
	-- regions, all of them run as plain regions instead.
	var regionCycle [Int] = [];
	for (r : regions) { regionCycle push!(ctx _regionBlockCycle(r)); }
	var ri = 0;
	while (ri < regions length) {
		let kk = regionCycle[ri];
		var seen = 0;
		for (x : regionCycle) { if (x == kk) { seen = seen + 1; } }
		let k = kk != NONE && seen > 1 ? NONE : kk;
		texts push!(k == NONE ? ctx _genRegion(regions[ri], owned[ri])
		            : (ctx _blockCycleForm(k) == "statespace" ? ctx _genStateSpace(k, regions[ri], owned[ri])
		                                                       : ctx _genBlockCycle(k, regions[ri])));
		plain push!(k == NONE);
		ri = ri + 1;
	}
	ctx _localizeDelays(texts, plain, s);
	for (t : texts) { s = s $ t; }
	s = s $ "\t%^_scatter(p, off, n);\n" fmt(name);
	if (ctx _pgActive) {
		s = s $ "\tp->pg_first = 0;\n";
		if (ctx _pgVoices > 0) { s = s $ "\tmemset(p->pg_vforce, 0, sizeof(p->pg_vforce));\n"; }
	}
	s = s $ "}\n\n";
	s
}

fn genFunPtrs(ctx Ctx, name String) String {
	var s = "tzpl_SynthFuns %^_funs = {\n" fmt(name);
	s = s $ "\t.alloc = (tzpl_SynthData* (*)(int32_t))%^_alloc,\n" fmt(name);
	s = s $ "\t.free = (tzpl_SErr (*)(tzpl_SynthData*))%^_free,\n" fmt(name);
	s = s $ "\t.init = (tzpl_SErr (*)(tzpl_SynthData*))%^_init,\n" fmt(name);
	s = s $ "\t.uninit = (tzpl_SErr (*)(tzpl_SynthData*))%^_uninit,\n" fmt(name);
	s = s $ "\t.reset = (tzpl_SErr (*)(tzpl_SynthData*))%^_reset,\n" fmt(name);
	s = s $ "\t.process = (void (*)(tzpl_SynthData*, int32_t, int32_t))%^_process,\n" fmt(name);
	if (ctx _voicerNodes length > 0) {
		s = s $ "\t.resetVoice = (tzpl_SErr (*)(tzpl_SynthData*, int32_t, int32_t))%^_resetVoice,\n" fmt(name);
	}
	if (ctx.bufSerials length > 0) {
		s = s $ "\t.swapBuffer = (tzpl_Buffer* (*)(tzpl_SynthData*, int64_t, tzpl_Buffer*))%^_swapBuffer,\n" fmt(name);
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
		if (!`cgFlatVoice && ctx _isVoiceGraphDeep(ctx.graphOf[lu])) {
			-- AoS: the voice's slots are VoiceState members (mirrors the C++
			-- genSampleBankResolvers AoS branch).
			s = s $ "static void %^_lu%^_resolve(%^* p, int v) {\n" fmt(name, u, name);
			s = s $ "\tauto& vs = p->voice_state[v];\n";
			s = s $ "\ttzpl_SampleBank* bk = p->bank%^;\n" fmt(bser);
			s = s $ "\tint idx = bk ? bk->map[vs.lu%^_p * 128 + vs.lu%^_v] : TZPL_SAMPLEBANK_UNMAPPED;\n" fmt(u, u);
			s = s $ "\tif (idx == TZPL_SAMPLEBANK_UNMAPPED) {\n";
			s = s $ "\t\tvs.lu%^_buf = nullptr;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_rootKey = 0.0f;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_sr = 0.0;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_len = 0.0;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_loopStart = 0.0;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_loopEnd = 0.0;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_hasLoop = 0;\n" fmt(u);
			s = s $ "\t} else {\n";
			s = s $ "\t\ttzpl_SampleBankEntry* e = &bk->samples[idx];\n";
			s = s $ "\t\tvs.lu%^_buf = e->buf;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_rootKey = e->rootKey;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_sr = e->sampleRate;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_len = e->buf ? (f64)e->buf->length : 0.0;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_loopStart = e->loopStart;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_loopEnd = e->loopEnd;\n" fmt(u);
			s = s $ "\t\tvs.lu%^_hasLoop = e->hasLoop;\n" fmt(u);
			s = s $ "\t}\n";
			s = s $ "}\n\n";
		} else if (ctx _isVoiceGraph(ctx.graphOf[lu])) {
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
				-- Deep: a lookup nested in a control-flow subgraph of the
				-- voice body is still per-voice (mirrors the C++, which
				-- checks isVoicerSubgraph transitively).
				if (ctx _isVoiceGraphDeep(ctx.graphOf[lu])) {
					let mv = ctx _voicerMaxVoices(ctx _voicerNodes[0]);
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

-- A tzpl_SignalType initializer: {{dims}, elem, rate}.
fn _sigType(sh Shape, t NumType, r Rate) String =
	"{{{%^, %^, %^, %^}}, %^, %^}" fmt(sh.dims[0], sh.dims[1], sh.dims[2], sh.dims[3],
		_typeTag(t), _rateCode(r));

fn _portDef(ctx Ctx, n NIdx, fallback String, sn Int) String {
	let nm = match (ctx.kind[n]) {
		inletK(name, _, _): name length == 0 ? "%^%^" fmt(fallback, sn) : name;
		outletK(name, _):   name length == 0 ? "%^%^" fmt(fallback, sn) : name;
		_: fallback;
	};
	let ty = _sigType(ctx.shape[n], ctx.typ[n], ctx.nrate[n]);
	let spec = match (ctx.kind[n]) {
		inletK(_, _, sp): match (sp) {
			-- The ABI warp ordinal is the lang ControlWarp ordinal + 1 (0 is
			-- tzpl_cwNone); `param` carries the step size of the step warp.
			some(is): "1, {%^, %^, %^, %^, (tzpl_ControlWarp)%^, (tzpl_ControlKind)%^}"
				fmt(ftosF64(is.spec.lo), ftosF64(is.spec.hi), ftosF64(is.spec.init),
				    ftosF64(is.spec.warp _warpStepSize), is.spec.warp ordinal + 1, is.kind ordinal);
			none: "0, {}";
		};
		_: "0, {}";
	};
	"\t\t{\"%^\", %^, %^},\n" fmt(nm, ty, spec)
}

-- Warp payload (step size for .step; 0.0 otherwise) for spec emission.
fn _warpStepSize(w ControlWarp) Float {
	match (w) {
		ControlWarp.step(sz): sz;
		_: 0.0;
	}
}

-- Per-column defaults of the voicer's `notes` input: gate 0, then each note
-- parameter's spec init over its columns. The engine fills a new note's
-- unspecified parameters from these.
fn _noteDefaults(ctx Ctx) [Float] {
	var out [Float] = 0.0 repeat(`cgNotesCols);
	for (np : ctx.noteParams) {
		match (ctx.kind[np]) {
			noteParamK(spec, name, col): {
				if (name != "gate") {
					var c = 0;
					while (c < ctx.chans[np] && col + c < out length) { out[col + c] = spec.init; c = c + 1; }
				}
			}
			_: {}
		}
	}
	out
}

fn genLoad(ctx Ctx, name String) String {
	-- ABI version stamp: the version of tzpl_plugin_abi.h the plugin was
	-- compiled against. Loaders refuse plugins they do not understand.
	var s = "extern \"C\" int64_t tzpl_abi_version = TZPL_PLUGIN_ABI_VERSION;\n\n";
	s = s $ "extern \"C\" tzpl_SynthDef load() {\n\ttzpl_SynthDef def = {};\n";
	s = s $ "\tdef.name = \"%^\";\n" fmt(name);
	s = s $ "\tdef.funs = %^_funs;\n" fmt(name);
	s = s $ "\tdef.num_ins = %^;\n" fmt(ctx.inlets length);
	s = s $ "\tdef.num_outs = %^;\n" fmt(ctx.outlets length);
	if (ctx.inlets length > 0) {
		s = s $ "\tstatic tzpl_PortDef ins[] = {\n";
		for (n : ctx.inlets) { s = s $ _portDef(ctx, n, "in", _portSerial(ctx, n)); }
		s = s $ "\t};\n\tdef.ins = ins;\n";
	}
	if (ctx.outlets length > 0) {
		s = s $ "\tstatic tzpl_PortDef outs[] = {\n";
		for (n : ctx.outlets) { s = s $ _portDef(ctx, n, "out", _portSerial(ctx, n)); }
		s = s $ "\t};\n\tdef.outs = outs;\n";
	}
	if (ctx _voicerNodes length > 0) {
		s = s $ "\tdef.voiced = 1;\n\tdef.maxVoices = %^;\n" fmt(ctx _voicerMaxVoices(ctx _voicerNodes[0]));
		var defs [String] = [];
		for (x : ctx _noteDefaults) { defs push!(ftosF64(x) $ "f"); }
		s = s $ "\tstatic const float noteDefaults[] = {%^};\n\tdef.noteDefaults = noteDefaults;\n" fmt(defs join(", "));
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
		s = s $ "\tlist.buffers[%^] = {\"buf%^\", {{{1, 1, 1, %^}}, tzpl_kF64, tzpl_constRate}, %^};\n"
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
	inletK(_, sn, _): sn;
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
	var `cgSimdWidth Int = serialForms() ? 0 : simdWidth;
	-- Active SIMD width while emitting inside a SIMD loop (0 = scalar). Set by
	-- genLoop; read by the var-ref / op / sink paths to pick vector forms.
	var `cgSimdW Int = 0;
	-- Sample-vector mode (block engine M8): inside a region whose loops are all
	-- one channel, the 4 SIMD lanes are 4 consecutive samples. A per-sample
	-- value is a vector (loaded from its block buffer row, unaligned); lower
	-- rates splat.
	var `cgSampleVec Bool = false;
	-- Port-rows mode (block engine M8): a channel-vector region steps 4
	-- samples at a time; its direct ports are transposed into 4 rows of
	-- channels (t_in<k>[k * C + c]) and back, and the body reads and writes
	-- row k.
	var `cgPortRows Bool = false;
	-- Flat-voice-mode state: when cgVoiceCount > 0 we are emitting a voicer body
	-- (the body's loops run per-voice over maxVoices). cgVoiceGraph is the body
	-- graph index, so var refs for body nodes get the per-voice `voice_`/[i] form.
	var `cgVoiceCount Int = 0;
	var `cgVoiceGraph Int = NONE;
	var `cgVoiceChans Int = 1;        -- per-voice channel count (body root chans)
	var `cgFlatChanShift Int = 0;     -- log2(perVoiceChans) for the current flat loop
	var `cgFlatChanMask Int = 0;      -- perVoiceChans - 1 for the current flat loop
	var `cgFlatTrip Int = 0;          -- flat element trip count for the current
	                                  -- loop (voices x chans, or loop chans for a
	                                  -- voice-expanded phi loop)
	-- Reset-loop emission state (mirrors inResetMode / singleVoiceMode):
	-- cgInResetMode makes NoteParam/Control roots read their live sources;
	-- cgSingleVoice makes flat loops run for just the voice `vi` in scope.
	var `cgInResetMode Bool = false;
	var `cgSingleVoice Bool = false;
	-- True while emitting a gets_own_loop block (put/join) inside the
	-- per-voice wrapper genLoop adds in flat voice mode; the emitters then
	-- add `i * chans` bases to voice-local operands.
	var `cgFlatOwnLoop Bool = false;
	-- Voice layout mode (mirrors the C++ flatVoiceMode): flat SoA when the
	-- voice body has no control flow; AoS (VoiceState struct, per-voice
	-- loops, vs. refs) when it branches. cgInVoiceLoop is true inside an
	-- AoS per-voice block; cgVoiceIdx names its index ("v", or "vi" in the
	-- noteOn per-note recompute).
	var flatV = true;
	for (n : ctx.sorted) {
		if (isControlFlowKind(ctx.kind[n]) && ctx _isVoiceGraphDeep(ctx.graphOf[n])) { flatV = false; }
	}
	-- Flat (SoA) voices index state with shifts and masks: only for a
	-- power-of-two per-voice channel count. Anything else runs AoS.
	for (v : ctx _voicerNodes) {
		if (!(ctx _voicerChansForGraph(ctx _voicerBodyGraph(v)) hasSingleBit)) { flatV = false; }
	}
	var `cgFlatVoice Bool = flatV;
	-- The voicer's `notes` input: its port serial and row width.
	var `cgNotesSn Int = 0;
	var `cgNotesCols Int = 1;
	if (ctx.notesInlets length > 0) {
		let ni = ctx.notesInlets[0];
		match (ctx.kind[ni]) { inletK(_, sn, _): { `cgNotesSn = sn; } _: {} }
		`cgNotesCols = ctx.shape[ni].dims[3];
	}
	var `cgDstShape Shape = scalarShape();
	var `cgTables [String] = [];
	var `cgRootBuf [Int] = [];
	var `cgBlockTemps [Int] = [];
	var `cgReaderSub Int = NONE;
	var `cgReaderExpr String = "";
	var `cgSuppressDelay Int = NONE;
	`cgRootBuf = ctx rootBufNodes;
	`cgBlockTemps = ctx blockTempNodes;
	var `cgInVoiceLoop Bool = false;
	var `cgVoiceIdx String = "v";

	var s = "\n";
	s = s $ "#include \"tzpl_plugin_abi.h\"\n";
	s = s $ "#include \"tzpl_matrix_transform.hpp\"\n";
	s = s $ "#include \"tzpl_random.hpp\"\n";
	s = s $ "#include \"tzpl_delay_interp.hpp\"\n";
	if (ctx _spectralChains length > 0) { s = s $ "#include \"tzpl_fft.hpp\"\n"; }
	if (ctx.blockCycles length > 0) {
		s = s $ "#include \"tzpl_dsp/scan.hpp\"\n#include \"tzpl_dsp/state_space.hpp\"\n";
	}
	var anyDirect = false;
	for (x : ctx.inlets $ ctx.outlets) { if (ctx _directPort(x)) { anyDirect = true; } }
	if (anyDirect) { s = s $ "#include \"tzpl_dsp/vec_types.hpp\"\n"; }
	s = s $ "#include <algorithm>\n#include <cmath>\n#include <cstdio>\n#include <cstring>\n#include <cstdlib>\n#include <array>\n\n";
	s = s $ "using namespace synthdef;\nusing namespace simd;\n";
	s = s $ "\n";

	-- Shared-input pointer (mirrors the C++ genClass emission): exported so
	-- the loader can repoint it at the engine's process-global table.
	if (ctx _usesSharedInput) {
		s = s $ "static tzpl_SharedInput tzpl_sharedInputFallback = {};\n";
		s = s $ "extern \"C\" tzpl_SharedInput const* tzpl_sharedInput = &tzpl_sharedInputFallback;\n\n";
	}

	s = s $ "extern tzpl_SynthFuns %^_funs;\n\n" fmt(name);
	s = s $ "typedef struct %^ {\n" fmt(name);
	-- tzpl_SynthData, then the synth's own state.
	s = s $ "\ttzpl_SynthFuns funs;\n\tstruct tzpl_Engine* engine;\n\tstruct tzpl_Node* node;\n";
	s = s $ "\tint32_t num_ins;\n\tint32_t num_outs;\n\tint32_t maxBlock;\n";
	s = s $ "\ttzpl_InPort* ins;\n\ttzpl_OutPort* outs;\n";
	s = s $ "\tdouble fs, sd; // sample rate, sample dur\n\n";
	s = s $ genDeclConstVars(ctx);
	s = s $ genDeclInstVars(ctx);
	s = s $ ctx genParamGroupDecls;
	s = s $ "} %^;\n\n" fmt(name);
	s = s $ genAllocFun(ctx, name);
	s = s $ genFreeFun(ctx, name);
	s = s $ genSampleBankResolvers(ctx, name);
	s = s $ genGatherFun(ctx, name);
	s = s $ genInitFun(ctx, name);
	s = s $ genUninitFun(ctx, name);
	s = s $ genResetFun(ctx, name);
	s = s $ genTickFun(ctx, name);
	s = s $ genVoicerNoteFuns(ctx, name);
	s = s $ genSwapBufferFun(ctx, name);
	s = s $ genSwapSampleBankFun(ctx, name);
	s = s $ genFunPtrs(ctx, name);
	s = s $ genLoad(ctx, name);
	s = s $ genLoadBufferDefs(ctx);
	s = s $ genLoadSampleBankDefs(ctx);
	s = s $ genLoadTags(ctx);
	-- Channel map tables used by the code above go before the struct.
	let tables = `cgTables join;
	if (tables length > 0) {
		let at = match (s indexOf("extern tzpl_SynthFuns ")) { some(k): k; none: 0; };
		s = s substring(0, at) $ tables $ "\n" $ s substring(at, s length - at);
	}
	s
}
