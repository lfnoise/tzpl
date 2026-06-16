-- Tier-3 A/B variable-delay half (reference): a cubic-interpolated variable
-- delay read, compiled with the C++ compiler (defSynth). The synthc half builds
-- the identical graph with defSynthX; the two renders must be bit-identical.
import synthdef.*;
import common_ugens.*;
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
vdelayVoice defSynth("ab_vdelay");

begin(0);
newNode("ab_vdelay", 100);
connect(100, 0, 0, 0);
sched();
