-- synthc/difftest.x
-- Differential-test helpers: compare synthc's analysis dump against the C++
-- compiler's dump (obtained via the synthdefAnalysisDump FFI on the same
-- graph serialized to an s-expression).
--
-- Tiers:
--   M0: compare the structural prefix of each "-- SORTED EXPRS" line
--       (index, serial, inputs, n-inputs, n-consumers, rate), masking the
--       cut/type/chans/str columns that only exist after the inference
--       passes land (M1).
--   M1+: compare full dumps byte-for-byte.

import synthdef.*;
import synthc.ir.*;

-- The lines of the "-- SORTED EXPRS" section (exclusive of headers).
fn sortedSection(dump String) [String] {
	var out [String] = [];
	var active = false;
	for (line : dump split("\n")) {
		if (line startsWith("-- SORTED EXPRS")) {
			active = true;
			continue;
		}
		if (active && line startsWith("--")) { break; }
		if (active && line length > 0) { out push!(line); }
	}
	out
}

-- Keep "   <i> <sn> [<inputs>] <nins> <ncons> <rate>" and drop the
-- cut/type/chans/str tail.
fn m0NormalizeLine(line String) String {
	let parts = line split("] ");
	if (parts length < 2) { return line; }
	var tokens [String] = [];
	for (t : parts[1] split(" ")) {
		if (t length > 0) { tokens push!(t); }
	}
	if (tokens length < 3) { return line; }
	parts[0] $ "] " $ tokens[0] $ " " $ tokens[1] $ " " $ tokens[2]
}

fn m0Normalize(dump String) [String] {
	var out [String] = [];
	for (line : dump sortedSection) {
		out push!(line m0NormalizeLine);
	}
	out
}

-- Compare two M0-normalized dumps; returns "PASS" or a diff report.
fn m0Compare(mine String, theirs String) String {
	let a = mine m0Normalize;
	let b = theirs m0Normalize;
	var report [String] = [];
	if (a length != b length) {
		report push!("line count differs: synthc %^ vs C++ %^" fmt(a length, b length));
	}
	let n = min(a length, b length);
	var i = 0;
	while (i < n) {
		if (a[i] != b[i]) {
			report push!("line %^ differs:" fmt(i));
			report push!("  synthc: " $ a[i]);
			report push!("  C++:    " $ b[i]);
		}
		i = i + 1;
	}
	report length == 0 ? "PASS" : report join("\n")
}

-- The lines of a named "-- SECTION" block (exclusive of the header), up to the
-- next "--"/"GRAPH"/"DELAY"/"SYNTH" marker.
fn dumpSection(dump String, header String) [String] {
	var out [String] = [];
	var active = false;
	for (line : dump split("\n")) {
		if (line startsWith(header)) { active = true; continue; }
		if (active) {
			if (line startsWith("--") || line startsWith("GRAPH")
				|| line startsWith("DELAY") || line startsWith("SYNTH")) { break; }
			if (line length > 0) { out push!(line); }
		}
	}
	out
}

-- Sort the space-separated tokens inside the FIRST [...] of a line. Used to make
-- the antecedent SET on TREE/LOOP lines order-independent (the C++ dump prints
-- them in hash-map order; synthc uses insertion order).
fn _sortBracket(line String) String {
	let byOpen = line split("[");
	if (byOpen length < 2) { return line; }
	let prefix = byOpen[0];
	let afterOpen = byOpen[1];
	let byClose = afterOpen split("]");
	if (byClose length < 2) { return line; }
	let inner = byClose[0];
	let suffix = byClose[1];
	var toks [String] = [];
	for (t : inner split(" ")) { if (t length > 0) { toks push!(t); } }
	prefix $ "[" $ toks sort join(" ") $ "]" $ suffix
}

-- Normalize the TREES section: TREE header lines get their antecedent bracket
-- sorted; member expr lines (whose [inputs] are order-significant) are kept.
fn treesNormalize(dump String) [String] {
	var out [String] = [];
	for (line : dump dumpSection("-- TREES")) {
		if (line startsWith("    TREE")) { out push!(line _sortBracket); }
		else { out push!(line); }
	}
	out
}

