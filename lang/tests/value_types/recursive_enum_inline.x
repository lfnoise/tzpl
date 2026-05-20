-- Recursive enums: no-data cases share an immortal singleton, and a case
-- payload that is itself an inline composite (here a tuple of two recursive
-- refs) is stored inline in the Enum rather than via a separate boxed Tuple.
-- This test pins the observable behavior; the optimization is allocation-only
-- and must not change results.

enum Tree { leaf, node(Tree, Tree) }

fn build(d Int) Tree {
    if (d <= 0) { Tree.leaf } else { Tree.node((build(d - 1), build(d - 1))) }
}
fn count(t Tree) Int {
    match (t) {
        Tree.leaf: return 1;
        Tree.node(p): return 1 + count(p.0) + count(p.1);
    }
    0
}

count(build(0)) println;        -- 1
count(build(1)) println;        -- 3
count(build(3)) println;        -- 15
count(build(10)) println;       -- 2047

-- Heterogeneous recursive enum: payload mixes a value and a recursive ref.
enum LL { empty, cons(Int, LL) }
fn sum(l LL) Int {
    match (l) {
        LL.empty: return 0;
        LL.cons(p): return p.0 + sum(p.1);
    }
    0
}
let xs = LL.cons((1, LL.cons((2, LL.cons((3, LL.empty))))));
sum(xs) println;                -- 6
