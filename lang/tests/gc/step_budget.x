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
-- the current scan code (after the Phase 6 step 3 bounded-fan-out
-- refactor) the worst case at a 1 ms budget hovers around 1.05-1.2 ms;
-- accept up to 1.5x as a soft tolerance that still catches a regression
-- that breaks budget enforcement entirely (e.g. an unbounded gcScanChildren
-- override creeping back in for a new container type).
const BUDGET_NS = 1000000;
const TOLERANCE_NS = (BUDGET_NS * 3) / 2;
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
