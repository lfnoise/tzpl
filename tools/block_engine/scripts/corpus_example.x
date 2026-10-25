-- Renders one synthdef from examples/example_synthdefs.x for the
-- block-engine reference corpus (devplans/BLOCK_ENGINE_PLAN.md, M0).
--   CORPUS_ITEM  the synthdef name, e.g. bubbles
-- The render's --duration ends it; the node just plays until then.
import synthdef.*;
import clock.*;
import example_synthdefs.*;

fn envOr(name String, dflt String) String = match (getEnv(name)) {
	Option.some(s): s;
	Option.none: dflt;
};

let item = envOr("CORPUS_ITEM", "bubbles");
go(coro fn() Float {
	item playFor(3600.0) yieldAll;
}());
