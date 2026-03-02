-- Pythagorean Triples (Rosetta Code)
-- Use @1/@2 Cartesian product to generate (a,b) pairs,
-- filter for a^2 + b^2 == c^2 where c = floor(sqrt(a^2+b^2)).

fn isPythag(a Int, b Int) Bool {
    let c2 = a * a + b * b;
    let c = c2 sqrt floor;
    c * c == c2 && a + b + c <= 100
}

fn isPrimPythag(a Int, b Int) Bool =
    isPythag(a, b) && gcd(a, b) == 1;

fn mkPair(a Int, b Int) (Int, Int) = (a, b);

-- Cartesian product of a and b ranges, flatten, filter for a < b
let pairs = mkPair((3..48) toArray @1, (4..48) toArray @2) join
    filter(fn(p (Int, Int)) Bool { p.0 < p.1 && isPythag(p.0, p.1) });

pairs length println;
pairs filter(fn(p (Int, Int)) Bool { isPrimPythag(p.0, p.1) }) length println;
