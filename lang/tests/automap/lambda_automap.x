-- Auto-mapping over lambda (closure) values

-- Basic: implicit auto-map of array through lambda
let sq = fn(x Int) Int { x * x };
sq([1, 2, 3, 4, 5]) println;

-- Pipeline style
[1, 2, 3, 4, 5] sq println;

-- Lambda with two params, one auto-mapped, one scalar
let add = fn(a Int, b Int) Int { a + b };
[10, 20, 30] add(5) println;

-- Two arrays (zip semantics)
add([1, 2, 3], [10, 20, 30]) println;

-- Different lengths (zip = shortest)
add([1, 2, 3, 4, 5], [100, 200]) println;

-- Closure capturing a variable
let make_adder = fn(n Int) { fn(x Int) Int { x + n } };
let add10 = make_adder(10);
add10([1, 2, 3]) println;

-- Lambda returning Float
let half = fn(x Int) Float { x / 2.0 };
[1, 2, 3, 4] half println;

-- Nested array: auto-map at depth 2
let double = fn(x Int) Int { x * 2 };
[[1, 2, 3], [4, 5, 6]] double println;

-- Three levels deep
[[[1, 2], [3, 4]], [[5, 6], [7, 8]]] double println;

-- Nested with scalar arg
[[1, 2, 3], [4, 5, 6]] add(10) println;

-- Single element array
sq([42]) println;

-- Empty array
let empty [Int] = [];
sq(empty) println;

-- Explicit @ with lambda
sq([1, 2, 3] @) println;

-- Explicit @ in pipeline
[1, 2, 3] @ sq println;

-- Range piped through lambda
(1..10) toArray sq println;

-- Void-returning lambda auto-mapped for side effects
let accum = fn(x Int) { print(x) };
[10, 20, 30] accum;
println("");

-- Global lambda auto-map
var g_sq = fn(x Int) Int { x * x };
[2, 3, 4] g_sq println;
