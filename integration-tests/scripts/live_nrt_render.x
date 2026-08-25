-- live_nrt_render.x -- a full proxy session rendered offline (run with
-- `tzpl_app --nogui --nrt /tmp/live_nrt.wav --duration 10`): define, play,
-- crossfade-redefine under a dependent, set params, pattern into a voicer
-- proxy, then fade everything out. live_nrt_compare.x asserts the result
-- is non-silent.
--
-- NRT discipline: the script is the render's setup callback and must NOT
-- block on time at top level (the render thread advances the clock and
-- shares the VM lock). Instead the session runs as a clock coroutine; and
-- since compiles execute inline in a render context, define()/play()
-- complete synchronously here -- no awaits needed.

import std.result.*;
import synthdef.*;
import common_ugens.*;
import live.*;
import audio_engine.*;
import clock.*;

let drone = ndef(2);
let bright = ndef(2);
let keys = ndef(2);

coro fn session() Float {
    -- a stereo drone, heard directly
    define(drone, fn() S {
        let f = control("freq", espec(40.0, 800.0, 110.0));
        sinosc([1.0, 1.003] * f) * 0.2
    });
    play(drone);
    yield 2.0;

    -- crossfade to a richer source while a dependent reads it
    fadeTime(0.5);
    define(bright, fn() S {
        (drone() * 0.6) + (sinosc([660.0, 662.0]) * 0.05)
    });
    play(bright);
    define(drone, fn() S {
        let f = control("freq", espec(40.0, 800.0, 110.0));
        (sinosc([1.0, 1.5, 2.01, 3.0] * f) sum(2)) * 0.12
    });
    drone set("freq", 165.0);
    yield 2.0;

    -- notes into a voicer proxy
    define(keys, fn() S {
        voicer(8, fn() S {
            let f = noteParam("freq", espec(20.0, 2000.0, 440.0));
            let a = noteParam("amp", lspec(0.0, 1.0, 0.5));
            sinosc(f) * a * (gate() adsr(0.01, 0.1, 0.7, 0.2))
        }) sum
    });
    play(keys);
    let pl = play(keys, List(event(0.0, 0.5, degree(0)),
                             event(0.5, 0.5, degree(2)),
                             event(1.0, 0.5, degree(4)),
                             event(1.5, 1.0, degree(7))));
    yield 3.0;
    pl stop;

    -- fade out and free everything; the tail of the render goes quiet
    endAll(0.5);
    yield 2.0;
    "live nrt session complete" println;
}

go(session());
