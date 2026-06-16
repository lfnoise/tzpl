-- Tier-3 A/B comparator: assert the synthc-compiled renders are bit-identical
-- to the C++-compiled references, for both an audio-rate synth and a
-- control-bearing synth. Also asserts each reference render is non-silent, so a
-- silence-vs-silence comparison can never pass vacuously.
import synthdef.*;

var `fail = 0;

fn _check(label String, refPath String, synPath String) Void {
	let refMax = audioFileMaxAbs(refPath);
	let diff = compareAudioFiles(refPath, synPath);
	if (refMax < 0.0001) {
		`fail = `fail + 1;
		println("  FAIL " $ label $ ": reference render is silent (maxAbs=" $ refMax toString $ ")");
	} else if (diff < 0.000000001) {
		println("  PASS " $ label $ ": bit-identical (maxAbs=" $ refMax toString $ ", diff=" $ diff toString $ ")");
	} else {
		`fail = `fail + 1;
		println("  FAIL " $ label $ ": differs (max sample diff=" $ diff toString $ ")");
	}
}

_check("audio", "/tmp/synthc_ab_ref.wav", "/tmp/synthc_ab_synthc.wav");
_check("control", "/tmp/synthc_ab_ctrl_ref.wav", "/tmp/synthc_ab_ctrl_synthc.wav");

if (`fail == 0) {
	println("RENDER AB PASS");
} else {
	println("RENDER AB FAIL");
}
