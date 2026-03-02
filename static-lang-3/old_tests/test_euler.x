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

println(euler1(10));
println(euler1(1000));

-- Problem 2: Sum of even Fibonacci numbers <= 4 million
fn euler2(limit Int) Int {
    var sum = 0;
    var a = 1;
    var b = 2;
    while (b <= limit) {
        if (b % 2 == 0) {
            sum = sum + b;
        }
        let next = a + b;
        a = b;
        b = next;
    }
    sum
}

println(euler2(100));
println(euler2(4000000));

-- Problem 6: Difference between sum of squares and square of sum
fn euler6(n Int) Int {
    var sum = 0;
    var sum_sq = 0;
    var k = 1;
    while (k <= n) {
        sum = sum + k;
        sum_sq = sum_sq + k * k;
        k = k + 1;
    }
    let sq_sum = sum * sum;
    sq_sum - sum_sq
}

println(euler6(10));
println(euler6(100));

-- Problem 9 approach: find a*b*c where a+b+c=1000, a^2+b^2=c^2
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

println(euler9());

-- Integer square root (floor) via linear search
fn isqrt(n Int) Int {
    var r = 0;
    while ((r + 1) * (r + 1) <= n) {
        r = r + 1;
    }
    r
}

println(isqrt(0));
println(isqrt(1));
println(isqrt(4));
println(isqrt(9));
println(isqrt(10));
println(isqrt(100));
println(isqrt(144));
println(isqrt(1000000));

-- Triangle numbers
fn triangle(n Int) Int = n * (n + 1) // 2;

var t = 1;
while (t <= 10) {
    println(triangle(t));
    t = t + 1;
}
