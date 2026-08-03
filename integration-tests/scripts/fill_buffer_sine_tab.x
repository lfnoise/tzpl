-- fillBuffer end-to-end (table half): build one period of a sine with the
-- ifft builtin, push it into a buffer with fillBuffer, and play it back
-- through a cubic-interpolated vread wavetable oscillator. Rendered via the
-- app's --nrt mode by run_fill_buffer_test.sh; must match the sinosc
-- reference render to interpolation error.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import audio_engine.*;

let n = 512;

-- One-partial spectrum: im[1] = -n/2 is sin(2*pi*i/n) at unit amplitude
-- (packed split layout: re[0..n/2), im[0..n/2); im[k] at index n/2 + k).
var spec = [Float]();
for (k : (0 .. n - 1)) { spec push!(0.0); }
spec[n // 2 + 1] = -(n toFloat) / 2.0;
let table = ifft(spec) unwrap;

fn sineTab() S {
	let b = bufferVar();
	b vread(phasor(440.0) f64 * 512.0, Interpolation.cubic) f32 outlet
}

defSynthX(sineTab, "fbSineTab");
begin();
newNode("fbSineTab", 100);
fillBuffer(100, 0, 1, table);
connect(100, 0, 0, 0);
sched(0);
