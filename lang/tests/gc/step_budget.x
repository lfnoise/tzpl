-- Phase 6: verify that step-budget enforcement actually caps the
-- worst single-step duration. Tighten to 1 ms, run an allocation-heavy
-- workload long enough that several cycles fire, then check that the
-- safepoint max-step never exceeded the budget by more than an
-- overshoot bound (single-object gcScanChildren cost).

enum Tree { leaf, node (Tree, Tree) }

fn build(depth Int) Tree {
    if (depth == 0) { Tree.leaf }
    else { Tree.node((build(depth - 1), build(depth - 1))) }
}

fn check(t Tree) Int {
    match (t) {
        Tree.leaf: 1;
        Tree.node(c): 1 + check(c.0) + check(c.1);
    }
}

-- 1 ms budget. Overshoot bound is kCheckEvery * worstUnitCost; under
-- the current scan code the worst case is around a few hundred us, so
-- accept up to 2 ms total (2x budget) -- a soft tolerance that still
-- catches a regression that breaks budget enforcement entirely.
const BUDGET_NS = 1000000;
const TOLERANCE_NS = 2 * BUDGET_NS;
__gc_set_step_budget_ns(BUDGET_NS);

-- Reset stats AFTER setting budget so the measurement is clean.
__gc_reset_step_stats();

-- Allocate enough trees that the auto-trigger fires several cycles.
let kept = build(12);
var d = 4;
while (d <= 12) {
    var i = 0;
    while (i < (1 << (12 - d + 4))) {
        let _x = build(d) check;
        i = i + 1;
    }
    d = d + 1;
}

let cycles = __gc_cycles_completed();
let steps  = __gc_step_count();
let max_ns = __gc_step_max_ns();
let sp_max = __gc_step_max_ns_by_source(0);  -- safepoint bucket

-- Print the invariants the test checks.
(cycles >= 1)             println;
(steps  >= 1)             println;
(max_ns <= TOLERANCE_NS)  println;
(sp_max <= TOLERANCE_NS)  println;
kept check println;
