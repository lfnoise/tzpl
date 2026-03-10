-- QA: Built-in math functions

-- Trig
println(sin(0.0));      -- 0.0
println(cos(0.0));      -- 1.0
println(tan(0.0));      -- 0.0

-- Inverse trig
println(asin(0.0));     -- 0.0
println(acos(1.0));     -- 0.0
println(atan(0.0));     -- 0.0

-- atan2
println(atan2(1.0, 1.0));  -- ~0.785 (pi/4)
println(atan2(0.0, 1.0));  -- 0.0

-- Hyperbolic
println(sinh(0.0));     -- 0.0
println(cosh(0.0));     -- 1.0
println(tanh(0.0));     -- 0.0

-- exp, log
println(exp(0.0));      -- 1.0
println(exp(1.0));      -- ~2.718
println(log(1.0));      -- 0.0

-- exp2, log2
println(exp2(3.0));     -- 8.0
println(exp2(0.0));     -- 1.0
println(log2(8.0));     -- 3.0
println(log2(1024.0));  -- 10.0

-- log10
println(log10(100.0));  -- 2.0
println(log10(1000.0)); -- 3.0

-- sqrt, cbrt
println(sqrt(4.0));     -- 2.0
println(sqrt(2.0));     -- ~1.414
println(cbrt(27.0));    -- 3.0
println(cbrt(8.0));     -- 2.0

-- pow
println(pow(2.0, 10.0));  -- 1024.0
println(pow(3.0, 0.0));   -- 1.0
println(pow(0.0, 0.0));   -- 1.0

-- hypot
println(hypot(3.0, 4.0));   -- 5.0
println(hypot(0.0, 0.0));   -- 0.0

-- floor, ceil, round, trunc
println(floor(3.7));    -- 3.0
println(floor(-3.7));   -- -4.0
println(ceil(3.2));     -- 4.0
println(ceil(-3.2));    -- -3.0
println(round(3.5));    -- 4.0
println(round(3.4));    -- 3.0
println(trunc(3.7));    -- 3.0
println(trunc(-3.7));   -- -3.0

-- copysign
println(copysign(1.0, -1.0));   -- -1.0
println(copysign(-1.0, 1.0));   -- 1.0

-- remainder
println(remainder(5.5, 2.0));

-- gamma
println(tgamma(5.0));   -- 24.0 (4!)
println(tgamma(1.0));   -- 1.0

-- erf
println(erf(0.0));      -- 0.0

-- pi-scaled trig
println(sinpi(0.5));    -- 1.0
println(cospi(0.5));    -- ~0.0
println(sinpi(0.0));    -- 0.0
println(cospi(0.0));    -- 1.0
