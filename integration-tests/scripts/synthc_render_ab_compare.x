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
_check("vdelay", "/tmp/synthc_ab_vdelay_ref.wav", "/tmp/synthc_ab_vdelay_synthc.wav");
_check("bubbles", "/tmp/synthc_ab_bubbles_ref.wav", "/tmp/synthc_ab_bubbles_synthc.wav");
_check("rng", "/tmp/synthc_ab_rng_ref.wav", "/tmp/synthc_ab_rng_synthc.wav");
_check("cf", "/tmp/synthc_ab_cf_ref.wav", "/tmp/synthc_ab_cf_synthc.wav");
-- merge: synthc collapses two identical delay lines into one (Faust-style); must
-- render bit-identically to the C++ two-buffer build.
_check("merge", "/tmp/synthc_ab_merge_ref.wav", "/tmp/synthc_ab_merge_synthc.wav");

-- Spectral: no C++ reference (the C++ compiler's SIMD spectral codegen is
-- broken), so assert the synthc render is non-silent and didn't crash.
fn _checkNonSilent(label String, path String) Void {
	let m = audioFileMaxAbs(path);
	if (m > 0.0001) {
		println("  PASS " $ label $ ": non-silent end-to-end (maxAbs=" $ m toString $ ")");
	} else {
		`fail = `fail + 1;
		println("  FAIL " $ label $ ": silent / not rendered (maxAbs=" $ m toString $ ")");
	}
}
_checkNonSilent("spectral", "/tmp/synthc_ab_spectral_x.wav");

if (`fail == 0) {
	println("RENDER AB PASS");
} else {
	println("RENDER AB FAIL");
}
