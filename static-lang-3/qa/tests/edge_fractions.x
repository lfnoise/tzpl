-- QA: Fraction arithmetic edge cases
-- Note: 1/3 println doesn't work because space pipeline has higher
-- precedence than /, so it parses as 1 / (3 println). Use println(1/3).

-- Basic fractions
println(1/3);
println(-1/3);
println(0/1);

-- Auto-simplification
println(2/4);     -- 1/2
println(6/9);     -- 2/3
println(12/8);    -- 3/2

-- Fraction arithmetic
println(1/3 + 1/6);    -- 1/2
println(1/2 * 2/3);    -- 1/3
println(1/2 - 1/3);    -- 1/6
println((2/3) / (4/5));  -- 5/6

-- Fraction comparison
println(1/3 < 1/2);    -- true
println(1/2 > 1/3);    -- true
println(2/4 == 1/2);   -- true
println(1/3 != 1/2);   -- true
println(1/3 >= 1/3);   -- true
println(1/3 <= 1/2);   -- true

-- Mixed Int/Fraction arithmetic
println(1/3 + 1);      -- 4/3
println(2 * (1/3));     -- 2/3
println(1 - 1/3);      -- 2/3

-- abs, sign on fractions
println(abs(-1/3));     -- 1/3
println(sign(-1/3));    -- -1
println(sign(1/3));     -- 1
println(sign(0/1));     -- 0

-- min, max on fractions
println(min(1/3, 1/2)); -- 1/3
println(max(1/3, 1/2)); -- 1/2

-- clamp on fractions
println(clamp(1/6, 1/4, 1/2));   -- 1/4
println(clamp(3/4, 1/4, 1/2));   -- 1/2
println(clamp(1/3, 1/4, 1/2));   -- 1/3

-- Fraction to Float
println(toFloat(1/3));  -- 0.333...
println(toFloat(1/2));  -- 0.5

-- Int to Fraction
println(toFraction(5));  -- 5/1

-- Negative fraction
println(-3/4);
println((-3)/4);

-- Large numerator/denominator
println(999999999/1000000000);

-- Negation
println(-(1/2));
println(-(-(1/2)));
