-- Phase 5: validate that the tracing GC reclaims a self-referential cycle.
-- Constructs a Ref-mediated cycle inside a function; on return, no root
-- references the cycle, so a tracing cycle's mark phase leaves the cycled
-- objects White and sweep frees them.
--
-- Under the legacy ARC scheme these objects would have leaked (refcount
-- never reaches zero). The tracing GC reclaims them: lastWhiteCount() must
-- be at least N after the cycle.

fn make_cycle() Void {
    let r = ref(any(0));
    setref(r, any(r));
}

-- Run one cycle to establish a baseline.
__gc_trace_cycle();
let baseline_whites = __gc_trace_whites();

-- Create N cycles, then trace.
let N = 5;
var i = 0;
while (i < N) {
    make_cycle();
    i = i + 1;
}

__gc_trace_cycle();
let after_whites = __gc_trace_whites();

-- The cycle objects should appear in the post-leak white set.
let reclaimed = after_whites - baseline_whites;
("baseline whites: " $ toString(baseline_whites)) println;
("post-cycle whites: " $ toString(after_whites)) println;
("cycle objects reclaimed: " $ toString(reclaimed)) println;
("tracing reclaims cycles: " $ toString(reclaimed >= N)) println;
