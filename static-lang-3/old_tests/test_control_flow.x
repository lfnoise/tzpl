-- Control flow tests: nested if/else, while loops, boolean logic

-- Nested if/else
fn classify(n Int) String {
    if (n < 0) {
        return "negative";
    } else {
        if (n == 0) {
            return "zero";
        } else {
            if (n < 10) {
                return "small";
            } else {
                if (n < 100) {
                    return "medium";
                } else {
                    return "large";
                }
            }
        }
    }
}

println(classify(-5));
println(classify(0));
println(classify(7));
println(classify(42));
println(classify(999));

-- FizzBuzz
fn fizzbuzz(n Int) String {
    if (n % 15 == 0) {
        return "FizzBuzz";
    }
    if (n % 3 == 0) {
        return "Fizz";
    }
    if (n % 5 == 0) {
        return "Buzz";
    }
    return "";
}

var i = 1;
while (i <= 20) {
    let fb = fizzbuzz(i);
    if (fb == "") {
        println(i);
    } else {
        println(fb);
    }
    i = i + 1;
}

-- Nested while loops: multiplication table
var row = 1;
while (row <= 5) {
    var col = 1;
    var line = "";
    while (col <= 5) {
        let val = row * col;
        -- Build a simple representation
        col = col + 1;
    }
    row = row + 1;
}

-- Sum of squares
fn sum_of_squares(n Int) Int {
    var total = 0;
    var k = 1;
    while (k <= n) {
        total = total + k * k;
        k = k + 1;
    }
    total
}

println(sum_of_squares(1));
println(sum_of_squares(5));
println(sum_of_squares(10));
println(sum_of_squares(100));

-- Is prime
fn is_prime(n Int) Int {
    if (n < 2) {
        return 0;
    }
    var d = 2;
    while (d * d <= n) {
        if (n % d == 0) {
            return 0;
        }
        d = d + 1;
    }
    1
}

-- Print primes up to 50
var p = 2;
while (p <= 50) {
    if (is_prime(p)) {
        println(p);
    }
    p = p + 1;
}

-- Count primes up to N
fn count_primes(n Int) Int {
    var count = 0;
    var k = 2;
    while (k <= n) {
        if (is_prime(k)) {
            count = count + 1;
        }
        k = k + 1;
    }
    count
}

println(count_primes(100));
println(count_primes(1000));
