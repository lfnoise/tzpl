-- QA: Recursive/self-referential data structures

-- Binary tree using enums (common pattern)
enum Tree {
    leaf Int,
    node (Tree, Tree)
}

-- Construction
let t = Tree.node((Tree.leaf(1), Tree.node((Tree.leaf(2), Tree.leaf(3)))));
t println;

-- Recursive function over tree
fn tree_sum(t2 Tree) Int {
    match (t2) {
        Tree.leaf(v): return v;
        Tree.node(pair): return tree_sum(pair.0) + tree_sum(pair.1);
    }
    0
}
tree_sum(t) println;  -- 1 + 2 + 3 = 6

-- Tree depth
fn tree_depth(t3 Tree) Int {
    match (t3) {
        Tree.leaf(_): return 1;
        Tree.node(pair): {
            let left = tree_depth(pair.0);
            let right = tree_depth(pair.1);
            return 1 + (left > right ? left : right);
        }
    }
    0
}
tree_depth(Tree.leaf(1)) println;  -- 1
tree_depth(t) println;  -- 3

-- Simple linked list using enum
enum IntList {
    cons (Int, IntList),
    empty
}

let lst = IntList.cons((1, IntList.cons((2, IntList.cons((3, IntList.empty))))));
lst println;

fn ilist_sum(l IntList) Int {
    match (l) {
        IntList.empty: return 0;
        IntList.cons(pair): return pair.0 + ilist_sum(pair.1);
    }
    0
}
ilist_sum(lst) println;  -- 6
ilist_sum(IntList.empty) println;  -- 0
