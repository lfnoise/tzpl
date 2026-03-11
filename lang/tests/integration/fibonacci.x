-- Fibonacci integration test

-- Recursive fibonacci
fn fib(n Int) Int {
    if (n <= 1) { return n; }
    fib(n - 1) + fib(n - 2)
}

-- Print first 15 fibonacci numbers
var i = 0;
while (i <= 14) {
    i fib println;
    i = i + 1;
}

-- Iterative fibonacci
fn fib_iter(n Int) Int {
    if (n <= 1) { return n; }
    var a = 0;
    var b = 1;
    var k = 2;
    while (k <= n) {
        let next = a + b;
        a = b;
        b = next;
        k = k + 1;
    }
    b
}

-- Verify both match
let r = 20 fib;
let it = 20 fib_iter;
r println;
it println;
println(r == it);
