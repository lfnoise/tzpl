-- Unary operator overloading

-- Unary negation on a struct
struct Vec2 { x Float, y Float }

fn -(v Vec2) Vec2 = Vec2 { x: -v.x, y: -v.y };

let v = Vec2 { x: 1.0, y: 2.0 };
println(-v);

-- Double negation
println(-(-v));

-- Unary negation coexists with binary subtraction
fn -(a Vec2, b Vec2) Vec2 = Vec2 { x: a.x - b.x, y: a.y - b.y };

let zero = Vec2 { x: 0.0, y: 0.0 };
println(zero - v);
println(-v);

-- Unary negation on tuple struct
struct Pair(Int, Int);
fn -(p Pair) Pair = Pair(-p.0, -p.1);

let p = Pair(3, 7);
println(-p);

-- Unary logical not on custom type
struct Flag { val Bool }
fn !(f Flag) Flag = Flag { val: !f.val };

let t = Flag { val: true };
let f = Flag { val: false };
println(!t);
println(!f);

-- Unary bitwise not on custom type
struct Mask { bits Int }
fn ~(m Mask) Mask = Mask { bits: ~m.bits };

let m = Mask { bits: 0 };
println(~m);
let m2 = Mask { bits: 255 };
println(~m2);

-- Unary negation in expressions
fn +(a Vec2, b Vec2) Vec2 = Vec2 { x: a.x + b.x, y: a.y + b.y };

let a = Vec2 { x: 3.0, y: 4.0 };
let b = Vec2 { x: 1.0, y: 2.0 };
println(a + (-b));

-- Built-in unary ops still work
println(-42);
println(-3.14);
println(!true);
println(~0);
