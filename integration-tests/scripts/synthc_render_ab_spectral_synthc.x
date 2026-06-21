-- Tier-3 (synthc-only): an identity spectral chain via defSynthX. There is no
-- C++ reference -- the C++ compiler's SIMD spectral codegen emits invalid C++
-- (illegal lvalue cast), so its dylib never builds. This render guards the
-- end-to-end spectral pipeline: it must compile, load, render NON-SILENT, and
-- (critically) NOT crash at teardown (the engine double-uninit bug, fixed M5.0).
import synthdef.*;
import synthc.compile.*;
import common_ugens.*;
import audio_engine.*;
fn spec() S {
	let sig = (0.3 lfsaw) * 0.3;
	spectralChain(sig, 256, 128, fn(frame S) { frame }) outlet
}
defSynthX(spec, "ab_spec");
begin(0);
newNode("ab_spec", 100);
connect(100, 0, 0, 0);
sched();
