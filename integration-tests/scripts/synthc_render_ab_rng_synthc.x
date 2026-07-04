-- Tier-3 A/B (synthc half, RNG): compile the SAME RNG-driven synth with the
-- Tzopilotl-hosted compiler (defSynthX) and play it. Rendered under the same
-- TZPL_RNG_SEED as the reference half, so the random stream matches and the
-- audio must be bit-identical.
import synthdef.*;
import synthc.compile.*;
import audio_engine.*;
fn rnoise() S {
	let n = birand(1) * 0.2;
	let y = delayVar(); y init(1, 0.0);
	let fb = y read(1) * 0.9 + n * 0.1;
	y write(fb); fb outlet
}
defSynthX(rnoise, "ab_rnoise");
begin();
newNode("ab_rnoise", 100);
connect(100, 0, 0, 0);
sched(0);
