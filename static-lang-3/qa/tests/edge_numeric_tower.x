-- QA: Numeric tower promotion (Int -> Fraction -> Float -> Complex)

-- Int / Int -> Fraction
println(1 / 3);        -- 1/3
println(5 / 2);        -- 5/2
println(6 / 3);        -- 2/1

-- Fraction + Int -> Fraction
println(1/2 + 1);      -- 3/2
println(1 + 1/3);      -- 4/3

-- Fraction + Float -> Float
println(1/2 + 0.5);    -- 1.0
println(1/3 + 0.0);    -- 0.333...

-- Int + Float -> Float
println(1 + 0.5);      -- 1.5
println(42 + 0.0);     -- 42.0

-- Float + Complex -> Complex
println(1.0 + 2i);     -- 1+2i
println(0.0 + 0i);     -- 0+0i

-- Int + Complex -> Complex
println(1 + 2i);       -- 1+2i

-- Comparisons across types
println(1/2 < 1.0);    -- true
println(1/2 > 0.0);    -- true
println(1/2 == 0.5);   -- true

-- Fraction in array (all elements promoted)
println([1/2, 1/3, 1/4]);

-- Mixed numeric operations
println(1/2 * 2.0);      -- 1.0
println(1/3 * 3);        -- 1/1
println((1/2) / 2);      -- 1/4

-- Nested promotion
let x = 1 / 3;       -- Fraction
let y = x + 0.5;     -- Float
let z = y + 1i;      -- Complex
z println;

-- Power with different numeric types
println(pow(2.0, 3.0));   -- 8.0
println(pow(2.0, 10.0));  -- 1024.0
