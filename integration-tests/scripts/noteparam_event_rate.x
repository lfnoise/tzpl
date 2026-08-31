-- NoteParam event-rate sample accuracy: a mid-note noteSetParams must land
-- exactly on its scheduled sample. The voice emits its noteParam as DC while
-- gated, so the rendered WAV is a step function whose edges pin down when
-- the event-rate recompute (np_active -> processEvents -> materialized
-- per-voice value) took effect. Rendered by `tzpl_app --nrt`; the checker in
-- run_noteparam_event_test.sh asserts the exact step samples.
import synthdef.*;
import common_ugens.*;
import audio_engine.*;

fn npVoice() S {
	voicer(4, fn() S {
		let v = noteParam("val", ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
		v * gate()
	}) sum outlet
}
npVoice defSynth("np_event");

safetyLimiter(false);
begin();
newNode("np_event", 100);
connect(100, 0, 0, 0);
noteOn(100, 1, [0.25]);
sched(0);
-- Mid-note param changes at 0.04 s and 0.07 s (60 BPM: beats are seconds).
begin(); noteSetParams(100, 1, 0, [0.75]); sched(0, 0, 0.04);
begin(); noteSetParams(100, 1, 0, [0.5]); sched(0, 0, 0.07);
