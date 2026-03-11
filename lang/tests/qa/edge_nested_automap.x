-- QA: Complex nested auto-mapping scenarios

-- Basic @ with unary function
fn negate(x Int) Int = -x;
[1, 2, 3] @ negate println;

-- @@ with unary function (two levels deep)
[[1, 2], [3, 4]] @@ negate println;

-- @@@ (three levels deep)
[[[1, 2], [3, 4]], [[5, 6], [7, 8]]] @@@ negate println;

-- @ on binary operator with scalar
println([1, 2, 3] @ + 10);
println(10 + [1, 2, 3] @);

-- @@ on binary operator
println([[1, 2], [3, 4]] @@ + 10);

-- Cartesian: @1/@2 basic
fn add(a Int, b Int) Int = a + b;
let result = add([1, 2] @1, [10, 20] @2);
result println;

-- Cartesian with single element
let r1 = add([1] @1, [10, 20, 30] @2);
r1 println;

let r2 = add([1, 2, 3] @1, [10] @2);
r2 println;

-- Cartesian with multiplication
fn mul(a Int, b Int) Int = a * b;
let r3 = mul([1, 2, 3] @1, [10, 100] @2);
r3 println;

-- Auto-map struct construction
struct Point { x Int, y Int }
let pts = Point { x: [1, 2, 3], y: 0 };
pts println;

-- Auto-map tuple struct construction
struct Vec2(Int, Int);
let vecs = Vec2([1, 2, 3], 0);
vecs println;

-- @ with reverse
[[1, 2, 3], [4, 5, 6]] @ reverse println;

-- @ with sort
[[3, 1, 2], [6, 4, 5]] @ sort println;

-- @ with length
[[1, 2, 3], [4, 5], [6]] @ length println;

-- Auto-map with take/drop
[[1, 2, 3, 4], [5, 6, 7, 8]] @ take(2) println;
[[1, 2, 3, 4], [5, 6, 7, 8]] @ drop(2) println;

-- Auto-map with overloaded function
fn process(x Int) String = "int";
fn process(x String) String = "string";

[1, 2, 3] process println;  -- auto-maps over array
["a", "b"] process println;

-- Field access auto-map
let points2 = [Point{1, 10}, Point{2, 20}, Point{3, 30}];
points2.x println;
points2.y println;

-- Chained field access + arithmetic
println(points2.x + points2.y);
