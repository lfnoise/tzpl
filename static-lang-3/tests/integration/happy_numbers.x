-- Happy Numbers (Rosetta Code)
-- Recursive digit-square sum, Floyd's cycle detection via recursion.

fn sumDigitSquares(n Int) Int {
    if (n < 10) { n * n }
    else { (n % 10) * (n % 10) + sumDigitSquares(n // 10) }
}

fn isHappyHelper(slow Int, fast Int, started Bool) Bool {
    if (started && slow == fast) { slow == 1 }
    else {
        isHappyHelper(
            slow sumDigitSquares,
            fast sumDigitSquares sumDigitSquares,
            true
        )
    }
}

fn isHappy(n Int) Bool = isHappyHelper(n, n, false);

-- Filter the range to find happy numbers, take first 8
(1..100) toArray filter(fn(n Int) Bool { n isHappy }) take(8) println;
