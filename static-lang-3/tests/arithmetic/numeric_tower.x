-- Numeric tower: Int -> Fraction -> Float -> Complex

-- Int + Int stays Int
println(2 + 3);

-- Int / Int produces Fraction
println(1 / 2);
println(1 / 3);

-- Fraction + Fraction
println(1/2 + 1/3);

-- Fraction * Fraction
println(1/2 * 1/3);

-- Fraction - Fraction
println(1/2 - 1/3);

-- Int promoted to Float
println(2 + 3.5);
println(10 * 0.5);

-- Float arithmetic
println(3.14159265 + 2.71828183);
println(3.14159265 * 2.71828183);

-- Complex arithmetic
let z1 = 3.0 + 4i;
let z2 = 1.0 + 2i;
z1 println;
z2 println;
println(z1 + z2);
println(z1 * z2);
println(z1 - z2);
println(-z1);

-- Int + Complex (promotion)
println(10 + 3i);
