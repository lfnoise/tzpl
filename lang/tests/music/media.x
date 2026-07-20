-- music.media: Haskore-style Music<T> algebra + perform.
import music.media.*;
import std.test.*;

-- $ / | / mdur algebra
let a = note(1.0, degree(0));
let b = note(0.5, degree(2));
let c = note(2.0, degree(4));
let m1 = a $ b $ (c | note(1.0, degree(-3)));
assertNear(m1 mdur, 3.5, 1e-12, "mdur seq+par");
assertNear((m1 $ mrest(1.5)) mdur, 5.0, 1e-12, "mdur with rest");
assertNear(m1 tempo(2.0) mdur, 1.75, 1e-12, "tempo halves duration");
assertNear((a $ b) times(3) mdur, 4.5, 1e-12, "times");
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

-- tempo/trans/dyn nesting (trans is MODAL: whole scale degrees)
let nested = (a $ b) dyn(0.5) trans(7) tempo(2.0);
"-- perform nested controls" println;
let es2 = nested perform;
es2 showEvents;
assertNear((es2 head).dur, 0.5, 1e-12, "tempo scales dur");
assertNear((es2 head).amp, 0.25, 1e-12, "dyn scales amp");
match ((es2 head).pitch) {
    Pitch.degree(d): assertEq(d.0, 7, "trans shifts degree");
    _: assertTrue(false, "trans shifts degree");
}
let es2b = es2 drop(1) head;
assertNear(es2b.t, 0.5, 1e-12, "tempo scales onsets");

-- tempo inside mdur-driven sequencing stays consistent
let mixed = a $ (b times(4) tempo(4.0)) $ a;
assertNear(mixed mdur, 2.5, 1e-12, "nested tempo mdur");
let esx = mixed perform;
assertNear((esx drop(5) head).t, 1.5, 1e-12, "post-tempo onset");

-- mmap payload transformation
let octUp = m1 mmap(fn(p Pitch) Pitch { transposed(p, 7) });
match ((octUp perform head).pitch) {
    Pitch.degree(d): assertEq(d.0, 7, "mmap transpose");
    _: assertTrue(false, "mmap transpose");
}

-- modal transposition moves the degree only: accidentals ride along,
-- non-degree pitches are untouched
match (transposed(degree(1, 1.0), 2)) {
    Pitch.degree(d): {
        assertEq(d.0, 3, "transposed keeps accidental (degree)");
        assertNear(d.1, 1.0, 1e-12, "transposed keeps accidental (acc)");
    }
    _: assertTrue(false, "transposed keeps accidental");
}
match (transposed(Pitch.step(60.0), 5)) {
    Pitch.step(s): assertNear(s, 60.0, 1e-12, "modal trans leaves step pitches");
    _: assertTrue(false, "modal trans leaves step pitches");
}

-- MNote payload with params + inst tag
let mn = (note(1.0, mnote(Pitch.step(69.0), 0.8, [('pan, -0.5)])) inst(3))
       $ note(1.0, mnote(Pitch.step(72.0)));
"-- perform MNote" println;
let es3 = mn perform;
es3 showEvents;
assertEq((es3 head).params length, 2, "params + inst tag");
assertEq((es3 drop(1) head).params length, 0, "inst scoped to subtree");

testSummary();
