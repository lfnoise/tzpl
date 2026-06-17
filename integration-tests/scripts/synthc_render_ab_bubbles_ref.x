-- Tier-3 A/B named-synth half (reference): bubbles compiled with the C++
-- compiler (defSynth). The synthc half builds the identical graph with
-- defSynthX; the two renders must be bit-identical.
import synthdef.*;
import common_ugens.*;
import audio_engine.*;

-- bubbles (from example_synthdefs.x): an lfsaw-modulated sinosc through a comb
-- delay, with a multichannel modulator -- a named M1 exit synth exercising
-- oscillators, comb delay, and broadcasting.
fn bubbles() S =
	0.4 lfsaw * 24 + [8, 7.23] lfsaw * 3 + 81 |> nnhz sinosc * 0.04 |> combn(0.2, 4) outlet;
bubbles defSynth("ab_bubbles");

begin(0);
newNode("ab_bubbles", 100);
connect(100, 0, 0, 0);
sched();
