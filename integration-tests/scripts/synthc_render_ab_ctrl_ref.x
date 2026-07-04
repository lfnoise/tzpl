-- Tier-3 A/B control half (reference): a control-bearing synth compiled with
-- the C++ compiler (defSynth) and driven by scheduled control changes. Rendered
-- to WAV by `tzpl_app --nrt`. The synthc half (synthc_render_ab_ctrl_synthc.x)
-- builds the identical graph with defSynthX; the two renders must be
-- bit-identical. See run_synthc_render_ab.sh.
--
-- The synth is a sine oscillator whose frequency and amplitude are both
-- event-rate controls, so it exercises the full control path: two iso-groups,
-- per-control activation, and processEvents -> instance-var -> audio-loop
-- propagation in the engine.
import synthdef.*;
import common_ugens.*;
import audio_engine.*;

fn ctrlVoice() S {
	let freq = control("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.2, warp: ControlWarp.linear });
	(sinosc(freq) * amp) outlet
}
ctrlVoice defSynth("ab_ctrl");

begin();
newNode("ab_ctrl", 100);
connect(100, 0, 0, 0);
setControl(100, 0, 330.0);
setControl(100, 1, 0.3);
sched(0);
-- Scheduled control changes exercise dynamic, sample-accurate control updates.
begin(); setControl(100, 0, 660.0); sched(0, 0, 0.04);
begin(); setControl(100, 1, 0.15); sched(0, 0, 0.07);
