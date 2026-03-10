-- QA: Division by zero edge cases

-- Float division by zero (IEEE 754)
println(1.0 / 0.0);      -- inf
println(-1.0 / 0.0);     -- -inf
println(0.0 / 0.0);      -- nan

-- Note: % only works on integers, not floats

-- Integer division by zero
-- println(1 // 0);       -- what happens?
-- println(1 % 0);        -- what happens?
-- println(0 // 0);       -- what happens?

-- Fraction: 0/1 is valid
println(0/1);
println(0/1 + 1/2);

-- Division producing fraction
println(1 / 3);
println(7 / 2);

-- Fraction division
println((1/2) / (1/3));  -- 3/2
println((1/2) / (1/2));  -- 1/1

-- Fraction division by zero fraction?
-- println((1/2) / (0/1));  -- what happens?
