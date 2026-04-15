
import common_ugens.*;
import synthdef.*;

fn blipTest() S {
	let h = 1/30 sinosc(0.75) bilin(1, 48);
	[36, 36.13] blip(0, h) mul(0.2) fadein(0.1) outlet
}


blipTest defSynth("blipTest") println;

go(coro fn() Float {
	"start playing" println;
	"blipTest" playFor(30) yieldAll;
	"done playing" println;
}());

"done evaluating" println;




