-- Kaprekar Numbers (Rosetta Code)
-- k^2 can be split into two parts that sum to k.
-- All helpers are recursive, then filter a range.

fn numDigits(n Int) Int {
    if (n < 10) { 1 } else { 1 + numDigits(n // 10) }
}

fn powOf10(n Int) Int {
    if (n == 0) { 1 } else { 10 * powOf10(n - 1) }
}

fn isKaprekarHelper(sq Int, n Int, d Int, maxD Int) Bool {
    if (d >= maxD) { false }
    else {
        let p = powOf10(d);
        let right = sq % p;
        let left = sq // p;
        if (right > 0 && left + right == n) { true }
        else { isKaprekarHelper(sq, n, d + 1, maxD) }
    }
}

fn isKaprekar(n Int) Bool {
    if (n == 1) { true }
    else { isKaprekarHelper(n * n, n, 1, numDigits(n * n)) }
}

-- Filter range for Kaprekar numbers
(1..10000) toArray filter(fn(n Int) Bool { n isKaprekar }) println;
