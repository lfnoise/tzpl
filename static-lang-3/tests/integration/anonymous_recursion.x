-- Anonymous Recursion (Rosetta Code)
-- Demonstrate recursion within a closure using a Ref for self-reference.

fn make_fib() (Int) Int {
    let self = &fn(n Int) Int { 0 };
    let fib = fn(n Int) Int {
        if (n <= 1) { n }
        else { (*self)(n - 1) + (*self)(n - 2) }
    };
    self <- fib;
    fib
}

let fib = make_fib();

-- Map the anonymous recursive fib over a range
(0..10) toArray map(fn(n Int) Int { fib(n) }) println;
