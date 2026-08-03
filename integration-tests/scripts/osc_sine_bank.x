-- osc end-to-end (table half): a wavetable bank built from a single sine
-- partial makes every band-limited table a pure sine, so osc at a fixed
-- frequency must match sinosc to interpolation error. Rendered by
-- run_osc_test.sh.
import synthdef.*;
import common_ugens.*;
import wavetables.*;
import synthc.compile.*;
import audio_engine.*;

let bank = oscTables([1.0], [Float](), 0.0, 2048, 8);

fn oscSine() S {
	let b = bufferVar();
	b osc(440.0, 0, 2048, 8) outlet
}

defSynthX(oscSine, "oscSineBank");
begin();
newNode("oscSineBank", 100);
fillBuffer(100, 0, 1, bank);
connect(100, 0, 0, 0);
sched(0);
