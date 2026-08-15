-- ui_synth_controls.x -- derive a synth's whole interface from its def.
--
-- Every control declared in the synthdef carries its ControlSpec into
-- the compiled plugin, so `controls(node)` can materialize a panel of
-- correctly ranged, warped, engine-bound widgets -- no manual slider
-- setup. The panel is named after the def ("wobble" here); dragging a
-- widget sends setControl directly from the GUI thread (no VM entry),
-- so it stays live even while a long eval is running.
--
-- Run: evaluate this file in the app, with audio on.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;
import ui.*;

fn wobble() S {
	let freq = control("freq", ControlSpec { lo: 50.0, hi: 2000.0, init: 220.0, warp: ControlWarp.exponential });
	let amp = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.3, warp: ControlWarp.linear });
	let rate = control("rate", ControlSpec { lo: 0.1, hi: 20.0, init: 4.0, warp: ControlWarp.exponential });
	let vib = rate sinosc(0) mul(freq mul(0.01));
	freq add(vib) sinosc(0) mul(amp) outlet
}

wobble defSynthX("wobble") await;
masterGain(0.2);

let node = play("wobble");
"playing def: %^" fmt(node defName) println;

-- One widget per declared control, in a panel named after the def.
let ws = controls(node);
"materialized %^ controls" fmt(ws length) println;

-- Display widgets tap the node's output: a level meter and a scope.
let lvl = meter("level", node);
let sc = scope("out", node);

-- A single control can also be materialized by name, and values can be
-- set from code (fires the engine binding like a drag would).
let freqW = control(node, "freq");
freqW setValue(330.0);

-- Or skip the widget: setControl also addresses controls by name,
-- resolved against the node's def when the bundle is submitted. (The
-- widget doesn't see this value; it moves only on drags and setValue.)
begin();
setControl(node, "rate", 6.0);
sched(0);

"interface ready -- drag away" println;
