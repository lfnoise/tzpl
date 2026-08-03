-- Comparator for run_osc_test.sh.
import synthdef.*;

var `fail = 0;

let refMax = audioFileMaxAbs("/tmp/osc_sine_ref.wav");
let diff = compareAudioFiles("/tmp/osc_sine_ref.wav", "/tmp/osc_sine_bank.wav");
if (refMax < 0.5) {
	`fail = `fail + 1;
	println("  FAIL sine: reference render is silent (maxAbs=" $ refMax toString $ ")");
} else if (diff < 0.0001) {
	println("  PASS sine: osc over a sine bank matches sinosc (diff=" $ diff toString $ ")");
} else {
	`fail = `fail + 1;
	println("  FAIL sine: osc differs from sinosc (diff=" $ diff toString $ ")");
}

let sweepMax = audioFileMaxAbs("/tmp/osc_sweep.wav");
if (sweepMax > 0.1 && sweepMax < 1.1) {
	println("  PASS sweep: FM sweep in range (maxAbs=" $ sweepMax toString $ ")");
} else {
	`fail = `fail + 1;
	println("  FAIL sweep: FM sweep out of range (maxAbs=" $ sweepMax toString $ ")");
}

if (`fail == 0) {
	println("OSC PASS");
} else {
	println("OSC FAIL");
}
