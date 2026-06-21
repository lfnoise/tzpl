-- Tier-3 A/B control half (synthc): the SAME control-bearing synth as
-- synthc_render_ab_ctrl_ref.x, compiled with the Tzopilotl-hosted compiler
-- (defSynthX) and driven by the identical scheduled control changes. The
-- rendered audio must be bit-identical to the reference half.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn ctrlVoice() S {
	let freq = control("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.2, warp: ControlWarp.linear });
	(sinosc(freq) * amp) outlet
}
defSynthX(ctrlVoice, "ab_ctrl");

begin(0);
newNode("ab_ctrl", 100);
connect(100, 0, 0, 0);
setControl(100, 0, 330.0);
setControl(100, 1, 0.3);
sched();
begin(0); setControl(100, 0, 660.0); sched(0.04);
begin(0); setControl(100, 1, 0.15); sched(0.07);
