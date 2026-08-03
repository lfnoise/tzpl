-- Comparator for run_fill_buffer_test.sh: asserts the wavetable render
-- matches sinosc to interpolation error, and the non-pow2 wrap renders are
-- bit-identical.
import synthdef.*;

var `fail = 0;

let sineMax = audioFileMaxAbs("/tmp/fb_sine_ref.wav");
let sineDiff = compareAudioFiles("/tmp/fb_sine_ref.wav", "/tmp/fb_sine_tab.wav");
if (sineMax < 0.5) {
	`fail = `fail + 1;
	println("  FAIL sine: reference render is silent (maxAbs=" $ sineMax toString $ ")");
} else if (sineDiff < 0.00001) {
	println("  PASS sine: wavetable matches sinosc (diff=" $ sineDiff toString $ ")");
} else {
	`fail = `fail + 1;
	println("  FAIL sine: wavetable differs from sinosc (diff=" $ sineDiff toString $ ")");
}

let wrapMax = audioFileMaxAbs("/tmp/fb_wrap6.wav");
let wrapDiff = compareAudioFiles("/tmp/fb_wrap6.wav", "/tmp/fb_wrap12.wav");
if (wrapMax < 0.0001) {
	`fail = `fail + 1;
	println("  FAIL wrap: render is silent (maxAbs=" $ wrapMax toString $ ")");
} else if (wrapDiff < 0.000000001) {
	println("  PASS wrap: non-pow2 buffer wraps at length (maxAbs=" $ wrapMax toString $ ")");
} else {
	`fail = `fail + 1;
	println("  FAIL wrap: 6-frame and 12-frame renders differ (diff=" $ wrapDiff toString $ ")");
}

if (`fail == 0) {
	println("FILL BUFFER PASS");
} else {
	println("FILL BUFFER FAIL");
}