fn treesCompare(mine String, theirs String) String {
	let a = mine treesNormalize;
	let b = theirs treesNormalize;
	var report [String] = [];
	if (a length != b length) {
		report push!("TREE line count differs: synthc %^ vs C++ %^" fmt(a length, b length));
	}
	let n = min(a length, b length);
	var i = 0;
	while (i < n) {
		if (a[i] != b[i]) {
			report push!("tree line %^ differs:" fmt(i));
			report push!("  synthc: " $ a[i]);
			report push!("  C++:    " $ b[i]);
		}
		i = i + 1;
	}
	report length == 0 ? "PASS" : report join("\n")
}

-- Full-dump comparison that sorts the antecedent bracket on every TREE/LOOP
-- header line (order-independent), used for control-flow dumps where the trees
-- also appear inside the per-graph loop sections. Member expr lines (6-space
-- indent) keep their [inputs] order, which is significant.
fn normalizeDump(dump String) [String] {
	var out [String] = [];
	for (line : dump split("\n")) {
		if (line startsWith("    TREE") || line startsWith("  LOOP")) {
			out push!(line _sortBracket);
		} else {
			out push!(line);
		}
	}
	out
}

fn cfDumpCompare(mine String, theirs String) String {
	let a = mine normalizeDump;
	let b = theirs normalizeDump;
	var report [String] = [];
	let n = min(a length, b length);
	var i = 0;
	while (i < n) {
		if (a[i] != b[i]) {
			report push!("line %^:" fmt(i));
			report push!("  synthc: " $ a[i]);
			report push!("  C++:    " $ b[i]);
		}
		i = i + 1;
	}
	if (a length != b length) {
		report push!("line count: synthc %^ vs C++ %^" fmt(a length, b length));
	}
	report length == 0 ? "PASS" : ("dumps differ:\n" $ report join("\n"))
}

-- Full-dump comparison for M1+.
fn fullCompare(mine String, theirs String) String {
	if (mine == theirs) { return "PASS"; }
	let a = mine split("\n");
	let b = theirs split("\n");
	var report [String] = ["dumps differ:"];
	let n = min(a length, b length);
	var i = 0;
	while (i < n) {
		if (a[i] != b[i]) {
			report push!("line %^:" fmt(i));
			report push!("  synthc: " $ a[i]);
			report push!("  C++:    " $ b[i]);
		}
		i = i + 1;
	}
	if (a length != b length) {
		report push!("line count: synthc %^ vs C++ %^" fmt(a length, b length));
	}
	report join("\n")
}

-- A per-voice delay-advance line, e.g. `for (int v = 0; ...) ++p->voice_dN_wrpos[v];`.
fn _isVoiceAdvanceLine(line String) Bool =
	(line split("++p->voice_d") length > 1) && (line split("_wrpos[v];") length > 1);

-- Sort maximal runs of per-voice delay-advance lines. The C++ emits these in
-- graph->delayBufs (unordered_set, pointer-hash) order, which is not derivable
-- and is semantically irrelevant -- the wrpos increments are independent. Mirrors
-- the antecedent-bracket sorting the dump comparators use for the same class of
-- C++ hash-order non-determinism.
fn _normVoiceAdvance(src String) [String] {
	let lines = src split("\n");
	var out [String] = [];
	var i = 0;
	while (i < lines length) {
		if (lines[i] _isVoiceAdvanceLine) {
			var run [String] = [];
			while (i < lines length && lines[i] _isVoiceAdvanceLine) { run push!(lines[i]); i = i + 1; }
			for (l : run sort) { out push!(l); }
		} else {
			out push!(lines[i]); i = i + 1;
		}
	}
	out
}

-- Codegen comparison for voicers: like fullCompare, but order-normalizes the
-- per-voice delay-advance lines (see _normVoiceAdvance).
fn voicerCppCompare(mine String, theirs String) String {
	let a = mine _normVoiceAdvance;
	let b = theirs _normVoiceAdvance;
	var report [String] = ["dumps differ:"];
	let n = min(a length, b length);
	var i = 0;
	while (i < n) {
		if (a[i] != b[i]) {
			report push!("line %^:" fmt(i));
			report push!("  synthc: " $ a[i]);
			report push!("  C++:    " $ b[i]);
		}
		i = i + 1;
	}
	if (a length != b length) {
		report push!("line count: synthc %^ vs C++ %^" fmt(a length, b length));
	}
	report length == 1 ? "PASS" : report join("\n")
}
