-- live/plan.x -- pure planning layer of the live proxy system.
--
-- Everything here is engine-free: graph scanning, def-name mangling, quant
-- math, and swap choreography expressed as [PxOp] values that live.proxy
-- lowers to engine bundles. Keeping this half pure lets the exact command
-- sequences (including the connect-new-before-breaking-old ordering the
-- engine requires) be asserted by a headless test script.
--
-- Imports only synthdef (for the SignalGraph/SignalExprKind types the
-- scanners walk), so it needs a binary with the synthdef FFI registered
-- (tzpl_app) but no running engine.

import synthdef.*;

---------------------------------------------------------------------------
-- Def-name mangling. Proxy defs are internal: the user never sees them.

-- Anchor: passthrough node whose inlet is the proxy's summing point and
-- whose outlet is its stable output. One shared def per channel count.
fn anchorDefName(chans Int) String = "_pxA" $ chans toString;

-- Monitor: anchor -> monitor -> Audio Out, with volume as a fadeable inlet.
fn monitorDefName(chans Int) String = "_pxM" $ chans toString;

-- Source wrapper: one def name per proxy, hot-reloaded on every redefine.
fn pxDefName(silo Int, serial Int) String =
    "_px" $ silo toString $ "_" $ serial toString;

-- Inlet name marking a proxy reference emitted by live.proxy's sig().
fn pxRefName(idx Int) String = "pxref" $ idx toString;

fn isPxRefName(name String) Bool = name startsWith("pxref");

---------------------------------------------------------------------------
-- Graph wrapping

-- Guarantee the user's graph function ends in an outlet so every source def
-- has exactly one output port for the anchor connection.
fn wrapGraphFn(f GraphFn) GraphFn {
    fn() S {
        let r = f();
        match (r.kind) {
            outlet(nm): r;
            _: r outlet;
        }
    }
}

---------------------------------------------------------------------------
-- Graph scans.
--
-- Engine input-port order = inlet declaration order = the order inlet exprs
-- appear in graph.exprs, so a single walk with a running port counter maps
-- pxref inlets to engine ports. Only TOP-LEVEL inlets become ports;
-- subgraphs (voicer/if_/for_/switch/spectralChain bodies) keep their exprs
-- in their own SignalGraph and are recursed only where meaningful.

-- (port index, ref sequence number) for every pxref inlet. The sequence
-- number is the order sig() was called in, which is also the order the
-- collected proxy-reference array holds them.
fn scanRefPorts(g SignalGraph) [(Int, Int)] {
    var out = [(Int, Int)]();
    var port = 0;
    var seq = 0;
    for (e : g.exprs) {
        match (e.kind) {
            inlet(typ, ch, nm): {
                if (nm isPxRefName) {
                    out push!((port, seq));
                    seq = seq + 1;
                }
                port = port + 1;
            }
            _: 0;
        }
    }
    out
}

-- Count pxref inlets hiding inside nested subgraphs. Proxy references are
-- only supported at the top level of a graph function (an inlet inside a
-- voicer body is not an engine port); live.proxy reports these as errors.
fn countNestedRefs(g SignalGraph) Int {
    var n = 0;
    for (e : g.exprs) {
        match (e.kind) {
            voicer(v, sub): n = n + sub _countRefsAnywhere;
            if_(a, b): n = n + a _countRefsAnywhere + b _countRefsAnywhere;
            for_(c, sub): n = n + sub _countRefsAnywhere;
            switch_(gs): for (sg : gs) { n = n + sg _countRefsAnywhere; }
            spectralChain(fs, hs, sub): n = n + sub _countRefsAnywhere;
            _: 0;
        }
    }
    n
}

fn _countRefsAnywhere(g SignalGraph) Int {
    var n = 0;
    for (e : g.exprs) {
        match (e.kind) {
            inlet(typ, ch, nm): if (nm isPxRefName) { n = n + 1; }
            voicer(v, sub): n = n + sub _countRefsAnywhere;
            if_(a, b): n = n + a _countRefsAnywhere + b _countRefsAnywhere;
            for_(c, sub): n = n + sub _countRefsAnywhere;
            switch_(gs): for (sg : gs) { n = n + sg _countRefsAnywhere; }
            spectralChain(fs, hs, sub): n = n + sub _countRefsAnywhere;
            _: 0;
        }
    }
    n
}

-- Names of every control() / trigger() / toggle() / choice() in the graph,
-- in declaration order, including those inside subgraphs. Used to filter
-- stored params before re-applying them to a new source node (setControl on
-- an unknown name would abort the whole swap bundle).
fn scanControls(g SignalGraph) [String] {
    var out = [String]();
    g _scanControlsInto(out);
    out
}

fn _scanControlsInto(g SignalGraph, out [String]) Void {
    for (e : g.exprs) {
        match (e.kind) {
            control(spec, ch, nm, k): out push!(nm);
            voicer(v, sub): sub _scanControlsInto(out);
            if_(a, b): { a _scanControlsInto(out); b _scanControlsInto(out); }
            for_(c, sub): sub _scanControlsInto(out);
            switch_(gs): for (sg : gs) { sg _scanControlsInto(out); }
            spectralChain(fs, hs, sub): sub _scanControlsInto(out);
            _: 0;
        }
    }
}

-- noteParam names and default values (spec.init) in declaration order,
-- excluding "gate": the compilers pin gate to param slot 0 and noteOn
-- params map positionally onto the REMAINING noteParams in declaration
-- order (shared/tzpl_voicer.hpp writes incoming params starting at row 1).
-- This is exactly a music.score Voice's names/defaults.
fn scanNoteParams(g SignalGraph) ([Symbol], [Float]) {
    var names = [Symbol]();
    var defaults = [Float]();
    g _scanNoteParamsInto(names, defaults);
    (names, defaults)
}

