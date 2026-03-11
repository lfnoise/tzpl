-- Catalan Numbers (Rosetta Code)
-- C(0)=1, C(n) = C(n-1) * 2*(2n-1) / (n+1)
-- Use scan to generate the sequence from the recurrence.

let catalans = (1..15) toArray scan(1, fn(c Int, n Int) Int {
    c * 2 * (2 * n - 1) // (n + 1)
});
catalans println;
