-- Higher-order functions

-- Function as parameter
fn apply(f (Int) Int, x Int) Int = f(x);
let double = fn(x Int) Int { x * 2 };
let triple = fn(x Int) Int { x * 3 };
apply(double, 5) println;
apply(triple, 5) println;

-- Function with two params
fn apply2(f (Int, Int) Int, a Int, b Int) Int = f(a, b);
let mul = fn(a Int, b Int) Int { a * b };
apply2(mul, 6, 7) println;

-- Function returning a function
fn make_adder(n Int) (Int) Int {
    let result = fn(x Int) Int { x + n };
    result
}
let add5 = make_adder(5);
let add10 = make_adder(10);
add5(3) println;
add10(3) println;

-- Composition
fn compose(f (Int) Int, g (Int) Int) (Int) Int {
    let result = fn(x Int) Int { f(g(x)) };
    result
}
let inc = fn(x Int) Int { x + 1 };
let dbl = fn(x Int) Int { x * 2 };
let inc_then_dbl = compose(dbl, inc);
let dbl_then_inc = compose(inc, dbl);
inc_then_dbl(3) println;
dbl_then_inc(3) println;

-- Map with lambda
[1, 2, 3, 4, 5] map(fn(x Int) { x * x }) println;

-- Filter with lambda
[1, 2, 3, 4, 5, 6] filter(fn(x Int) { x % 2 == 0 }) println;

-- Fold with lambda
[1, 2, 3, 4, 5] fold(0, fn(acc Int, x Int) { acc + x }) println;

-- Test predicate
fn test_pred(f (Int) Bool, x Int) Bool = f(x);
let is_positive = fn(x Int) Bool { x > 0 };
test_pred(is_positive, 5) println;
test_pred(is_positive, -3) println;
