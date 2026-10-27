-- synthc/compile.x
-- Driver for the Tzopilotl-hosted synthdef compiler: build the SignalGraph,
-- import + analyze, generate C++, then compile to a .dylib and load it into the
-- engine via FFI. The public entry point is defSynth.

import synthdef.*;
import std.result.*;
import synthc.ir.*;
import synthc.importer.*;
import synthc.passes.*;
import synthc.codegen.*;

-- Result of a synthc compile.
enum CompileResult {
	ok String,        -- dylib path
	failed String,    -- error message
}

-- Compile a graph to C++ source (no disk). Returns the generated source, or an
-- "error: ..." string if import/analysis failed.
--
-- Compile a graph to C++ source for plugin ABI v3 (no disk). Returns the
-- generated source, or an "error: ..." string if import/analysis failed.
-- The serial code generator (block-engine M4) computes every node per
-- sample inside process(off, n); rewrites on, channel SIMD width 4.
fn compileToCpp(g SignalGraph, name String, applyRewrites Bool = true, simdWidth Int = 4,
                tags [String] = [String]()) String {
	let ctx = g importGraph(name, applyRewrites, tags) analyzeForCodegen;
	if (ctx.errors length > 0) {
		var msg = "error: " $ name $ " analysis:";
		for (e : ctx.errors) { msg = msg $ "\n  " $ e; }
		return msg;
	}
	genCpp(ctx, name, simdWidth)
}

-- Print the analysis dump of each (name, graph function), for the golden
-- dump tests (integration-tests/run_synthc_dump_test.sh). With
-- SYNTHC_CORPUS_MODE=compile, compile and load each one instead and print
-- "COMPILE OK <name>" or "COMPILE FAIL <name>: <error>".
fn dumpCorpus(entries [(String, GraphFn)]) Void {
	let compile = match (getEnv("SYNTHC_CORPUS_MODE")) { some(m): m == "compile"; none: false; };
	for (e : entries) {
		if (compile) {
			match (defSynthChecked(e.1, "corpus_" $ e.0) await) {
				ok(_): println("COMPILE OK " $ e.0);
				err(msg): println("COMPILE FAIL " $ e.0 $ ": " $ msg);
			}
		} else {
			println(analysisDump(makeGraph(e.1), e.0));
		}
	}
}

-- The analysis dump of a graph (golden tests, debugging).
fn analysisDump(g SignalGraph, name String, applyRewrites Bool = true) String {
	let ctx = g importGraph(name, applyRewrites) analyze;
	var out = ctx dumpToString;
	for (e : ctx.errors) { out = out $ "ERROR " $ e $ "\n"; }
	out
}

-- Full pipeline: graph -> C++ -> dylib (written + compiled via clang). Returns
-- the dylib path on success or an error message.
fn compileGraph(g SignalGraph, name String) CompileResult {
	let cpp = compileToCpp(g, name);
	if (cpp startsWith("error:")) { return CompileResult.failed(cpp); }
	let path = writeAndCompileCpp(name, cpp);
	if (path startsWith("error:")) { return CompileResult.failed(path); }
	CompileResult.ok(path)
}

-- Compile, then load the resulting dylib into the engine and register the def.
fn compileAndLoadGraph(g SignalGraph, name String) String {
	match (compileGraph(g, name)) {
		failed(msg): msg;
		ok(path): loadSynthDylib(path);   -- "" on success, error otherwise
	}
}

-- Public entry point: builds the graph from a synth function, compiles it
-- with synthc, loads it, and reports. Returns the generated C++ source (handy
-- for inspection/testing).
--
-- Async: the graph analysis and C++ generation run here (fast), but the clang
-- compile + dylib load run on a worker thread so playing sequences keep
-- running. Await the returned future before the def's FIRST play; when
-- redefining a def that is already playing, fire-and-forget is fine. Nodes
-- already running the def keep their old code (the engine refcounts the
-- superseded def); only nodes created after the new build loads use it.
-- (Inside an NRT render the compile runs synchronously, so the future is
-- already resolved.)
--
-- A failed compile PANICS (prints the error and halts) -- a script must not
-- sail past a def that never loaded. Harnesses that need to tolerate and
-- report failures use defSynthChecked, which returns ok(cpp) / err(message)
-- instead of halting.
fn defSynth(synthFun GraphFn, synthName String) Future<String> =
	defSynth(synthFun, synthName, [String]());

