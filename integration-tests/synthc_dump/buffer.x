-- Graphs from the retired synthc_buffer_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

fn frd() S   = bufferVar() read(0) * 0.5 |> outlet;
fn frd2() S  = bufferVar() read(0, 2, 1) outlet;
fn vrdN() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.none) outlet }
fn vrdL() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.linear) outlet }
fn vrdC() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.cubic) outlet }
fn vrdG() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.lagrange) outlet }
fn vrdS() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.sinc) outlet }
fn vrd2() S { let b = bufferVar(); let i = (0.3 lfsaw) * 100.0; b vread(i, Interpolation.cubic, 2) outlet }
fn blen() S  = bufferVar() length * 0.01 |> outlet;
fn bwr() S {
	let b = bufferVar();
	let sig = (0.4 lfsaw) * 0.3;
	let idx = (0.1 lfsaw) * 100.0;
	b write(sig, idx);
	sig outlet
}
fn brdback() S {
	let b = bufferVar();
	let idx = (0.1 lfsaw) * 100.0;
	b vread(idx, Interpolation.linear) * 0.5 |> outlet
}

dumpCorpus([
	("frd", frd),
	("frd2", frd2),
	("vrdN", vrdN),
	("vrdL", vrdL),
	("vrdC", vrdC),
	("vrdG", vrdG),
	("vrdS", vrdS),
	("vrd2", vrd2),
	("blen", blen),
	("bwr", bwr),
	("brdback", brdback),
]);
