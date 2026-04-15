import common_ugens.*;
import synthdef.*;

fn lfimpTest() = lfimp(1) mul(0.2) outlet;

lfimpTest defSynth("lfimpTest") println;

go(coro fn() Float {
	"start playing" println;
	"lfimpTest" playFor(4.1) yieldAll;
	"done playing" println;
}());




