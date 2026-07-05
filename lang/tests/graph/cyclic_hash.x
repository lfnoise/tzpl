-- Cycle-safe hashing.
-- Hashing a cyclic value used to stack-overflow; now the fast path is
-- fuel-bounded and falls back to a visited-map slow path: a preliminary
-- structural hash is stored on entering each heap composite, so a cycle
-- hitting back into an in-progress node absorbs the preliminary value.

enum Tree {
    node [Tree],
    leaf Int,
}

var ka = [Tree.leaf(1)];
ka push!(Tree.node(ka));
var kb = [Tree.leaf(1)];
kb push!(Tree.node(kb));
var kd = [Tree.leaf(2)];
kd push!(Tree.node(kd));

-- Hashing a cyclic value terminates and is stable.
(ka hash == ka hash) println;

-- Isomorphic separately-built cycles hash identically (the preliminary
-- hash is structural: kind + local data, never pointers).
(ka hash == kb hash) println;

-- A differing leaf inside the cycle changes the hash.
(ka hash != kd hash) println;

-- Hash law on cyclic values: ka == kb, so their hashes must agree for
-- Map/Set use. A cyclic key round-trips through a Map, and an isomorphic
-- (equal) copy of the key finds the same entry.
var mm = [Tree.node(ka): "first"];
mm[Tree.node(kb)] unwrap println;

-- Cycle through a Ref.
enum Chain {
    link Ref<Chain>,
    end,
}
var r1 = &Chain.end;
r1 <- Chain.link(r1);
var r2 = &Chain.end;
r2 <- Chain.link(r2);
(*r1 hash == *r2 hash) println;

-- Cycle through Map values.
enum MT {
    mmap [String: MT],
    mleaf Int,
}
var m1 = ["x": MT.mleaf(7)];
m1["loop"] = MT.mmap(m1);
var m2 = ["x": MT.mleaf(7)];
m2["loop"] = MT.mmap(m2);
(m1 hash == m2 hash) println;

-- Shared-substructure DAG: the memo collapses 2^40 node visits to linear.
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
(buildShared(40) hash == buildShared(40) hash) println;

-- Acyclic value big enough to exhaust the fast-path fuel: slow-path hash
-- must agree with the fast-path hash of an identical smaller traversal.
let s1 = (1..6000) toArray @ toString;
let s2 = (1..6000) toArray @ toString;
(s1 hash == s2 hash) println;
