-- Closures and lambda functions

-- No capture
let add = fn(a Int, b Int) Int { a + b };
add(3, 4) println;

-- Single capture
let x = 10;
let addX = fn(a Int) Int { a + x };
addX(5) println;

-- Multiple captures
let a = 3;
let b = 5;
let sum3 = fn(c Int) Int { a + b + c };
sum3(7) println;

-- No arguments
let val = 42;
let getVal = fn() Int { val };
getVal() println;

-- Float capture
let pi = 3.14159;
let double_pi = fn() Float { pi * 2.0 };
double_pi() println;

-- String capture
let greeting = "hello";
let greet = fn(name String) String { greeting $ " " $ name };
"world" greet println;

-- Bool return with capture
let threshold = 10;
let isAbove = fn(n Int) Bool { n > threshold };
isAbove(5) println;
isAbove(15) println;

-- Value-capture semantics
var y = 10;
let captureY = fn() Int { y };
y = 999;
captureY() println;
y println;

-- Multiple calls
let double = fn(n Int) Int { n * 2 };
double(3) println;
double(7) println;

-- Lambda calling lambda
let inc = fn(n Int) Int { n + 1 };
let applyTwice = fn(n Int) Int { inc(inc(n)) };
applyTwice(5) println;

-- Closure factory
fn make_adder(n Int) (Int) Int {
    let result = fn(x Int) Int { x + n };
    result
}
let add5 = make_adder(5);
let add10 = make_adder(10);
add5(3) println;
add10(3) println;

-- Array of lambdas
let fns = [fn(x Int) Int { x * 2 }, fn(x Int) Int { x * 3 }, fn(x Int) Int { 0 - x }];
println(fns[0](5));
println(fns[1](5));
println(fns[2](5));
