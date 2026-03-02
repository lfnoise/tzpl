-- QA: Runtime edge cases and special float values

-- Float division by zero
println(1.0 / 0.0);    -- Inf
println(-1.0 / 0.0);   -- -Inf
println(0.0 / 0.0);    -- NaN

-- Infinity arithmetic
let inf = 1.0 / 0.0;
println(inf + 1.0);     -- Inf
println(inf - inf);     -- NaN
println(inf * 0.0);     -- NaN

-- NaN properties (IEEE 754)
let nan = 0.0 / 0.0;
println(nan == nan);    -- false (IEEE)
println(nan != nan);    -- true (IEEE)

-- Very small float
println(1.0e-100);

-- Negative zero
println(-0.0 == 0.0);   -- true (IEEE)

-- Integer division by zero
-- println(1 // 0);  -- could crash, testing separately

-- Large integer arithmetic (doesn't overflow into float)
println(1000000 * 1000000);        -- 1000000000000
println(1000000000 * 1000000000);  -- 1000000000000000000
