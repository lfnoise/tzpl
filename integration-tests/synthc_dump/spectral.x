-- Graphs from the retired synthc_spectral_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

fn identity() S {
	let sig = 0.3 lfsaw;
	spectralChain(sig, 256, 128, fn(frame S) { frame }) outlet
}
fn scale() S {
	let sig = 0.3 lfsaw;
	spectralChain(sig, 256, 128, fn(frame S) { frame * 0.5 }) outlet
}
fn stereoScale() S {
	let sig = [0.3, 0.4] lfsaw;
	spectralChain(sig, 512, 256, fn(frame S) { frame * 0.5 }) outlet
}

dumpCorpus([
	("identity", identity),
	("scale", scale),
	("stereo_scale", stereoScale),
]);
