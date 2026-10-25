-- bundles.x
-- First-class audio engine command bundles.
--
-- The audio_engine FFI records commands into a hidden thread-local bundle
-- between begin() and go()/sched(). This module provides a data-structure
-- alternative: `EngineCmd` is a plain enum describing one command, and a
-- `Bundle` is an ordered sequence of them. A Bundle is an ordinary value --
-- it can be built up incrementally, stored, passed between functions,
-- inspected, and scheduled any number of times, on any silo.
--
--     import bundles.*;
--
--     let b = bundle()
--         newNode("sinosc", 101)
--         setInput(101, 0, 240.0)
--         connect(101, 0, 0, 0);
--
--     b go(0);              -- send now to silo 0
--     b sched(0, 0, 16.0);  -- and again on silo 0, clock 0, at beat 16
--
-- Scheduling returns an audio_engine Err code (as Int). The engine validates
-- the whole bundle at submit; on the first error it discards the entire
-- bundle (atomic abort) and that error is returned. The Bundle value itself
-- is untouched by scheduling, so a failed or successful submit can simply be
-- submitted again.

import audio_engine.*;

---------------------------------------------------------------------------
-- EngineCmd: one bundleable engine command.
-- Cases mirror the bundleable audio_engine FFI builders; payload order
-- matches the FFI argument order.

-- The kinds of input value operation (EngineCmd.input's op field).
const kInputStep = 0;            -- set (with a ramp time: ramp)
const kInputFade = 1;            -- crossfade (setInputX)
const kInputImpulse = 2;         -- one sample, then back

enum EngineCmd {
	newNode(String, Int),                              -- defName, nodeID
	freeNode Int,                                      -- nodeID
	freeAllNodes,
	channelOffset Int,                                 -- offset
	connect(Int, Int, Int, Int),                       -- srcNode, srcPort, dstNode, dstPort
	connectX(Int, Int, Int, Int, Float, FadeCurve),    -- ... , xfade, curve
	disconnectInput(Int, Int),                         -- dstNode, dstPort
	disconnectInputX(Int, Int, Float, FadeCurve),      -- ... , xfade, curve
	disconnectSource(Int, Int, Int, Int),              -- srcNode, srcPort, dstNode, dstPort
	disconnectSourceX(Int, Int, Int, Int, Float, FadeCurve),
	disconnectOutput(Int, Int),                        -- srcNode, srcPort
	disconnectNode Int,                                -- nodeID
	reconnectOutput(Int, Int, Int, Int, Float, FadeCurve), -- old src, new src, xfade, curve
	replaceNode(Int, Int, Float, FadeCurve),           -- oldID, newID, xfade, curve
	-- An input value operation (setInput & co. in audio_engine.x): node;
	-- port index, or -1 and the input's name; region: flat channels [first,
	-- first+count) (count 0 = the whole input), or a box when offset is
	-- non-empty; values; time (ramp or crossfade); curve; op (kInput*).
	-- Kept flat (no nested enum payloads): see the note at _issueInput.
	input(Int, Int, String, Int, Int, [Int], [Int], [Float], Float, FadeCurve, Int),
	noteOn(Int, Int, [Float]),                         -- node, noteID, params
	noteOff(Int, Int),                                 -- node, noteID
	allNotesOff Int,                                   -- node
	noteSetParams(Int, Int, Int, [Float], Float),      -- node, noteID, firstParam, params, ramp
}

---------------------------------------------------------------------------
-- Bundle: an ordered sequence of commands.

struct Bundle {
	cmds [EngineCmd],
}

fn bundle() Bundle {
	Bundle { cmds: [EngineCmd]() }
}

-- Append one command. Returns the bundle so calls chain pipeline-style.
fn add(b Bundle, c EngineCmd) Bundle {
	b.cmds push!(c);
	b
}

fn length(b Bundle) Int { b.cmds length }

-- Independent copy: appending to one no longer affects the other.
fn copy(b Bundle) Bundle { Bundle { cmds: b.cmds copy } }

---------------------------------------------------------------------------
-- Builders: one per command, mirroring the FFI names and argument order.

fn newNode(b Bundle, defName String, nodeID Int) Bundle {
	b add(EngineCmd.newNode(defName, nodeID))
}

fn freeNode(b Bundle, nodeID Int) Bundle {
	b add(EngineCmd.freeNode(nodeID))
}

fn freeAllNodes(b Bundle) Bundle {
	b add(EngineCmd.freeAllNodes)
}

fn channelOffset(b Bundle, offset Int) Bundle {
	b add(EngineCmd.channelOffset(offset))
}

