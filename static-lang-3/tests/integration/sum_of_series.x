-- Sum of a Series (Rosetta Code)
-- Compute sum of 1/k^2 for k=1..1000 (approximates pi^2/6).
-- Auto-map the inverse-square function, then fold to sum.

fn inv_sq(k Int) Float = 1.0 / (k * k);

(1..1000) toArray inv_sq fold(0.0, fn(a Float, b Float) Float { a + b }) println;
