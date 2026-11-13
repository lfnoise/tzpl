-- nrt_handler_timing.x -- rendered by run_nrt_handler_timing_test.sh.
-- In an NRT render, a bundle a clock handler schedules at its own beat
-- (getBeats() inside the handler) must land on that beat's sample, as it
-- does live. The renderer used to fire handlers after the block holding
-- their beat, so every such bundle landed late, on the next block boundary.
--
-- 73 bpm keeps the beat grid off the block grid. Two DC probes: the left
-- channel is stepped by a sched() handler, the right by an async fn
-- parked in `await delayBeats`. The safety limiter is off: its one-block
-- lookahead delays the whole output, which would offset every step.

import synthdef.*;
import common_ugens.*;
import audio_engine.*;
import bundles.*;
import live.*;
import clock.*;

safetyLimiter(false);
fadeTime(0.0);
setTempo(73.0);

let probe = ndef(2);
probe <- fn() S {
    [control("l", lspec(0.0, 1.0, 0.0)), control("r", lspec(0.0, 1.0, 0.0))] join
};
probe play;

fn setOnBeat(p Proxy, name String, v Float) Void {
    let s = *p.state;
    bundle() setInput(s.src, name, v) sched(p.silo, 0, getBeats());
}

-- Left: handler at beats 0.25, 0.75, 1.25, ... alternating 0.1 / 0.3.
let n = &0;
sched(0.25, fn() Float {
    probe setOnBeat("l", (*n % 2 == 0) ? 0.1 : 0.3);
    n <- (*n + 1);
    0.5
});

-- Right: async loop at beats 0.4, 1.1, 1.8, ... alternating 0.2 / 0.6.
async fn rightSteps() Void {
    await delayBeats(0.4);
    var k = 0;
    while (k < 8) {
        probe setOnBeat("r", (k % 2 == 0) ? 0.2 : 0.6);
        k = k + 1;
        await delayBeats(0.7);
    }
}
rightSteps();
