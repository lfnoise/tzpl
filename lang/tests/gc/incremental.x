-- Phase 3d: drive enough allocation pressure to trigger an automatic
-- tracing cycle and verify everything still runs to completion. The
-- collector advances incrementally across safepoints inside the loop;
-- SATB barriers preserve the snapshot invariant as the inner closures'
-- temporaries get overwritten.

fn build_pair(i Int) (String, Int) {
    ("item-" $ toString(i), i * 2)
}

-- A heap object reachable through a global, so the synchronous trace below has
-- a live root to mark. Only the DATA segment (var/let globals) is scanned for
-- roots -- the immutable code image (functions/builtins) holds immortal
-- pointers and is never marked -- so this exercises data-global root scanning.
let kept = "live-root";

var i = 0;
var total = 0;
while (i < 20000) {
    let p = build_pair(i);
    total = total + p.1;
    i = i + 1;
}

-- If we got here without crashing, mark/sweep ran cleanly across many
-- safepoints. Print the deterministic result so the test framework can
-- diff against an expected file.
total println;

-- Also report that the tracer is still healthy by running one more
-- synchronous cycle.
let final_blacks = __gc_trace_cycle();
("traced (final root count > 0): " $ toString(final_blacks > 0)) println;
