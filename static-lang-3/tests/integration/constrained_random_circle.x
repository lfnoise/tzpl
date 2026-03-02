-- Constrained Random Points on a Circle (Rosetta Code)
-- Find all integer grid points in an annulus (10 <= dist <= 15).
-- Use @1/@2 Cartesian product to generate all (x,y) pairs, filter.

fn inAnnulus(x Int, y Int) Bool {
    let d2 = x * x + y * y;
    d2 >= 100 && d2 <= 225
}

fn mkPair(x Int, y Int) (Int, Int) = (x, y);

-- Cartesian product of x and y ranges, flatten, filter
let points = mkPair((-15..15) toArray @1, (-15..15) toArray @2) join
    filter(fn(p (Int, Int)) Bool { inAnnulus(p.0, p.1) });

points length println;
