-- Numeric tower: Int -> Fraction -> Float -> Complex

-- Fraction arithmetic (int/int produces fraction)
let half = 1 / 2;
println(half);

let third = 1 / 3;
println(third);

-- Fraction + Fraction
let five_sixths = half + third;
println(five_sixths);

-- Fraction * Fraction
let one_sixth = half * third;
println(one_sixth);

-- Fraction - Fraction
let diff = half - third;
println(diff);

-- Float arithmetic
let pi = 3.14159265;
let e = 2.71828183;
println(pi + e);
println(pi * e);

-- Int promoted to Float
let x = 2 + 3.5;
println(x);

let y = 10 * 0.5;
println(y);

-- Complex arithmetic
let z1 = 3.0 + 4i;
let z2 = 1.0 + 2i;
println(z1);
println(z2);

-- Complex + Complex
let zsum = z1 + z2;
println(zsum);

-- Complex * Complex: (3+4i)(1+2i) = 3+6i+4i+8i^2 = (3-8)+(6+4)i = -5+10i
let zprod = z1 * z2;
println(zprod);

-- Complex - Complex
let zdiff = z1 - z2;
println(zdiff);

-- Complex negation
let zneg = -z1;
println(zneg);

-- Int + Complex (promotion)
let zint = 10 + 3i;
println(zint);

-- Boolean as integer
let t = true;
let f = false;
let bsum = 1 + 1;
println(bsum);

-- Logical operators
let a = true;
let b = false;
println(a && b);
println(a || b);
println(!a);
println(!b);
