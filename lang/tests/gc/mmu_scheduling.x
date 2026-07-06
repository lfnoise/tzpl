-- MMU (minimum mutator utilization) scheduling.
-- Verifies: (1) the governor is off by default on an NRT VM; (2) it can be
-- enabled and configured at runtime; (3) under an allocation-heavy workload
-- that forces several GC cycles it never violates the utilization target
-- without escalating the safety valve (guarantee-or-escalate); (4) the
-- workload still computes correctly -- no premature frees, no OOM -- proving
-- the throttled/escalated collector kept up.

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

-- Off by default (this is an NRT VM; only RT/audio VMs preset it on).
(__gc_mmu_enabled() == 1) println;              -- false

-- Enable: 80% mutator target over a 10 ms window. Tighten the step budget so
-- each step is comfortably under one bucket (window / 16 = 625 us).
const TARGET_PERMILLE = 800;
const WINDOW_NS = 10000000;
__gc_set_step_budget_ns(500000);
__gc_mmu_set_target(TARGET_PERMILLE, WINDOW_NS);
__gc_mmu_set_enabled(1);
(__gc_mmu_enabled() == 1) println;              -- true

-- Clean measurement AFTER config so warm-up doesn't pollute the low-water.
__gc_reset_step_stats();
__gc_mmu_reset();

-- Allocate enough trees that the proportional auto-trigger fires several
-- cycles while the governor is throttling collector time.
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
let minMut = __gc_mmu_min_mutator_permille();
let esc    = __gc_mmu_escalations();

-- Cycles actually ran under the governor.
(cycles >= 1) println;                          -- true
-- Observed utilization is a valid parts-per-thousand figure.
(minMut >= 0 && minMut <= 1000) println;        -- true
-- Guarantee-or-escalate: either the target held (within a small overshoot
-- tolerance) or the safety valve lifted the cap because allocation outran
-- the in-flight cycle. Never a silent violation of the target.
((minMut + 10 >= TARGET_PERMILLE) || (esc >= 1)) println;  -- true
-- Correctness: full tree of depth 12 has 2^13 - 1 nodes.
kept check println;                             -- 8191
