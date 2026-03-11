-- Functions from Tzopilotl by Example

-- Block body
fn max(a Int, b Int) Int {
    if (a > b) {
        a
    } else {
        b
    }
}

fn square(x Int) Int {
    x * x
}

-- Explicit return
fn abs(x Int) Int {
    if (x < 0) {
        return -x;
    }
    x
}

max(3, 7) println;
square(5) println;
abs(-10) println;
abs(10) println;

-- Expression body
fn square2(x Int) Int = x * x;
fn max2(a Int, b Int) Int = a > b ? a : b;
fn average(a Float, b Float) Float = (a + b) / 2.0;
fn greet(who String) String = "Hi, " $ who $ "!";

square2(4) println;
max2(3, 7) println;
average(3.0, 7.0) println;
greet("World") println;

-- Void functions
fn say_hello(name String) Void {
    println("Hello, " $ name $ "!");
}
say_hello("Alice");

-- Inferred return types
fn square3(x Int) = x * x;
fn add_one(x Int) { x + 1 }
fn to_float(x Int) = x * 1.0;
fn abs_val(x Int) {
    if (x < 0) { -x } else { x }
}

square3(6) println;
add_one(9) println;
to_float(3) println;
abs_val(-7) println;
abs_val(7) println;

-- Declaration order doesn't matter
fn foo(x Int) = bar(x) + 1;
fn bar(x Int) = x * 2;
foo(5) println;

-- Lambdas infer return types too
let mul = fn(a Int, b Int) { a * b };
mul(3, 4) println;

-- Lambda capturing
let greeting = "hello";
let greet2 = fn(name String) String { greeting $ " " $ name };
"world" greet2 println;