fn connect(b Bundle, srcNode Int, srcPort Int, dstNode Int, dstPort Int) Bundle {
	b add(EngineCmd.connect(srcNode, srcPort, dstNode, dstPort))
}

fn connectX(b Bundle, srcNode Int, srcPort Int, dstNode Int, dstPort Int,
            xfade Float, curve FadeCurve) Bundle {
	b add(EngineCmd.connectX(srcNode, srcPort, dstNode, dstPort, xfade, curve))
}

fn disconnectInput(b Bundle, dstNode Int, dstPort Int) Bundle {
	b add(EngineCmd.disconnectInput(dstNode, dstPort))
}

fn disconnectInputX(b Bundle, dstNode Int, dstPort Int, xfade Float, curve FadeCurve) Bundle {
	b add(EngineCmd.disconnectInputX(dstNode, dstPort, xfade, curve))
}

fn disconnectSource(b Bundle, srcNode Int, srcPort Int, dstNode Int, dstPort Int) Bundle {
	b add(EngineCmd.disconnectSource(srcNode, srcPort, dstNode, dstPort))
}

fn disconnectSourceX(b Bundle, srcNode Int, srcPort Int, dstNode Int, dstPort Int,
                     xfade Float, curve FadeCurve) Bundle {
	b add(EngineCmd.disconnectSourceX(srcNode, srcPort, dstNode, dstPort, xfade, curve))
}

fn disconnectOutput(b Bundle, srcNode Int, srcPort Int) Bundle {
	b add(EngineCmd.disconnectOutput(srcNode, srcPort))
}

fn disconnectNode(b Bundle, nodeID Int) Bundle {
	b add(EngineCmd.disconnectNode(nodeID))
}

fn reconnectOutput(b Bundle, oldSrcNode Int, oldSrcPort Int, newSrcNode Int, newSrcPort Int,
                   xfade Float, curve FadeCurve) Bundle {
	b add(EngineCmd.reconnectOutput(oldSrcNode, oldSrcPort, newSrcNode, newSrcPort, xfade, curve))
}

fn replaceNode(b Bundle, oldID Int, newID Int, xfade Float, curve FadeCurve) Bundle {
	b add(EngineCmd.replaceNode(oldID, newID, xfade, curve))
}

-- Input values: the bundle forms of audio_engine.x's setInput family. The
-- port is an index or a name (a name is resolved when the bundle is
-- submitted; unknown names abort it); the value is a Float or [Float].
fn _portIndex(port Int) Int = port;
fn _portIndex(port String) Int = -1;
fn _portName(port Int) String = "";
fn _portName(port String) String = port;
fn _values(v Float) [Float] = [v];
fn _values(v Int) [Float] = [v toFloat];
fn _values(vs [Float]) [Float] = vs;

fn setInput<P, V>(b Bundle, node Int, port P, value V, rampTime Float = 0.0,
                  curve FadeCurve = FadeCurve.fadeLinear) Bundle =
	b add(EngineCmd.input(node, _portIndex(port), _portName(port), 0, 0, [Int](), [Int](),
	                      _values(value), rampTime, curve, kInputStep));

fn setInputAt<P, V>(b Bundle, node Int, port P, first Int, value V, rampTime Float = 0.0,
                    curve FadeCurve = FadeCurve.fadeLinear) Bundle {
	let vs = _values(value);
	b add(EngineCmd.input(node, _portIndex(port), _portName(port), first, vs length,
	                      [Int](), [Int](), vs, rampTime, curve, kInputStep))
}

fn setInputBox<P, V>(b Bundle, node Int, port P, offset [Int], extent [Int], value V,
                     rampTime Float = 0.0, curve FadeCurve = FadeCurve.fadeLinear) Bundle =
	b add(EngineCmd.input(node, _portIndex(port), _portName(port), 0, 0, offset, extent,
	                      _values(value), rampTime, curve, kInputStep));

fn setInputX<P, V>(b Bundle, node Int, port P, value V, xfade Float,
                   curve FadeCurve = FadeCurve.fadeLinear) Bundle =
	b add(EngineCmd.input(node, _portIndex(port), _portName(port), 0, 0, [Int](), [Int](),
	                      _values(value), xfade, curve, kInputFade));

fn impulseInput<P, V>(b Bundle, node Int, port P, value V) Bundle =
	b add(EngineCmd.input(node, _portIndex(port), _portName(port), 0, 0, [Int](), [Int](),
	                      _values(value), 0.0, FadeCurve.fadeLinear, kInputImpulse));

