-- Factorial (Rosetta Code)
-- Recursive factorial, auto-mapped over a range.

fn factorial(n Int) Int {
    if (n <= 1) { 1 } else { n * factorial(n - 1) }
}

-- Auto-map factorial over 0..10
(0..10) toArray factorial println;
