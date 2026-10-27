-- Note parameters are columns of the voiced synth's `notes` input; the
-- engine writes a voice's row as input-stage steps and ramps. A mid-note
-- noteSetParams must land exactly on its scheduled sample, a ramped one must
-- ramp linearly from the current value, and each voice's row must stay its
-- own. The voice emits its noteParam as DC while gated, so the rendered WAV
-- (left: the sum of voices) is a step/ramp function. Rendered by
-- `tzpl_app --nrt`; the checker in run_noteparam_event_test.sh asserts the
-- exact samples.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;

fn npVoice() S {
	voicer(4, fn() S {
		let v = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
		v * gate()
	}) sum outlet
}
npVoice defSynth("np_event") await;

safetyLimiter(false);
begin();
newNode("np_event", 100);
connect(100, 0, 0, 0);
noteOn(100, 1, [0.25]);
sched(0);
-- Mid-note param changes at 0.04 s and 0.07 s (60 BPM: beats are seconds).
begin(); noteSetParams(100, 1, 0, [0.75]); sched(0, 0, 0.04);
begin(); noteSetParams(100, 1, 0, [0.5]); sched(0, 0, 0.07);
-- A second voice adds 0.125 from 0.09 s; it must not disturb voice 1's row.
begin(); noteOn(100, 2, [0.125]); sched(0, 0, 0.09);
-- Voice 1 ramps 0.5 -> 1.0 over 20 ms from 0.10 s.
begin(); noteSetParams(100, 1, 0, [1.0], 0.02); sched(0, 0, 0.10);
