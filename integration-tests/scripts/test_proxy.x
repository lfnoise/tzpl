-- test_proxy.x -- audible walkthrough of the live proxy system. Run with a
-- real audio device:
--
--     ./build/app/tzpl_app --nogui -I lang/modules -I bridge/modules \
--         integration-tests/scripts/test_proxy.x
--
-- ~35 seconds: drone -> slow crossfade redefine -> effect proxy reading it
-- -> param change -> notes into a voicer proxy -> everything fades out.

import std.result.*;
import synthdef.*;
import common_ugens.*;
import filters.*;
import effects.*;
import live.*;
import audio_engine.*;
import clock.*;

engineStart();
"live proxy audible test (~35s)" println;

let drone = ndef(2);
let echoy = ndef(2);
let keys = ndef(2);

"1: drone (dual sine, lowpassed)" println;
define(drone, fn() S {
    let f = control("freq", espec(40.0, 800.0, 110.0));
    (sinosc([1.0, 1.003] * f) * 0.2) lpf(900.0)
}) await;
play(drone) await;
delayReal(5.0) await;

"2: slow crossfade to an overtone stack (4s fade)" println;
fadeTime(4.0);
define(drone, fn() S {
    let f = control("freq", espec(40.0, 800.0, 110.0));
    (sinosc([1.0, 1.5, 2.01, 3.0] * f) sum(2)) * 0.12
}) await;
delayReal(6.0) await;

"3: effect proxy reading the drone; dry path fades off" println;
define(echoy, fn() S { drone() echo(0.4, 0.5, 0.5) }) await;
play(echoy) await;
drone stop;
delayReal(6.0) await;

"4: set freq to 165 (survives future redefines)" println;
drone set("freq", 165.0);
delayReal(4.0) await;

"5: notes into a voicer proxy" println;
define(keys, fn() S {
    voicer(8, fn() S {
        let f = noteParam("freq", espec(20.0, 2000.0, 440.0));
        let a = noteParam("amp", lspec(0.0, 1.0, 0.5));
        sinosc(f) * a * (gate() adsr(0.01, 0.1, 0.6, 0.4))
    }) sum
}) await;
play(keys, 0.7) await;
let pl = play(keys, List(event(0.0, 0.5, degree(0)),
                         event(0.5, 0.5, degree(2)),
                         event(1.0, 0.5, degree(4)),
                         event(1.5, 0.5, degree(7)),
                         event(2.0, 2.0, degree(9))));
delayReal(6.0) await;
pl stop;

"6: fade everything out (3s) and free" println;
endAll(3.0) await;
delayReal(1.0) await;
"done" println;
engineStop();
