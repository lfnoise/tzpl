-- Tier-3 A/B variable-delay half (synthc): the SAME variable-delay synth as
-- synthc_render_ab_vdelay_ref.x, compiled with the Tzopilotl-hosted compiler
-- (defSynthX). The rendered audio must be bit-identical to the reference half.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

-- A variable (signal-rate) delay read with cubic interpolation, exercising the
-- runtime-sized ring buffer (calloc/mask), the tzpl_delay_cubic kernel, and a
-- stable feedback path.
fn vdelayVoice() S {
	let osc = sinosc(220.0);
	let d = delayVar(0.05);
	d init(1, 0.0);
	let dt = (sinosc(3.0) * 0.01 + 0.02);
	let r = d vread(dt, Interpolation.cubic);
	d write(osc * 0.5 + r * 0.4);
	r outlet
}
defSynthX(vdelayVoice, "ab_vdelay");

begin();
newNode("ab_vdelay", 100);
connect(100, 0, 0, 0);
sched(0);
