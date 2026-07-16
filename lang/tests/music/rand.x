-- music.rand: coin / gauss / wpick, seeded via the randSeed builtin.
import music.core.*;
import std.test.*;

-- randSeed makes the per-VM RNG reproducible
randSeed(2026);
let a = urand();
randSeed(2026);
let b = urand();
assertNear(a, b, 0.0, "randSeed reproducible");

-- coin respects probability bounds
randSeed(7);
assertTrue(!coin(0.0), "coin(0) never");
assertTrue(coin(1.0), "coin(1) always");
randSeed(7);
var heads = 0;
for (i : (1..2000)) { if (coin(0.25)) { heads = heads + 1; } }
assertTrue(heads > 400 && heads < 600, "coin(0.25) frequency");

-- gauss: sample statistics near mu/sigma
randSeed(11);
var sum = 0.0;
var sumsq = 0.0;
let n = 4000;
for (i : (1..n)) {
    let g = gauss(10.0, 2.0);
    sum = sum + g;
    sumsq = sumsq + g * g;
}
let mean = sum / n toFloat;
let variance = sumsq / n toFloat - mean * mean;
assertNear(mean, 10.0, 0.15, "gauss mean");
assertNear(variance sqrt, 2.0, 0.15, "gauss sigma");

-- wpick: heavily weighted element dominates, zero weight never picked
randSeed(13);
var counts = [0, 0, 0];
for (i : (1..3000)) {
    let v = wpick([0, 1, 2], [1.0, 8.0, 0.0]);
    counts[v] = counts[v] + 1;
}
assertEq(counts[2], 0, "wpick zero weight");
assertTrue(counts[1] > counts[0] * 4, "wpick weighting");
assertEq(counts[0] + counts[1], 3000, "wpick total");

-- seeded golden values (pin the stream so pattern goldens stay stable)
randSeed(42);
"-- seeded stream" println;
urands() take(4) collect(4) println;
irands(0, 9) take(6) collect(6) println;

testSummary();
