-- Binary trees. Allocation/GC stress test from the Computer Language Benchmarks Game.
-- Builds many short-lived trees plus one long-lived tree and totals up node counts.
--
-- Tzopilotl's GC only collects when the stack has been collapsed (register-file
-- contents aren't roots), so dead short-lived trees accumulate inside the inner
-- loop. The TLSF backup allocator transparently grows the pool by malloc when
-- it exhausts, so the benchmark runs at depths much higher than the initial
-- 64 MB pool would otherwise allow -- but the peak working set still grows
-- proportional to the total tree alloc per depth-pass, which on this machine
-- caps practical N at ~16 (about 1.5 GB resident).

const MIN_DEPTH = 4;
const MAX_DEPTH = 16;

enum Tree {
    leaf,
    node (Tree, Tree)
}

fn build(depth Int) Tree {
    if (depth <= 0) {
        Tree.leaf
    } else {
        Tree.node((build(depth - 1), build(depth - 1)))
    }
}

fn check(t Tree) Int {
    match (t) {
        Tree.leaf: return 1;
        Tree.node(pair): return 1 + check(pair.0) + check(pair.1);
    }
    0
}

-- Stretch tree
let stretch_depth = MAX_DEPTH + 1;
("stretch tree of depth " $ toString(stretch_depth) $ "\t check: " $ toString(check(build(stretch_depth)))) println;

-- Long-lived tree (kept alive across all the short-lived passes)
let long_lived = build(MAX_DEPTH);

-- For each depth from MIN to MAX (step 2), build many trees and total their checks.
var depth = MIN_DEPTH;
while (depth <= MAX_DEPTH) {
    var iterations = 1;
    var s = 0;
    while (s < MAX_DEPTH - depth + MIN_DEPTH) {
        iterations = iterations * 2;
        s = s + 1;
    }
    var sum = 0;
    var i = 0;
    while (i < iterations) {
        sum = sum + check(build(depth));
        i = i + 1;
    }
    (toString(iterations) $ "\t trees of depth " $ toString(depth) $ "\t check: " $ toString(sum)) println;
    depth = depth + 2;
}

("long lived tree of depth " $ toString(MAX_DEPTH) $ "\t check: " $ toString(check(long_lived))) println;
