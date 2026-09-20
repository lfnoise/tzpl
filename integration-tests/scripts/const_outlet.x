-- A constant-only graph must be patchable into the audio graph.
--
-- `140 scalar outlet` used to yield an out port declared tzpl_constRate, and
-- Silo::connect requires the two ends' rates to match exactly (it does no
-- conversion), so EVERY connect of such a def failed with errRateMismatch --
-- engine error 20. That hit both compilers and both paths: plain
-- defSynth + play, and live's `y <- fn() S { 140 scalar }` swap bundle.
--
-- Both compilers now declare a sub-event-rate outlet audio rate, which is
-- exact: a const/init/reset source holds one value for the whole block. Event
-- rate is NOT promoted -- crossing that boundary is eventToAudio's job.
--
-- One compiler per render, selected by TZPL_CONST_MODE, because the engine's
-- output node takes a single multi-channel port -- two sources would sum and
-- a failure could not be attributed. run_const_outlet_test.sh renders both and
-- pins the DC level, which proves the connect succeeded AND that the declared
-- rate matches what the plugin actually writes: silence or a wrong value means
-- the port was connected but never filled.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;

fn dc() S { 0.25 scalar outlet }

let mode = match (getEnv("TZPL_CONST_MODE")) {
	Option.some(s): s;
	Option.none: "cpp";
};

if (mode == "x") {
	dc defSynthX("const_dc_x") await;
	safetyLimiter(false);
	begin();
	newNode("const_dc_x", 100);
	connect(100, 0, 0, 0);
	sched(0);
} else {
	dc defSynth("const_dc_cpp") await;
	safetyLimiter(false);
	begin();
	newNode("const_dc_cpp", 100);
	connect(100, 0, 0, 0);
	sched(0);
}
