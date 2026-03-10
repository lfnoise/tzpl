-- QA: Edge cases in functions

-- Default arguments
fn greet(name String, greeting String = "Hello") String = greeting $ " " $ name;
greet("World", "Hi") println;
greet("World") println;

-- Multiple defaults
fn range_info(start Int = 0, stop Int = 10, step Int = 1) String {
    fmt("%^ to %^ by %^", start, stop, step)
}
range_info(1, 5, 2) println;
range_info(1, 5) println;
range_info(1) println;
range_info() println;

-- Default referencing preceding parameter
fn make_range(lo Int, hi Int = lo + 10) String = fmt("%^ to %^", lo, hi);
make_range(5, 20) println;
make_range(5) println;

-- Variadic functions (typed)
fn sum_all(...xs Int) Int {
    var total = 0;
    for (x : xs) {
        total = total + x;
    }
    total
}
sum_all(1, 2, 3) println;
sum_all(1) println;
sum_all() println;

-- Variadic functions (untyped)
fn wrap(...xs) = xs;
wrap(1, "hello", true) println;
wrap(42) println;
wrap() println;

-- Function overloading with different types
fn describe(x Int) String = "int";
fn describe(x Float) String = "float";
fn describe(x String) String = "string";
fn describe(x Bool) String = "bool";

42 describe println;
3.14 describe println;
"hi" describe println;
true describe println;

-- Recursive function
fn fib(n Int) Int {
    match (n) {
        0: return 0;
        1: return 1;
        _: return fib(n - 1) + fib(n - 2);
    }
    0
}
fib(0) println;
fib(1) println;
fib(10) println;

-- Mutual recursion
fn is_even(n Int) Bool {
    if (n == 0) { return true; }
    is_odd(n - 1)
}
fn is_odd(n Int) Bool {
    if (n == 0) { return false; }
    is_even(n - 1)
}
is_even(0) println;
is_even(1) println;
is_even(4) println;
is_odd(3) println;
is_odd(4) println;

-- Function as value
let f = fn(x Int) Int { x * 2 };
let g = fn(x Int) Int { x + 1 };

-- Apply in sequence
g(f(3)) println;  -- (3*2)+1 = 7
f(g(3)) println;  -- (3+1)*2 = 8

-- Inferred return type
fn add_one(x Int) = x + 1;
add_one(41) println;

fn multiply(a Int, b Int) { a * b }
multiply(6, 7) println;

-- Expression body function
fn square(x Int) Int = x * x;
square(7) println;

-- Overloading with different param counts
fn info(x Int) String = fmt("int: %^", x);
fn info(x Int, y Int) String = fmt("pair: %^ %^", x, y);
info(42) println;
info(1, 2) println;

-- Function passed as argument
fn apply(f2 (Int) Int, x Int) Int = f2(x);
apply(fn(x Int) Int { x * 3 }, 7) println;

-- Higher-order: function returning function
fn make_multiplier(n Int) {
    fn(x Int) { x * n }
}
let times3 = make_multiplier(3);
let times7 = make_multiplier(7);
times3(10) println;
times7(10) println;
