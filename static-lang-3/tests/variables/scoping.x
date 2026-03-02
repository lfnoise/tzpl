-- Variable scoping

-- Block scoping
fn block_test() Int {
    let x = 99;
    x
}
let x = 1;
block_test() println;
x println;

-- Inner scope can see outer variables
fn outer_scope() Void {
    let outer = 10;
    fn inner() Int = outer + 5;
    inner() println;
}
outer_scope();

-- Function parameters
fn add(a Int, b Int) Int = a + b;
add(3, 4) println;

-- Var mutation inside function
fn counter() Int {
    var n = 0;
    n = n + 1;
    n = n + 1;
    n = n + 1;
    n
}
counter() println;

-- Let in different scopes with same name
fn scope_a() Int { let a = 100; a }
fn scope_b() Int { let a = 200; a }
scope_a() println;
scope_b() println;

-- Nested function scopes
fn outer_fn() Int {
    let z = 10;
    fn inner_fn() Int = z + 5;
    inner_fn()
}
outer_fn() println;

-- Variable shadowing in lambda
let val = 42;
let f = fn() Int { let val = 99; val };
f() println;
val println;
