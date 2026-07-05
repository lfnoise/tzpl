-- prettyString / prettyPrint: Wadler-style width-aware layout.
-- Each container prints on one line when it fits the width, otherwise it
-- breaks one element per line with 2-space indent. At unbounded width the
-- output equals toString.

let v = [[1, 2, 3], [4, 5, 6], [7, 8, 9]];
v prettyString println;
v prettyString(20) println;
v prettyPrint(10);

struct Point { x Float, y Float }
let p = Point { x: 1.5, y: 2.5 };
p prettyString(80) println;
p prettyString(12) println;

let m = ["alpha": [1, 2], "beta": [3, 4]];
m prettyString(14) println;

enum Shape {
    circle Float,
    rect (Float, Float),
    dot,
}
Shape.rect((3.0, 4.0)) prettyString(80) println;
Shape.rect((3.0, 4.0)) prettyString(10) println;
Shape.dot prettyString(5) println;

-- Cyclic values print with ^n^ markers, same as the flat printer.
enum Tree {
    node [Tree],
    leaf Int,
}
var ka = [Tree.leaf(1)];
ka push!(Tree.node(ka));
ka prettyString(80) println;

-- Flat equivalence: unbounded width reproduces toString exactly.
(v prettyString(1000000) == v toString) println;
(p prettyString(1000000) == p toString) println;
(m prettyString(1000000) == m toString) println;
((1, "two", 3.5) prettyString(1000000) == (1, "two", 3.5) toString) println;
(List(1, 2, 3) prettyString(1000000) == List(1, 2, 3) toString) println;
(Set(1, 2, 3) prettyString(1000000) == Set(1, 2, 3) toString) println;
(#[1, 2, 3] prettyString(1000000) == #[1, 2, 3] toString) println;
(&42 prettyString(1000000) == &42 toString) println;
((1..10) prettyString(1000000) == (1..10) toString) println;
(1.5 prettyString == 1.5 toString) println;
-- Symbols: toString is the conversion form (no quote); prettyString keeps
-- the display form, matching print.
'sym prettyString println;
