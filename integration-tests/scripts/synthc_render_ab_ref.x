-- Tier-3 A/B (reference half): compile a synth with the C++ compiler (defSynth)
-- and play it. Rendered to WAV by `tzpl_app --nrt`; the whole script is the
-- render setup. See run_synthc_render_ab.sh.
import synthdef.*;
import audio_engine.*;
fn voice() S {
	let drive = (fs() / 440.0) sin * 0.001;
	let y = delayVar(); y init(1, 0.0);
	let fb = y read(1) * 0.95 + drive;
	y write(fb); fb outlet
}
voice defSynth("ab_voice");
begin();
newNode("ab_voice", 100);
connect(100, 0, 0, 0);
sched(0);
