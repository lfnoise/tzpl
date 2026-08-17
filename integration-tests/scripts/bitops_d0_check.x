-- Regression: audio-rate integer bit ops and d(0) semantics.
--
-- Bit ops: ctz/clz/popCount/bitWidth and >>> (ushr) were plumbed through
-- both compilers in Aug 2026 (sexpr name entries, synthc importer/fold,
-- runtime scalar+vector helpers in tzpl_matrix_transform.hpp). >>> in
-- particular had never worked at runtime -- both compilers emitted calls
-- to a `ushr` helper that did not exist.
--
-- d(0): a delay of ZERO samples resolves at graph-build time to the
-- written signal itself (synthdef.x records the write per delayVar).
-- Before the fix an offset-0 read silently behaved as d(1), which put a
-- z^-1 on the Kellett pinking filter's pole terms and collapsed its
-- response near Nyquist.
--
-- The runner also byte-compares each def's generated C++ across the two
-- compilers (name-normalized).
import synthdef.*;
import synthc.compile.*;
import std.result.*;
import audio_engine.*;

let fails = &0;

async fn cc2(f GraphFn, name String) Void {
	match (f defSynthChecked(name $ "_cpp") await) {
		ok(sx): { println("PASS " $ name $ " cpp"); }
		err(m): { fails <- *fails + 1; println("FAIL " $ name $ " cpp: " $ m); }
	}
	match (f defSynthXChecked(name $ "_x") await) {
		ok(cpp): { println("PASS " $ name $ " synthc"); }
		err(m): { fails <- *fails + 1; println("FAIL " $ name $ " synthc: " $ m); }
	}
}

-- every bit op at audio rate on a live integer signal
fn bitops() S {
	let r = rand64();
	let a = (r >>> 45) f64 * 1.9073486328125e-6;
	let b = (r ctz + r clz + r popCount + r bitWidth) f64 * 0.001;
	(a + b) f32 * 0.05 |> outlet
}
cc2(bitops, "rt_bitops") await;

-- d(0) must BE the written signal's node, resolved at graph build; the
-- flag is set during _makeTopGraph inside defSynth*Checked.
let d0same = &false;
fn d0graph() S {
	let d = delayVar();
	let x = urand(1);
	let w = x write(d);
	d0same <- d(0).id == w.id && d(0).id == x.id;
	-- the d(1) tap bounds the delay (an offset-0-only delay has no length)
	(d(0) - x + d(1) * 1e-30) f32 |> outlet
}
cc2(d0graph, "rt_d0") await;

if (*d0same) { println("PASS d(0) is the written node"); }
else { fails <- *fails + 1; println("FAIL d(0) did not resolve to the written node"); }

if (*fails == 0) { println("BITOPS D0 ALL PASS"); }
else { println("BITOPS D0 FAILED: " $ *fails toString); }
