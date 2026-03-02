-- QA: Integer division edge cases

-- Basic integer division
println(10 // 3);     -- 3
println(10 // 5);     -- 2
println(7 // 2);      -- 3
println(1 // 1);      -- 1
println(0 // 5);      -- 0

-- Negative integer division
println(-10 // 3);    -- -3 or -4? (truncation vs floor)
println(10 // -3);    -- -3 or -4?
println(-10 // -3);   -- 3 or 4?

-- Integer modulo
println(10 % 3);      -- 1
println(10 % 5);      -- 0
println(7 % 2);       -- 1

-- Negative modulo
println(-10 % 3);     -- -1 or 2?
println(10 % -3);     -- 1 or -2?

-- Large integer division
println(1000000000000 // 7);
println(1000000000000 % 7);

-- Division producing fraction (not integer division)
println(1 / 3);       -- 1/3 (Fraction)
println(7 / 2);       -- 7/2 (Fraction)

-- Verify: integer division vs fraction division
let idiv = 7 // 2;    -- 3 (Int)
let fdiv = 7 / 2;     -- 7/2 (Fraction)
println(idiv);
println(fdiv);
