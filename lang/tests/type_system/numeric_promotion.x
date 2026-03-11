-- Numeric promotion rules

-- Int + Float -> Float
println(2 + 3.5);
println(10 * 0.5);
println(1 + 0.0);

-- Int / Int -> Fraction
println(1 / 2);
println(3 / 4);

-- Fraction + Float -> Float
let half = 1 / 2;
println(half + 0.5);
println(half * 2.0);

-- Int + Complex -> Complex
println(10 + 3i);

-- Float + Complex -> Complex
println(1.0 + 2i);

-- Struct field promotion: Int to Float
struct Point { x Float, y Float }
let p = Point { x: 5, y: 10 };
p println;
p.x println;

-- Array promotion
println([1, 2, 3] + 0.5);

-- Function parameter promotion
fn to_float(x Float) = x;
3 to_float println;
[1, 2, 3] to_float println;
List(1/5, 2/5, 3/5) to_float println;
