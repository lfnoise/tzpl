-- Tier-3 A/B (synthc half, control flow): the SAME if_ synth via defSynthX. The
-- rendered audio must be bit-identical to the reference half.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;
fn cfsyn() S {
	let sig = (0.3 lfsaw) * 0.3;
	if_(sig > 0.0, fn() { sig }, fn() { sig * 0.5 }) |> outlet
}
defSynthX(cfsyn, "ab_cf");
begin();
newNode("ab_cf", 100);
connect(100, 0, 0, 0);
sched(0);
