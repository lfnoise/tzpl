-- synthc buffer differential test (M2).
--
-- Byte-matches synthc's generated C++ against the C++ compiler (rewrites off)
-- for the buffer family: fixed read, var read (each interpolation), write, and
-- length, single- and multi-channel, with startChan offsets. Exercises the
-- RandState-style inst-var decl (tzpl_Buffer* bufN), null-init, the guarded
-- read/write/length emission, and the swapBuffer function + funs wiring.

import synthdef.*;
import common_ugens.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;
import synthc.difftest.*;

var `failures Int = 0;

fn checkBuf(name String, synthFn GraphFn) Void {
	let g = makeGraph(synthFn);
	let theirs = synthdefGenCppFromSexpr(g toSynthSexpr(name), 0, false);
	let ctx = g importGraph(name) analyzeM1;
	if (ctx.errors length > 0) {
		`failures = `failures + 1;
		println("  FAIL " $ name $ " (errors):");
		for (e : ctx.errors) { println("    " $ e); }
		return;
	}
	let v = fullCompare(genCpp(ctx, name), theirs);
	if (v == "PASS") { println("  PASS " $ name); }
	else { `failures = `failures + 1; println("  FAIL " $ name); println(v); }
}

-- fixed read (constant index)
fn frd() S   = bufferVar() read(0) * 0.5 |> outlet;
-- fixed read, multichannel + startChan
fn frd2() S  = bufferVar() read(0, 2, 1) outlet;
-- var read, each interpolation
fn vrdN() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.none) outlet }
fn vrdL() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.linear) outlet }
fn vrdC() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.cubic) outlet }
fn vrdG() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.lagrange) outlet }
fn vrdS() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.sinc) outlet }
-- var read multichannel
fn vrd2() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.cubic, 2) outlet }
-- length
fn blen() S  = bufferVar() length * 0.01 |> outlet;
-- write (sink) + passthrough output
fn bwr() S {
	let b = bufferVar();
	let sig = (0.4 lfsaw) * 0.3;
	let idx = (0.1 lfsaw) * 100.0;
	b write(sig, idx);
	sig outlet
}
-- read back an f64 buffer value and use it directly (f64 output)
fn brdback() S {
	let b = bufferVar();
	let idx = (0.1 lfsaw) * 100.0;
	b vread(idx, Interpolation.linear) * 0.5 |> outlet
}

checkBuf("frd", frd);
checkBuf("frd2", frd2);
checkBuf("vrdN", vrdN);
checkBuf("vrdL", vrdL);
checkBuf("vrdC", vrdC);
checkBuf("vrdG", vrdG);
checkBuf("vrdS", vrdS);
checkBuf("vrd2", vrd2);
checkBuf("blen", blen);
checkBuf("bwr", bwr);
checkBuf("brdback", brdback);

if (`failures == 0) {
	println("M2 BUFFER DIFF PASS");
} else {
	println("M2 BUFFER DIFF FAIL: " $ `failures toString);
}