fn impulseInputAt<P, V>(b Bundle, node Int, port P, first Int, value V) Bundle {
	let vs = _values(value);
	b add(EngineCmd.input(node, _portIndex(port), _portName(port), first, vs length,
	                      [Int](), [Int](), vs, 0.0, FadeCurve.fadeLinear, kInputImpulse))
}

fn noteOn(b Bundle, node Int, noteID Int, params [Float]) Bundle {
	b add(EngineCmd.noteOn(node, noteID, params))
}

fn noteOff(b Bundle, node Int, noteID Int) Bundle {
	b add(EngineCmd.noteOff(node, noteID))
}

fn allNotesOff(b Bundle, node Int) Bundle {
	b add(EngineCmd.allNotesOff(node))
}

-- Change a sounding note's parameters from firstParam (0 = the first note
-- parameter), stepped or ramped over rampTime seconds.
fn noteSetParams(b Bundle, node Int, noteID Int, firstParam Int, params [Float],
                 rampTime Float = 0.0) Bundle {
	b add(EngineCmd.noteSetParams(node, noteID, firstParam, params, rampTime))
}

---------------------------------------------------------------------------
-- Scheduling.

-- Replay one command into the FFI's open thread-local bundle.
fn _issue(c EngineCmd) Int {
	match (c) {
		newNode(def, id)                  : newNode(def, id);
		freeNode(id)                      : freeNode(id);
		freeAllNodes                      : freeAllNodes();
		channelOffset(off)                : channelOffset(off);
		connect(s, sp, d, dp)             : connect(s, sp, d, dp);
		connectX(s, sp, d, dp, x, cv)     : connectX(s, sp, d, dp, x, cv ordinal);
		disconnectInput(d, dp)            : disconnectInput(d, dp);
		disconnectInputX(d, dp, x, cv)    : disconnectInputX(d, dp, x, cv ordinal);
		disconnectSource(s, sp, d, dp)    : disconnectSource(s, sp, d, dp);
		disconnectSourceX(s, sp, d, dp, x, cv) : disconnectSourceX(s, sp, d, dp, x, cv ordinal);
		disconnectOutput(s, sp)           : disconnectOutput(s, sp);
		disconnectNode(id)                : disconnectNode(id);
		reconnectOutput(os, osp, ns, nsp, x, cv) : reconnectOutput(os, osp, ns, nsp, x, cv ordinal);
		replaceNode(oldID, newID, x, cv)  : replaceNode(oldID, newID, x, cv ordinal);
		input(n, pi, pn, first, count, off, ext, vs, t, cv, op) :
			_issueInput(n, pi, pn, first, count, off, ext, vs, t, cv, op);
		noteOn(n, nid, params)            : noteOn(n, nid, params);
		noteOff(n, nid)                   : noteOff(n, nid);
		allNotesOff(n)                    : allNotesOff(n);
		noteSetParams(n, nid, first, params, ramp) : noteSetParams(n, nid, first, params, ramp);
	}
}

-- EngineCmd.input's payload is deliberately flat. An earlier form carried
-- nested enums (port: index | named, region: whole | channels | box) and
-- crashed the VM intermittently in tzpl_app renders after bundles built from
-- it were discarded (a garbage Float word later read as an object pointer
-- in ListNode::force) -- a VM bug with nested enum payloads not yet found.
fn _issueInput(n Int, pi Int, pn String, first Int, count Int, off [Int], ext [Int],
               vs [Float], t Float, cv FadeCurve, op Int) Int {
	if (off length > 0) { return inputOpBox(n, pi, pn, off, ext, vs, t, cv ordinal, op); }
	inputOp(n, pi, pn, first, count, vs, t, cv ordinal, op)
}

-- Submit `b` to `silo` on clock slot `clock` at `beat` with the given policy.
-- Returns an audio_engine Err code (errNone == 0 on success). The engine
-- validates the whole bundle here; on error nothing is applied.
fn schedPolicy(b Bundle, silo Int, clock Int, beat Float, policy SchedPolicy) Int {
	var err = begin();
	if (err == 0) {
		for (c : b.cmds) { _issue(c); }
		err = schedPolicy(silo, clock, beat, policy ordinal);
	}
	err
}

-- Send `b` to `silo` for immediate execution.
fn go(b Bundle, silo Int) Int {
	b schedPolicy(silo, 0, 0.0, SchedPolicy.schedImmediate)
}

fn sched(b Bundle, silo Int) Int {
	b go(silo)
}

-- Schedule `b` on `silo` at `beat` of clock slot `clock`.
fn sched(b Bundle, silo Int, clock Int, beat Float) Int {
	b schedPolicy(silo, clock, beat, SchedPolicy.schedBetterLateThanNever)
}

