-- Perfect Numbers (Rosetta Code)
-- A perfect number equals the sum of its proper divisors.
-- Recursive divisor sum, then filter a range.

fn sumDivisorsFrom(n Int, i Int) Int {
    if (i * i > n) { 0 }
    else if (n % i != 0) { sumDivisorsFrom(n, i + 1) }
    else if (i == n // i) { i + sumDivisorsFrom(n, i + 1) }
    else { i + n // i + sumDivisorsFrom(n, i + 1) }
}

fn properDivisorSum(n Int) Int = 1 + sumDivisorsFrom(n, 2);

fn isPerfect(n Int) Bool = n > 1 && n properDivisorSum == n;

-- Filter 2..10000 for perfect numbers
(2..10000) toArray filter(fn(n Int) Bool { n isPerfect }) println;
