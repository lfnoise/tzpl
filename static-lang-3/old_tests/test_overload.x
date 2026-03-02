struct Point { x Float, y Float }

fn +(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };
fn *(s Float, p Point) Point = Point { x: s * p.x, y: s * p.y };
fn -(a Point, b Point) Point = Point { x: a.x - b.x, y: a.y - b.y };

-- Regular function overloading
fn describe(x Int) String = "an integer";
fn describe(x Float) String = "a float";
fn describe(x String) String = "a string";

let p1 = Point { x: 1.0, y: 2.0 };
let p2 = Point { x: 3.0, y: 4.0 };
println(p1 + p2);
println(2.0 * p1);
println(p2 - p1);

println(describe(42));
println(describe(3.14));
println(describe("hi"));
