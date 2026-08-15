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
	setInput(Int, Int, Float),                         -- node, port, value
	setInputX(Int, Int, Float, Float, FadeCurve),      -- node, port, value, xfade, curve
	setControl(Int, Int, Float),                       -- node, controlID, value
	setControlNamed(Int, String, Float),               -- node, controlName, value
	noteOn(Int, Int, [Float]),                         -- node, noteID, params
	noteOff(Int, Int),                                 -- node, noteID
	allNotesOff Int,                                   -- node
	noteSetParams(Int, Int, Int, [Float]),             -- node, noteID, firstParam, params
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

fn setInput(b Bundle, node Int, port Int, value Float) Bundle {
	b add(EngineCmd.setInput(node, port, value))
}

fn setInputX(b Bundle, node Int, port Int, value Float, xfade Float, curve FadeCurve) Bundle {
	b add(EngineCmd.setInputX(node, port, value, xfade, curve))
}

fn setControl(b Bundle, node Int, controlID Int, value Float) Bundle {
	b add(EngineCmd.setControl(node, controlID, value))
}

-- By-name overload: the engine resolves the name against the node's def when
-- the bundle is submitted (unknown names abort the bundle).
fn setControl(b Bundle, node Int, controlName String, value Float) Bundle {
	b add(EngineCmd.setControlNamed(node, controlName, value))
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

fn noteSetParams(b Bundle, node Int, noteID Int, firstParam Int, params [Float]) Bundle {
	b add(EngineCmd.noteSetParams(node, noteID, firstParam, params))
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
		setInput(n, p, v)                 : setInput(n, p, v);
		setInputX(n, p, v, x, cv)         : setInputX(n, p, v, x, cv ordinal);
		setControl(n, cid, v)             : setControl(n, cid, v);
		setControlNamed(n, name, v)       : setControl(n, name, v);
		noteOn(n, nid, params)            : noteOn(n, nid, params);
		noteOff(n, nid)                   : noteOff(n, nid);
		allNotesOff(n)                    : allNotesOff(n);
		noteSetParams(n, nid, first, params) : noteSetParams(n, nid, first, params);
	}
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
		setInput(n, p, v)                 : "setInput(%^, %^, %^)" fmt(n, p, v);
		setInputX(n, p, v, x, cv)         : "setInputX(%^, %^, %^, %^, %^)" fmt(n, p, v, x, cv tag toString);
		setControl(n, cid, v)             : "setControl(%^, %^, %^)" fmt(n, cid, v);
		setControlNamed(n, name, v)       : "setControl(%^, \"%^\", %^)" fmt(n, name, v);
		noteOn(n, nid, params)            : "noteOn(%^, %^, %^)" fmt(n, nid, params _fmtFloats);
		noteOff(n, nid)                   : "noteOff(%^, %^)" fmt(n, nid);
		allNotesOff(n)                    : "allNotesOff(%^)" fmt(n);
		noteSetParams(n, nid, first, params) : "noteSetParams(%^, %^, %^, %^)" fmt(n, nid, first, params _fmtFloats);
	}
}

fn toString(b Bundle) String {
	var lines = [String]();
	for (c : b.cmds) { lines push!("    " $ c toString); }
	"Bundle {\n" $ lines _joined("\n") $ "\n}"
}
