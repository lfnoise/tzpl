-- osc FM sweep: a saw bank swept 20 Hz -> 10 kHz exercises the per-sample
-- table re-selection and the smoothStep crossfade between adjacent tables.
-- The comparator asserts the render is non-silent and stays within the
-- normalized +-1 table range (NaNs or a broken crossfade would violate it).
import synthdef.*;
import common_ugens.*;
import wavetables.*;
import synthc.compile.*;
import audio_engine.*;

let bank = sawTables(0.0, 2048, 24);

fn oscSweep() S {
	let b = bufferVar();
	let freq = phasor(10.0) linexp(0.0, 1.0, 20.0, 10000.0);
	b osc(freq, 0, 2048, 24) outlet
}

defSynthX(oscSweep, "oscSweep");
begin();
newNode("oscSweep", 100);
fillBuffer(100, 0, 1, bank);
connect(100, 0, 0, 0);
sched(0);
