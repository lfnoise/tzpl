-- QA: Edge cases in structs and enums

-- Struct with single field
struct Wrapper { value Int }
let w = Wrapper { value: 42 };
w println;
w.value println;

-- Struct update (spread) - override all fields
struct Point { x Float, y Float }
let p = Point { x: 1.0, y: 2.0 };
let p2 = Point { ...p, x: 10.0, y: 20.0 };
p2 println;

-- Struct update - override no fields (clone)
let p3 = Point { ...p };
p3 println;
p3.x println;
p3.y println;

-- Positional construction
let p4 = Point{3.0, 4.0};
p4.x println;
p4.y println;

-- Tuple struct
struct Color(Int, Int, Int);
let red = Color(255, 0, 0);
red println;
red.0 println;
red.1 println;
red.2 println;

-- Tuple struct destructuring
let Color(r, g, b) = red;
r println;
g println;
b println;

-- Struct as function parameter
fn distance(a Point, b Point) Float {
    let dx = a.x - b.x;
    let dy = a.y - b.y;
    sqrt(dx * dx + dy * dy)
}
distance(Point{0.0, 0.0}, Point{3.0, 4.0}) println;

-- Struct equality
let p5 = Point{1.0, 2.0};
let p6 = Point{1.0, 2.0};
println(p5 == p6);
println(p5 != p6);

let p7 = Point{1.0, 3.0};
println(p5 == p7);

-- Nested struct
struct Line { start Point, end Point }
let line = Line { start: Point{0.0, 0.0}, end: Point{1.0, 1.0} };
line.start.x println;
line.end.y println;

-- Template struct
struct Pair<A, B> { first A, second B }
let pair1 = Pair { first: 1, second: "hello" };
pair1 println;
pair1.first println;
pair1.second println;

-- Template struct with spread
let pair2 = Pair { ...pair1, first: 99 };
pair2.first println;
pair2.second println;

-- Enum with tuple payload
enum Shape {
    circle Float,
    rect (Float, Float),
    point
}

let c = Shape.circle(5.0);
let r2 = Shape.rect((3.0, 4.0));
let pt = Shape.point;

c println;
r2 println;
pt println;

-- ordinal and tag
ordinal(c) println;
ordinal(r2) println;
ordinal(pt) println;
tag(c) println;
tag(r2) println;
tag(pt) println;

-- Enum equality
println(Shape.point == Shape.point);
println(Shape.circle(1.0) == Shape.circle(1.0));
println(Shape.circle(1.0) == Shape.circle(2.0));

-- Template enum (needs explicit type args)
enum Result<T, E> {
    ok T,
    err E
}

let success = Result<Int, String>.ok(42);
let failure = Result<Int, String>.err("oops");
success println;
failure println;
tag(success) println;
tag(failure) println;

-- Struct operator overloading
fn +(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };
let sum_pt = Point{1.0, 2.0} + Point{3.0, 4.0};
sum_pt println;

-- Int promotion in struct fields
let p_int = Point { x: 1, y: 2 };
p_int println;  -- should show 1.0, 2.0

-- Struct in array
let points = [Point{1.0, 2.0}, Point{3.0, 4.0}, Point{5.0, 6.0}];
points length println;
points[0] println;
points[2] println;
