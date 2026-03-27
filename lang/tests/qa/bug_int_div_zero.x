-- QA: ACCEPTED - Integer division/modulo by zero returns silent wrong results
-- Follows C semantics (undefined behavior). For a creative coding language
-- meant for performance, weird results are preferable to crashing.

-- Integer div/mod by zero produce platform-dependent results (C UB):
println(1 // 0);     -- 0
println(1 % 0);      -- 1
println(10 // 0);    -- 0
println(10 % 0);     -- 10
println(0 // 0);     -- 0
println(0 % 0);      -- 0

-- For comparison, float division by zero correctly produces special values:
println(1.0 / 0.0);  -- inf
println(0.0 / 0.0);  -- nan

-- Fraction division by zero crashes (better than silent, but could be nicer):
-- println((1/2) / (0/1));  -- Fatal error: rational: divide by zero
