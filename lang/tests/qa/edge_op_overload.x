-- QA: Operator overloading edge cases

struct Vec2 { x Float, y Float }

-- Arithmetic operators
fn +(a Vec2, b Vec2) Vec2 = Vec2 { x: a.x + b.x, y: a.y + b.y };
fn -(a Vec2, b Vec2) Vec2 = Vec2 { x: a.x - b.x, y: a.y - b.y };
fn *(s Float, v Vec2) Vec2 = Vec2 { x: s * v.x, y: s * v.y };
fn *(v Vec2, s Float) Vec2 = Vec2 { x: v.x * s, y: v.y * s };
fn /(v Vec2, s Float) Vec2 = Vec2 { x: v.x / s, y: v.y / s };

-- Comparison operators
fn ==(a Vec2, b Vec2) Bool = a.x == b.x && a.y == b.y;
fn !=(a Vec2, b Vec2) Bool = !(a == b);

-- Helper functions
fn mag(v Vec2) Float = sqrt(v.x * v.x + v.y * v.y);
fn dot(a Vec2, b Vec2) Float = a.x * b.x + a.y * b.y;

let v1 = Vec2{1.0, 2.0};
let v2 = Vec2{3.0, 4.0};

-- Test arithmetic
println(v1 + v2);         -- Vec2 { x: 4.0, y: 6.0 }
println(v2 - v1);         -- Vec2 { x: 2.0, y: 2.0 }
println(2.0 * v1);        -- Vec2 { x: 2.0, y: 4.0 }
println(v1 * 3.0);        -- Vec2 { x: 3.0, y: 6.0 }
println(v2 / 2.0);        -- Vec2 { x: 1.5, y: 2.0 }

-- Test comparison
println(v1 == v1);        -- true
println(v1 == v2);        -- false
println(v1 != v2);        -- true

-- Test compound operations
let sum = v1 + v2;
mag(sum) println;

-- dot product
dot(v1, v2) println;       -- 1*3 + 2*4 = 11.0

-- Chained operations
println(v1 + v2 + Vec2{10.0, 10.0});

-- Auto-map custom functions on arrays of structs
let vecs = [Vec2{1.0, 0.0}, Vec2{0.0, 1.0}, Vec2{3.0, 4.0}];
vecs mag println;

-- Unary negation overloading (now fixed)
fn -(v Vec2) Vec2 = Vec2 { x: -v.x, y: -v.y };
println(-v1);

-- Unary bitwise not on custom type
struct Mask { bits Int }
fn ~(m Mask) Mask = Mask { bits: ~m.bits };
println(~Mask{0});
println(~Mask{255});

-- Unary logical not on custom type
struct Flag { val Bool }
fn !(f Flag) Flag = Flag { val: !f.val };
println(!Flag{true});
println(!Flag{false});
