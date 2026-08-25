-- live_proxy_check.x -- functional checks for live.proxy without audio.
-- The engine is never started, so every bundle executes synchronously
-- inline and is validated against the real silo graph: a bad wiring
-- sequence fails loudly here. Compiles run on the async worker; each
-- define is awaited.

import std.test.*;
import std.result.*;
import synthdef.*;
import common_ugens.*;
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

-- free + clearAll leave a clean registry
verb free;
assertEq((*verb.state).anchor, 0, "freed proxy dropped its nodes");
clearAll();
assertEq(dump() length, 0, "registry empty after clearAll");

if (testSummary() == 0) { "LIVE PROXY ALL PASS" println; }
