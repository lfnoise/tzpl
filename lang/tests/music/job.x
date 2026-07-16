-- music.job: hierarchical collections + behaviors (seeded).
import music.job.*;
import std.test.*;

let a = shape([[1.0, 0.0, 0.5]]);          -- one whole-beat note, degree 0
let b = shape([[0.5, 4.0, 0.5], [0.5, 5.0, 0.5]]);
let c = shape([[2.0, 7.0, 0.5]]);

-- seqc: sequential onsets
let sq = realize(Coll.seqc([Coll.leaf(a), Coll.leaf(b), Coll.leaf(c)]));
"-- seqc" println;
sq showEvents;
assertEq(sq length, 4, "seqc count");
assertNear((sq drop(3) head).t, 2.0, 1e-12, "seqc running onset");

-- parc: simultaneous, merged in onset order
let pc = realize(Coll.parc([Coll.leaf(b), Coll.leaf(c)]));
assertEq(pc length, 3, "parc count");
assertNear((pc head).t, 0.0, 1e-12, "parc onset");
assertNear(realizeDur(Coll.parc([Coll.leaf(b), Coll.leaf(c)])), 2.0, 1e-12, "parc dur = max");

-- rpt: repeats re-realize the child
let rp = realize(Coll.rpt((3, Coll.leaf(b))));
assertEq(rp length, 6, "rpt count");
assertNear((rp drop(5) head).t, 2.5, 1e-12, "rpt onsets chain");

-- sel inOrder: cycles children per visit, even under rpt
let cyc = Coll.rpt((4, Coll.sel((Behavior.inOrder, [Coll.leaf(a), Coll.leaf(c)]))));
let ce = realize(cyc);
assertEq(ce length, 4, "inOrder count");
match ((ce head).pitch) {
    Pitch.degree(d): assertNear(d, 0.0, 1e-12, "inOrder first pick");
    _: assertTrue(false, "inOrder first pick");
}
match ((ce drop(1) head).pitch) {
    Pitch.degree(d): assertNear(d, 7.0, 1e-12, "inOrder second pick");
    _: assertTrue(false, "inOrder second pick");
}
assertNear(realizeDur(cyc), 6.0, 1e-12, "inOrder durations 1+2+1+2");

-- sel shuffled: over 2 passes of 3 children, each child appears exactly twice
randSeed(77);
let sh3 = Coll.rpt((6, Coll.sel((Behavior.shuffled,
    [Coll.leaf(a), Coll.leaf(b), Coll.leaf(c)]))));
let se = realize(sh3);
var ones = 0;
var twos = 0;
var others = 0;
for (e : se) {
    match (e.pitch) {
        Pitch.degree(d): {
            if (d == 0.0) { ones = ones + 1; }
            else if (d == 7.0) { twos = twos + 1; }
            else { others = others + 1; }
        }
        _: 0;
    }
}
assertEq(ones, 2, "shuffled: a twice");
assertEq(twos, 2, "shuffled: c twice");
assertEq(others, 4, "shuffled: b twice (2 notes each)");

-- sel weighted: zero weight never picked
randSeed(78);
let we = realize(Coll.rpt((20, Coll.sel((Behavior.weighted([1.0, 0.0]),
    [Coll.leaf(a), Coll.leaf(c)])))));
var sawC = false;
for (e : we) {
    match (e.pitch) {
        Pitch.degree(d): { if (d == 7.0) { sawC = true; } }
        _: 0;
    }
}
assertTrue(!sawC, "weighted zero never picked");

-- atRandom under seed is reproducible
randSeed(79);
let r1 = realize(Coll.rpt((5, Coll.sel((Behavior.atRandom, [Coll.leaf(a), Coll.leaf(c)])))));
randSeed(79);
let r2 = realize(Coll.rpt((5, Coll.sel((Behavior.atRandom, [Coll.leaf(a), Coll.leaf(c)])))));
assertEq(r1 length, r2 length, "atRandom reproducible under seed");

testSummary();
