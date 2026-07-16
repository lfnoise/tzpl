-- pat_audible: music.pat streams playing live through the engine.
-- A weighted-random pentatonic melody over a slow bass, assembled with
-- bind/merge and played as beat-scheduled Bundles (music.play).
--
-- Run interactively (Ctrl-C to stop):
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       integration-tests/scripts/pat_audible.x

import synthdef.*;
import common_ugens.*;
import clock.*;
import audio_engine.*;
import music.pat.*;
import music.play.*;

---------------------------------------------------------------------------
-- A simple sine voice: noteParams pitch (MIDI-style note number) and amp.

fn sineVoice() S {
    let g   = gate();
    let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
    let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });
    let env = g fadein(0.005) lag(0.25);
    pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(16, sineVoice) sum outlet;
sineSynth defSynth("pat_sine");

---------------------------------------------------------------------------

engineStart();
masterGain(0.2);
setTempo(0, 132.0);

let v = pitchVoice(101);
voiceBundle(v, "pat_sine") go(0);

randSeed(2026);

-- melody: weighted random pentatonic degrees, cycling short durations
let melody = bind(
    wpicks([0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 7.0], [4.0, 2.0, 3.0, 2.0, 3.0, 1.0, 1.0]) degree,
    List(0.5, 0.25, 0.25) cyc,
    rands(0.25, 0.55),
    List(0.7) cyc);

-- bass: slow roots and fifths below
let bass = bind(
    List(-5.0, -5.0, -4.0, -3.0) cyc degree,
    List(2.0) cyc,
    List(0.5) cyc,
    List(0.95) cyc);

let tune = merge(melody, bass) takeDur(64.0);

let p = play(tune, v, et12, pentMinor);
"playing 64 beats of pentatonic patterns -- Ctrl-C to stop" println;
