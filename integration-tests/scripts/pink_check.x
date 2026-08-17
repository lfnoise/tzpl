-- Regression: the pink noise family. Compiles pink through BOTH compilers
-- (byte parity checked by the runner), pinkf/pinkfe through synthc, and
-- renders 30 s of each for the runner's spectral checks:
--   pink:   slope ~ -3 dB/octave, no dip (the unweighted ladder had a
--           -1.2 dB sag near fs/5)
--   pinkf:  flat to Nyquist (the pre-d(0)-fix state reads collapsed the
--           top: -3.4 dB @10k, -11 dB @20k)
--   pinkfe: within its economy-filter tolerance
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import std.result.*;
import audio_engine.*;

let fails = &0;

async fn cc(f GraphFn, name String, both Bool) Void {
	if (both) {
		match (f defSynthChecked(name $ "_cpp") await) {
			ok(sx): { println("PASS " $ name $ " cpp"); }
			err(m): { fails <- *fails + 1; println("FAIL " $ name $ " cpp: " $ m); }
		}
	}
	match (f defSynthXChecked(name $ "_x") await) {
		ok(cpp): { println("PASS " $ name $ " synthc"); }
		err(m): { fails <- *fails + 1; println("FAIL " $ name $ " synthc: " $ m); }
	}
}

fn gPink() S = pink() * 0.5 |> outlet;
fn gPinkf() S = pinkf() * 0.5 |> outlet;
fn gPinkfe() S = pinkfe() * 0.5 |> outlet;

cc(gPink, "pink_reg", true) await;
cc(gPinkf, "pinkf_reg", false) await;
cc(gPinkfe, "pinkfe_reg", false) await;

fn renderOne(defName String, path String) Void {
	let h = renderNRT(path, 30.0, fn() Void {
		safetyLimiter(false);
		play(defName);
	});
	await renderDone(h);
	println("RENDERED " $ path);
}

if (*fails == 0) {
	renderOne("pink_reg_x", "/tmp/tzpl_pink_reg.wav");
	renderOne("pinkf_reg_x", "/tmp/tzpl_pinkf_reg.wav");
	renderOne("pinkfe_reg_x", "/tmp/tzpl_pinkfe_reg.wav");
	println("PINK COMPILE ALL PASS");
} else {
	println("PINK COMPILE FAILED: " $ *fails toString);
}
