-- live/proxy.x -- handle-based node proxies for live coding (JITLib-style).
--
-- A Proxy is a persistent, symbolic connection point in a silo's signal
-- graph. The user writes signal functions and refers to other proxies as
-- values; this module owns every node ID, connect/disconnect, bundle,
-- crossfade, and deferred cleanup.
--
--     import live.*;
--
--     -- create once:
--     let drone = ndef(2);
--     let verb = ndef(2);
--
--     -- redefine freely; each assignment crossfades old -> new:
--     drone <- fn() S { sinosc([220.0, 220.7]) * 0.2 };
--     verb <- fn() S { drone() reverb(3.5, 0.4, 0.5) };
--     verb play;
--
--     drone set("freq", 165.0);     -- controls, re-applied across redefines
--     verb amp(0.5);                -- monitor volume (dependents unaffected)
--     endAll(2.0);                  -- fade everything out and free it
--
-- Topology: each proxy owns a hidden passthrough "anchor" node. The anchor
-- inlet is the summing point (engine fan-in mixers sum multiple sources for
-- free) and the anchor outlet is the stable output dependents connect to,
-- so redefines never rewire readers. play() adds a separate monitor node
-- (anchor -> monitor -> Audio Out) whose volume inlet only affects what you
-- hear. Nodes unreachable from Audio Out cost nothing, so an unplayed,
-- unreferenced proxy is free.
--
-- Identity is the handle: re-running `let p = ndef(...)` makes a NEW proxy
-- (the old one keeps sounding until clearAll()). The live-coding pattern is
-- create-once, then redefine through `<-`.
--
-- Requires the audio engine bridge (tzpl_app); not loadable under plain tzpl.

export live.plan.*;

import synthdef.*;
import synthc.compile.*;
import std.result.*;
import bundles.*;
import audio_engine.*;
import clock.*;

---------------------------------------------------------------------------
-- Types

struct ProxyState {
    chans Int,
    anchor Int,                 -- anchor node id, 0 until created
    monitor Int,                -- monitor node id, 0 until first play
    src Int,                    -- current source node id, 0 = silent
    playing Bool,
    building Bool,              -- a compile is in flight (serializes redefines)
    pending [GraphFn],          -- newest redefine queued during a build (0/1)
    params [String: Float],     -- last-set values, re-applied on swap
    ctlNames [String],          -- control names of the current def
    noteNames [Symbol],         -- noteParam names (sans gate), declaration order
    noteDefaults [Float],       -- noteParam spec.init values
    fadeTime Float,             -- per-proxy override; < 0 = use silo default
    vol Float,                  -- last monitor volume (survives reshape)
    reads [(Int, Int)],         -- (referenced proxy serial, own inlet port)
    tap Int,                    -- meter tap id, 0 = no meter
}

struct Proxy {
    silo Int,
    serial Int,
    state Ref<ProxyState>,
}

struct SiloDefaults {
    fadeTime Float,
    curve FadeCurve,
    quant Float,                -- beats; 0 = off
    clock Int,
}

---------------------------------------------------------------------------
-- Module state. Top-level vars are snapshotted into functions by value, so
-- shared state lives behind Refs or in heap containers that are mutated in
-- place and never reassigned.

-- Proxy node ids start far above hand-written scripts and synthdef.play's
-- 1000+ range. Every source generation takes a FRESH id (freed ids stay
-- taken until the free executes, so ids are never reused).
let _nextNodeID = &1000000;
fn _takeNodeID() Int {
    let id = *_nextNodeID;
    _nextNodeID <- id + 1;
    id
}

let _nextSerial = &0;
fn _takeSerial() Int {
    let s = *_nextSerial;
    _nextSerial <- s + 1;
    s
}

var _defaults [Int: Ref<SiloDefaults>] = [:];  -- silo -> defaults, put! in place
var _registry = [Proxy]();                     -- every live proxy, in place
var _anchorDefsReady [Int: Bool] = [:];        -- chans -> anchor def loaded
var _monDefsReady [Int: Bool] = [:];           -- chans -> monitor def loaded