fn _scanNoteParamsInto(g SignalGraph, names [Symbol], defaults [Float]) Void {
    for (e : g.exprs) {
        match (e.kind) {
            noteParam(spec, ch, nm): {
                if (nm != "gate") {
                    names push!(nm toSymbol);
                    defaults push!(spec.init);
                }
            }
            voicer(v, sub): sub _scanNoteParamsInto(names, defaults);
            if_(a, b): {
                a _scanNoteParamsInto(names, defaults);
                b _scanNoteParamsInto(names, defaults);
            }
            for_(c, sub): sub _scanNoteParamsInto(names, defaults);
            switch_(gs): for (sg : gs) { sg _scanNoteParamsInto(names, defaults); }
            spectralChain(fs, hs, sub): sub _scanNoteParamsInto(names, defaults);
            _: 0;
        }
    }
}

---------------------------------------------------------------------------
-- Quantization

-- The first beat strictly after `now` that lies on a `q`-beat boundary;
-- `q <= 0` means no quantization (fire now).
fn nextQuant(now Float, q Float) Float =
    q <= 0.0 ? now : ((now / q) floor + 1.0) * q;

---------------------------------------------------------------------------
-- Swap choreography as data.
--
-- PxOp mirrors the subset of engine commands the proxy layer emits. The
-- crossfade curve is deliberately absent: it is uniform per operation and
-- supplied by live.proxy when lowering to a Bundle.

enum PxOp {
    opNewNode (String, Int),                    -- defName, nodeID
    opFreeNode Int,                             -- nodeID
    opSetControl (Int, String, Float),          -- node, control name, value
    opSetInput (Int, Int, Float),               -- node, port, value
    opSetInputX (Int, Int, Float, Float),       -- node, port, value, fade
    opConnect (Int, Int, Int, Int),             -- src, srcPort, dst, dstPort
    opConnectX (Int, Int, Int, Int, Float),     -- ... + fade
    opDisconnectSourceX (Int, Int, Int, Int, Float),
}

-- The core redefine: bring up the new source silently (params + reference
-- wiring first), fade it INTO the anchor's fan-in, and only then fade the
-- old source OUT -- the engine requires new links to exist before old ones
-- break, and ordering within one atomic bundle satisfies that.
--   refWires: (referenced proxy's anchor node, dst port on the new source)
--   params:   stored values already filtered against the new def's controls
fn swapOps(defName String, newID Int, oldSrc Int, anchor Int,
           refWires [(Int, Int)], params [(String, Float)],
           fade Float) [PxOp] {
    var ops = [PxOp]();
    ops push!(PxOp.opNewNode(defName, newID));
    for (p : params) {
        ops push!(PxOp.opSetControl(newID, p.0, p.1));
    }
    for (w : refWires) {
        ops push!(PxOp.opConnect(w.0, 0, newID, w.1));
    }
    ops push!(PxOp.opConnectX(newID, 0, anchor, 0, fade));
    if (oldSrc != 0) {
        ops push!(PxOp.opDisconnectSourceX(oldSrc, 0, anchor, 0, fade));
    }
    ops
}

-- First play: create the monitor, set its volume inlet, wire
-- anchor -> monitor, and fade the monitor into Audio Out's fan-in.
fn playOps(monDef String, monID Int, anchor Int, vol Float, fade Float) [PxOp] {
    var ops = [PxOp]();
    ops push!(PxOp.opNewNode(monDef, monID));
    ops push!(PxOp.opSetInput(monID, 1, vol));
    ops push!(PxOp.opConnect(anchor, 0, monID, 0));
    ops push!(PxOp.opConnectX(monID, 0, 0, 0, fade));
    ops
}

-- Stop listening: remove only this proxy's monitor from Audio Out's fan-in.
-- The monitor node stays (unreached = zero cost) so replay is instant.
fn stopOps(monID Int, fade Float) [PxOp] =
    [PxOp.opDisconnectSourceX(monID, 0, 0, 0, fade)];

-- Fade the current source out of the anchor. The caller frees it after the
-- fade (the engine sends no fade-completion event, so cleanup is timed).
fn silenceOps(src Int, anchor Int, fade Float) [PxOp] =
    [PxOp.opDisconnectSourceX(src, 0, anchor, 0, fade)];

---------------------------------------------------------------------------
-- Printing (for tests and dump())

fn toString(op PxOp) String {
    match (op) {
        opNewNode(def, id): "newNode(%^, %^)" fmt(def, id);
        opFreeNode(id): "freeNode(%^)" fmt(id);
        opSetControl(n, nm, v): "setControl(%^, %^, %^)" fmt(n, nm, v);
        opSetInput(n, p, v): "setInput(%^, %^, %^)" fmt(n, p, v);
        opSetInputX(n, p, v, f): "setInputX(%^, %^, %^, %^)" fmt(n, p, v, f);
        opConnect(s, sp, d, dp): "connect(%^, %^, %^, %^)" fmt(s, sp, d, dp);
        opConnectX(s, sp, d, dp, f):
            "connectX(%^, %^, %^, %^, %^)" fmt(s, sp, d, dp, f);
        opDisconnectSourceX(s, sp, d, dp, f):
            "disconnectSourceX(%^, %^, %^, %^, %^)" fmt(s, sp, d, dp, f);
    }
}

fn opsToString(ops [PxOp]) String {
    var out = "";
    for (op : ops) { out = out $ op toString $ "\n"; }
    out
}
