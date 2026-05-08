import common_ugens.*;
import synthdef.*;
import clock.*;

fn seqTest() S {
	-- let middleC = 261.6255653;
	let middleC = 256;
	let pattern = [1/1, 6/5, 3/2, 9/5, 2/1, 12/5, 3, 18/5] * middleC;
	-- equivalently: let pattern = [10, 12, 15, 18, 20, 24, 30, 36]/10 * middleC;
	let detune = [-0.04, 0.04];
	( 
		  (1 lfimp seq(pattern * 0.25, 8) + detune) smoothSaw(5) * 1.4
		+ (2 lfimp seq(pattern * 1.0,  8) + detune) smoothSaw(4) 
		+ (4 lfimp seq(pattern * 2.0,  8) + detune) smoothSaw(3) * 0.7 
	)
	* 0.2 |> combn(3/8, 4) outlet
}

seqTest defSynth("seqTest") println;

go(coro fn() Float {
	"start playing" println;
	"seqTest" playFor(24.0) yieldAll;
	"done playing" println;
}());















