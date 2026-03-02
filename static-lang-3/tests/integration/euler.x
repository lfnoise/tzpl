-- Project Euler-style problems

-- Problem 1: Sum of multiples of 3 or 5 below 1000
fn euler1(limit Int) Int {
    var sum = 0;
    var n = 1;
    while (n < limit) {
        if (n % 3 == 0 || n % 5 == 0) {
            sum = sum + n;
        }
        n = n + 1;
    }
    sum
}
10 euler1 println;
1000 euler1 println;

-- Problem 2: Sum of even Fibonacci numbers <= 4 million
fn euler2(limit Int) Int {
    var sum = 0;
    var a = 1;
    var b = 2;
    while (b <= limit) {
        if (b % 2 == 0) { sum = sum + b; }
        let next = a + b;
        a = b;
        b = next;
    }
    sum
}
100 euler2 println;
4000000 euler2 println;

-- Problem 6: Sum of squares vs square of sum
fn euler6(n Int) Int {
    var sum = 0;
    var sum_sq = 0;
    var k = 1;
    while (k <= n) {
        sum = sum + k;
        sum_sq = sum_sq + k * k;
        k = k + 1;
    }
    sum * sum - sum_sq
}
10 euler6 println;
100 euler6 println;

-- Problem 9: Pythagorean triplet with a+b+c=1000
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

-- Triangle numbers
fn triangle(n Int) Int = n * (n + 1) // 2;
var t = 1;
while (t <= 10) {
    t triangle println;
    t = t + 1;
}
