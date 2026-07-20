-- music.pat: stream vocabulary + bind (seeded for reproducibility).
import music.pat.*;
import std.test.*;

-- deterministic series
assertEq(series(0.0, 2.5) take(5) collect(5), [0.0, 2.5, 5.0, 7.5, 10.0], "series");
assertEq(geom(1.0, 2.0) take(5) collect(5), [1.0, 2.0, 4.0, 8.0, 16.0], "geom");
assertEq(once(7) collect(3), [7], "once");
assertEq(embed(List(List(1, 2), once(9), List(3, 4))) collect(8), [1, 2, 9, 3, 4], "embed");

-- structural combinators
assertEq(lace([List(1, 2, 3), List(10, 20), List(100)]) collect(9),
         [1, 10, 100, 2, 20, 3], "lace uneven");
assertEq(lace([(1..) toList, List(0, 0)]) take(7) collect(7),
         [1, 0, 2, 0, 3, 4, 5], "lace with infinite");
assertEq(List(7, 8, 9, 10) stutter(List(2, 0, 3, 1)) collect(9),
         [7, 7, 9, 9, 9, 10], "stutter counts");

-- random streams under a fixed seed: bounds + shape properties
randSeed(1234);
let br = browns(0.0, 10.0, 2.0) take(200) collect(200);
var ok = true;
for (x : br) { if (x < 0.0 || x > 10.0) { ok = false; } }
assertTrue(ok, "browns stays in bounds");
var maxJump = 0.0;
var i = 1;
while (i < br length) {
    let j = br[i] - br[i - 1] abs;
    if (j > maxJump) { maxJump = j; }
    i = i + 1;
}
assertTrue(maxJump <= 4.0 + 1e-12, "browns step bound (reflection doubles at most)");

randSeed(99);
let xr = xpicks([1, 2, 3]) take(300) collect(300);
var noRepeat = true;
i = 1;
while (i < xr length) {
    if (xr[i] == xr[i - 1]) { noRepeat = false; }
    i = i + 1;
}
assertTrue(noRepeat, "xpicks never repeats");

randSeed(7);
let sh = shufs([1, 2, 3, 4]) take(12) collect(12);
var pass = 0;
while (pass < 3) {
    var seen = Set(0);
    var k = 0;
    while (k < 4) {
        seen insert!(sh[pass * 4 + k]);
        k = k + 1;
    }
    assertEq(seen length, 5, "shufs pass %^ is a permutation" fmt(pass));
    pass = pass + 1;
}

randSeed(5);
let ws = wpicks([1, 2], [0.0, 1.0]) take(50) collect(50);
var allTwo = true;
for (x : ws) { if (x != 2) { allTwo = false; } }
assertTrue(allTwo, "wpicks zero weight");

randSeed(6);
let cs = coins(1.0) take(10) collect(10);
var allTrue = true;
for (c : cs) { if (!c) { allTrue = false; } }
assertTrue(allTrue, "coins(1) all true");

randSeed(8);
let gs = gausses(5.0, 0.5) take(500) collect(500);
var gsum = 0.0;
for (g : gs) { gsum = gsum + g; }
assertNear(gsum / 500.0, 5.0, 0.1, "gausses mean");

-- pitch lifting via auto-mapping
let ps = [0, 2, 4] degree;
assertEq(ps length, 3, "degree lift length");
"-- lifted pitches" println;
for (p : ps) { p toString println; }

-- bind: running-sum onsets, sustain = dur * legato, ends at shortest
let es = bind([0, 1, 2, 4] toList degree,
              List(0.5, 0.5, 1.0) cyc,
              List(0.9, 0.3) cyc,
              List(0.5) cyc,
              [('cutoff, series(1000.0, 500.0))]);
"-- bind result" println;
es showEvents;
assertEq(es length, 4, "bind ends at shortest (pitch)");
let e2 = es drop(2) head;
assertNear(e2.t, 1.0, 1e-12, "bind onset running sum");
assertNear(e2.sustain, 0.5, 1e-12, "bind sustain legato");

-- infinite bind cut by takeDur
let inf = bind(iter(0, fn(d Int) Int { d + 1 }) degree, List(0.25) cyc);
assertEq(inf takeDur(2.0) length, 8, "infinite bind takeDur");

testSummary();
