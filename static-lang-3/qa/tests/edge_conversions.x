-- QA: Edge cases in type conversions

-- toInt
println(toInt(3.14));
println(toInt(3.99));
println(toInt(-3.14));
println(toInt(-3.99));
println(toInt(0.0));

-- toFloat
println(toFloat(42));
println(toFloat(0));
println(toFloat(-42));

-- toFloat from fraction
println(toFloat(1/3));
println(toFloat(1/2));
println(toFloat(2/1));

-- Fraction operations
let f = 1/3;
let g = 2/3;
println(f + g);  -- should be 1/1 or 3/3 simplified

-- Fraction comparison
println(1/3 < 2/3);
println(2/3 < 1/3);
println(1/3 == 1/3);
println(1/2 == 2/4);

-- Fraction arithmetic
println(1/3 + 1/6);  -- 1/2
println(1/2 - 1/3);  -- 1/6
println(2/3 * 3/4);  -- 1/2
println(1/2 / 1/3);  -- 3/2

-- Fraction simplification
println(2/4);   -- should print 1/2
println(6/9);   -- should print 2/3
println(10/5);  -- should print 2/1

-- Complex decomposition
let z = 3.0 + 4i;
real(z) println;
imag(z) println;
abs(z) println;  -- 5.0
norm(z) println;  -- 25.0
conj(z) println;

-- Complex from polar
let w = polar(1.0, 0.0);
real(w) println;  -- 1.0
imag(w) println;  -- 0.0 (or very close)

-- abs on different types
println(abs(-42));
println(abs(42));
println(abs(-3.14));
println(abs(3.14));
println(abs(-1/3));
println(abs(1/3));

-- sign on different types
println(sign(-42));
println(sign(0));
println(sign(42));
println(sign(-3.14));
println(sign(0.0));
println(sign(3.14));

-- min/max
println(min(1, 2));
println(min(2, 1));
println(max(1, 2));
println(max(2, 1));
println(min(1.0, 2.0));
println(max(1.0, 2.0));

-- clamp
println(clamp(5, 0, 10));
println(clamp(-5, 0, 10));
println(clamp(15, 0, 10));
println(clamp(0, 0, 10));
println(clamp(10, 0, 10));
