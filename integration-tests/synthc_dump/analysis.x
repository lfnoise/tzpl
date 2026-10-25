-- Graphs from the retired synthc_analysis_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;

fn ctrlDelay() S {
	let freq = control("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
	let osc = (fs() / freq) sin;
	let vib = (fs() / freq) cos;
	let d = delayVar();
	d init(1, 0.0);
	let r = d read(1);
	let mixed = osc * 0.5 + vib * 0.5 + r;
	d write(mixed);
	mixed outlet
}
fn twoCtrl() S {
	let f = control("freq", ControlSpec { lo: 20.0, hi: 2000.0, init: 440.0, warp: ControlWarp.linear });
	let a = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	((fs() / f) sin * a) outlet
}
fn vdelayCtrl() S {
	let d = delayVar(0.05);
	d init(1, 0.0);
	let dt = control("dt", ControlSpec { lo: 0.0, hi: 0.05, init: 0.01, warp: ControlWarp.linear });
	let inp = (fs() / 440.0) sin;
	let r = d vread(dt, Interpolation.cubic);
	d write(inp * 0.5 + r * 0.9);
	r outlet
}
fn integrator() S {
	let osc = (fs() / 440.0) sin;
	let d = delayVar();
	d init(1, 0.0);
	let r = d read(1);
	let mixed = osc * 0.5 + r;
	d write(mixed);
	mixed outlet
}
fn stereo() S {
	let b = (fs() / [110.0, 110.5]) sin;
	(b * 0.3) outlet
}
fn folded() S {
	let freq = 440.0 * 2.0;               -- -> 880.0
	let amp = [0.5, 0.25] f32;            -- cast-on-constant fold
	let osc = (fs() / freq) sin;
	(osc * amp) outlet
}

dumpCorpus([
	("ctrl_delay", ctrlDelay),
	("two_ctrl", twoCtrl),
	("vdelay_ctrl", vdelayCtrl),
	("integrator", integrator),
	("stereo", stereo),
	("folded", folded),
	("initrate", fn() S { ((fs() / 440.0) sin * 0.2) outlet }),
]);
