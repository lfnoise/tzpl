-- Vector Products (Rosetta Code)
-- Dot product, cross product, scalar triple, vector triple.
-- All expressed as pure functions on tuples.

fn dot(a (Int, Int, Int), b (Int, Int, Int)) Int =
    a.0 * b.0 + a.1 * b.1 + a.2 * b.2;

fn cross(a (Int, Int, Int), b (Int, Int, Int)) (Int, Int, Int) =
    (a.1 * b.2 - a.2 * b.1,
     a.2 * b.0 - a.0 * b.2,
     a.0 * b.1 - a.1 * b.0);

fn scalarTriple(a (Int, Int, Int), b (Int, Int, Int), c (Int, Int, Int)) Int =
    dot(a, cross(b, c));

fn vectorTriple(a (Int, Int, Int), b (Int, Int, Int), c (Int, Int, Int)) (Int, Int, Int) =
    cross(a, cross(b, c));

let a = (3, 4, 5);
let b = (4, 3, 5);
let c = (-5, -12, -13);

println(dot(a, b));
println(cross(a, b));
println(scalarTriple(a, b, c));
println(vectorTriple(a, b, c));
