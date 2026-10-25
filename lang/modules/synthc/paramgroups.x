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

	-- Sorted order visits inputs first.
	for (x : ctx.sorted) {
		if (ctx _isBlockSource(x)) {
			sources[x] push!(ctx _sourceName(x));
			continue;
		}
		if (ctx.nrate[x] != Rate.block) { continue; }
		for (p : ctx.ins[x]) {
			if (ctx.nrate[p] == Rate.block) {
				for (src : sources[p]) { _insertSorted(sources[x], src); }
			}
		}
	}

	var keys [String: Int] = [:];
	for (x : ctx.sorted) {
		if (ctx.nrate[x] != Rate.block) { continue; }
		let key = sources[x] join(" ");
		let g = match (keys[key]) {
			some(g): g;
			none: {
				let g = ctx.paramGroups length;
				keys[key] = g;
				ctx.paramGroups push!(ParamGroup { sources: sources[x], nodes: [NIdx](),
				                                   perVoice: false });
				g
			}
		};
		ctx.paramGroups[g].nodes push!(x);
		ctx.groupOf[x] = g;
		-- A group fed by note parameters runs, and caches, per voice.
		for (src : sources[x]) {
			if (src startsWith("notes[")) {
				ctx.paramGroups[g] = ParamGroup { ...ctx.paramGroups[g], perVoice: true };
			}
		}
	}
}
