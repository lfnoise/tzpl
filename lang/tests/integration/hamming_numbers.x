-- Hamming Numbers (Rosetta Code)
-- Numbers whose only prime factors are 2, 3, and 5.
-- Recursive predicate, then filter a range.

fn isHamming(n Int) Bool {
    if (n == 1) { true }
    else if (n % 2 == 0) { isHamming(n // 2) }
    else if (n % 3 == 0) { isHamming(n // 3) }
    else if (n % 5 == 0) { isHamming(n // 5) }
    else { false }
}

-- Filter 1..100 for Hamming numbers, take first 20
(1..100) toArray filter(fn(n Int) Bool { n isHamming }) take(20) println;