---------------------------------------------------------------------------
-- Inspection.

-- Local join/format helpers so this module has no dependency outside
-- bridge/modules (strings.x lives in lang/modules).
fn _joined(strings [String], separator String) String {
	var out = "";
	var between = false;
	for (s : strings) {
		if (between) { out = out $ separator; } else { between = true; }
		out = out $ s;
	}
	out
}

fn _fmtFloats(a [Float]) String {
	"[" $ (a @ toString) _joined(", ") $ "]"
}

fn toString(c EngineCmd) String {
	match (c) {
		newNode(def, id)                  : "newNode(\"%^\", %^)" fmt(def, id);
		freeNode(id)                      : "freeNode(%^)" fmt(id);
		freeAllNodes                      : "freeAllNodes()";
		channelOffset(off)                : "channelOffset(%^)" fmt(off);
		connect(s, sp, d, dp)             : "connect(%^, %^, %^, %^)" fmt(s, sp, d, dp);
		connectX(s, sp, d, dp, x, cv)     : "connectX(%^, %^, %^, %^, %^, %^)" fmt(s, sp, d, dp, x, cv tag toString);
		disconnectInput(d, dp)            : "disconnectInput(%^, %^)" fmt(d, dp);
		disconnectInputX(d, dp, x, cv)    : "disconnectInputX(%^, %^, %^, %^)" fmt(d, dp, x, cv tag toString);
		disconnectSource(s, sp, d, dp)    : "disconnectSource(%^, %^, %^, %^)" fmt(s, sp, d, dp);
		disconnectSourceX(s, sp, d, dp, x, cv) : "disconnectSourceX(%^, %^, %^, %^, %^, %^)" fmt(s, sp, d, dp, x, cv tag toString);
		disconnectOutput(s, sp)           : "disconnectOutput(%^, %^)" fmt(s, sp);
		disconnectNode(id)                : "disconnectNode(%^)" fmt(id);
		reconnectOutput(os, osp, ns, nsp, x, cv) : "reconnectOutput(%^, %^, %^, %^, %^, %^)" fmt(os, osp, ns, nsp, x, cv tag toString);
		replaceNode(oldID, newID, x, cv)  : "replaceNode(%^, %^, %^, %^)" fmt(oldID, newID, x, cv tag toString);
		input(n, pi, pn, first, count, off, ext, vs, t, cv, op) :
			_inputString(n, pi, pn, first, count, off, ext, vs, t, cv, op);
		noteOn(n, nid, params)            : "noteOn(%^, %^, %^)" fmt(n, nid, params _fmtFloats);
		noteOff(n, nid)                   : "noteOff(%^, %^)" fmt(n, nid);
		allNotesOff(n)                    : "allNotesOff(%^)" fmt(n);
		noteSetParams(n, nid, first, params, ramp) : ramp > 0.0
			? "noteSetParams(%^, %^, %^, %^, %^)" fmt(n, nid, first, params _fmtFloats, ramp)
			: "noteSetParams(%^, %^, %^, %^)" fmt(n, nid, first, params _fmtFloats);
	}
}

-- An input command printed as the builder call that makes it.
fn _inputString(n Int, pi Int, pn String, first Int, count Int, off [Int], ext [Int],
                vs [Float], t Float, cv FadeCurve, op Int) String {
	let ps = pi >= 0 ? pi toString : "\"" $ pn $ "\"";
	let v = vs length == 1 ? vs[0] toString : vs _fmtFloats;
	let name = op == kInputStep ? "setInput" : (op == kInputFade ? "setInputX" : "impulseInput");
	var fname = name;
	var at = "";
	if (off length > 0) {
		fname = name == "setInput" ? "setInputBox" : name $ "Box";
		at = ", [" $ (off @ toString) _joined(", ") $ "], [" $ (ext @ toString) _joined(", ") $ "]";
	} else if (count > 0) {
		fname = name $ "At";
		at = ", " $ first toString;
	}
	var args = "%^, %^%^, %^" fmt(n, ps, at, v);
	if (op != kInputImpulse && (t > 0.0 || op == kInputFade)) {
		args = args $ ", " $ t toString;
		if (cv != FadeCurve.fadeLinear) { args = args $ ", " $ cv tag toString; }
	}
	"%^(%^)" fmt(fname, args)
}

fn toString(b Bundle) String {
	var lines = [String]();
	for (c : b.cmds) { lines push!("    " $ c toString); }
	"Bundle {\n" $ lines _joined("\n") $ "\n}"
}
