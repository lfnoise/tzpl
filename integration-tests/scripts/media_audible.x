-- media_audible: a Haskore-style canon through music.media.
-- One theme, played against itself two beats later a fifth down, over a
-- drone; the whole form is a single Music value interpreted by perform
-- and played sample-accurately by music.play.
--
-- Run interactively (Ctrl-C to stop):
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       integration-tests/scripts/media_audible.x

import synthdef.*;
import common_ugens.*;
import clock.*;
import audio_engine.*;
import music.media.*;
import music.play.*;

fn sineVoice() S {
    let g   = gate();
    let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
    let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });
    let env = g fadein(0.01) lag(0.3);
    pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(16, sineVoice) sum outlet;
sineSynth defSynth("media_sine");

engineStart();
masterGain(0.2);
setTempo(0, 96.0);

let v = pitchVoice(101);
voiceBundle(v, "media_sine") go(0);

-- the theme, in scale degrees
fn deg(d Float, dur Float) Music<Pitch> = note(dur, Pitch.degree(d));
let theme = line([deg(0.0, 1.0), deg(1.0, 1.0), deg(2.0, 0.5), deg(1.0, 0.5),
                  deg(2.0, 0.5), deg(3.0, 0.5), deg(4.0, 2.0)]);

-- theme against itself: two beats late, a fifth down, quieter
let canon = theme
          & (mrest(2.0) $ trans(-4.0, dyn(0.7, theme)));

-- close with the theme in doubled tempo over a low drone
let coda = tempo(2.0, theme)
         & dyn(0.6, note(3.0, Pitch.degree(-7.0)));

let piece = canon $ mrest(0.5) $ coda;

let p = play(piece perform, v);
"playing canon (%^ beats) -- Ctrl-C to stop" fmt(piece mdur) println;
