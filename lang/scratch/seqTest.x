import common_ugens.*;
import synthdef.*;

fn seqTest() S {
	let pattern = [60, 63, 67, 70, 74, 77, 81, 84] nnhz * 0.5;
	( 
--		1.5 lfimp seq(pattern * 0.25, 8) sinosc cb * 1.5 +
		3.0 lfimp seq(pattern * 1.0, 8) sinosc cb 
--		+ 6.0 lfimp seq(pattern * 4.0, 8) sinosc * 0.25 
	)
	* 0.25 |> combn(2/3, 4) outlet
}

seqTest defSynth("seqTest") println;

go(coro fn() Float {
	"start playing" println;
	"seqTest" playFor(24.0) yieldAll;
	"done playing" println;
}());

"done evaluating" println;



