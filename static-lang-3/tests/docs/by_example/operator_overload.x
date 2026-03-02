-- Operator Overloading from Language X by Example

struct Point { x Float, y Float }

fn +(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };
fn -(a Point, b Point) Point = Point { x: a.x - b.x, y: a.y - b.y };
fn *(s Float, p Point) Point = Point { x: s * p.x, y: s * p.y };

let p1 = Point { x: 1.0, y: 2.0 };
let p2 = Point { x: 3.0, y: 4.0 };
println(p1 + p2);
println(p2 - p1);
println(2.0 * p1);
