-- Tier-3 A/B (reference half, RNG): compile an RNG-driven synth with the C++
-- compiler (defSynth) and play it. Rendered under TZPL_RNG_SEED so the random
-- stream is deterministic; the synthc half seeds identically, so the audio must
-- be bit-identical. A leaky integrator driven by bipolar noise exercises both
-- the RandState/arc4seedrand init path and the urand/birand expression path.
import synthdef.*;
import audio_engine.*;
fn rnoise() S {
	let n = birand(1) * 0.2;
	let y = delayVar(); y init(1, 0.0);
	let fb = y read(1) * 0.9 + n * 0.1;
	y write(fb); fb outlet
}
rnoise defSynth("ab_rnoise");
begin(0);
newNode("ab_rnoise", 100);
connect(100, 0, 0, 0);
sched();
