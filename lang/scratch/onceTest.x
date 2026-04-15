import common_ugens.*;
import synthdef.*;

fn t0() S {
	let y = delayVar() init(1, 1);
	y <- 0;
	y(1)
}

fn onceTest() S {
	t0() debug("t0", 1, 1) outlet;
}

onceTest defSynth("onceTest") println;

go(coro fn() Float {
	"start playing" println;
	"onceTest" playFor(0.2) yieldAll;
	"done playing" println;
}());

"done evaluating" println;