fn _siloDefaults(silo Int) Ref<SiloDefaults> {
    match (_defaults[silo]) {
        some(r): return r;
        none: 0;
    }
    let r = &SiloDefaults { fadeTime: 1.0, curve: FadeCurve.fadeEqualPower,
                            quant: 0.0, clock: 0 };
    _defaults[silo] = r;
    r
}

fn _fade(p Proxy) Float {
    let s = *p.state;
    s.fadeTime >= 0.0 ? s.fadeTime : (*_siloDefaults(p.silo)).fadeTime
}

fn _curve(p Proxy) FadeCurve = (*_siloDefaults(p.silo)).curve;

---------------------------------------------------------------------------
-- Silo-wide settings

-- Default crossfade time (seconds) for redefines/play/stop in a silo.
fn fadeTime(t Float, silo Int = 0) Void {
    let r = _siloDefaults(silo);
    r <- SiloDefaults { ...(*r), fadeTime: t max(0.0) };
}

-- Per-proxy override; a negative value returns to the silo default.
fn fadeTime(p Proxy, t Float) Void {
    p.state <- ProxyState { ...(*p.state), fadeTime: t };
}

fn fadeCurve(c FadeCurve, silo Int = 0) Void {
    let r = _siloDefaults(silo);
    r <- SiloDefaults { ...(*r), curve: c };
}

-- Land redefines (and play/stop) on a beat boundary of the silo's clock;
-- 0 turns quantization off.
fn quant(beats Float, silo Int = 0) Void {
    let r = _siloDefaults(silo);
    r <- SiloDefaults { ...(*r), quant: beats max(0.0) };
}

fn quantClock(clock Int, silo Int = 0) Void {
    let r = _siloDefaults(silo);
    r <- SiloDefaults { ...(*r), clock: clock };
}

---------------------------------------------------------------------------
-- Creating proxies

-- A silent placeholder proxy: no def, no nodes yet. Define it with `<-`.
fn ndef(chans Int = 2, silo Int = 0) Proxy {
    var params [String: Float] = [:];
    let st = &ProxyState {
        chans: chans asChans, anchor: 0, monitor: 0, src: 0,
        playing: false, building: false, pending: [GraphFn](),
        params: params, ctlNames: [String](), noteNames: [Symbol](),
        noteDefaults: [Float](), fadeTime: -1.0,
        vol: 1.0, reads: [(Int, Int)](), tap: 0,
    };
    let p = Proxy { silo: silo, serial: _takeSerial(), state: st };
    _registry push!(p);
    p
}

-- Create and define in one step (fire-and-forget; compile errors print).
fn ndef(f GraphFn, chans Int = 2, silo Int = 0) Proxy {
    let p = ndef(chans, silo);
    define(p, f);
    p
}

