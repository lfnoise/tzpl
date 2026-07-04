-- Tier-3 A/B (reference half, control flow): an if_-driven waveshaper compiled
-- by the C++ compiler. synthc's generated C++ byte-matches, so the audio must be
-- bit-identical.
import synthdef.*;
import common_ugens.*;
import audio_engine.*;
fn cfsyn() S {
	let sig = (0.3 lfsaw) * 0.3;
	if_(sig > 0.0, fn() { sig }, fn() { sig * 0.5 }) |> outlet
}
cfsyn defSynth("ab_cf");
begin();
newNode("ab_cf", 100);
connect(100, 0, 0, 0);
sched(0);
