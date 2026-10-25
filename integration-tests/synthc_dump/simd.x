-- Graphs from the retired synthc_simd_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

let dtSpec = ControlSpec { lo: 0.0, hi: 1.0, init: 0.3, warp: ControlWarp.linear };

fn s4() S { (inlet(FLOAT32, 4) sin) * [0.1, 0.2, 0.3, 0.4] |> outlet }
fn s8() S { (inlet(FLOAT32, 8) sin) * 0.1 |> outlet }
fn srate() S { (inlet(FLOAT32, 4) * fs()) sin |> outlet }
fn mx() S { (inlet(FLOAT32, 4) max(0.0)) * [0.5, 0.6, 0.7, 0.8] |> outlet }
fn add2() S { inlet(FLOAT32, 8) + (inlet(FLOAT32, 8) tanh) |> outlet }
fn cst() S { (inlet(FLOAT32, 4) * [1.0, 2.0, 3.0, 4.0]) + [10.0, 20.0, 30.0, 40.0] |> outlet }
fn cast4() S { ((inlet(FLOAT32, 4) f64) sin) f32 |> outlet }
fn ctl() S {
	let a = control("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5, warp: ControlWarp.linear });
	(inlet(FLOAT32, 4) * a) |> outlet
}
fn mono4() S { (inlet(FLOAT32, 1) + [0.1, 0.2, 0.3, 0.4]) |> outlet }
fn d4() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(1, 0.0);
	let r = d read(1); d write(x * 0.5 + r); (x * 0.5 + r) |> outlet
}
fn d8() S {
	let x = inlet(FLOAT32, 8); let d = delayVar(); d init(1, 0.0);
	let r = d read(1); d write(x + r * 0.9); r |> outlet
}
fn r4() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(2, 0.0);
	let r = d read(2); d write(x); r |> outlet
}
fn r8() S {
	let x = inlet(FLOAT32, 8); let d = delayVar(); d init(2, 0.0);
	let r = d read(2); d write(x); r |> outlet
}
fn rfb() S {
	let x = inlet(FLOAT32, 4); let d = delayVar(); d init(3, 0.0);
	let r = d read(3); d write(x * 0.5 + r * 0.5); r |> outlet
}
fn vc() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.cubic) |> outlet }
fn vl() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.linear) |> outlet }
fn vg() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.lagrange) |> outlet }
fn vs() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.sinc) |> outlet }
fn vn() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = inlet(FLOAT32, 4); d vread(off, Interpolation.none) |> outlet }
fn sc() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = control("dt", dtSpec); d vread(off, Interpolation.cubic) |> outlet }
fn ss() S { let x = inlet(FLOAT32, 4); let d = delayVar(1.0); d write(x); let off = control("dt", dtSpec); d vread(off, Interpolation.sinc) |> outlet }
fn sn() S { let x = inlet(FLOAT32, 8); let d = delayVar(1.0); d write(x); let off = control("dt", dtSpec); d vread(off, Interpolation.none) |> outlet }

dumpCorpus([
	("s4", s4),
	("s8", s8),
	("srate", srate),
	("mx", mx),
	("add2", add2),
	("cst", cst),
	("cast4", cast4),
	("ctl", ctl),
	("mono4", mono4),
	("d4", d4),
	("d8", d8),
	("r4", r4),
	("r8", r8),
	("rfb", rfb),
	("vc", vc),
	("vl", vl),
	("vg", vg),
	("vs", vs),
	("vn", vn),
	("sc", sc),
	("ss", ss),
	("sn", sn),
]);
