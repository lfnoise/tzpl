-- pat_nrt: offline render of a music.pat stream -- in Bohlen-Pierce.
-- Walks the 13-step tritave against a drone, rendered via music.play render()
-- (which drives the same Bundle player on the render-local scheduler).
--
-- Run:
--   ./build/app/tzpl_app --nogui -I lang/modules -I bridge/modules \
--       integration-tests/scripts/pat_nrt.x

import synthdef.*;
import common_ugens.*;
import clock.*;
import audio_engine.*;
import music.pat.*;
import music.play.*;

fn sineVoice() S {
    let g   = gate();
    let pch = noteParam("pitch", ControlSpec { lo: 0.0, hi: 127.0, init: 60.0, warp: ControlWarp.linear });
    let amp = noteParam("amp",   ControlSpec { lo: 0.0, hi: 1.0,   init: 0.5,  warp: ControlWarp.linear });
    let env = g fadein(0.005) lag(0.2);
    pch nnhz sinosc cb * env * amp
}
fn sineSynth() S = voicer(16, sineVoice) sum outlet;
sineSynth defSynth("pat_sine") await;

randSeed(42);

-- one full tritave walk, one step per 0.4 beats
let walk = bind(series(0.0, 1.0) take(14) degree,
                List(0.4) cyc,
                List(0.5) cyc,
                List(0.8) cyc);

-- tritave drone under it
let drone = bind(List(0.0, 0.0) degree,
                 List(2.8) cyc,
                 List(0.3) cyc,
                 List(1.0) cyc);

let v = pitchVoice(101);
let tune = merge(drone, walk);

-- Bohlen-Pierce anchored at 220 Hz; every step of the tuning is a degree.
let h = render(tune, v, "pat_sine", "/tmp/pat_nrt.wav", 8.0,
               bp() root(220.0, 0.0), chromatic(13));
await renderDone(h);
"rendered /tmp/pat_nrt.wav" println;
