-- Fibonacci Sequence (Rosetta Code)
-- Recursive fib, auto-mapped over a range.

fn fib(n Int) Int {
    if (n <= 1) { n } else { fib(n - 1) + fib(n - 2) }
}

-- Auto-map fib over 0..15
(0..15) toArray fib println;
