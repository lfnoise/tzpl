-- Overloaded operators

struct Point { x Float, y Float }

fn +(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };
fn -(a Point, b Point) Point = Point { x: a.x - b.x, y: a.y - b.y };
fn *(s Float, p Point) Point = Point { x: s * p.x, y: s * p.y };

let p1 = Point { x: 1.0, y: 2.0 };
let p2 = Point { x: 3.0, y: 4.0 };

println(p1 + p2);
println(p2 - p1);
println(2.0 * p1);

-- Overloaded comparison
fn ==(a Point, b Point) Bool = a.x == b.x && a.y == b.y;
let p3 = Point { x: 1.0, y: 2.0 };
println(p1 == p3);
println(p1 == p2);

-- Bitwise overloads on custom struct
struct Mask { bits Int }
fn &(a Mask, b Mask) Mask = Mask { bits: a.bits & b.bits };
fn |(a Mask, b Mask) Mask = Mask { bits: a.bits | b.bits };
fn ^(a Mask, b Mask) Mask = Mask { bits: a.bits ^ b.bits };

let m1 = Mask { bits: 255 };
let m2 = Mask { bits: 15 };
println(m1 & m2);
println(m1 | m2);
println(m1 ^ m2);

-- Function overloading by type
fn describe(x Int) String = "an integer";
fn describe(x Float) String = "a float";
fn describe(x String) String = "a string";

describe(42) println;
describe(3.14) println;
describe("hi") println;
