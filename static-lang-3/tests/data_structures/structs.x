-- Struct tests

-- Basic struct
struct Point { x Float, y Float }
let p = Point { x: 1.0, y: 2.0 };
p println;
p.x println;
p.y println;

-- Struct with mixed types
struct Person { name String, age Int }
let alice = Person { name: "Alice", age: 30 };
alice println;
alice.name println;
alice.age println;

-- Nested structs
struct Line { start Point, end Point }
let line0 = Line { start: Point{0.0, 0.0}, end: Point{1.0, 1.0} };
line0.start.x println;
line0.end.y println;

-- Field access in expressions
let dist_sq = p.x * p.x + p.y * p.y;
dist_sq println;

-- Struct as function argument
fn magnitude(pt Point) Float = pt.x * pt.x + pt.y * pt.y;
p magnitude println;
Point { x: 3.0, y: 4.0 } magnitude println;

-- Struct with int-to-float promotion
let p3 = Point { x: 5, y: 10 };
p3 println;
p3.x println;

-- Nested struct construction (inline)
let line = Line { start: p, end: Point { x: 3.0, y: 4.0 } };
line println;
line.start println;
line.end println;
line.start.x println;
line.end.y println;

-- Struct fields in control flow
fn is_origin(pt Point) Bool {
    if (pt.x == 0.0 && pt.y == 0.0) { return true; }
    false
}
let origin = Point { x: 0.0, y: 0.0 };
origin is_origin println;
p is_origin println;

-- Positional construction
let p4 = Point{3.0, 4.0};
p4 println;
p4.x println;
p4.y println;

-- Positional with promotion
let p5 = Point{7, 8};
p5 println;

-- Fields in different order
let p6 = Point { y: 100.0, x: 50.0 };
p6 println;
p6.x println;
p6.y println;
