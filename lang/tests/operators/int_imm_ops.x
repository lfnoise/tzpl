-- Verify the i16-immediate Int opcodes (ADDI, SUBI, MULI, EQI, NEI, LTI, LEI, GTI, GEI).
-- The peephole fires when a binary op has an IntLiteral operand in [-32768, 32767].
-- Each test exercises both RHS-literal and LHS-literal placement, including the
-- comparison-flipping cases (e.g. `5 < n` -> emits GTI with n on the left).

var n = 10;

-- ADD
println(n + 1);          -- 11
println(n + 32767);      -- 32777
println(n + (-1));       -- 9
println(n + (-32768));   -- -32758
println(1 + n);          -- 11
println(0 + n);          -- 10

-- SUB (RHS literal only; LHS-literal falls back to the regular SUB path)
println(n - 1);          -- 9
println(n - 32767);      -- -32757
println(n - (-1));       -- 11

-- MUL
println(n * 2);          -- 20
println(n * (-3));       -- -30
println(2 * n);          -- 20

-- Comparisons
println(n < 11);         -- true
println(n < 10);         -- false
println(n <= 10);        -- true
println(n > 9);          -- true
println(n >= 10);        -- true
println(n == 10);        -- true
println(n != 10);        -- false

-- LHS-literal forms (codegen flips the relation)
println(11 > n);         -- true   (n < 11)
println(10 > n);         -- false  (n < 10)
println(10 >= n);        -- true   (n <= 10)
println(9 < n);          -- true   (n > 9)
println(10 <= n);        -- true   (n >= 10)
println(10 == n);        -- true
println(10 != n);        -- false

-- Out-of-range literals fall through to the regular LOAD_INT + arith path.
println(n + 40000);      -- 40010
println(n - 100000);     -- -99990

-- Mixed: literal-on-both-sides should still be const-folded at the AST level.
println(3 + 4);          -- 7
println(10 < 20);        -- true
