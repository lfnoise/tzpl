-- `k * abs(x)` must keep its sign when k < 0 (issue #13).
--
-- Both compilers' rewriters had `k * abs(x) -> abs(k * x)` unguarded. The
-- identity k|x| == |kx| holds only for k >= 0; for k < 0 the result flipped
-- from <= 0 to >= 0 with no diagnostic. K is a low-rate pattern variable, so
-- a negative control was rewritten just like a negative constant. The rule
-- now fires only on a positive constant.
--
-- One compiler and one case per render, selected by TZPL_ABS_MODE (cpp | x)
-- and TZPL_ABS_CASE (negconst | negctl | posconst);
-- run_neg_abs_rewrite_test.sh renders all six and checks the sign and level.
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;

fn envOr(name String, dflt String) String = match (getEnv(name)) {
	Option.some(s): s;
	Option.none: dflt;
};

let mode = envOr("TZPL_ABS_MODE", "cpp");
let kase = envOr("TZPL_ABS_CASE", "negconst");

-- |x| peaks at 10, so each case peaks at |k| * 10 * 0.01.
fn negConst() S {
	let x = sinosc(100.0) * 10.0;
	(-2.0 * abs(x)) * 0.01 |> outlet
}

fn negCtl() S {
	let x = sinosc(100.0) * 10.0;
	let k = control("k", ControlSpec { -4.0, 4.0, -2.0, ControlWarp.linear });
	(k * abs(x)) * 0.01 |> outlet
}

fn posConst() S {
	let x = sinosc(100.0) * 10.0;
	(3.0 * abs(x)) * 0.01 |> outlet
}

let f = kase == "negctl" ? negCtl : (kase == "posconst" ? posConst : negConst);
let name = "neg_abs_" $ kase $ "_" $ mode;
if (mode == "x") {
	f defSynthX(name) await;
} else {
	f defSynthX(name) await;
}
safetyLimiter(false);
begin();
newNode(name, 100);
connect(100, 0, 0, 0);
sched(0);
