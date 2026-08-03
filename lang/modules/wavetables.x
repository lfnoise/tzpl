-- wavetables.x -- band-limited wavetable bank generation.
--
-- Builds the multi-table banks read by the `osc` family of wavetable
-- oscillators in common_ugens.x (after the oscillators in SAPF's
-- OscilUGens.cpp). A bank holds `numTables` tables of `tableLen` samples,
-- one per 1/3 octave: table k is band-limited to
--     harmonics(k) = max(1, floor(2^(k/3)))
-- partials, so table 0 is a pure sine (for the highest frequencies) and the
-- last table carries the most partials (for the lowest). The oscillator
-- picks the table from its playback frequency as
--     tableF = 3 * log2(nyquist / |freq|)
-- clipped to [0, numTables-1]; floor(2^(k/3)) guarantees the selected
-- table's top partial never exceeds Nyquist.
--
-- Each table is built SAPF-style: write the partials into a half spectrum
-- (with an optional cos^smooth rolloff, see oscTables' smooth parameter),
-- inverse real FFT via the ifft builtin, normalize the peak to +-1. Tables are laid
-- out consecutively with 4 interpolation guard samples each,
--     [x[N-1], x[0..N-1], x[0], x[1], x[2]]
-- so stride = tableLen + 4 and table k's x[0] sits at k*stride + 1. The
-- guards replicate the cyclic wrap, which keeps a linear- or
-- cubic-interpolated read inside the sub-table without any whole-buffer
-- wrapping (lagrange/sinc reads need wider guards and are not supported).
--
-- NOT real-time safe (uses the ifft builtin): generate banks at patch-build
-- time and push them into an engine buffer with
--     fillBuffer(nodeID, bufID, 1, sawTables());

-- Number of interpolation guard samples per table (1 before, 3 after).
const oscTableGuard = 4;

-- The stride in samples from one table's slot to the next.
fn oscTableStride(tableLen Int) Int = tableLen + oscTableGuard;

-- Band limit of table k: max(1, floor(2^(k/3))) partials.
fn oscTableHarmonics(k Int) Int = max(1.0, exp2(k toFloat / 3.0) floor) toInt;

-- Build a band-limited wavetable bank from a partial list. amps[i] is the
-- amplitude of partial i+1 (the fundamental is amps[0]); phases[i] is its
-- phase in radians (empty = all zero: pure sine partials).
--
-- smooth mitigates the Gibbs phenomenon. Band-limiting a table means
-- truncating its Fourier series, and with a brickwall cutoff (smooth = 0)
-- a truncated series overshoots by ~9% at any discontinuity in the ideal
-- waveform and rings with visible ripple beside it -- adding more partials
-- narrows the ripple but never shrinks the overshoot. That is the Gibbs
-- phenomenon, and on a saw or square table it shows up as a spike and
-- ringing at each edge (costing headroom after normalization, since the
-- overshoot becomes the peak). smooth > 0 replaces the brickwall with
-- SAPF's gradual cos^smooth rolloff -- partial j is scaled by
-- cos(pi/2 * j/h)^smooth within the table's band limit h -- which fades
-- the series out instead of chopping it, suppressing the overshoot and
-- ripple at the cost of slightly duller top partials. Try smooth = 1..2
-- for edgy waves; 0 keeps the full brightness of an exact truncation.
--
-- Returns the concatenated bank (numTables * (tableLen + 4)
-- samples), ready for fillBuffer(nodeID, bufID, 1, bank).
fn oscTables(amps [Float], phases [Float] = [Float](),
             smooth Float = 0.0, tableLen Int = 16384, numTables Int = 30) [Float] {
	let n = tableLen;
	let stride = n oscTableStride;
	let halfN = n // 2;

	var bank = [Float]();
	for (i : (1 .. numTables * stride)) { bank push!(0.0); }

	var spec = [Float]();
	for (i : (1 .. n)) { spec push!(0.0); }

	for (k : (0 .. numTables - 1)) {
		let h = min(k oscTableHarmonics, min(amps length, halfN - 1));

		-- Partial j+1 with amplitude a and phase p is a*sin(2*pi*(j+1)*x + p):
		-- re[j+1] = (n/2)*a*sin(p), im[j+1] = -(n/2)*a*cos(p).
		for (i : (1 .. n)) { spec[i - 1] = 0.0; }
		for (j : (0 .. h - 1)) {
			var a = amps[j];
			if (smooth > 0.0) {
				a = a * pow(cospi(0.5 * j toFloat / h toFloat), smooth);
			}
			let p = j < phases length ? phases[j] : 0.0;
			spec[j + 1] = n toFloat / 2.0 * a * sin(p);
			spec[halfN + j + 1] = -(n toFloat) / 2.0 * a * cos(p);
		}

		let table = spec ifft unwrap;

		-- Normalize the peak to +-1.
		var peak = 0.0;
		for (i : (0 .. n - 1)) { peak = max(peak, table[i] abs); }
		let scale = peak > 0.0 ? 1.0 / peak : 1.0;

		-- Lay into the bank with cyclic guard samples.
		let base = k * stride + 1;
		for (i : (0 .. n - 1)) { bank[base + i] = table[i] * scale; }
		bank[base - 1] = bank[base + n - 1];
		bank[base + n] = bank[base];
		bank[base + n + 1] = bank[base + 1];
		bank[base + n + 2] = bank[base + 2];
	}
	bank
}

-- Classic waveforms. All partials are sine-phased, matching their
-- non-band-limited ideals up to an overall sign/phase convention.

-- Sawtooth: every partial at 1/i.
fn sawTables(smooth Float = 0.0, tableLen Int = 16384, numTables Int = 30) [Float] {
	let maxH = numTables - 1 |> oscTableHarmonics;
	var amps = [Float]();
	for (i : (1 .. maxH)) { amps push!(1.0 / i toFloat); }
	amps oscTables([Float](), smooth, tableLen, numTables)
}

-- Square: odd partials at 1/i.
fn squareTables(smooth Float = 0.0, tableLen Int = 16384, numTables Int = 30) [Float] {
	let maxH = numTables - 1 |> oscTableHarmonics;
	var amps = [Float]();
	for (i : (1 .. maxH)) { amps push!(i % 2 == 1 ? 1.0 / i toFloat : 0.0); }
	amps oscTables([Float](), smooth, tableLen, numTables)
}

-- Triangle: odd partials at 1/i^2 with alternating sign.
fn triTables(smooth Float = 0.0, tableLen Int = 16384, numTables Int = 30) [Float] {
	let maxH = numTables - 1 |> oscTableHarmonics;
	var amps = [Float]();
	var phases = [Float]();
	var sign = 1.0;
	for (i : (1 .. maxH)) {
		if (i % 2 == 1) {
			amps push!(sign / (i * i) toFloat);
			sign = -sign;
		} else {
			amps push!(0.0);
		}
		phases push!(0.0);
	}
	amps oscTables(phases, smooth, tableLen, numTables)
}
