-- Generic enums whose cases mention the enum itself (directly, in a tuple,
-- or through a collection). Regression test: monomorphization used to
-- recurse forever on the self-reference (cache-after-resolve), and template
-- arg deduction used to recurse forever unifying M<T> against M<Int>.

-- direct self-recursion in a tuple payload
enum Tree<T> {
    leaf T,
    node (Tree<T>, Tree<T>),
}

fn total(m Tree<Int>) Int {
    match (m) {
        Tree.leaf(v): v;
        Tree.node(pair): {
            let (a, b) = pair;
            total(a) + total(b)
        }
    }
}

-- generic function over the generic recursive enum (exercises deduction)
fn depth<T>(m Tree<T>) Int {
    match (m) {
        Tree.leaf(v): 1;
        Tree.node(pair): {
            let (a, b) = pair;
            1 + max(depth(a), depth(b))
        }
    }
}

let t = Tree.node((Tree.leaf(1), Tree.node((Tree.leaf(2), Tree.leaf(3)))));
t total println;
t depth println;
t println;

let s = Tree.node((Tree.leaf("a"), Tree.leaf("b")));
s depth println;
s println;

-- single direct self-recursive payload
enum Chain<T> {
    end T,
    link Chain<T>,
}

fn unwind<T>(c Chain<T>) T {
    match (c) {
        Chain.end(v): v;
        Chain.link(inner): unwind(inner);
    }
}

Chain.link(Chain.link(Chain.end(42))) unwind println;

-- recursion through an array payload
enum Rose<T> {
    tip T,
    branch [Rose<T>],
}

fn tips<T>(r Rose<T>) Int {
    match (r) {
        Rose.tip(v): 1;
        Rose.branch(kids): {
            var n = 0;
            for (k : kids) { n = n + k tips; }
            n
        }
    }
}

let r = Rose.branch([Rose.tip(1.0), Rose.branch([Rose.tip(2.0), Rose.tip(3.0)]), Rose.tip(4.0)]);
r tips println;
r println;
