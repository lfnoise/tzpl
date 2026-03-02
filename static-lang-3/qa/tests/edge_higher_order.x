-- QA: Higher-order function edge cases

-- Function as first-class value
fn double(x Int) Int = x * 2;
fn triple(x Int) Int = x * 3;

let f = double;
f(5) println;   -- 10

-- Passing function as argument
fn apply(f2 (Int) Int, x Int) Int = f2(x);
apply(double, 5) println;  -- 10
apply(triple, 5) println;  -- 15

-- Returning function
fn make_multiplier(n Int) (Int) Int {
    return fn(x Int) Int { x * n };
}
let times4 = make_multiplier(4);
times4(5) println;   -- 20
times4(10) println;  -- 40

-- Function in array
let fns = [double, triple];
fns[0](5) println;  -- 10
fns[1](5) println;  -- 15

-- Map with function
[1, 2, 3] map(double) println;    -- [2, 4, 6]
[1, 2, 3] map(triple) println;   -- [3, 6, 9]

-- Composition pattern
fn compose(f3 (Int) Int, g (Int) Int) (Int) Int {
    return fn(x Int) Int { f3(g(x)) };
}
let double_then_triple = compose(triple, double);
double_then_triple(5) println;  -- triple(double(5)) = triple(10) = 30

-- Closure over mutable ref
fn make_counter() {
    let count = &0;
    return fn(dummy Int) Int {
        let current = *count;
        count <- current + 1;
        current
    };
}
let counter = make_counter();
counter(0) println;  -- 0
counter(0) println;  -- 1
counter(0) println;  -- 2

-- Recursive function via named fn
fn fib(n Int) Int {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}
fib(10) println;   -- 55

-- Mutual recursion
fn is_even(n Int) Bool {
    if (n == 0) { return true; }
    return is_odd(n - 1);
}
fn is_odd(n Int) Bool {
    if (n == 0) { return false; }
    return is_even(n - 1);
}
is_even(10) println;  -- true
is_odd(7) println;    -- true
is_even(3) println;   -- false
