-- music.spans: pattern-as-function-of-time combinators + epsilon policy.
import music.spans.*;
import std.test.*;

fn parts<T>(hs [Hap<T>]) [Float] {
    var out = [Float]();
    for (h : hs sortHaps) { out push!(h.part.a); }
    out
}
fn vals(hs [Hap<Int>]) [Int] {
    var out = [Int]();
    for (h : hs sortHaps) { out push!(h.value); }
    out
}

-- pure: one hap per cycle, split at cycle boundaries
let p1 = pure(7);
assertEq(p1 cycles(3.0) length, 3, "pure 3 cycles");
let frag = p1 call(span(0.5, 2.5));
assertEq(frag length, 3, "pure fragments");
assertEq(frag parts, [0.5, 1.0, 2.0], "pure fragment starts");
match (frag[0].whole) {
    Option.some(w): assertNear(w.a, 0.0, 1e-12, "fragment keeps whole");
    Option.none: assertTrue(false, "fragment keeps whole");
}

-- exact integer boundaries produce exactly one hap per cycle
assertEq(p1 call(span(1.0, 2.0)) length, 1, "integer boundary query");
assertEq(p1 call(span(2.999999998, 3.000000002)) length, 2, "epsilon boundary split");

-- fastcat / slowcat
let fc = fastcat([pure(1), pure(2), pure(3)]);
assertEq(fc cycles(1.0) vals, [1, 2, 3], "fastcat order");
assertEq(fc cycles(1.0) parts, [0.0, 0.3333333333333333, 0.6666666666666666], "fastcat non-dyadic thirds");
assertEq(fc cycles(2.0) length, 6, "fastcat repeats");
let sc = slowcat([pure(1), pure(2)]);
assertEq(sc cycles(4.0) vals, [1, 2, 1, 2], "slowcat rotation");

-- fast / slow / rev
assertEq(pure(9) fast(3.0) cycles(1.0) length, 3, "fast(3) count");
assertEq(pure(9) slow(2.0) cycles(2.0) length, 1, "slow(2) count");
let rv = fastcat([pure(1), pure(2), pure(3)]) rev;
assertEq(rv cycles(1.0) vals, [3, 2, 1], "rev reverses");
assertEq(rv rev cycles(1.0) vals, [1, 2, 3], "rev twice = id");

-- early / late
assertEq(fastcat([pure(1), pure(2), pure(3), pure(4)]) early(0.25) cycles(1.0) vals,
         [2, 3, 4, 1], "early rotates");

-- every / iterp
let ev = fastcat([pure(1), pure(2)]) every(2, fn(p Pattern<Int>) Pattern<Int> { p rev });
assertEq(ev cycles(2.0) vals, [2, 1, 1, 2], "every other cycle reversed");
let it = fastcat([pure(1), pure(2), pure(3), pure(4)]) iterp(4);
assertEq(it cycles(2.0) vals, [1, 2, 3, 4, 2, 3, 4, 1], "iter rotates per cycle");

-- stack
assertEq(stack([pure(1), pure(2) fast(2.0)]) cycles(1.0) length, 3, "stack count");

-- euclid: classic tresillo e(3,8)
assertEq(euclidMask(3, 8, 0), [true, false, false, true, false, false, true, false], "e(3,8) mask");
let e38 = pure(1) euclid(3, 8);
assertEq(e38 cycles(1.0) length, 3, "e(3,8) onsets");
assertEq(e38 cycles(1.0) parts, [0.0, 0.375, 0.75], "e(3,8) positions");
assertEq(pure(1) euclid(3, 8, 2) cycles(1.0) length, 3, "rotated euclid count");

-- signals + segment + range
let sg = sawPat() range(0.0, 8.0) segment(4);
let sv = sg cycles(1.0) sortHaps;
assertEq(sv length, 4, "segment count");
assertNear(sv[0].value, 1.0, 1e-9, "segment samples midpoint");
assertNear(sv[3].value, 7.0, 1e-9, "segment last");

-- zipl: left structure, right sampled
let z = zipl(fastcat([pure(10), pure(20)]), steady(5.0),
             fn(a Int, b Float) Int { a + b toInt });
assertEq(z cycles(1.0) vals, [15, 25], "zipl combine");

-- events + setters + patEvents
let pp = fastcat([pure(degree(0)), pure(degree(2)), pure(degree(4))]);
let pe = pp events withAmp(sawPat() range(0.2, 0.8) segment(3))
            withParam('cutoff, steady(1500.0));
let es = patEvents(pe, 2.0, 3.0);
"-- patEvents (2 cycles, 3 beats per cycle)" println;
es showEvents;
assertEq(es length, 6, "patEvents count");
assertNear((es head).t, 0.0, 1e-9, "first onset");
assertNear((es drop(3) head).t, 3.0, 1e-9, "second cycle in beats");
assertNear((es drop(1) head).dur, 1.0, 1e-9, "dur in beats");
assertNear((es drop(1) head).sustain, 0.8, 1e-9, "sustain from legato fraction");
assertEq((es head).params length, 1, "withParam carried");

-- epsilon: non-dyadic splits at fast(3) still one onset per slot
let nd = pure(1) fast(3.0) filterOnsets;
assertEq(nd cycles(2.0) length, 6, "fast(3) over 2 cycles, exact");

testSummary();
