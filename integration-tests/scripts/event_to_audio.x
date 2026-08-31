-- eventToAudio + event-rate trigger semantics, rendered as a step/impulse
-- pattern the checker in run_event_to_audio_test.sh pins to exact samples.
--
-- L: `c eventToAudio tr` -- the promoted (audio-rate) trigger fires a
--    ONE-SAMPLE impulse on the exact sample of each 0 -> positive control
--    change. Multiplied by eventToAudio(1.0) to prove the builder passes
--    constants through unchanged (identity).
-- R: `c tr` -- the pure event-rate trigger is a HELD value: 1 from a
--    0 -> positive change until the next control event clears it. Guards the
--    delay-read activation fix (the z1 read re-runs per event; before the
--    fix it never ran and the trigger never cleared).
import synthdef.*;
import common_ugens.*;
import audio_engine.*;

fn e2aDemo() S {
	let c = control("trig", ControlSpec { lo: 0.0, hi: 10.0, init: 0.0, warp: ControlWarp.linear });
	let imp = (c eventToAudio) tr * eventToAudio(1.0);
	let held = (c tr) * (1.0 + 1.0e-30 * white());
	[imp, held] join outlet
}
e2aDemo defSynth("e2a_demo");

safetyLimiter(false);
begin();
newNode("e2a_demo", 100);
connect(100, 0, 0, 0);
sched(0);
-- 60 BPM: beats are seconds.
begin(); setControl(100, 0, 1.0); sched(0, 0, 0.04);   -- 0 -> 1: trigger
begin(); setControl(100, 0, 0.5); sched(0, 0, 0.07);   -- 1 -> 0.5: no trigger
begin(); setControl(100, 0, 0.0); sched(0, 0, 0.10);   -- release: no trigger
begin(); setControl(100, 0, 2.0); sched(0, 0, 0.12);   -- 0 -> 2: trigger
