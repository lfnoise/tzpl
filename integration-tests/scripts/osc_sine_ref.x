-- osc end-to-end (reference half): the same tone from sinosc.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn oscRef() S {
	sinosc(440.0) outlet
}

defSynthX(oscRef, "oscSineRef");
begin();
newNode("oscSineRef", 100);
connect(100, 0, 0, 0);
sched(0);
