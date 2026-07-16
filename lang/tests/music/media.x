-- music.media: Haskore-style Music<T> algebra + perform.
import music.media.*;
import std.test.*;

-- $ / | / mdur algebra
let a = note(1.0, Pitch.degree(0.0));
let b = note(0.5, Pitch.degree(2.0));
let c = note(2.0, Pitch.degree(4.0));
let m1 = a $ b $ (c | note(1.0, Pitch.degree(-3.0)));
assertNear(m1 mdur, 3.5, 1e-12, "mdur seq+par");
assertNear((m1 $ mrest(1.5)) mdur, 5.0, 1e-12, "mdur with rest");
assertNear(tempo(2.0, m1) mdur, 1.75, 1e-12, "tempo halves duration");
assertNear(times(3, a $ b) mdur, 4.5, 1e-12, "times");
assertNear(line([a, b, c]) mdur, 3.5, 1e-12, "line");
assertNear(chord([a, b, c]) mdur, 2.0, 1e-12, "chord");

-- perform: onsets, durations, parallel merge
"-- perform m1" println;
let es1 = m1 perform;
es1 showEvents;
assertEq(es1 length, 4, "perform count");
let e3 = es1 drop(2) head;
assertNear(e3.t, 1.5, 1e-12, "par onset");
let e4 = es1 drop(3) head;
assertNear(e4.t, 1.5, 1e-12, "par onset same");

-- retro: reversal preserves duration; retro of retro performs identically
assertNear(m1 retro mdur, 3.5, 1e-12, "retro preserves dur");
"-- perform retro(m1)" println;
m1 retro perform showEvents;
"-- perform retro(retro(m1))" println;
m1 retro retro perform showEvents;

-- tempo/trans/dyn nesting
let nested = tempo(2.0, trans(12.0, dyn(0.5, a $ b)));
"-- perform nested controls" println;
let es2 = nested perform;
es2 showEvents;
assertNear((es2 head).dur, 0.5, 1e-12, "tempo scales dur");
assertNear((es2 head).amp, 0.25, 1e-12, "dyn scales amp");
match ((es2 head).pitch) {
    Pitch.degree(d): assertNear(d, 12.0, 1e-12, "trans shifts degree");
    _: assertTrue(false, "trans shifts degree");
}
let es2b = es2 drop(1) head;
assertNear(es2b.t, 0.5, 1e-12, "tempo scales onsets");

-- tempo inside mdur-driven sequencing stays consistent
let mixed = a $ tempo(4.0, times(4, b)) $ a;
assertNear(mixed mdur, 2.5, 1e-12, "nested tempo mdur");
let esx = mixed perform;
assertNear((esx drop(5) head).t, 1.5, 1e-12, "post-tempo onset");

-- mmap payload transformation
let octUp = m1 mmap(fn(p Pitch) Pitch { transposed(p, 7.0) });
match ((octUp perform head).pitch) {
    Pitch.degree(d): assertNear(d, 7.0, 1e-12, "mmap transpose");
    _: assertTrue(false, "mmap transpose");
}

-- MNote payload with params + inst tag
let mn = inst(3, note(1.0, mnote(Pitch.step(69.0), 0.8, [('pan, -0.5)])))
       $ note(1.0, mnote(Pitch.step(72.0)));
"-- perform MNote" println;
let es3 = mn perform;
es3 showEvents;
assertEq((es3 head).params length, 2, "params + inst tag");
assertEq((es3 drop(1) head).params length, 0, "inst scoped to subtree");

testSummary();
