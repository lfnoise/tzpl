-- Cycle-safe flat printing.
-- Printing a cyclic value used to stack-overflow. A re-entered container
-- now prints as ^n^ (the same object appears n container levels up the
-- print path) and recursion stops there.

enum Tree {
    node [Tree],
    leaf Int,
}

-- Self-referential array: ka[1] holds a node whose payload is ka itself.
var ka = [Tree.leaf(1)];
ka push!(Tree.node(ka));
ka println;
Tree.node(ka) println;

-- Two-array cycle: the marker counts container levels, so the re-entry
-- two arrays up prints ^2^.
var kb1 = [Tree.leaf(1)];
var kb2 = [Tree.leaf(2)];
kb1 push!(Tree.node(kb2));
kb2 push!(Tree.node(kb1));
kb1 println;

-- Cycle through a Ref.
enum Chain {
    link Ref<Chain>,
    end,
}
var r = &Chain.end;
r <- Chain.link(r);
r println;

-- Cycle through Map values.
enum MT {
    mmap [String: MT],
    mleaf Int,
}
var m = ["x": MT.mleaf(7)];
m["loop"] = MT.mmap(m);
m println;

-- DAG sharing without a cycle prints in full, exactly as before.
var shared = [Tree.leaf(9)];
let dag = [Tree.node(shared), Tree.node(shared)];
dag println;
