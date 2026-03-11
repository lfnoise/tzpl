-- Recursive and mutually recursive type tests

-- 1. Recursive enum (linked list)
enum IntList {
    empty,
    cons (Int, IntList)
}
IntList.empty println;
IntList.cons((1, IntList.cons((2, IntList.cons((3, IntList.empty)))))) println;

-- 2. Recursive struct via Option
struct Node {
    value Int,
    next Option<Node>
}
Node { value: 1, next: Option<Node>.none } println;
Node { value: 1, next: Option.some(Node { value: 2, next: Option<Node>.none }) } println;

-- 3. Self-recursive enum with multiple recursive cases (expression tree)
enum Expr {
    lit Int,
    add (Expr, Expr),
    neg Expr
}
Expr.lit(42) println;
Expr.neg(Expr.lit(1)) println;
Expr.add((Expr.lit(1), Expr.neg(Expr.lit(2)))) println;

-- 4. Mutually recursive struct and enum
struct Tree {
    value Int,
    children ChildList
}
enum ChildList {
    leaf,
    branch (Tree, ChildList)
}
let lf = Tree { value: 1, children: ChildList.leaf };
lf println;
Tree { value: 0, children: ChildList.branch((lf, ChildList.leaf)) } println;

-- 5. Two mutually recursive structs via Option
struct PairA {
    value Int,
    other Option<PairB>
}
struct PairB {
    value Int,
    other Option<PairA>
}
PairA { value: 1, other: Option.some(PairB { value: 2, other: Option<PairA>.none }) } println;

-- 6. Recursive function over recursive enum
fn sum(list IntList) Int {
    match (list) {
        IntList.empty: return 0;
        IntList.cons(pair): {
            let (n, rest) = pair;
            return n + sum(rest);
        }
    }
    0
}
sum(IntList.cons((10, IntList.cons((20, IntList.cons((30, IntList.empty))))))) println;

-- 7. Recursive function over expression tree
fn eval(e Expr) Int {
    match (e) {
        Expr.lit(n): return n;
        Expr.neg(inner): return 0 - eval(inner);
        Expr.add(pair): {
            let (left, right) = pair;
            return eval(left) + eval(right);
        }
    }
    0
}
eval(Expr.lit(42)) println;
eval(Expr.neg(Expr.lit(5))) println;
eval(Expr.add((Expr.lit(10), Expr.neg(Expr.lit(3))))) println;

-- 8. Pattern matching on recursive struct
fn nodeSum(n Node) Int {
    match (n.next) {
        Option.some(next): return n.value + nodeSum(next);
        Option.none: return n.value;
    }
    0
}
nodeSum(Node { value: 10, next: Option.some(Node { value: 20, next: Option.some(Node { value: 30, next: Option<Node>.none }) }) }) println;
