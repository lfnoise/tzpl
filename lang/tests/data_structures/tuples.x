-- Tuple tests

-- Pair
let p = (1, 2);
p println;
p.0 println;
p.1 println;

-- Triple
let t = (10, 20, 30);
t println;
t.0 println;
t.1 println;
t.2 println;

-- Mixed types
let m = (42, "hello", 3.14);
m println;
m.0 println;
m.1 println;
m.2 println;

-- Nested tuple
let n = ((1, 2), (3, 4));
n println;
n.0 println;
n.1 println;

-- Tuple arithmetic
let a = (1, 2);
let b = (3, 4);
println(a + b);
println(a * b);
println(a - b);

-- Scalar operations on tuples
println((10, 20) + 1);
println((10, 20) * 2);
println(-(3, 4));

-- Empty tuple
let u = ();
u println;

-- 1-tuple
let single = (42,);
single println;
single.0 println;

-- Trailing comma
let t3 = (1, 2, 3,);
t3 println;
