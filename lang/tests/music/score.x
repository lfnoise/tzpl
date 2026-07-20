-- music.score: pure Event -> timestamped NoteCmd lowering.
import music.pat.*;
import music.score.*;
import std.test.*;

let v = freqVoice(101, 4);

-- basic on/off ordering with a rest
let es = List(
    event(0.0, 1.0, Pitch.step(69.0), 0.9, 0.5),
    event(1.0, 1.0, Pitch.rest),
    event(2.0, 1.0, Pitch.step(81.0), 0.4, 0.25)
);
"-- basic score" println;
let sc = score(es, v);
sc showScore;
assertEq(sc length, 4, "score length (rest skipped)");
assertNear((sc head).0, 0.0, 1e-12, "first onset");
assertNear((sc drop(1) head).0, 0.5, 1e-12, "release at sustain");

-- eventParams: freq resolution + params lookup + defaults
let voiceX = voice(7, ['freq, 'amp, 'cutoff, 'q], [440.0, 0.5, 1000.0, 1.0]);
let e = event(0.0, 1.0, Pitch.step(69.0), 0.9, 0.5, [('cutoff, 2500.0)]);
let ps = eventParams(e, voiceX, et12, major);
assertNear(ps[0], 440.0, 1e-9, "freq param");
assertNear(ps[1], 0.9, 1e-12, "amp param");
assertNear(ps[2], 2500.0, 1e-12, "extra param by name");
assertNear(ps[3], 1.0, 1e-12, "default param");

-- 'pitch name: MIDI-style note number round-trips any tuning exactly
let vp = pitchVoice(7);
let bpNote = event(0.0, 1.0, Pitch.step(6.5), 0.5);
let bpPs = eventParams(bpNote, vp, bp(), major);
let wantHz = stepHz(bp(), 6.5);
assertNear(440.0 * pow(2.0, (bpPs[0] - 69.0) / 12.0), wantHz, 1e-9, "pitch nn encodes BP exactly");

-- overlapping sustains: releases interleave correctly, noteIDs round-robin
let held = List(
    event(0.0, 1.0, Pitch.step(60.0), 0.5, 4.0),   -- long note
    event(1.0, 1.0, Pitch.step(64.0), 0.5, 0.5),   -- short note inside it
    event(2.0, 1.0, Pitch.step(67.0), 0.5, 0.5)
);
"-- overlap score" println;
let sc2 = score(held, v);
sc2 showScore;
-- expected order: on(0), on(1)@1, off(1)@1.5, on(2)@2, off(2)@2.5, off(0)@4
assertEq(sc2 length, 6, "overlap count");
let times = sc2 map(fn(tc (Float, NoteCmd)) Float { tc.0 }) collect(6);
assertEq(times, [0.0, 1.0, 1.5, 2.0, 2.5, 4.0], "overlap times ordered");

-- noteID round-robin wraps at poly
let burst = List(
    event(0.0, 1.0, Pitch.step(60.0), 0.5, 0.1),
    event(1.0, 1.0, Pitch.step(61.0), 0.5, 0.1),
    event(2.0, 1.0, Pitch.step(62.0), 0.5, 0.1),
    event(3.0, 1.0, Pitch.step(63.0), 0.5, 0.1),
    event(4.0, 1.0, Pitch.step(64.0), 0.5, 0.1)
);
let sc3 = score(burst, v);   -- poly 4: fifth note reuses id 0
var onIds = [Int]();
for (tc : sc3) {
    match (tc.1) {
        noteOn(node, id, params): onIds push!(id);
        _: 0;
    }
}
assertEq(onIds, [0, 1, 2, 3, 0], "noteID round robin");

-- lazy: score of an infinite bind stream
let infSc = score(bind(iter(0, fn(d Int) Int { d + 1 }) degree, List(0.5) cyc), v) take(6);
assertEq(infSc length, 6, "score is lazy over infinite events");

testSummary();
