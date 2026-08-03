-- wavetables: band-limited wavetable bank generation (small sizes for speed).
import wavetables.*;

let n = 512;
let numTables = 4;
let stride = n oscTableStride;

-- Band limits follow max(1, floor(2^(k/3))): tables 0..2 are pure sines,
-- table 3 has two partials.
println([0, 1, 2, 3, 9, 29] @ oscTableHarmonics);

let saw = sawTables(0.0, n, numTables);
println("bank length:", saw length == numTables * stride ? "PASS" : "FAIL");

-- Table 0 is a single normalized sine partial: sin(2*pi*i/n) exactly.
var sineErr = 0.0;
for (i : (0 .. n - 1)) {
	sineErr = max(sineErr, abs(saw[1 + i] - sinpi(2.0 * i toFloat / n toFloat)));
}
println("table 0 pure sine:", sineErr < 0.000000000001 ? "PASS" : "FAIL");

-- Guard samples replicate the cyclic wrap on every table.
var guardsOk = true;
for (k : (0 .. numTables - 1)) {
	let base = k * stride + 1;
	if (saw[base - 1] != saw[base + n - 1]) { guardsOk = false; }
	if (saw[base + n] != saw[base]) { guardsOk = false; }
	if (saw[base + n + 1] != saw[base + 1]) { guardsOk = false; }
	if (saw[base + n + 2] != saw[base + 2]) { guardsOk = false; }
}
println("guard samples:", guardsOk ? "PASS" : "FAIL");

-- Every table is normalized to a +-1 peak.
var peaksOk = true;
for (k : (0 .. numTables - 1)) {
	let base = k * stride + 1;
	var peak = 0.0;
	for (i : (0 .. n - 1)) { peak = max(peak, saw[base + i] abs); }
	if (abs(peak - 1.0) > 0.000000000001) { peaksOk = false; }
}
println("normalized peaks:", peaksOk ? "PASS" : "FAIL");

-- Table 3 of the saw bank carries partials 1..2: it must differ from a sine.
let base3 = 3 * stride + 1;
var diff3 = 0.0;
for (i : (0 .. n - 1)) {
	diff3 = max(diff3, abs(saw[base3 + i] - saw[1 + i]));
}
println("table 3 band-limited saw:", diff3 > 0.1 ? "PASS" : "FAIL");

-- Square and triangle banks generate with the same shape.
let sq = squareTables(0.0, n, numTables);
let tri = triTables(0.0, n, numTables);
println("square/tri lengths:",
	sq length == numTables * stride && tri length == numTables * stride ? "PASS" : "FAIL");

-- smooth > 0 tapers the top partial: table 3 changes, table 0 does not.
let sawSmooth = sawTables(2.0, n, numTables);
var smoothDiff = 0.0;
var table0Diff = 0.0;
for (i : (0 .. n - 1)) {
	smoothDiff = max(smoothDiff, abs(sawSmooth[base3 + i] - saw[base3 + i]));
	table0Diff = max(table0Diff, abs(sawSmooth[1 + i] - saw[1 + i]));
}
println("smooth rolloff:", smoothDiff > 0.001 && table0Diff < 0.000000000001 ? "PASS" : "FAIL");
