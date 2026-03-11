-- Function type annotations

-- Function type on let binding
let add (Int, Int) Int = fn(a Int, b Int) Int { a + b };
add(3, 4) println;

-- Single-argument function type
let double (Int) Int = fn(x Int) Int { x * 2 };
double(5) println;

-- Zero-argument function type
let getVal () Int = fn() Int { 42 };
getVal() println;

-- Function type with Float
let halve (Float) Float = fn(x Float) Float { x / 2.0 };
halve(10.0) println;

-- Function type as function parameter
fn apply(f (Int) Int, x Int) Int = f(x);
apply(fn(x Int) Int { x * 3 }, 7) println;

-- Function returning function type
fn make_adder(n Int) (Int) Int {
    let result = fn(x Int) Int { x + n };
    result
}
make_adder(5)(10) println;

-- Nested function types (compose)
fn compose(f (Int) Int, g (Int) Int) (Int) Int {
    let result = fn(x Int) Int { f(g(x)) };
    result
}
let inc = fn(x Int) Int { x + 1 };
let dbl = fn(x Int) Int { x * 2 };
compose(dbl, inc)(3) println;

-- Function type with String
fn greet_with(f (String) String, name String) String = f(name);
let hello = fn(name String) String { "Hello, " $ name $ "!" };
greet_with(hello, "world") println;

-- Function type with Bool return
fn test_pred(f (Int) Bool, x Int) Bool = f(x);
let is_pos = fn(x Int) Bool { x > 0 };
test_pred(is_pos, 5) println;
test_pred(is_pos, -3) println;
