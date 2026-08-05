-- synthc/compile.x
-- Driver for the Tzopilotl-hosted synthdef compiler: build the SignalGraph,
-- import + analyze, generate C++, then compile to a .dylib and load it into the
-- engine via FFI. The public entry point is defSynthX, the synthc analogue of
-- synthdef.x's defSynth.

import synthdef.*;
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
-- The production pipeline runs with the algebraic rewriter ON and SIMD width 4 --
-- matching the C++ compiler's production defaults (max=min=4, so 2-channel loops
-- stay scalar; only the diff oracle uses width 2). Both are validated byte-for-byte
-- against the C++ production output (synthc_rewrite_diff.x + the M5 SIMD diff suites;
-- the full instrument corpus byte-matches at rewrites-on + width-4).
fn compileToCpp(g SignalGraph, name String, applyRewrites Bool = true, simdWidth Int = 4,
                tags [String] = [String]()) String {
	let ctx = g importGraph(name, applyRewrites, tags) analyzeM1;
	if (ctx.errors length > 0) {
		var msg = "error: " $ name $ " analysis:";
		for (e : ctx.errors) { msg = msg $ "\n  " $ e; }
		return msg;
	}
	genCpp(ctx, name, simdWidth)
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

-- Public entry point: the synthc analogue of defSynth. Builds the graph from a
-- synth function, compiles it with the Tzopilotl-hosted compiler, loads it, and
-- reports. Returns the generated C++ source (handy for inspection/testing).
--
-- Async: the graph analysis and C++ generation run here (fast), but the clang
-- compile + dylib load run on a worker thread so playing sequences keep
-- running. Await the returned future before the def's FIRST play; when
-- redefining a def that is already playing, fire-and-forget is fine -- players
-- keep the old def and hot-swap when the new one loads. (Inside an NRT render
-- the compile runs synchronously, so the future is already resolved.)
fn defSynthX(synthFun GraphFn, synthName String) Future<String> =
	defSynthX(synthFun, synthName, [String]());

async fn defSynthX(synthFun GraphFn, synthName String, tags [String]) String {
	let g = makeGraph(synthFun);
	let cpp = compileToCpp(g, synthName, true, 4,
	                       mergeSynthTags(tags, defaultSynthTags()));
	if (cpp startsWith("error:")) {
		println("ERROR compiling " $ synthName $ " (synthc): " $ cpp);
		return cpp;
	}
	let err = writeCompileAndLoadAsync(synthName, cpp) await;
	if (err length > 0) {
		println("ERROR compiling " $ synthName $ " (synthc): " $ err);
	} else {
		println(synthName $ " compiled successfully (synthc).");
	}
	cpp
}
