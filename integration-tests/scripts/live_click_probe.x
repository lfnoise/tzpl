-- live_click_probe.x -- minimal redefine probe for click hunting. Renders:
--   t=0    define sine 220 @ 0.2, play (fadeTime 1.0)
--   t=2    redefine to sine 331 (1 s crossfade expected)
--   t=4    stop (1 s fade out expected)
-- Run: tzpl_app --nogui --nrt /tmp/live_click.wav --duration 6 ...
-- A click shows up as a sample-to-sample jump far above the ~0.009
-- per-sample slope of a faded 331 Hz sine at 0.2 amplitude.

import std.result.*;
import synthdef.*;
import common_ugens.*;
import live.*;
import audio_engine.*;
import clock.*;

let drone = ndef(2);

coro fn session() Float {
    fadeTime(1.0);
    define(drone, fn() S { sinosc(220.0) * 0.2 });
    play(drone);
    yield 2.0;
    define(drone, fn() S { sinosc(331.0) * 0.2 });
    yield 2.0;
    drone stop;
    yield 1.5;
    "click probe done" println;
}

go(session());
