-- Input steps and impulses at exact samples (block engine; this replaces the
-- old eventToAudio / event-rate trigger render).
--
-- L: `c tr` on a control stepped by setInput -- a ONE-SAMPLE trigger on the
--    exact sample of each 0 -> positive step (controls are sampled inputs, so
--    a step is seen by the per-sample trigger with no event rate in between).
-- R: an input driven by impulseInput -- the value for one sample, then back
--    to the input's held value (0).
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;

fn e2aDemo() S {
	let c = control("trig", ControlSpec { lo: 0.0, hi: 10.0, init: 0.0, warp: ControlWarp.linear });
	let kick = control("kick", ControlSpec { lo: 0.0, hi: 10.0, init: 0.0, warp: ControlWarp.linear });
	[c tr, kick] join outlet
}
e2aDemo defSynthX("e2a_demo") await;

safetyLimiter(false);
begin();
newNode("e2a_demo", 100);
connect(100, 0, 0, 0);
sched(0);
-- 60 BPM: beats are seconds.
begin(); setInput(100, "trig", 1.0); impulseInput(100, "kick", 1.0); sched(0, 0, 0.04);
begin(); setInput(100, "trig", 0.5); impulseInput(100, "kick", 0.5); sched(0, 0, 0.07);
begin(); setInput(100, "trig", 0.0); sched(0, 0, 0.10);
begin(); setInput(100, "trig", 2.0); impulseInput(100, "kick", 0.25); sched(0, 0, 0.12);
