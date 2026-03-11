-- Basic function definitions and calls

-- Expression body (single line)
fn add(a Int, b Int) Int = a + b;
add(1, 2) println;
add(0, 0) println;
add(-5, 5) println;

-- Block body
fn multiply(a Int, b Int) Int {
    a * b
}
multiply(3, 4) println;
multiply(0, 100) println;

-- Return type annotation
fn square(x Int) Int = x * x;
square(5) println;
square(-3) println;
square(0) println;

-- Inferred return type
fn double(x Int) = x * 2;
double(7) println;

-- Block body with inferred return
fn add_one(x Int) { x + 1 }
add_one(9) println;

-- If-else trailing expression (inferred return)
fn abs_val(x Int) {
    if (x < 0) { -x } else { x }
}
abs_val(-7) println;
abs_val(7) println;

-- Void function
fn greet(name String) Void {
    println("Hello, " $ name $ "!");
}
greet("World");
greet("Alice");

-- Multiple parameters
fn clamp(x Int, lo Int, hi Int) Int = x < lo ? lo : x > hi ? hi : x;
clamp(5, 0, 10) println;
clamp(-3, 0, 10) println;
clamp(15, 0, 10) println;

-- Explicit return
fn early_return(x Int) Int {
    if (x < 0) {
        return -1;
    }
    return x;
}
early_return(-5) println;
early_return(10) println;

-- String function
fn repeat_str(s String, n Int) String {
    var result = "";
    var i = 0;
    while (i < n) {
        result = result $ s;
        i = i + 1;
    }
    result
}
repeat_str("ab", 3) println;
repeat_str("*", 5) println;
