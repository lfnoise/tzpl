-- List auto-mapping

-- List + scalar
println(List(1, 2, 3) + 10);
println(100 + List(1, 2, 3));

-- List * scalar
println(List(10, 20, 30) * 2);

-- List - scalar
println(List(10, 20, 30) - 5);

-- List + List (zip)
println(List(1, 2, 3) + List(10, 20, 30));

-- Negation
println(-List(1, 2, 3));

-- List + tuple
println(List(1, 2) + (10, 20));

-- List + array
println(List(1, 2) + [10, 20]);

-- Chained: (List + scalar) * scalar
println((List(1, 2, 3) + 10) * 2);

-- List / scalar (fraction promotion)
println(List(1, 2, 3) / 2);

-- Float list
println(List(1.0, 2.0, 3.0) + 0.5);

-- Int list + float
println(List(1, 2, 3) + 0.5);

-- Function auto-mapping over list with @
fn double(x Int) Int = x * 2;
List(1, 2, 3) @ double println;

-- sqrt over list
List(1, 2, 3) sqrt println;
