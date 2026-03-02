-- QA: Edge cases in scoping and variable binding

-- Let shadowing in nested scope
let x = 10;
fn test_shadow() Int {
    let x = 20;
    x
}
test_shadow() println;
x println;  -- outer x unaffected

-- var reassignment
var counter = 0;
counter = counter + 1;
counter = counter + 1;
counter = counter + 1;
counter println;  -- 3

-- var in loop
var sum = 0;
for (i : (1..5)) {
    sum = sum + i;
}
sum println;  -- 15

-- let in for loop body
for (i : (1..3)) {
    let doubled = i * 2;
    doubled println;
}

-- Function declaration order doesn't matter
fn first_fn() Int = second_fn() + 1;
fn second_fn() Int = 10;
first_fn() println;  -- 11

-- Nested function definitions
fn outer() Int {
    fn inner(x Int) Int = x * 2;
    inner(21)
}
outer() println;  -- 42

-- Simple inner scope
42 println;

-- Multiple declarations in same scope
let p = 1;
let q = 2;
let r = 3;
println(p + q + r);

-- Constants
const MAX = 100;
const MIN = 0;
println(MAX);
println(MIN);
println(MAX - MIN);

-- Const used in expressions
const PI = 3.14159;
const R = 5.0;
const AREA = PI * R * R;
AREA println;

-- Shadowing let with let in function
let val = 1;
fn use_val() Int {
    let val = 2;  -- shadows outer
    val
}
use_val() println;  -- 2
val println;  -- 1

-- Multiple var mutations
var v = 0;
v = 1;
v = 2;
v = 3;
v = 4;
v = 5;
v println;  -- 5
