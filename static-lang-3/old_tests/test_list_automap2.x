-- Test more list auto-mapping cases

-- List + tuple should produce lazy list of tuples
let a = List(1, 2) + (10, 20);
println(a);

-- List + array should produce lazy list of arrays
let b = List(1, 2) + [10, 20];
println(b);

-- Chained operations: (List + scalar) * scalar
let c = (List(1, 2, 3) + 10) * 2;
println(c);

-- List / scalar (division promotes to fraction)
let d = List(1, 2, 3) / 2;
println(d);

-- Float list
let e = List(1.0, 2.0, 3.0) + 0.5;
println(e);

-- Int list + float = float list
let f = List(1, 2, 3) + 0.5;
println(f);

-- Function auto-mapping over lists
fn double(x Int) Int = x * 2;
let g = double(List(1, 2, 3) @);
println(g);
