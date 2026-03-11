-- Function tests: recursion, multiple functions

--- Factorial ---
fn factorial(n Int) Int {
    if (n <= 1) {
        1
    } else {
        n * factorial(n - 1)
    }
}

println(factorial(0));
println(factorial(1));
println(factorial(5));
println(factorial(10));

--- Power function (iterative) ---
fn power(base Int, exp Int) Int {
    var result = 1;
    var i = 0;
    while (i < exp) {
        result = result * base;
        i = i + 1;
    }
    result
}

println(power(2, 0));
println(power(2, 1));
println(power(2, 10));
println(power(3, 5));

--- GCD (subtraction-based Euclidean algorithm) ---
fn gcd(a Int, b Int) Int {
    var x = a;
    var y = b;
    while (x != y) {
        if (x > y) {
            x = x - y;
        } else {
            y = y - x;
        }
    }
    x
}

println(gcd(12, 8));
println(gcd(100, 75));
println(gcd(17, 13));

--- Sum of digits ---
fn digit_sum(n Int) Int = n < 10 ? n : n % 10 + digit_sum(n // 10);

println(digit_sum(123));
println(digit_sum(9999));
println(digit_sum(1000000));

--- Collatz sequence length ---
fn collatz_len(n Int) Int {
    if (n == 1) {
        0
    } else if (n % 2 == 0) {
        1 + collatz_len(n // 2)
    } else {
        1 + collatz_len(3 * n + 1)
    }
}

println(collatz_len(1));
println(collatz_len(2));
println(collatz_len(6));
println(collatz_len(27));

println(3 + 4 |> collatz_len);

--- Function returning float ---
fn average(a Float, b Float) Float = (a + b) / 2.0;

println(average(3.0, 7.0));
println(1.0 + 2.0 |> average(7.0));
println(average(0.0, 100.0));

--- Fibonacci using accumulator ---
fn fib_acc(n Int, a Int, b Int) Int {
    if (n <= 0) { return a; }
    fib_acc(n - 1, b, a + b)
}
fn fib(n Int) Int = fib_acc(n, 0, 1);

println(fib(10));
println(fib(20));
println(fib(30));

--- String functions ---
fn repeat_str(s String, n Int) String {
    var result = "";
    var i = 0;
    while (i < n) {
        result = result $ s;
        i = i + 1;
    }
    result
}

println(repeat_str("ab", 5));
println(repeat_str("*", 10));

--- Absolute value ---
fn abs_float(x Float) Float = x < 0.0 ? -x : x;

println(abs_float(-3.14));
println(abs_float(2.71));
println(abs_float(0.0));

--- Min and Max ---
fn min(a Int, b Int) Int = a < b ? a : b;

fn max(a Int, b Int) Int = a > b ? a : b;

println(min(3, 7));
println(min(7, 3));
println(max(3, 7));
println(max(7, 3));

--- Clamp ---
fn clamp(x Int, lo Int, hi Int) Int = x < lo ? lo : x > hi ? hi : x;

clamp(5, 0, 10) println;
clamp(-3, 0, 10) println;
clamp(15, 0, 10) println;

--- Space-pipeline syntax: x f === f(x), x f(y) === f(x, y) ---

-- Simple 1-arg pipeline
println(7 collatz_len);

-- With extra args: x f(y) === f(x, y)
println(3.0 average(7.0));

-- 1-arg pipeline with float
println(-3.14 abs_float);

-- 3-arg: x f(y, z) === f(x, y, z)
println(15 clamp(0, 10));

-- Chaining: x f g === g(f(x))
println(6 digit_sum collatz_len);

-- Chaining with args: x f(y) g(z) === g(f(x, y), z)
println(3.0 average(7.0) average(10.0));

3.0 average(7.0) average(10.0) println;

--- Inferred return type tests ---

-- Simple expression body
fn square(x Int) = x * x;
println(square(5));

-- Block body with trailing expression
fn add_one(x Int) { x + 1 }
println(add_one(9));

-- If-else trailing expression
fn abs_val(x Int) {
    if (x < 0) { -x } else { x }
}
println(abs_val(-7));

-- Numeric promotion: int * float -> float
fn to_float(x Int) = x * 1.0;
println(to_float(3));

-- Lambda with inferred return type
let mul = fn(a Int, b Int) { a * b };
println(mul(3, 4));
