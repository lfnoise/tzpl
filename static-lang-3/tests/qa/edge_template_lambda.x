-- QA: Template lambdas (untyped parameters)

-- Basic untyped lambda
let double = fn(x) { x + x };
double(3) println;      -- 6
double(3.14) println;   -- 6.28

-- Multiple untyped params
let add = fn(a, b) { a + b };
add(1, 2) println;      -- 3
add(1.5, 2.5) println;  -- 4.0

-- With captures
let offset = 10;
let shifted = fn(x) { x + offset };
shifted(5) println;     -- 15

-- Passed to higher-order function
fn apply(f (Int) Int, x Int) Int = f(x);
apply(fn(x) { x * x }, 5) println;  -- 25

-- Passed to map
[1, 2, 3] map(fn(x) { x * 10 }) println;

-- Passed to filter
[1, 2, 3, 4, 5, 6] filter(fn(x) { x % 2 == 0 }) println;

-- Passed to fold
[1, 2, 3, 4, 5] fold(0, fn(acc, x) { acc + x }) println;  -- 15

-- Multi-use at different types
let square = fn(x) { x * x };
square(4) println;       -- 16
square(2.5) println;     -- 6.25

-- Identity template lambda
let id = fn(x) { x };
id(42) println;
id("hello") println;
id(true) println;

-- Nested: untyped lambda returning untyped lambda
let make_adder = fn(n Int) { fn(x) { x + n } };
let add5 = make_adder(5);
add5(10) println;     -- 15
add5(2.5) println;    -- 7.5

-- Mixed typed and untyped params
let mixed = fn(x, n Int) { x + x + n };
mixed(3, 1) println;    -- 7
mixed(1.5, 1) println;  -- 4.0

-- Space pipeline with untyped lambda
let negate = fn(x) { 0 - x };
42 negate println;       -- -42
3.14 negate println;     -- -3.14