async fn defSynth(synthFun GraphFn, synthName String, tags [String]) String {
	match (defSynthChecked(synthFun, synthName, tags) await) {
		ok(cpp): {
			println(synthName $ " compiled successfully (synthc).");
			cpp
		}
		err(msg): { panic(msg); "" }
	}
}

-- defSynth with strict recurrences: every recurrence (filters, phasors,
-- envelopes) runs in its plain sample order instead of a block form, so the
-- synth matches its per-sample definition exactly rather than within
-- rounding. For reference renders; slower. TZPL_STRICT_RECURRENCES=1 does
-- the same for every compile.
fn defSynthStrict(synthFun GraphFn, synthName String) Future<String> =
	defSynthStrict(synthFun, synthName, [String]());

async fn defSynthStrict(synthFun GraphFn, synthName String, tags [String]) String {
	var `scStrictRecurrences Bool = true;
	defSynth(synthFun, synthName, tags) await
}

-- Fallible form of defSynth: ok(generated C++ source) on success,
-- err(message) on a failed analysis, compile, or load. Never halts and
-- prints nothing.
fn defSynthChecked(synthFun GraphFn, synthName String) Future<Result<String, String>> =
	defSynthChecked(synthFun, synthName, [String]());

async fn defSynthChecked(synthFun GraphFn, synthName String, tags [String]) Result<String, String> {
	let g = makeGraph(synthFun);
	defSynthGraphChecked(g, synthName, tags) await
}

-- The names from when defSynth was the retired C++ compiler's entry point
-- and synthc's was defSynthX; kept so existing scripts still run.
fn defSynthX(synthFun GraphFn, synthName String) Future<String> =
	defSynth(synthFun, synthName);
fn defSynthX(synthFun GraphFn, synthName String, tags [String]) Future<String> =
	defSynth(synthFun, synthName, tags);
fn defSynthXStrict(synthFun GraphFn, synthName String) Future<String> =
	defSynthStrict(synthFun, synthName);
fn defSynthXStrict(synthFun GraphFn, synthName String, tags [String]) Future<String> =
	defSynthStrict(synthFun, synthName, tags);
fn defSynthXChecked(synthFun GraphFn, synthName String) Future<Result<String, String>> =
	defSynthChecked(synthFun, synthName);
fn defSynthXChecked(synthFun GraphFn, synthName String, tags [String]) Future<Result<String, String>> =
	defSynthChecked(synthFun, synthName, tags);

-- Graph-level fallible entry: compile and load an already-built SignalGraph.
-- For callers (like live.proxy) that build the graph themselves in order to
-- scan it before compiling -- avoids running the graph function twice.
fn defSynthGraphChecked(g SignalGraph, synthName String) Future<Result<String, String>> =
	defSynthGraphChecked(g, synthName, [String]());

async fn defSynthGraphChecked(g SignalGraph, synthName String, tags [String]) Result<String, String> {
	let cpp = compileToCpp(g, synthName, true, 4,
	                       mergeSynthTags(tags, defaultSynthTags()));
	if (cpp startsWith("error:")) {
		return Result<String, String>.err("compiling " $ synthName $ " (synthc) failed: " $ cpp);
	}
	let err = writeCompileAndLoadAsync(synthName, cpp) await;
	if (err length > 0) {
		return Result<String, String>.err("compiling " $ synthName $ " (synthc) failed: " $ err);
	}
	Result<String, String>.ok(cpp)
}
