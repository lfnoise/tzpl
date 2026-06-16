-- Tier-3 A/B (synthc half): compile the SAME synth with the Tzopilotl-hosted
-- compiler (defSynthX) and play it. The rendered audio must be bit-identical to
-- the reference half.
import synthdef.*;
import synthc.compile.*;
import audio_engine.*;
fn voice() S {
	let drive = (fs() / 440.0) sin * 0.001;
	let y = delayVar(); y init(1, 0.0);
	let fb = y read(1) * 0.95 + drive;
	y write(fb); fb outlet
}
defSynthX(voice, "ab_voice");
begin(0);
newNode("ab_voice", 100);
connect(100, 0, 0, 0);
sched();
