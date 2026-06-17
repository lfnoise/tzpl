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
	chans < loopChans ? "[%^&%^]" fmt(cel, chans - 1) : "[%^]" fmt(cel);

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
		delayFixReadK(d, k): _delayFixReadExpr(ctx, n, d, k, cel);
		delayVarReadK(d, ip): _delayVarReadExpr(ctx, n, d, ip, cel);
		_:              "/* FIXME %^ */" fmt(nodeStr(ctx, n));
	}
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

	-- Declare multichannel temp-var roots that are not consumed in their loop.
	if (loop.chans > 1) {
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
	if (loop.chans == 1) {
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

fn genDeclInstVars(ctx Ctx) String {
	var s = "";
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
	-- inInitMode is only for genDelayAlloc (runtime delay sizing); init LOOPS
	-- reference cross-tree roots as variables just like audio loops do.
	s = s $ genLoops(ctx, ctx.initLoops);
	s = s $ genDelayAlloc(ctx);
	s = s $ genDelayInit(ctx);
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

fn genDelayAdvance(ctx Ctx) String {
	var s = "";
	for (d : ctx.delays) {
		if (d.allocSize != 1) {
			let evtRate = d.writer != NONE && ctx.nrate[d.writer] == Rate.event;
			if (!evtRate) { s = s $ _tabs(`cgIndent) $ "++p->d%^_wrpos;\n" fmt(d.serial); }
		}
	}
	if (s length > 0) { s = s $ "\n"; }
	s
}

fn genTickFun(ctx Ctx, name String) String {
	var s = "void %^_processAudio(%^* p) {\n" fmt(name, name);
	s = s $ genLoops(ctx, ctx.audioLoops);
	s = s $ genDelayAdvance(ctx);
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
	s = s $ "};\n\n";
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
	s = s $ genFunPtrs(ctx, name);
	s = s $ genLoad(ctx, name);
	s
}
