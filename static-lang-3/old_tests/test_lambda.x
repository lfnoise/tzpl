-- Test 1: Simple lambda with no captures
let add = fn(a Int, b Int) Int { a + b };
println(add(3, 4));

-- Test 2: Lambda with single capture
let x = 10;
let addX = fn(a Int) Int { a + x };
println(addX(5));

-- Test 3: Lambda with multiple captures
let a = 3;
let b = 5;
let sum3 = fn(c Int) Int { a + b + c };
println(sum3(5));

-- Test 4: Lambda with no arguments
let val = 42;
let getVal = fn() Int { val };
println(getVal());

-- Test 5: Nested scope capture
let outer = 10;
let inner = outer;
let f = fn() Int { inner };
println(f());

-- Test 6: Value-capture semantics
var y = 10;
let captureY = fn() Int { y };
println(captureY());

-- Test 7: Multiple calls to same lambda
let double = fn(n Int) Int { n * 2 };
println(double(3));
println(double(7));

-- Test 8: Lambda calling another lambda
let inc = fn(n Int) Int { n + 1 };
let applyTwice = fn(n Int) Int { inc(inc(n)) };
println(applyTwice(5));