---------------------------------------------------------------------------
-- Symbol-keyed proxies: opt-in idempotent upsert.
--
-- ndef('drone, f) looks the name up first: re-running the same cell finds
-- the existing proxy and crossfades it (JITLib-style) instead of minting a
-- new one, so whole-cell re-runs are safe. Anonymous ndef() stays available
-- for throwaway proxies. free()/clearAll() release the name.

var _named [Symbol: Proxy] = [:];

fn ndef(name Symbol, chans Int = 2, silo Int = 0) Proxy {
    match (_named[name]) {
        some(p): return p;
        none: 0;
    }
    let p = ndef(chans, silo);
    _named[name] = p;
    p
}

fn ndef(name Symbol, f GraphFn, chans Int = 2, silo Int = 0) Proxy {
    let p = ndef(name, chans, silo);
    define(p, f);
    p
}

fn _forgetName(p Proxy) Void {
    var stale = [Symbol]();
    for (k : _named keys) {
        match (_named[k]) {
            some(q): if (q.serial == p.serial) { stale push!(k); }
            none: 0;
        }
    }
    for (k : stale) { _named remove!(k); }
}

---------------------------------------------------------------------------
-- Referencing a proxy inside a graph function.
--
-- sig() emits an inlet on the def being built and records which proxy it
-- refers to; after the swap the module wires that proxy's anchor outlet
-- into the port. `drone()` is sugar for `drone sig` via the call protocol.
-- Only valid while a graph function passed to ndef/`<-` is being built, and
-- only at the top level (not inside voicer/if_/for_ bodies).

fn sig(p Proxy) S {
    let idx = `pxRefs length;
    `pxRefs = `pxRefs push(p);
    inlet(FLOAT32, (*p.state).chans, pxRefName(idx))
}

fn call(p Proxy) S = p sig;

---------------------------------------------------------------------------
-- Defining / redefining

-- Redefine: crossfade the running source to a new one built from f.
fn <-(p Proxy, f GraphFn) Void { define(p, f); }
fn ->(f GraphFn, p Proxy) Void { define(p, f); }

-- Bulk param set: p <- ["freq": 165.0, "cutoff": 800.0];
fn <-(p Proxy, ps [String: Float]) Void {
    for (k : ps keys) { set(p, k, get(ps, k, 0.0)); }
}

-- Awaitable form of `<-` for scripts and tests. While a build is in
-- flight, newer redefines queue (only the newest survives) and resolve to
-- ok("queued").
fn define(p Proxy, f GraphFn) Future<Result<String, String>> = _defineAsync(p, f);

fn _isRegistered(p Proxy) Bool {
    for (q : _registry) { if (q.serial == p.serial) { return true; } }
    false
}

async fn _defineAsync(p Proxy, f GraphFn) Result<String, String> {
    -- a freed handle being deliberately redefined re-enters the registry
    if (!(p _isRegistered)) { _registry push!(p); }
    if ((*p.state).building) {
        let s = *p.state;
        s.pending clear!;
        s.pending push!(f);
        return Result<String, String>.ok("queued");
    }
    p.state <- ProxyState { ...(*p.state), building: true };
    let result = _buildAndSwap(p, f) await;
    p.state <- ProxyState { ...(*p.state), building: false };
    match (result) {
        ok(m): 0;
        err(msg): println("live: redefine failed: " $ msg);
    }
    let s2 = *p.state;
    if (s2.pending length > 0) {
        let nf = s2.pending pop!;
        define(p, nf);
    }
    result
}

async fn _buildAndSwap(p Proxy, f GraphFn) Result<String, String> {
    -- Build the graph with a fresh proxy-reference collector in dynamic
    -- scope; sig() calls during f() append to it.
    var `pxRefs [Proxy] = [];
    let g = makeGraph(wrapGraphFn(f));
    let refs = `pxRefs;

    if (g countNestedRefs > 0) {
        return Result<String, String>.err(
            "proxy references (sig) inside voicer/if_/for_/switch bodies are not supported");
    }
    let refPorts = g scanRefPorts;      -- [(port, ref sequence)]
    let ctls = g scanControls;
    let (noteNames, noteDefaults) = g scanNoteParams;

    -- Compile + hot-load. One def name per proxy: the engine refcounts
    -- superseded defs, so the running source keeps its old code and only
    -- the replacement node picks up the new build.
    let defName = pxDefName(p.silo, p.serial);
    match (defSynthGraphChecked(g, defName, ["proxy"]) await) {
        err(msg): return Result<String, String>.err(msg);
        ok(cpp): 0;
    }

    -- the proxy was freed while the compile ran: the def loaded harmlessly,
    -- but no nodes should be built for it
    if (!(p _isRegistered)) {
        return Result<String, String>.ok("abandoned (proxy was freed)");
    }

    -- Anchors: this proxy's own, then every referenced proxy's.
    if (!(_ensureAnchor(p) await)) {
        return Result<String, String>.err("anchor creation failed");
    }
    for (rp : refs) {
        if (rp.silo != p.silo) {
            return Result<String, String>.err(
                "cannot reference a proxy in another silo (no cross-silo wires)");
        }
        if (!(_ensureAnchor(rp) await)) {
            return Result<String, String>.err("referenced proxy's anchor creation failed");
        }
    }

    -- Stored params filtered against the new def's controls; values whose
    -- control vanished are kept for later definitions but not applied.
    var params = [(String, Float)]();
    let stored = (*p.state).params;
    for (k : stored keys) {
        if (ctls contains(k)) {
            params push!((k, get(stored, k, 0.0)));
        } else {
            println("live: stored param \"" $ k $ "\" has no control in the new definition (kept, not applied)");
        }
    }

    -- Reference wiring: (referenced proxy's anchor, port on the new source),
    -- plus the durable (serial, port) record reshape uses to find dependents.
    var wires = [(Int, Int)]();
    var reads = [(Int, Int)]();
    for (rp2 : refPorts) {
        let target = refs[rp2.1];
        wires push!(((*target.state).anchor, rp2.0));
        reads push!((target.serial, rp2.0));
    }

    let old = (*p.state).src;
    let newID = _takeNodeID();
    let fade = _fade(p);
    let d = *_siloDefaults(p.silo);
    let e = _runOps(swapOps(defName, newID, old, (*p.state).anchor,
                            wires, params, fade), p.silo, d);
    if (e != 0) {
        return Result<String, String>.err("swap bundle rejected, engine error " $ e toString);
    }
    p.state <- ProxyState { ...(*p.state), src: newID, ctlNames: ctls,
                            noteNames: noteNames, noteDefaults: noteDefaults,
                            reads: reads };
    if (old != 0) {
        _freeLater(p.silo, old, fade + 0.25 + _quantDelaySecs(d));
    }
    Result<String, String>.ok(defName)
}

---------------------------------------------------------------------------
-- Listening

-- Route the proxy to Audio Out through its monitor, fading in. vol only
-- scales what you hear; proxies reading this one get the unscaled signal.
-- Returns a Future (engine Err code) -- discard it live, await it in
-- scripts that need the monitor up before proceeding.
fn play(p Proxy, vol Float = 1.0) Future<Int> = _playAsync(p, vol);

async fn _playAsync(p Proxy, vol Float) Int {
    if (!(_ensureAnchor(p) await)) { return -1; }
    let s = *p.state;
    let d = *_siloDefaults(p.silo);
    if (s.playing && s.monitor != 0) {
        return bundle() setInputX(s.monitor, 1, vol, 0.05, d.curve) go(p.silo);
    }
    var mon = s.monitor;
    var e = 0;
    if (mon == 0) {
        if (!(_ensureMonDef(s.chans) await)) { return -1; }
        mon = _takeNodeID();
        e = _runOps(playOps(monitorDefName(s.chans), mon, (*p.state).anchor,
                            vol, _fade(p)), p.silo, d);
    } else {
        -- monitor survives stop(); refresh volume and fade back in
        e = _runOps([PxOp.opSetInput(mon, 1, vol),
                     PxOp.opConnectX(mon, 0, 0, 0, _fade(p))], p.silo, d);
    }
    if (e == 0) {
        p.state <- ProxyState { ...(*p.state), monitor: mon, playing: true,
                                vol: vol };
    } else {
        println("live: play failed, engine error " $ e toString);
    }
    e
}

-- Fade this proxy's monitor out of Audio Out. Other playing proxies are
-- untouched; the monitor node stays for instant replay.
fn stop(p Proxy) Void {
    let s = *p.state;
    if (!s.playing || s.monitor == 0) { return; }
    let d = *_siloDefaults(p.silo);
    _runOps(stopOps(s.monitor, _fade(p)), p.silo, d);
    p.state <- ProxyState { ...(*p.state), playing: false };
}

-- Monitor volume, ramped over `fade` seconds.
fn amp(p Proxy, v Float, fade Float = 0.05) Void {
    let s = *p.state;
    p.state <- ProxyState { ...s, vol: v };
    if (s.monitor == 0) { return; }
    bundle() setInputX(s.monitor, 1, v, fade, _curve(p)) go(p.silo);
}

---------------------------------------------------------------------------
-- Params

-- Set a control by name on the current source. The value is stored on the
-- proxy and re-applied after every redefine (as long as the new definition
-- declares a control of that name).
fn set(p Proxy, name String, value Float) Void {
    let s = *p.state;
    let stored = s.params;
    stored[name] = value;
    if (s.src != 0 && s.ctlNames contains(name)) {
        bundle() setControl(s.src, name, value) go(p.silo);
    }
}

---------------------------------------------------------------------------
-- Reshaping

-- Change the proxy's channel count in place. A new anchor at the new width
-- takes over: the source feeds it, and every dependent wire and the monitor
-- crossfade from the old anchor to the new one (both carry the same signal
-- during the fade). Dependents' own definitions keep their old reference
-- width until their next redefine; the engine adapts the mismatch
-- (power-of-two wrap/fold) in the meantime.
fn reshape(p Proxy, chans Int) Future<Int> = _reshapeAsync(p, chans);

async fn _reshapeAsync(p Proxy, chans Int) Int {
    let newChans = chans asChans;
    let s = *p.state;
    if (newChans == s.chans) { return 0; }
    if (s.anchor == 0) {
        -- nothing built yet: just adopt the new width
        p.state <- ProxyState { ...(*p.state), chans: newChans };
        return 0;
    }
    if (!(_ensureAnchorDef(newChans) await)) { return -1; }
    var monDef = "";
    var newMon = 0;
    if (s.playing && s.monitor != 0) {
        if (!(_ensureMonDef(newChans) await)) { return -1; }
        monDef = monitorDefName(newChans);
        newMon = _takeNodeID();
    }

    -- every live wire currently fed by the old anchor outlet
    var depWires = [(Int, Int)]();
    for (q : _registry) {
        let qs = *q.state;
        if (q.silo == p.silo && qs.src != 0) {
            for (r : qs.reads) {
                if (r.0 == p.serial) { depWires push!((qs.src, r.1)); }
            }
        }
    }

    let cur = *p.state;                 -- re-read after the awaits
    let newAnchor = _takeNodeID();
    let fade = _fade(p);
    let d = *_siloDefaults(p.silo);
    let e = _runOps(reshapeOps(anchorDefName(newChans), newAnchor, cur.anchor,
                               cur.src, depWires, monDef, newMon, cur.monitor,
                               cur.vol, fade), p.silo, d);
    if (e != 0) {
        println("live: reshape failed, engine error " $ e toString);
        return e;
    }
    -- a stopped monitor is kept for instant replay; point it at the new
    -- anchor (it is silent, so this rewire is inaudible)
    if (!cur.playing && cur.monitor != 0) {
        bundle() connect(newAnchor, 0, cur.monitor, 0) go(p.silo);
    }
    let margin = fade + 0.25 + _quantDelaySecs(d);
    _freeLater(p.silo, cur.anchor, margin);
    if (newMon != 0 && cur.monitor != 0) { _freeLater(p.silo, cur.monitor, margin); }
    p.state <- ProxyState { ...(*p.state), chans: newChans, anchor: newAnchor,
                            monitor: newMon != 0 ? newMon : cur.monitor };
    -- move the meter to the new anchor
    if (cur.tap != 0) {
        begin(); untap(cur.tap); go(p.silo);
        let tid = allocTapID();
        begin(); tapOutlet(newAnchor, 0, tid, TapMode.tapMeter ordinal); go(p.silo);
        p.state <- ProxyState { ...(*p.state), tap: tid };
    }
    0
}

---------------------------------------------------------------------------
-- Meters
--
-- A meter taps the anchor outlet, so it reads what dependents and the
-- monitor receive (pre-volume). Peak/rms report signal only while the
-- proxy is reachable from Audio Out (played, or read by a played proxy) --
-- an unreachable proxy does not run at all.

fn meter(p Proxy) Future<Int> = _meterAsync(p);

async fn _meterAsync(p Proxy) Int {
    let s = *p.state;
    if (s.tap != 0) { return s.tap; }
    if (!(_ensureAnchor(p) await)) { return 0; }
    let tid = allocTapID();
    begin();
    tapOutlet((*p.state).anchor, 0, tid, TapMode.tapMeter ordinal);
    let e = go(p.silo);
    if (e != 0) {
        println("live: meter failed, engine error " $ e toString);
        return 0;
    }
    p.state <- ProxyState { ...(*p.state), tap: tid };
    tid
}

fn peak(p Proxy) Float {
    let t = (*p.state).tap;
    t == 0 ? 0.0 : tapPeak(t)
}

fn rms(p Proxy) Float {
    let t = (*p.state).tap;
    t == 0 ? 0.0 : tapRms(t)
}

fn unmeter(p Proxy) Void {
    let t = (*p.state).tap;
    if (t == 0) { return; }
    begin(); untap(t); go(p.silo);
    p.state <- ProxyState { ...(*p.state), tap: 0 };
}

---------------------------------------------------------------------------
-- Lifecycle

-- Fade the source out and free it; the proxy stays (anchor, monitor,
-- stored params) and can be redefined.
fn silence(p Proxy) Void {
    let s = *p.state;
    if (s.src == 0) { return; }
    let d = *_siloDefaults(p.silo);
    _runOps(silenceOps(s.src, s.anchor, _fade(p)), p.silo, d);
    _freeLater(p.silo, s.src, _fade(p) + 0.25 + _quantDelaySecs(d));
    p.state <- ProxyState { ...(*p.state), src: 0 };
}

-- Fade out and free everything the proxy owns (source, monitor, anchor)
-- and unregister it. Proxies that referenced this one keep running; their
-- reference inlet falls back to silence. The handle can be redefined later,
-- which re-creates its nodes from scratch.
fn free(p Proxy) Void {
    let s = *p.state;
    let d = *_siloDefaults(p.silo);
    let fade = _fade(p);
    if (s.tap != 0) {
        begin(); untap(s.tap); go(p.silo);
    }
    if (s.playing && s.monitor != 0) {
        _runOps(stopOps(s.monitor, fade), p.silo, d);
    }
    if (s.src != 0) {
        _runOps(silenceOps(s.src, s.anchor, fade), p.silo, d);
    }
    let margin = fade + 0.25 + _quantDelaySecs(d);
    if (s.src != 0) { _freeLater(p.silo, s.src, margin); }
    if (s.monitor != 0) { _freeLater(p.silo, s.monitor, margin); }
    if (s.anchor != 0) { _freeLater(p.silo, s.anchor, margin); }
    p.state <- ProxyState { ...(*p.state), src: 0, monitor: 0, anchor: 0,
                            playing: false, tap: 0 };
    _unregister(p);
    _forgetName(p);
}

-- Free every registered proxy in the silo -- the recovery for orphaned
-- proxies whose handles were rebound. Never touches non-proxy nodes.
fn clearAll(silo Int = 0) Void {
    -- filter first: free() unregisters, so don't iterate _registry itself
    _registry filter(fn(q Proxy) Bool = q.silo == silo) free;
}

-- Fade every playing proxy out over `fade` seconds, then free everything.
-- Awaitable so scripts can hold the engine open until the fade completes.
fn endAll(fade Float, silo Int = 0) Future<Void> = _endAllAsync(fade, silo);

async fn _endAllAsync(fade Float, silo Int) Void {
    let d = *_siloDefaults(silo);
    for (q : _registry) {
        let s = *q.state;
        if (q.silo == silo && s.playing && s.monitor != 0) {
            _runOps(stopOps(s.monitor, fade), silo, d);
            q.state <- ProxyState { ...(*q.state), playing: false };
        }
    }
    delayReal(fade + 0.3) await;
    clearAll(silo);
}

fn _unregister(p Proxy) Void {
    let keep = _registry filter(fn(q Proxy) Bool = q.serial != p.serial);
    _registry clear! append!(keep);
}

---------------------------------------------------------------------------
-- Introspection

fn dump(silo Int = 0) String {
    var out = "";
    for (q : _registry) {
        if (q.silo == silo) {
            let s = *q.state;
            out = out $ "px" $ q.serial toString
                $ ": chans=" $ s.chans toString
                $ " anchor=" $ s.anchor toString
                $ " src=" $ s.src toString
                $ " monitor=" $ s.monitor toString
                $ (s.playing ? " playing" : "")
                $ "\n";
        }
    }
    out
}

---------------------------------------------------------------------------
-- Internals: def caching, op lowering, timed cleanup

-- Compile-and-load the shared anchor def for a channel count, once per
-- session. (Two proxies racing on the same count just compile it twice;
-- the second load supersedes the first, harmlessly.)
async fn _ensureAnchorDef(chans Int) Bool {
    if (get(_anchorDefsReady, chans, false)) { return true; }
    let g = makeGraph(fn() S { inlet(FLOAT32, chans, "in") outlet });
    match (defSynthGraphChecked(g, anchorDefName(chans), ["proxy"]) await) {
        err(msg): {
            println("live: anchor def failed: " $ msg);
            return false;
        }
        ok(cpp): 0;
    }
    _anchorDefsReady[chans] = true;
    true
}

async fn _ensureMonDef(chans Int) Bool {
    if (get(_monDefsReady, chans, false)) { return true; }
    let g = makeGraph(fn() S {
        (inlet(FLOAT32, chans, "in") * inlet(FLOAT32, 1, "vol")) outlet
    });
    match (defSynthGraphChecked(g, monitorDefName(chans), ["proxy"]) await) {
        err(msg): {
            println("live: monitor def failed: " $ msg);
            return false;
        }
        ok(cpp): 0;
    }
    _monDefsReady[chans] = true;
    true
}

-- Create the proxy's anchor node if it does not exist yet.
async fn _ensureAnchor(p Proxy) Bool {
    if ((*p.state).anchor != 0) { return true; }
    let chans = (*p.state).chans;
    if (!(_ensureAnchorDef(chans) await)) { return false; }
    if ((*p.state).anchor != 0) { return true; }   -- raced with another op
    let a = _takeNodeID();
    let e = bundle() newNode(anchorDefName(chans), a) go(p.silo);
    if (e != 0) {
        println("live: anchor node failed, engine error " $ e toString);
        return false;
    }
    p.state <- ProxyState { ...(*p.state), anchor: a };
    true
}

-- Lower a PxOp sequence to one atomic bundle and submit it -- immediately,
-- or on the silo's quant boundary when quantization is on.
fn _runOps(ops [PxOp], silo Int, d SiloDefaults) Int {
    var b = bundle();
    for (op : ops) {
        match (op) {
            opNewNode(def, id): b = b newNode(def, id);
            opFreeNode(id): b = b freeNode(id);
            opSetControl(n, nm, v): b = b setControl(n, nm, v);
            opSetInput(n, prt, v): b = b setInput(n, prt, v);
            opSetInputX(n, prt, v, x): b = b setInputX(n, prt, v, x, d.curve);
            opConnect(sn, sp, dn, dp): b = b connect(sn, sp, dn, dp);
            opConnectX(sn, sp, dn, dp, x): b = b connectX(sn, sp, dn, dp, x, d.curve);
            opDisconnectSourceX(sn, sp, dn, dp, x):
                b = b disconnectSourceX(sn, sp, dn, dp, x, d.curve);
        }
    }
    d.quant <= 0.0
        ? b go(silo)
        : b sched(silo, d.clock, nextQuant(getBeats(d.clock) + getLatency(), d.quant))
}

-- Seconds until a quantized bundle would fire (added to free-later margins).
fn _quantDelaySecs(d SiloDefaults) Float {
    if (d.quant <= 0.0) { return 0.0; }
    let now = getBeats(d.clock);
    let target = nextQuant(now + getLatency(), d.quant);
    (target - now) * 60.0 / getTempo(d.clock)
}

-- The engine sends no fade-completion event, so freeing a faded-out node is
-- timed client-side. A stale free (node already gone) is a harmless error.
async fn _freeLater(silo Int, id Int, secs Float) Void {
    delayReal(secs) await;
    bundle() freeNode(id) go(silo);
}
