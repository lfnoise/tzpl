-- spans_audible: patterns as functions of time (music.spans), live.
-- A euclidean bass, an offbeat chord stab, and a melody that reverses every
-- fourth cycle, with amp riding a slow sine -- all one Pattern value,
-- rendered to 16 cycles of events and played.
--
-- Run interactively (Ctrl-C to stop):
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       integration-tests/scripts/spans_audible.x

import synthdef.*;
import common_ugens.*;
import clock.*;
import audio_engine.*;
import music.spans.*;
import music.play.*;

fn sineVoice() S {
    let g   = gate();
    let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
    let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });
    let env = g fadein(0.005) lag(0.15);
    pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(16, sineVoice) sum outlet;
sineSynth defSynth("spans_sine") await;

engineStart();
masterGain(0.2);
setTempo(0, 140.0);

let v = pitchVoice(101);
voiceBundle(v, "spans_sine") go(0);

fn d(x Int) Pattern<Pitch> = pure(degree(x));

-- euclidean bass: tresillo on the root, two octaves down
let bass = d(-14) euclid(3, 8) events(0.8, 0.6);

-- offbeat stab
let stab = d(2) fast(2.0) late(0.25) events(0.35, 0.3);

-- melody: four degrees, rotating each cycle, reversed every 4th cycle,
-- with amplitude riding a slow sine
let melody = [0, 4, 5, 7] d fastcat
    iterp(4)
    every(4, fn(p Pattern<Pitch>) Pattern<Pitch> { p rev })
    events(0.5, 0.9)
    withAmp(sinePat() slow(4.0) range(0.25, 0.6) segment(4));

let tune = stack([bass, stab, melody]);

let p = play(patEvents(tune, 16.0, 2.0), v);
"playing 16 cycles of span patterns -- Ctrl-C to stop" println;
