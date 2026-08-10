-- NRT <-> engine TempoClock sync: clock-module callbacks and delayBeats are
-- slaved to the ENGINE's TempoClock slots (silo 0's copy of the clocks the
-- silos schedule against), firing latency-early as the actual slot reaches
-- the beat. Run with --tempo-clocks 2 (slot 1 cases). Self-checking.
import audio_engine.*;
import clock.*;

-- 1. A handler scheduled at an absolute beat on slot 0 fires when the ENGINE
-- clock (read via the audio_engine clockBeats FFI, not the scheduler) is at
-- that beat, within the latency lead + polling tolerance.
var fired [Float] = [];
let target = clockBeats(0) ceil + 2.0;
at(0, target, fn() Void { fired push!(clockBeats(0)); });
await delayBeats(0, 3.5);
if (fired length == 1 && fired[0] >= target - 0.3 && fired[0] <= target + 0.2) {
    println("CLOCKSYNC slot0 handler: OK");
} else {
    println("CLOCKSYNC slot0 handler: BAD fired "
        $ (fired length > 0 ? fired[0] toString : "never") $ " target " $ target toString);
}

-- 2. Slot 1 at a different tempo, set through the AUDIO_ENGINE FFI (the path
-- that never touched the NRT scheduler before): 4 beats at 240 BPM is ~1s.
setTempo(1, 240.0);
let t1 = getStreamTime();
await delayBeats(1, 4.0);
let dt1 = getStreamTime() - t1;
if (dt1 >= 0.55 && dt1 < 2.0) { println("CLOCKSYNC slot1 240bpm: OK"); }
else { println("CLOCKSYNC slot1 240bpm: BAD dt " $ dt1 toString); }

-- 3. An ENGINE-side tempo change made DURING an NRT wait moves the wait:
-- start 3 beats at 60 BPM (~3s), jump slot 0 to 600 BPM ~0.3s in via the
-- audio_engine FFI from a wall-clock task; the remaining beats collapse.
async fn changer() Void {
    await delayReal(0.3);
    setTempo(0, 600.0);
}
let ch = changer();
let t2 = getStreamTime();
await delayBeats(0, 3.0);
let dt2 = getStreamTime() - t2;
setTempo(0, 60.0);
await ch;
if (dt2 >= 0.25 && dt2 < 1.5) { println("CLOCKSYNC engine tempo change: OK"); }
else { println("CLOCKSYNC engine tempo change: BAD dt " $ dt2 toString); }

-- 4. getBeats(clock) reads the engine slot: after the slot-1 tempo episode,
-- slot 1 should be well ahead of slot 0 in beats.
if (getBeats(1) > getBeats(0) + 1.0) { println("CLOCKSYNC getBeats slots: OK"); }
else {
    println("CLOCKSYNC getBeats slots: BAD b0 " $ getBeats(0) toString
        $ " b1 " $ getBeats(1) toString);
}
