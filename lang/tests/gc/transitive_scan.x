-- Phase 3 of tracing-GC project: verify the tracing GC walks objects
-- transitively from the root set, reaching nested heap allocations.
-- Running in shadow mode -- ARC still owns reclamation, tracing observes.

-- Allocate some heap structures: an array, a lazy list (built from map/filter),
-- and the lambdas they capture. All should be reachable from the auto-release
-- pool (which is a tracing root) and should appear in the Black set.
let xs = [1, 2, 3, 4, 5];
let ys = xs map(fn(x Int) Int { x + 100 });
let zs = ys filter(fn(x Int) Bool { x > 102 });

let roots  = __gc_trace_cycle();
let blacks = __gc_trace_blacks();
let whites = __gc_trace_whites();

-- The exact numbers depend on codegen details (which intermediate generators
-- are kept alive), but the invariants are:
--   blacks >= 1   -- at least one user-allocated heap object must be Black
--   whites == 0   -- nothing reachable is left as garbage
(blacks >= 1) println;
(whites == 0) println;
