import synthdef.*;
import common_ugens.*;

fn bubbles() S =
	0.4 lfsaw * 24
	+ [8, 7.23] lfsaw * 3
	+ 81
	|> nnhz sinosc * 0.04
	|> combn(0.2, 4) outlet;

bubbles defSynth("bubbles") println;

