-- samplebank end-to-end (fixture half): a 660 Hz sine rendered to a wav
-- that run_samplebank_test.sh then loads as the high zone of a sample bank.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn sineHi() S {
	sinosc(660.0) outlet
}

defSynthX(sineHi, "sbFixHi");
begin();
newNode("sbFixHi", 100);
connect(100, 0, 0, 0);
sched(0);
