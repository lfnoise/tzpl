-- delayReal: awaitable WALL-CLOCK delay (clock module), vs the core
-- delay(beats) whose virtual-beat timeline a top-level await fast-forwards.
-- Self-checking; run via run_delayreal_test.sh (or directly with --nogui).
-- Elapsed time is measured with the engine's stream clock, which advances in
-- real time while audio runs.
import audio_engine.*;
import clock.*;

-- 1. Top-level await really waits.
let t0 = getStreamTime();
await delayReal(1.5);
let dt = getStreamTime() - t0;
if (dt >= 1.3 && dt < 5.0) { println("DELAYREAL top-level: OK"); }
else { println("DELAYREAL top-level: BAD dt " $ dt toString); }

-- 2. Works inside an async fn, chained.
async fn stepper() Int {
    await delayReal(0.4);
    await delayReal(0.4);
    42
}
let t1 = getStreamTime();
let v = await stepper();
let dt2 = getStreamTime() - t1;
if (v == 42 && dt2 >= 0.6 && dt2 < 4.0) { println("DELAYREAL async fn: OK"); }
else { println("DELAYREAL async fn: BAD v " $ v toString $ " dt " $ dt2 toString); }

-- 3. Zero/negative resolve immediately (no hang).
await delayReal(0.0);
await delayReal(0.0 - 1.0);
println("DELAYREAL zero/negative: OK");

-- 4. The core virtual-beat delay is unchanged: still instant at top level.
let t2 = getStreamTime();
await delay(100.0);
if (getStreamTime() - t2 < 1.0) { println("DELAYREAL virtual delay: unchanged"); }
else { println("DELAYREAL virtual delay: BAD (waited)"); }

-- 5. A tempo change DURING the wait must not move the wall deadline: the
-- delay queue is seconds-based, not beat-based. 0.2 beats at the default
-- 60 BPM = 0.2s in, the clock jumps to 600 BPM; the 1.5s wait must still
-- take ~1.5s (a beat-based deadline would collapse to ~0.33s).
import clock.*;
after(0.2, fn() Void { setTempo(600.0); });
let t3 = getStreamTime();
await delayReal(1.5);
let dt3 = getStreamTime() - t3;
setTempo(60.0);
if (dt3 >= 1.3 && dt3 < 5.0) { println("DELAYREAL tempo change: OK"); }
else { println("DELAYREAL tempo change: BAD dt " $ dt3 toString); }

-- 6. delayBeats waits in MUSICAL time on the live clock: 1 beat at the
-- default 60 BPM is ~1s of real time (minus the 50ms latency lead).
let t4 = getStreamTime();
await delayBeats(1.0);
let dt4 = getStreamTime() - t4;
if (dt4 >= 0.7 && dt4 < 4.0) { println("DELAYBEATS 60bpm: OK"); }
else { println("DELAYBEATS 60bpm: BAD dt " $ dt4 toString); }

-- 7. ...and it tracks the tempo: 2 beats at 240 BPM is ~0.5s, nowhere near
-- the 2s it would take on a tempo-deaf clock.
setTempo(240.0);
let t5 = getStreamTime();
await delayBeats(2.0);
let dt5 = getStreamTime() - t5;
setTempo(60.0);
if (dt5 >= 0.2 && dt5 < 1.2) { println("DELAYBEATS 240bpm: OK"); }
else { println("DELAYBEATS 240bpm: BAD dt " $ dt5 toString); }
