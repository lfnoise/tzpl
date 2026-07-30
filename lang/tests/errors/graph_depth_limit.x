-- Lowering graphMaxDepth turns a too-deep == traversal into a runtime error
-- (the limit guards the C++ stack on the slow path). The chain is longer than
-- the fast-path fuel (4096) so the compare falls back to the guarded slow
-- path; with the limit lowered below the chain depth, == raises.

enum Chain { link Chain, end }

fn buildChain(n Int) Chain {
    var c = Chain.end;
    for (i : (1..n)) { c = Chain.link(c); }
    c
}

-- Depth 5000 compares fine under the default limit (10000).
(buildChain(5000) == buildChain(5000)) println;

-- Below the chain depth: == raises instead of overflowing the C++ stack.
setGraphMaxDepth(1000);
(buildChain(5000) == buildChain(5000)) println;
