-- 100 Doors (Rosetta Code)
-- After 100 passes, only perfect square doors remain open.
-- The open doors are simply 1^2, 2^2, ..., 10^2.

fn square(n Int) Int = n * n;

-- Auto-map square over 1..10
(1..10) toArray square println;
