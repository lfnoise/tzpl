-- ui_widgets.x -- a tour of the `ui` module's input widgets.
--
-- Run: evaluate this file in the app (or paste into a notebook code
-- cell). No synth is needed; widgets appear in floating panel windows,
-- or inline if a notebook panel cell claims the panel name.
--
-- See lang/docs/Live_Controls_and_Notebooks.html for the full guide.

import ui.*;
import synthdef.*;

-- Widgets land in the default "Controls" panel until panel() is called.
let cutoff = slider("cutoff", ControlSpec { lo: 20.0, hi: 20000.0, init: 440.0, warp: ControlWarp.exponential });
let res = slider("resonance", 0.0, 1.0, 0.3);   -- linear sugar form
let gain = number("gain", 0.5);
let mute = toggle("mute", false);
let kick = button("kick");

-- A 2-D pad with an independent ControlSpec per axis.
let filt = xy("filter",
	ControlSpec { lo: 20.0, hi: 20000.0, init: 1000.0, warp: ControlWarp.exponential },
	ControlSpec { lo: 0.1, hi: 4.0, init: 1.0, warp: ControlWarp.linear });

-- Lang callbacks, coalesced to the latest value once per GUI frame.
cutoff onChange(fn(v Float) Void { "cutoff -> %^" fmt(v) println; });
kick onChange(fn(v Float) Void { "kick %^" fmt(v) println; });

-- Subsequent constructors target the named panel.
panel("Mixer");
let ch1 = slider("ch1", 0.0, 1.0, 0.8);
let ch2 = slider("ch2", 0.0, 1.0, 0.6);

-- Pattern-editing widgets (vector-valued; read with values()/notes()).
panel("Pattern");
let env = multislider("env", 8, ControlSpec { lo: 0.0, hi: 1.0, init: 0.0, warp: ControlWarp.linear });
let steps = matrix("steps", 4, 16);
let melody = pianoRoll("melody", 4.0);
melody setNotes([60.0, 0.0, 0.5, 64.0, 1.0, 0.5, 67.0, 2.0, 1.0]);
let note = label("hint", "paint env, click steps, edit notes");

env onChangeVec(fn(vs [Float]) Void { "env -> %^" fmt(vs) println; });

panel("");
"widgets ready" println;
