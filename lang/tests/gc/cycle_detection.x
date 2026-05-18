-- Phase 3e: validate that the tracing GC detects garbage that ARC alone
-- leaks. Constructs a Ref-mediated cycle inside a function so the only
-- live references after the function returns are the cycle itself. After
-- draining the auto-release pool (which otherwise pins everything alive
-- in file-mode execution), tracing must find the cycled objects as
-- white -- ARC cannot, because their refcounts never reach zero.

fn make_cycle() Void {
    let r = ref(any(0));
    setref(r, any(r));
}

-- Baseline: nothing leaked yet.
__gc_drain_pool();
__gc_trace_cycle();
let baseline_whites = __gc_trace_whites();

-- Create N cycles; each one leaks at least one cycled object.
let N = 5;
var i = 0;
while (i < N) {
    make_cycle();
    i = i + 1;
}

-- Drain the auto-release pool so it stops rooting the cycled objects,
-- then run a full tracing cycle.
__gc_drain_pool();
__gc_trace_cycle();
let after_whites = __gc_trace_whites();

-- The tracer must observe more white objects after the leaks than before.
-- (ARC cannot detect these; it sees refcount >= 1 for every cycled object.)
let leaked = after_whites - baseline_whites;
("baseline whites: " $ toString(baseline_whites)) println;
("post-leak whites: " $ toString(after_whites)) println;
("leaked objects detected: " $ toString(leaked)) println;
("tracing finds leaks ARC misses: " $ toString(leaked >= N)) println;
