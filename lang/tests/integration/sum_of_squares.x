-- Sum of Squares (Rosetta Code)
-- Auto-map square, then fold to sum.

fn square(x Int) Int = x * x;

(1..10) toArray square fold(0, fn(a Int, b Int) Int { a + b }) println;

[3, 1, 4, 1, 5, 9] square fold(0, fn(a Int, b Int) Int { a + b }) println;
