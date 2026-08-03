-- fft / ifft: real FFT over [Float] in packed split-complex layout
-- [re[0..N/2), im[0..N/2)] with re[0] = DC and im[0] = Nyquist.

-- Non-power-of-two and too-short inputs return none.
fft([1.0, 2.0, 3.0]) isNone println;
ifft([1.0, 2.0, 3.0, 4.0, 5.0]) isNone println;
fft([1.0, 2.0]) isNone println;

-- DC-only signal: all energy in re[0] = sum of samples.
let dc = fft([1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]) unwrap;
dc println;

-- Single cosine at bin 1, unit amplitude: re[1] = N/2, everything else 0.
fn cos2pi(x Float) Float = cospi(2.0 * x);
let n = 16;
let wave = (0 .. n - 1) toArray map(fn(i Int) Float { cos2pi(i toFloat / n toFloat) });
let spec = fft(wave) unwrap;
var specOk = true;
for (k : (0 .. n - 1)) {
	let expect = k == 1 ? n toFloat / 2.0 : 0.0;
	if (abs(spec[k] - expect) > 1e-9) { specOk = false; }
}
println("single-bin spectrum:", specOk ? "PASS" : "FAIL");

-- ifft of a hand-built one-partial spectrum reproduces the cosine.
var spec2 = [Float]();
for (k : (0 .. n - 1)) { spec2 push!(0.0); }
spec2[1] = n toFloat / 2.0;
let wave2 = ifft(spec2) unwrap;
var waveOk = true;
for (i : (0 .. n - 1)) {
	if (abs(wave2[i] - wave[i]) > 1e-12) { waveOk = false; }
}
println("one-partial ifft:", waveOk ? "PASS" : "FAIL");

-- Round trip is exact to double precision.
let sig = (0 .. 63) toArray map(fn(i Int) Float {
	sinpi(2.0 * 3.0 * i toFloat / 64.0) + 0.5 * cos2pi(7.0 * i toFloat / 64.0) + 0.25
});
let back = ifft(fft(sig) unwrap) unwrap;
var maxErr = 0.0;
for (i : (0 .. 63)) { maxErr = max(maxErr, abs(back[i] - sig[i])); }
println("roundtrip:", maxErr < 1e-12 ? "PASS" : "FAIL");

-- Nyquist component lands in im[0].
let nyq = fft([1.0, -1.0, 1.0, -1.0, 1.0, -1.0, 1.0, -1.0]) unwrap;
println("nyquist:", abs(nyq[4] - 8.0) < 1e-12 && abs(nyq[0]) < 1e-12 ? "PASS" : "FAIL");
