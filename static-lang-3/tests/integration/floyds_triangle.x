-- Floyd's Triangle (Rosetta Code)
-- Row k contains numbers from k*(k-1)/2+1 to k*(k+1)/2.
-- Auto-map a row-generating function over the range of rows.

fn floydRow(k Int) [Int] =
    (k * (k - 1) // 2 + 1 .. k * (k + 1) // 2) toArray;

-- Auto-map floydRow over 1..5
(1..5) toArray floydRow println;
