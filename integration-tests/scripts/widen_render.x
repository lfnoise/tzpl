-- Renders one f32/f64 mixing case (issue #16), selected by environment:
--   TZPL_WIDEN_MODE  cpp | x          -- C++ compiler (defSynth) or synthc (defSynthX)
--   TZPL_WIDEN_FORM  widened | cast   -- implicit widening, or the same graph
--                                        with the casts written out by hand
--   TZPL_WIDEN_CASE  index into the case table below
-- run_widen_test.sh renders every case in all four combinations and requires
-- them bit-identical: implicit widening must compute exactly what the
-- hand-written casts compute, in both compilers.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;

fn envOr(name String, dflt String) String = match (getEnv(name)) {
	Option.some(s): s;
	Option.none: dflt;
};

-- c, d: f32 audio; y: f64 audio; k: f64 constant (not 1.0, which the
-- rewriter's 1 * x -> x would remove).
fn widened(i Int, c S, d S, y S, k S) S = match (i) {
	0: y - c;
	1: c - y;
	2: c * y;
	3: y - min(c, d);
	4: min(c, d) * y;
	5: k - c;
	6: c + k;
	7: y + min(c, d) * k;
	8: select2(d > 0.0, y, c);
	9: select2(c > y, y, c);
	-- the rewriter turns this into (-K) + c, folding -K into a new constant:
	-- that constant must keep its f64 (it used to revert to the default, and
	-- the sum ran in f32). K = 2^24 + 1 is exact in f64 but rounds in f32,
	-- which moves the output by 0.1 -- far above its rounding step.
	_: c - 16777217.0 f64;
};

fn handCast(i Int, c S, d S, y S, k S) S = match (i) {
	0: y - (c f64);
	1: (c f64) - y;
	2: (c f64) * y;
	3: y - (min(c, d) f64);
	4: (min(c, d) f64) * y;
	5: k - (c f64);
	6: (c f64) + k;
	7: y + (min(c, d) f64) * k;
	8: select2(d > 0.0, y, c f64);
	9: select2((c f64) > y, y, c f64);
	_: (c f64) - 16777217.0 f64;
};

let mode = envOr("TZPL_WIDEN_MODE", "cpp");
let form = envOr("TZPL_WIDEN_FORM", "widened");
let idx = match (envOr("TZPL_WIDEN_CASE", "0") parseInt) {
	Option.some(n): n;
	Option.none: 0;
};

fn graph() S {
	let c = 3 sinosc; let d = 5 sinosc;
	let y = 7 lfsaw f64;
	let k = 1.5 f64;
	let v = form == "cast" ? handCast(idx, c, d, y, k) : widened(idx, c, d, y, k);
	v f32 * 0.1 |> outlet
}

let name = "widen_%^_%^_%^" fmt(mode, form, idx);
if (mode == "x") { graph defSynthX(name) await; } else { graph defSynthX(name) await; }
safetyLimiter(false);
begin();
newNode(name, 100);
connect(100, 0, 0, 0);
sched(0);
