-- Fibonacci sequence - recursive

fn fib(n Int) Int {
    if (n <= 1) {
        return n;
    }
    fib(n - 1) + fib(n - 2)
}

-- Print first 15 fibonacci numbers
var i = 0;
while (i <= 14) {
    println(fib(i));
    i = i + 1;
}

-- Iterative fibonacci
fn fib_iter(n Int) Int {
    if (n <= 1) {
        return n;
    }
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

-- Verify iterative matches recursive for fib(20)
let r = fib(20);
let it = fib_iter(20);
println(r);
println(it);
if (r == it) {
    println("iterative matches recursive");
} else {
    println("MISMATCH");
}
