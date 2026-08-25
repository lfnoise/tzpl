-- live_proxy_check.x -- functional checks for live.proxy without audio.
-- The engine is never started, so every bundle executes synchronously
-- inline and is validated against the real silo graph: a bad wiring
-- sequence fails loudly here. Compiles run on the async worker; each
-- define is awaited.

import std.test.*;
import std.result.*;
import synthdef.*;
import common_ugens.*;
import audio_engine.*;
import live.proxy.*;

fn okOr(r Result<String, String>, label String) Bool {
    match (r) {
        ok(m): return assertTrue(true, label);
        err(e): return assertTrue(false, label $ " -- " $ e);
    }
    false
}

-- create: no nodes yet
let drone = ndef(2);
assertEq((*drone.state).anchor, 0, "placeholder has no anchor");
assertEq((*drone.state).src, 0, "placeholder has no source");

-- first definition: anchor + source appear
okOr(define(drone, fn() S {
    let f = control("freq", espec(40.0, 800.0, 110.0));
    sinosc(f) * 0.2
}) await, "first define compiles and swaps");
let src1 = (*drone.state).src;
assertTrue((*drone.state).anchor != 0, "anchor created");
assertTrue(src1 != 0, "source created");
assertEq((*drone.state).ctlNames length, 1, "one control scanned");

-- set stores and applies
drone set("freq", 165.0);
assertNear(get((*drone.state).params, "freq", 0.0), 165.0, 0.0001,
           "param stored");

-- map-assignment sugar dispatches to set
drone <- ["freq": 120.0];
assertNear(get((*drone.state).params, "freq", 0.0), 120.0, 0.0001,
           "<- map stores params");

-- redefine: fresh source node, stored param re-applied (freq exists)
okOr(define(drone, fn() S {
    let f = control("freq", espec(40.0, 800.0, 220.0));
    sinosc([1.0, 1.5] * f) sum(2) * 0.1
}) await, "redefine compiles and swaps");
let src2 = (*drone.state).src;
assertTrue(src2 != 0 && src2 != src1, "redefine made a fresh source node");

-- dependent proxy referencing drone by handle
let verb = ndef(2);
okOr(define(verb, fn() S {
    (drone() * 0.5) + (drone sig * 0.25)
}) await, "dependent define with two refs wires up");
assertTrue((*verb.state).src != 0, "dependent source created");

-- redefining the source does not disturb the dependent
okOr(define(drone, fn() S { sinosc(330.0) * 0.1 }) await,
     "source redefine under a live dependent");

-- stored param's control vanished in the latest def: kept, not applied
drone set("freq", 90.0);
assertNear(get((*drone.state).params, "freq", 0.0), 90.0,
           0.0001, "param kept after control vanished");

-- play/stop/amp through the monitor
assertEq(play(verb, 0.8) await, 0, "play succeeds");
assertTrue((*verb.state).playing, "playing flag set");
assertTrue((*verb.state).monitor != 0, "monitor created");
verb amp(0.5);
verb stop;
assertFalse((*verb.state).playing, "stopped");
assertEq(play(verb) await, 0, "replay through existing monitor");

-- self-reference builds a legal feedback loop (one-sample delay)
let fb = ndef(1);
okOr(define(fb, fn() S { (fb() * 0.5) + sinosc(200.0) * 0.1 }) await,
     "self-reference (feedback) defines");

-- nested sig is rejected with a clear error, sound untouched
let bad = define(drone, fn() S {
    voicer(4, fn() S { drone() * gate() }) sum
}) await;
match (bad) {
    ok(m): assertTrue(false, "nested sig should be rejected");
    err(e): assertTrue(true, "nested sig rejected");
}
assertTrue((*drone.state).src != 0, "failed redefine keeps old source");

-- silence: source freed later, anchor stays, proxy still defined-able
drone silence;
assertEq((*drone.state).src, 0, "silenced source cleared");
assertTrue((*drone.state).anchor != 0, "anchor survives silence");
okOr(define(drone, fn() S { sinosc(440.0) * 0.05 }) await,
     "define after silence");

-- symbol-keyed ndef: idempotent upsert
let n1 = ndef('utest, 2);
let n2 = ndef('utest, 2);
assertEq(n1.serial, n2.serial, "symbol ndef is idempotent");
okOr(define(n1, fn() S { sinosc(150.0) * 0.1 }) await, "named proxy defines");
let n3 = ndef('utest, fn() S { sinosc(151.0) * 0.1 });
assertEq(n3.serial, n1.serial, "symbol ndef(f) reuses the proxy");
n1 free;
let n4 = ndef('utest, 2);
assertTrue(n4.serial != n1.serial, "freed name creates a fresh proxy");
n4 free;

-- meters tap the anchor outlet
let mp = ndef(2);
okOr(define(mp, fn() S { sinosc(200.0) * 0.1 }) await, "meter proxy defines");
let tid = meter(mp) await;
assertTrue(tid > 0, "meter installs a tap");
assertTrue(tapExists(tid), "tap exists");
assertEq(meter(mp) await, tid, "meter is idempotent");
assertTrue(mp peak >= 0.0, "peak readable");
assertTrue(mp rms >= 0.0, "rms readable");
mp unmeter;
assertFalse(tapExists(tid), "unmeter removes the tap");
mp free;

-- reshape under a live dependent
let w = ndef(1);
okOr(define(w, fn() S { sinosc(300.0) * 0.1 }) await, "mono proxy defines");
let dep2 = ndef(2);
okOr(define(dep2, fn() S { w() * 0.5 }) await, "dependent of mono defines");
let oldAnchor = (*w.state).anchor;
assertEq(reshape(w, 4) await, 0, "reshape succeeds");
assertEq((*w.state).chans, 4, "chans updated");
assertTrue((*w.state).anchor != oldAnchor, "reshape made a new anchor");
okOr(define(w, fn() S { sinosc([301.0, 302.0, 303.0, 304.0]) * 0.05 }) await,
     "redefine at the new width");
assertEq(reshape(w, 4) await, 0, "reshape to same width is a no-op");
w free;
dep2 free;

-- free + clearAll leave a clean registry
verb free;
assertEq((*verb.state).anchor, 0, "freed proxy dropped its nodes");
clearAll();
assertEq(dump() length, 0, "registry empty after clearAll");

if (testSummary() == 0) { "LIVE PROXY ALL PASS" println; }
