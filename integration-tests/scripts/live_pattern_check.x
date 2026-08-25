-- live_pattern_check.x -- functional checks for live.pattern (and the
-- live.x umbrella): auto-derived Voice from a voicer proxy, player
-- lifecycle. Engine never started; bundles run inline.

import std.test.*;
import std.result.*;
import synthdef.*;
import common_ugens.*;
import live.*;

fn okOr(r Result<String, String>, label String) Bool {
    match (r) {
        ok(m): return assertTrue(true, label);
        err(e): return assertTrue(false, label $ " -- " $ e);
    }
    false
}

let keys = ndef(2);
okOr(define(keys, fn() S {
    voicer(8, fn() S {
        let f = noteParam("freq", espec(20.0, 2000.0, 440.0));
        let a = noteParam("amp", lspec(0.0, 1.0, 0.5));
        let g = gate();
        sinosc(f) * a * g
    }) sum
}) await, "voicer proxy defines");

assertEq((*keys.state).noteNames length, 2, "two noteParams scanned");
assertEq((*keys.state).noteNames[0], 'freq, "freq first");
assertEq((*keys.state).noteNames[1], 'amp, "amp second");
assertNear((*keys.state).noteDefaults[0], 440.0, 0.0001, "freq default");

let es = List(event(0.0, 0.5, degree(0)),
              event(0.5, 0.5, degree(2)),
              event(1.0, 0.5, degree(4)));

let pl = play(keys, es);
assertFalse((*pl.state).stopped, "player running");
assertEq(pl.voice.names length, 2, "voice derived from proxy");

pl enqueue(es);
assertEq((*pl.state).queue length, 1, "enqueue queued a score");

pl replace(es);
assertFalse((*pl.state).stopped, "replace keeps playing");

pl stop;
assertTrue((*pl.state).stopped, "player stopped");

keys free;
clearAll();

if (testSummary() == 0) { "LIVE PATTERN ALL PASS" println; }
