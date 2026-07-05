-- Cycle-safe structural equality.
-- Cyclic values used to stack-overflow ==; now the fast path is fuel-bounded
-- and falls back to a union-find slow path with bisimulation semantics.

enum Tree {
    node [Tree],
    leaf Int,
}

-- Self-referential array cycle: ka[1] is a node whose payload is ka itself.
var ka = [Tree.leaf(1)];
ka push!(Tree.node(ka));

-- Comparing a cyclic value with itself terminates.
(Tree.node(ka) == Tree.node(ka)) println;

-- Isomorphic separately-built cycle: equal.
var kb = [Tree.leaf(1)];
kb push!(Tree.node(kb));
(Tree.node(ka) == Tree.node(kb)) println;
(ka == kb) println;

-- Different unrolling (period 2 vs period 1): bisimilar, so equal.
var kc1 = [Tree.leaf(1)];
var kc2 = [Tree.leaf(1)];
kc1 push!(Tree.node(kc2));
kc2 push!(Tree.node(kc1));
(Tree.node(ka) == Tree.node(kc1)) println;

-- One differing leaf inside the cycle: unequal.
var kd = [Tree.leaf(2)];
kd push!(Tree.node(kd));
(Tree.node(ka) == Tree.node(kd)) println;

-- Cyclic vs acyclic of the same shape prefix: unequal.
var ke = [Tree.leaf(1)];
ke push!(Tree.leaf(3));
(Tree.node(ka) == Tree.node(ke)) println;

-- Cycle through a Ref.
enum Chain {
    link Ref<Chain>,
    end,
}
var r1 = &Chain.end;
r1 <- Chain.link(r1);
var r2 = &Chain.end;
r2 <- Chain.link(r2);
(*r1 == *r2) println;

-- Cycle through Map values (keys stay acyclic).
enum MT {
    mmap [String: MT],
    mleaf Int,
}
var m1 = ["x": MT.mleaf(7)];
m1["loop"] = MT.mmap(m1);
var m2 = ["x": MT.mleaf(7)];
m2["loop"] = MT.mmap(m2);
(m1 == m2) println;
(MT.mmap(m1) == MT.mmap(m2)) println;
var m3 = ["x": MT.mleaf(8)];
m3["loop"] = MT.mmap(m3);
(m1 == m3) println;

-- Shared-substructure DAG: naive comparison is 2^40 node visits; the
-- union-find collapses it to linear.
enum P {
    pair [P],
    pval Int,
}
fn buildShared(depth Int) P {
    var cur = P.pval(0);
    for (i : (1..depth)) {
        cur = P.pair([cur, cur]);
    }
    cur
}
(buildShared(40) == buildShared(40)) println;

-- Acyclic value big enough to exhaust the fast-path fuel: the slow-path
-- restart must agree with the fast path.
let s1 = (1..6000) toArray @ toString;
let s2 = (1..6000) toArray @ toString;
(s1 == s2) println;
var s3 = (1..6000) toArray @ toString;
s3[5999] = "different";
(s1 == s3) println;
