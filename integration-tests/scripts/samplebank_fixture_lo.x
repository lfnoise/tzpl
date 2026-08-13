-- samplebank end-to-end (fixture half): a 440 Hz sine rendered to a wav
-- that run_samplebank_test.sh then loads as the low zone of a sample bank.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn sineLo() S {
	sinosc(440.0) outlet
}

defSynthX(sineLo, "sbFixLo");
begin();
newNode("sbFixLo", 100);
connect(100, 0, 0, 0);
sched(0);
