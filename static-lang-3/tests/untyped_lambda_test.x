-- Untyped lambda parameters: fn(x) { ... } is sugar for fn<T>(x T) { ... }

-- Basic untyped lambda
let double = fn(x) { x + x };
double(3) println;
double(3.14) println;

-- Multiple untyped params
let add = fn(a, b) { a + b };
add(10, 20) println;
add(1.5, 2.5) println;

-- With captures
let offset = 100;
let shift = fn(x) { x + offset };
shift(5) println;
shift(0.5) println;

-- Return type inference
let square = fn(x) { x * x };
square(7) println;
square(2.5) println;

-- Space-pipeline syntax
let negate = fn(x) { 0 - x };
42 negate println;
3.14 negate println;

-- Passed to higher-order function
fn apply(f (Int) Int, x Int) Int = f(x);
let inc = fn(x) { x + 1 };
apply(inc, 10) println;

-- Passed to map/filter/fold
[1, 2, 3, 4, 5] map(fn(x) { x * x }) println;
[1, 2, 3, 4, 5, 6] filter(fn(x) { x % 2 == 0 }) println;
[1, 2, 3, 4, 5] fold(0, fn(acc, x) { acc + x }) println;

-- Mixed: some params typed, some untyped
let mixed = fn(x, n Int) { x + x + n };
mixed(3, 1) println;
mixed(1.5, 1) println;

-- Identity function with different types
let id = fn(x) { x };
id(42) println;
id(3.14) println;
id("world") println;
id(true) println;

-- Nested: untyped lambda capturing from enclosing lambda
let make_adder = fn(n Int) { fn(x) { x + n } };
let add5 = make_adder(5);
add5(10) println;
add5(2.5) println;

-- Inline untyped lambda in map
[10, 20, 30] map(fn(x) { x * 3 }) println;

-- Multiple type params from multiple untyped params
let first = fn(a, b) { a };
first(1, "hello") println;
first("bye", 99) println;
