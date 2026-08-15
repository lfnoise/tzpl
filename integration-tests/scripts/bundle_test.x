-- bundle_test: first-class command bundles (bundles.x).
-- Runs entirely at the top level with no audio stream, so the harness
-- exercises it fully: commands execute synchronously at submit.
import audio_engine.*;
import bundles.*;

println("--- bundle_test ---------------------------");

-- Build a bundle as a plain value.
let b = bundle()
	newNode("sinosc", 501)
	setInput(501, 0, 240.0)
	setInputX(501, 1, 0.1, 0.2, FadeCurve.fadeLinear)
	connect(501, 0, 0, 0);

println(b toString);
"length: %^" fmt(b length) println;

-- Submit it to silo 0 immediately.
"go(0) err: %^ (expect 0)" fmt(b go(0)) println;

-- Tear down with a second bundle.
"free err: %^ (expect 0)" fmt(bundle() freeNode(501) go(0)) println;

-- A bad bundle fails at submit and is discarded atomically.
let bad = bundle()
	newNode("no_such_def", 502)
	connect(502, 0, 0, 0);
"bad bundle err: %^ (expect nonzero)" fmt(bad go(0)) println;

-- The abort left nothing behind: node ID 502 is still available.
"recreate err: %^ (expect 0)" fmt(bundle() newNode("sinosc", 502) freeNode(502) go(0)) println;

-- The same Bundle value can be scheduled on a clock as well.
let again = bundle() newNode("sinosc", 503) freeNode(503);
"sched err: %^ (expect 0)" fmt(again sched(0, 0, 4.0)) println;

-- setControl by name resolves against the node's def at submit. The built-in
-- sinosc def has no controls, so the name can't resolve and the bundle
-- aborts atomically (errControlNotFound); the node never appears.
let byName = bundle()
	newNode("sinosc", 504)
	setControl(504, "freq", 330.0);
println(byName toString);
"by-name on def without controls err: %^ (expect nonzero)" fmt(byName go(0)) println;
"node 504 free after abort err: %^ (expect 0)" fmt(bundle() newNode("sinosc", 504) freeNode(504) go(0)) println;
