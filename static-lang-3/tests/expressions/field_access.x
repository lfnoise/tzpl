-- Field access on structs and tuples

-- Declare all structs first
struct Point { x Float, y Float }
struct Line { start Point, end Point }

-- Struct field access
let p = Point { x: 1.0, y: 2.0 };
p.x println;
p.y println;

-- Nested struct field access (inline construction)
let line = Line { start: p, end: Point { x: 3.0, y: 4.0 } };
line.start.x println;
line.end.y println;

-- Tuple field access
let t = (10, 20, 30);
t.0 println;
t.1 println;
t.2 println;

-- Nested tuple
let nested = ((1, 2), (3, 4));
nested.0 println;
nested.1 println;

-- Struct in tuple
let sp = (Point { x: 5.0, y: 6.0 }, 42);
sp.0.x println;
sp.1 println;

-- Field access in expressions
let dist_sq = p.x * p.x + p.y * p.y;
dist_sq println;

-- Field access as function argument
fn add_floats(a Float, b Float) Float = a + b;
add_floats(p.x, p.y) println;
add_floats(p.x, 10.0) println;
