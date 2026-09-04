-- music.job: realizeCo, the incremental (lazy-stream) realize.
import music.job.*;
import std.test.*;

let a = shape([[1.0, 0.0, 0.5]]);          -- one whole-beat note, degree 0
let b = shape([[0.5, 4.0, 0.5], [0.5, 5.0, 0.5]]);
let c = shape([[2.0, 7.0, 0.5]]);

-- Deterministic trees: realizeCo produces the same reading as realize.
let form = Coll.seqc([
    Coll.leaf(a),
    Coll.parc([Coll.leaf(b), Coll.leaf(c)]),
    Coll.rpt((2, Coll.leaf(b))),
]);
let eager = realize(form);
let lazy = realizeCo(form);
"-- deterministic form (lazy)" println;
lazy showEvents;
assertEq(lazy length, eager length, "co matches realize: count");
var i = 0;
while (i < eager length) {
    let ee = eager drop(i) head;
    let le = lazy drop(i) head;
    assertNear(le.t, ee.t, 1e-12, "co matches realize: onset " $ i toString);
    assertNear(le.dur, ee.dur, 1e-12, "co matches realize: dur " $ i toString);
    i = i + 1;
}

-- t0 offsets the whole stream.
assertNear((realizeCo(form, 10.0) head).t, 10.0, 1e-12, "co t0 offset");

-- sel inOrder: per-visit cursor advances across visits, as in realize.
let cyc = Coll.rpt((4, Coll.sel((Behavior.inOrder, [Coll.leaf(a), Coll.leaf(c)]))));
let ce = realizeCo(cyc);
assertEq(ce length, 4, "co inOrder count");
match ((ce head).pitch) {
    Pitch.degree(d): assertEq(d.0, 0, "co inOrder first pick");
    _: assertTrue(false, "co inOrder first pick");
}
match ((ce drop(1) head).pitch) {
    Pitch.degree(d): assertEq(d.0, 7, "co inOrder second pick");
    _: assertTrue(false, "co inOrder second pick");
}
assertNear((ce drop(3) head).t, 4.0, 1e-12, "co inOrder onsets chain 1+2+1");

-- Seeded randomness under seqc/rpt: same reading as realize.
randSeed(77);
let sh3 = Coll.rpt((6, Coll.sel((Behavior.shuffled,
    [Coll.leaf(a), Coll.leaf(b), Coll.leaf(c)]))));
let se = realize(sh3);
randSeed(77);
let sc = realizeCo(sh3);
assertEq(sc length, se length, "co seeded shuffle: count");
var j = 0;
var samePitches = true;
while (j < se length) {
    let ep = (se drop(j) head).pitch toString;
    let lp = (sc drop(j) head).pitch toString;
    if (ep != lp) { samePitches = false; }
    j = j + 1;
}
assertTrue(samePitches, "co seeded shuffle: same picks as realize");

-- Incrementality: pulling the head of a huge form must not realize it all.
-- (1M-repeat form; eager realization would build 2M events here.)
let huge = Coll.rpt((1000000, Coll.leaf(b)));
let stream = realizeCo(huge);
assertNear((stream head).t, 0.0, 1e-12, "co huge form: first onset");
assertNear((stream drop(3) head).t, 1.5, 1e-12, "co huge form: fourth onset");

-- Bounding an incremental stream with takeDur.
let cut = takeDur(realizeCo(huge), 4.0);
assertEq(cut length, 8, "co takeDur bounds the stream");

testSummary();
