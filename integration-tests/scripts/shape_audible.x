-- shape_audible: HMSL-style shapes + hierarchical form, live.
-- A form built from shapes with morphological variations; the sel/shuffled
-- behavior re-rolls at every realize, and three different readings of the
-- same form are queued back to back on one Player.
--
-- Run interactively (Ctrl-C to stop):
--   ./build/app/tzpl_app --nogui --wait -I lang/modules -I bridge/modules \
--       integration-tests/scripts/shape_audible.x

import synthdef.*;
import common_ugens.*;
import clock.*;
import audio_engine.*;
import music.job.*;
import music.play.*;

fn sineVoice() S {
    let g   = gate();
    let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
    let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });
    let env = g fadein(0.005) lag(0.2);
    pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(16, sineVoice) sum outlet;
sineSynth defSynth("hmsl_sine") await;

engineStart();
masterGain(0.2);
setTempo(0, 120.0);

let v = pitchVoice(101);
voiceBundle(v, "hmsl_sine") go(0);

randSeed(7);

-- a theme shape and morphological variants
let theme = shape([[0.5, 0.0, 0.5], [0.25, 1.0, 0.4], [0.25, 2.0, 0.4],
                   [0.5, 3.0, 0.5], [0.5, 4.0, 0.6]]);
let riffs = [
    Coll.leaf(theme),
    Coll.leaf(theme retrograde),
    Coll.leaf(theme invertDim('degree, 2.0)),
    Coll.leaf(theme transposeDim('degree, 4.0) scaleDim('dur, 0.5)),
];

let cadence = shape([[1.0, 4.0, 0.5], [2.0, 0.0, 0.6]]);

-- the form: four shuffled riffs, then the cadence over a low drone
let form = Coll.seqc([
    Coll.rpt((4, Coll.sel((Behavior.shuffled, riffs)))),
    Coll.parc([
        Coll.leaf(cadence),
        Coll.leaf(shape([[3.0, -7.0, 0.4]])),
    ]),
]);

-- three different readings of the same form, back to back
let p = play(realize(form), v);
p enqueue(realize(form));
p enqueue(realize(form));
"playing three readings of the form -- Ctrl-C to stop" println;
