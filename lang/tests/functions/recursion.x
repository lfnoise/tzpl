-- Recursive functions

-- Factorial
fn factorial(n Int) Int {
    if (n <= 1) { 1 } else { n * factorial(n - 1) }
}
factorial(0) println;
factorial(1) println;
factorial(5) println;
factorial(10) println;

-- Fibonacci (recursive)
fn fib(n Int) Int {
    if (n <= 1) { return n; }
    fib(n - 1) + fib(n - 2)
}
fib(0) println;
fib(1) println;
fib(5) println;
fib(10) println;

-- GCD
fn gcd(a Int, b Int) Int {
    var x = a;
    var y = b;
    while (x != y) {
        if (x > y) { x = x - y; } else { y = y - x; }
    }
    x
}
gcd(12, 8) println;
gcd(100, 75) println;
gcd(17, 13) println;

-- Sum of digits
fn digit_sum(n Int) Int = n < 10 ? n : n % 10 + digit_sum(n // 10);
digit_sum(123) println;
digit_sum(9999) println;
digit_sum(1000000) println;

-- Collatz sequence length
fn collatz_len(n Int) Int {
    if (n == 1) { 0 }
    else if (n % 2 == 0) { 1 + collatz_len(n // 2) }
    else { 1 + collatz_len(3 * n + 1) }
}
collatz_len(1) println;
collatz_len(2) println;
collatz_len(6) println;
collatz_len(27) println;
