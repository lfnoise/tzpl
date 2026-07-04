-- Tier-3 A/B merge half (reference): a synth with two delay lines fed the IDENTICAL
-- signal + init, compiled by the C++ compiler (defSynth) -- which keeps two separate
-- buffers. The synthc half (synthc_render_ab_merge_synthc.x) builds the same graph
-- with defSynthX, which collapses the two lines into one (Faust-style delay sharing).
-- The two renders must be bit-identical: the merge is purely structural.
import synthdef.*;
import common_ugens.*;
import audio_engine.*;

fn mergesyn() S {
	let sig = (fs() / 220.0) sin * 0.001;
	let a = delayVar(); a init(4, 0.0);
	let b = delayVar(); b init(4, 0.0);
	a write(sig); b write(sig);
	(a read(1) * 0.6 + b read(4) * 0.4) outlet
}
mergesyn defSynth("ab_merge");

begin();
newNode("ab_merge", 100);
connect(100, 0, 0, 0);
sched(0);
