-- Advanced Examples from Language X by Example

-- Fibonacci (recursive)
fn fib(n Int) Int {
    if (n <= 1) { return n; }
    fib(n - 1) + fib(n - 2)
}

-- Fibonacci (iterative)
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

fib(0) println;
fib(1) println;
fib(10) println;
fib_iter(10) println;
fib_iter(20) println;

-- FizzBuzz
fn fizzbuzz(n Int) String {
    if (n % 15 == 0) { return "FizzBuzz"; }
    if (n % 3 == 0) { return "Fizz"; }
    if (n % 5 == 0) { return "Buzz"; }
    ""
}

var i = 1;
while (i <= 20) {
    let fb = fizzbuzz(i);
    if (fb == "") { i println; } else { fb println; }
    i = i + 1;
}

-- Euler Problem 9: Pythagorean Triplet
fn euler9() Int {
    var a = 1;
    while (a < 333) {
        var b = a + 1;
        while (b < 500) {
            let c = 1000 - a - b;
            if (c > b) {
                if (a * a + b * b == c * c) {
                    return a * b * c;
                }
            }
            b = b + 1;
        }
        a = a + 1;
    }
    0
}
euler9() println;

-- Complex number powers of i
let i_1 = 1i;
let i_2 = i_1 * i_1;
let i_3 = i_2 * i_1;
let i_4 = i_3 * i_1;
i_1 println;
i_2 println;
i_3 println;
i_4 println;

-- Bitwise operator overloading
struct Mask { bits Int }

fn &(a Mask, b Mask) Mask = Mask { bits: a.bits & b.bits };
fn |(a Mask, b Mask) Mask = Mask { bits: a.bits | b.bits };
fn ^(a Mask, b Mask) Mask = Mask { bits: a.bits ^ b.bits };
fn <<(a Mask, n Int) Mask = Mask { bits: a.bits << n };
fn >>(a Mask, n Int) Mask = Mask { bits: a.bits >> n };

let m1 = Mask { bits: 255 };
let m2 = Mask { bits: 15 };
println(m1 & m2);
println(m1 | m2);
println(m1 << 4);
