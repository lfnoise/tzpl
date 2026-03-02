-- Struct update syntax (spread operator)

-- Basic struct
struct Point { x Float, y Float }

-- Basic spread: override one field
let p1 = Point { x: 1.0, y: 2.0 };
let p2 = Point { ...p1, x: 10.0 };
p2.x println;
p2.y println;

-- Override the other field
let p3 = Point { ...p1, y: 20.0 };
p3.x println;
p3.y println;

-- Override all fields (spread is unused but valid)
let p4 = Point { ...p1, x: 100.0, y: 200.0 };
p4.x println;
p4.y println;

-- Override zero fields (copy)
let p5 = Point { ...p1 };
p5.x println;
p5.y println;

-- Multi-field struct
struct Employee { name String, age Int, dept String }
let fred = Employee { name: "Fred", age: 30, dept: "Engineering" };
let fred2 = Employee { ...fred, age: 31 };
fred2.name println;
fred2.age println;
fred2.dept println;

-- Verify original is unchanged (immutability)
fred.name println;
fred.age println;
fred.dept println;

-- Nested struct spread
struct Line { start Point, end Point }
let line1 = Line { start: Point { x: 0.0, y: 0.0 }, end: Point { x: 5.0, y: 5.0 } };
let line2 = Line { ...line1, start: Point { x: 1.0, y: 1.0 } };
line2.start.x println;
line2.start.y println;
line2.end.x println;
line2.end.y println;

-- Nested spread: update a field within a nested struct
let line3 = Line { ...line1, end: Point { ...line1.end, x: 60.0 } };
line3.start.x println;
line3.start.y println;
line3.end.x println;
line3.end.y println;

-- Int-to-float promotion with spread
let p6 = Point { ...p1, x: 42 };
p6.x println;
p6.y println;

-- Template struct with spread
struct Pair<A, B> { first A, second B }
let pair1 = Pair { first: 1, second: "hello" };
let pair2 = Pair { ...pair1, first: 2 };
pair2.first println;
pair2.second println;
