-- player_quant_render.x -- music.play quantized play/replace, rendered
-- offline (run via run_player_quant_test.sh). Each stream plays its own
-- pitch so player_quant_check.py can find when each one sounds:
--
--   beat 0.3  play A (440 Hz, 3-beat notes)          quant 1 -> starts at 1
--   beat 1.3  replace B (880 Hz, half beats)         quant 1 -> switch at 2
--             (A's note from beat 1 is cut at 2; A's beat-4 onset never plays)
--   beat 4.6  replace C (1320 Hz, whole beats)       quant 4 -> switch at 8
--   beat 8.2  replace D (660 Hz)                     quant 4 -> due at 12
--   beat 8.4  replace E (990 Hz)                     quant 1 -> switch at 9
--             (E lands before D, so D must never sound and C ends at 9)
--
-- The pre-fix player anchored a replaced stream at the call's beat but only
-- read it at its next wake, so B would have started at beat 4 (A's next
-- onset), off the grid.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import clock.*;
import audio_engine.*;
import music.pat.*;
import music.play.*;

fn sineVoice() S {
    let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 4000.0, init: 440.0, warp: ControlWarp.exponential });
    let a = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.3, warp: ControlWarp.linear });
    let env = gate() asr(0.002, 1.0, 0.005);
    f sinosc * env * a
}
fn sineSynth() S = voicer(8, sineVoice) sum outlet;
sineSynth defSynth("pq_sine") await;

setTempo(60.0);
safetyLimiter(false);
("PQ latency " $ getLatency() toString) println;

fn tone(f Float, dur Float, legato Float) List<Event> =
    bind(List(hz(f)) cyc, List(dur) cyc, List(0.3) cyc, List(legato) cyc);

let v = freqVoice(101, 8);
voiceBundle(v, "pq_sine") go(0);

coro fn session() Float {
    yield 0.3;
    let p = play(tone(440.0, 3.0, 0.9), v, et12, major, 0, 0, 1.0);
    yield 1.0;                                  -- beat 1.3
    p replace(tone(880.0, 0.5, 0.5), 1.0);
    yield 3.3;                                  -- beat 4.6
    p replace(tone(1320.0, 1.0, 0.5), 4.0);
    yield 3.6;                                  -- beat 8.2
    p replace(tone(660.0, 1.0, 0.5), 4.0);
    yield 0.2;                                  -- beat 8.4
    p replace(tone(990.0, 1.0, 0.5), 1.0);
}
go(session());
