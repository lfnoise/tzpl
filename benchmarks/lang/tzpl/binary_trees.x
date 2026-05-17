-- Binary trees. Allocation/GC stress test from the Computer Language Benchmarks Game.
-- Builds many short-lived trees plus one long-lived tree and totals up node counts.
--
-- Tzopilotl's GC heartbeat is time-based and only collects when the stack has
-- been collapsed (stack-live registers aren't GC roots), so an explicit gc()
-- call from inside a tight allocation loop is unsafe. As a result the
-- benchmark runs at a depth that fits the cumulative allocation in the
-- default 64 MB TLSF pool without needing an intermediate sweep -- smaller
-- than the canonical benchmarksgame N=21, but matched against Lua at the
-- same depth.

const MIN_DEPTH = 4;
const MAX_DEPTH = 11;

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
