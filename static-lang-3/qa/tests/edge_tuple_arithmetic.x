-- QA: Edge cases in tuple arithmetic

-- Tuple + Tuple
println((1, 2) + (3, 4));
println((1, 2, 3) + (4, 5, 6));

-- Tuple - Tuple
println((10, 20) - (3, 4));

-- Tuple * Tuple
println((2, 3) * (4, 5));

-- Tuple + Scalar
println((1, 2, 3) + 10);

-- Scalar + Tuple
println(10 + (1, 2, 3));

-- Tuple * Scalar
println((1, 2, 3) * 2);

-- Negation
println(-(1, 2, 3));

-- Nested tuple arithmetic
println(((1, 2), (3, 4)) + ((5, 6), (7, 8)));

-- Tuple with mixed types - does arithmetic still work?
-- println((1, 2.0) + (3, 4.0));  -- Int + Float in tuple

-- Single element tuple
println((42,) + (1,));
println((42,) * 2);
println(-(42,));

-- Empty tuple
let u = ();
u println;

-- Tuple comparison
println((1, 2) == (1, 2));
println((1, 2) == (1, 3));
println((1, 2) != (1, 3));

-- Tuple with float
println((1.0, 2.0) + (3.0, 4.0));
println((1.0, 2.0) * 2.0);

-- Tuple field access
let t = (10, 20, 30, 40);
t.0 println;
t.1 println;
t.2 println;
t.3 println;

-- Tuple array broadcasting
let tup = (1, 2, 3);
let arr = [10, 20];
-- What does tuple + array do?
-- println(tup + arr);

-- Large tuple
let big = (1, 2, 3, 4, 5, 6, 7, 8);
big println;
big.0 println;
big.7 println;
