-- synthc/paramgroups.x
-- Parameter groups (block-engine plan §4.5), analysis only.
--
-- Placement is the schedule lattice const < init < reset < block < audio: a
-- node sits at the most outer level its inputs allow (the importer computes
-- it as the max of its inputs' levels). Controls -- inputs with a spec -- and
-- note parameters are nominally `block`: usually held, so what depends only
-- on them (and on const/init/reset values) need not be computed per sample.
-- The block-placed nodes are the parameter groups. Each is keyed by its
-- source set: the inputs it depends on. At run time a group is recomputed
-- only when one of its sources changed, and runs per sample while any of
-- them is not constant for the block (the engine's isConst/version flags).
--
-- Source sets are per node here, naming whole inputs and note-parameter
-- columns. Per-channel provenance (plan §4.2 step 1a) refines them into the
-- per-channel units the code generator dispatches.

import synthdef.*;
import synthc.ir.*;

-- A source as it appears in a set: an input name, or "notes[c]" for a note
-- parameter column (a multichannel parameter names its first column).
fn _sourceName(ctx Ctx, n NIdx) String = match (ctx.kind[n]) {
	inletK(name, _, _):     name;
	noteParamK(_, _, col):  "notes[%^]" fmt(col);
	_:                      "";
};

fn _isBlockSource(ctx Ctx, n NIdx) Bool = match (ctx.kind[n]) {
	inletK(_, _, sp):    match (sp) { some(_): true; none: false; };
	noteParamK(_, _, _): true;
	_:                   false;
};

fn _insertSorted(lst [String], x String) Void {
	var i = 0;
	while (i < lst length && lst[i] < x) { i = i + 1; }
	if (i < lst length && lst[i] == x) { return; }
	lst push!(x);
	var k = lst length - 1;
	while (k > i) { lst[k] = lst[k - 1]; k = k - 1; }
	lst[i] = x;
}

fn findParamGroups(ctx Ctx) Void {
	let n = ctx numNodes;
	var sources [[String]] = [];
	var i = 0;
	while (i < n) { sources push!([String]()); i = i + 1; }
	ctx.paramGroups clear!;
	ctx.groupOf clear!;
	i = 0;
	while (i < n) { ctx.groupOf push!(NONE); i = i + 1; }

	-- The source nodes themselves (inlets, note parameters), per node.
	var srcNodes [[NIdx]] = [];
	i = 0;
	while (i < n) { srcNodes push!([NIdx]()); i = i + 1; }

	-- Sorted order visits inputs first.
	for (x : ctx.sorted) {
		if (ctx _isBlockSource(x)) {
			sources[x] push!(ctx _sourceName(x));
			srcNodes[x] push!(x);
			continue;
		}
		if (ctx.nrate[x] != Rate.block) { continue; }
		for (p : ctx.ins[x]) {
			if (ctx.nrate[p] == Rate.block) {
				for (src : sources[p]) { _insertSorted(sources[x], src); }
				for (sn : srcNodes[p]) {
					if (!(srcNodes[x] contains(sn))) { srcNodes[x] push!(sn); }
				}
			}
		}
	}

	var keys [String: Int] = [:];
	for (x : ctx.sorted) {
		if (ctx.nrate[x] != Rate.block) { continue; }
		-- One group per (source set, graph): a voice body's group caches per
		-- voice, a root-graph group once.
		let key = sources[x] join(" ") $ "|" $ ctx.graphOf[x] toString;
		let g = match (keys[key]) {
			some(g): g;
			none: {
				let g = ctx.paramGroups length;
				keys[key] = g;
				ctx.paramGroups push!(ParamGroup { sources: sources[x], nodes: [NIdx](),
				                                   perVoice: false, inputs: [Int](),
					                                   noteCols: [Int]() });
				g
			}
		};
		ctx.paramGroups[g].nodes push!(x);
		ctx.groupOf[x] = g;
		for (src : srcNodes[x]) { ctx _addGroupSource(g, src); }
		-- A group fed by note parameters runs, and caches, per voice.
		for (src : sources[x]) {
			if (src startsWith("notes[")) {
				ctx.paramGroups[g] = ParamGroup { ...ctx.paramGroups[g], perVoice: true };
			}
		}
	}
}

-- Record source node `src` (an inlet or a note parameter) in group g's
-- structured sources: an input's port serial, or the note columns it reads.
fn _addGroupSource(ctx Ctx, g Int, src NIdx) Void {
	match (ctx.kind[src]) {
		inletK(_, sn, _): {
			if (!(ctx.paramGroups[g].inputs contains(sn))) { ctx.paramGroups[g].inputs push!(sn); }
		}
		noteParamK(_, _, col): {
			var c = col;
			while (c < col + (ctx.chans[src] max(1))) {
				if (!(ctx.paramGroups[g].noteCols contains(c))) { ctx.paramGroups[g].noteCols push!(c); }
				c = c + 1;
			}
		}
		_: {}
	}
}

---------------------------------------------------------------------------
-- Block placement for the serial code generator (block-engine M6).
--
-- Parameter groups run in the root graph and in a flat-voice voicer body
-- (one group cache per voice). Anywhere else -- control-flow branch and
-- loop bodies, spectral frames, an AoS voice body -- the block level is
-- demoted to audio and computed every sample, as in M4.

fn _pgVoicers(ctx Ctx) [NIdx] {
	var out [NIdx] = [];
	for (x : ctx.sorted) { match (ctx.kind[x]) { voicerK(_): out push!(x); _: {} } }
	out
}

fn _pgVoiceBody(ctx Ctx, v NIdx) Int = ctx.graphOf[ctx.subs[v][0]];

fn _pgDeepVoiceGraph(ctx Ctx, g Int) Bool {
	var cur = g;
	while (cur != NONE) {
		for (v : ctx _pgVoicers) { if (ctx _pgVoiceBody(v) == cur) { return true; } }
		cur = ctx.graphs[cur].parent;
	}
	false
}

-- Mirrors genCpp's flat-voice decision: no control flow in a voice body and
-- power-of-two per-voice channel counts.
fn flatVoiceLayout(ctx Ctx) Bool {
	for (x : ctx.sorted) {
		if (isControlFlowKind(ctx.kind[x]) && ctx _pgDeepVoiceGraph(ctx.graphOf[x])) { return false; }
	}
	for (v : ctx _pgVoicers) {
		let perVoice = ctx.chans[ctx.ins[ctx.subs[v][0]][0]];
		if (!(perVoice hasSingleBit)) { return false; }
	}
	true
}

-- Is graph g one where block-placed nodes keep the block level?
fn blockGraph(ctx Ctx, g Int) Bool {
	if (g == 0) { return true; }
	if (!(ctx flatVoiceLayout)) { return false; }
	for (v : ctx _pgVoicers) { if (ctx _pgVoiceBody(v) == g) { return true; } }
	false
}

fn restrictBlockRate(ctx Ctx) Void {
	let n = ctx numNodes;
	var x = 0;
	while (x < n) {
		if (ctx.nrate[x] == Rate.block && !(ctx blockGraph(ctx.graphOf[x]))) {
			ctx.nrate[x] = Rate.audio;
		}
		x = x + 1;
	}
}

-- A block-placed value that is materialized keeps its value across samples
-- and blocks (a skipped group's consumers read the cached value), so it is
-- an instance variable, never a per-sample temp.
fn blockValuesToInstVars(ctx Ctx) Void {
	let n = ctx numNodes;
	var x = 0;
	while (x < n) {
		if (ctx.nrate[x] == Rate.block && ctx.cut[x] isTempVarCut) {
			ctx.cut[x] = GraphCut.rate;
		}
		x = x + 1;
	}
}
