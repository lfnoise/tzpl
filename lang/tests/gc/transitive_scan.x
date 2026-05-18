-- Phase 5 of tracing-GC project: verify the tracing GC reaches live heap
-- structures transitively. Under Phase 5 (ARC retired) the auto-release
-- pool is no longer a root: liveness is determined purely from register
-- stack maps + globals + dyn vars. Dead intermediate temporaries (the
-- closures captured by map/filter once their parent generators have been
-- consumed, etc.) therefore legitimately appear as whites.

let xs = [1, 2, 3, 4, 5];
let ys = xs map(fn(x Int) Int { x + 100 });
let zs = ys filter(fn(x Int) Bool { x > 102 });

-- Force the lazy chain to materialize so xs/ys/zs actually point at heap
-- data the tracer must follow transitively.
let zsList = zs toList;

let roots  = __gc_trace_cycle();
let blacks = __gc_trace_blacks();

-- The exact black count depends on codegen details, but at minimum we
-- expect the array `xs` and the materialized list `zsList` to be reached
-- through the live root set, so blacks must be > 0.
(blacks >= 1) println;
-- Sanity: the materialized result is still usable after a full cycle.
zsList length println;
