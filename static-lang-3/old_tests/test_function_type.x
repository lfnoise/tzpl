-- Test 1: Function type annotation on let binding
let add (Int, Int) Int = fn(a Int, b Int) Int { a + b };
println(add(3, 4));

-- Test 2: Single-argument function type
let double (Int) Int = fn(x Int) Int { x * 2 };
println(double(5));

-- Test 3: Zero-argument function type
let getVal () Int = fn() Int { 42 };
println(getVal());

-- Test 4: Function type with Float return
let halve (Float) Float = fn(x Float) Float { x / 2.0 };
println(halve(10.0));

-- Test 5: Function type as function parameter
fn apply(f (Int) Int, x Int) Int = f(x);
let triple = fn(x Int) Int { x * 3 };
println(apply(triple, 7));

-- Test 6: Function returning a function type
fn make_adder(n Int) (Int) Int {
    let result = fn(x Int) Int { x + n };
    result
}
let add5 = make_adder(5);
println(add5(10));

-- Test 7: Function type with multiple params passed as argument
fn apply2(f (Int, Int) Int, a Int, b Int) Int = f(a, b);
let mul = fn(a Int, b Int) Int { a * b };
println(apply2(mul, 6, 7));

-- Test 8: Nested function types — function taking a function and returning a function
fn compose(f (Int) Int, g (Int) Int) (Int) Int {
    let result = fn(x Int) Int { f(g(x)) };
    result
}
let inc = fn(x Int) Int { x + 1 };
let dbl = fn(x Int) Int { x * 2 };
let inc_then_dbl = compose(dbl, inc);
println(inc_then_dbl(3));

-- Test 9: Function type with String
fn greet_with(f (String) String, name String) String = f(name);
let hello = fn(name String) String { "Hello, " $ name $ "!" };
println(greet_with(hello, "world"));

-- Test 10: Function type with Bool return
fn test_pred(f (Int) Bool, x Int) Bool = f(x);
let is_positive = fn(x Int) Bool { x > 0 };
println(test_pred(is_positive, 5));
println(test_pred(is_positive, -3));
