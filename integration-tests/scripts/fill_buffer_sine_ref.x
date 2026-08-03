-- fillBuffer end-to-end (reference half): the same tone as
-- fill_buffer_sine_tab.x, generated directly by sinosc.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

fn sineRef() S {
	sinosc(440.0) outlet
}

defSynthX(sineRef, "fbSineRef");
begin();
newNode("fbSineRef", 100);
connect(100, 0, 0, 0);
sched(0);
