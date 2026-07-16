-- music.event: Event helpers, lazy merge, formatting.
import music.core.*;
import std.test.*;

let melody = List(
    event(0.0, 1.0, Pitch.degree(0.0)),
    event(1.0, 0.5, Pitch.degree(2.0), 0.7),
    event(1.5, 0.5, Pitch.rest),
    event(2.0, 2.0, Pitch.degree(4.0), 0.7, 1.5)
);

"-- showEvents" println;
melody showEvents;

-- default sustain is 80% of dur; explicit sustain wins
assertNear((melody head).sustain, 0.8, 1e-12, "default sustain");
let e4 = melody drop(3) head;
assertNear(e4.sustain, 1.5, 1e-12, "explicit sustain");
assertTrue(melody drop(2) head isRest, "rest detection");
assertTrue(!(melody head isRest), "non-rest detection");

-- offset / stretch / gain
let shifted = melody offset(10.0);
assertNear((shifted head).t, 10.0, 1e-12, "offset t");
assertNear((shifted head).dur, 1.0, 1e-12, "offset leaves dur");
let slow = melody stretch(2.0);
assertNear((slow drop(1) head).t, 2.0, 1e-12, "stretch t");
assertNear((slow drop(1) head).dur, 1.0, 1e-12, "stretch dur");
assertNear((slow drop(1) head).sustain, 0.8, 1e-12, "stretch sustain");
let quiet = melody gain(0.5);
assertNear((quiet drop(1) head).amp, 0.35, 1e-12, "gain amp");

-- merge: onset-ordered interleave, stable for ties (left first)
let voice2 = List(
    event(0.5, 1.0, Pitch.step(60.0)),
    event(1.0, 1.0, Pitch.step(64.0)),
    event(3.0, 1.0, Pitch.step(67.0))
);
"-- merged" println;
let merged = merge(melody, voice2);
merged showEvents;
assertEq(merged length, 7, "merge length");
assertNear((merged head).t, 0.0, 1e-12, "merge first");
assertNear((merged drop(6) head).t, 3.0, 1e-12, "merge last");

-- merge is lazy: works on infinite streams
coro fn ticks(dt Float) Event {
    var i = 0;
    while (true) {
        yield event(i toFloat * dt, dt, Pitch.degree(i toFloat));
        i = i + 1;
    }
}
let inf1 = ticks(1.0) toList;
let inf2 = ticks(0.75) toList offset(0.1);
let both = merge(inf1, inf2) take(6);
"-- lazy merge of two infinite streams" println;
both showEvents;
assertEq(both length, 6, "lazy merge take");

-- takeDur bounds an infinite stream by onset
let bounded = ticks(0.5) toList takeDur(2.0);
assertEq(bounded length, 4, "takeDur count");

-- params formatting
let withParams = event(0.0, 1.0, Pitch.hz(440.0), 0.9, 0.5, [('pan, 0.5), ('cutoff, 2000.0)]);
withParams showEvent println;

testSummary();
