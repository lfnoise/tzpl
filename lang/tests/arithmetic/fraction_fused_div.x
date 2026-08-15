-- Int / Int emits the fused DIV_INT_TO_FRAC opcode (GCD-reduce directly,
-- no INT_TO_FRAC conversions). The disassembly locks in the fused form;
-- the value checks lock in sign/zero/reduction behavior matching DIV_FRAC.

fn divideTwoInts(a Int, b Int) Fraction { a / b }

disassemble(divideTwoInts);

-- Reduction and sign normalization
println(divideTwoInts(30, 9));    -- 10/3
println(divideTwoInts(-30, 9));   -- -10/3
println(divideTwoInts(30, -9));   -- -10/3
println(divideTwoInts(-30, -9));  -- 10/3
println(divideTwoInts(0, 5));     -- 0/1
println(divideTwoInts(7, 1));     -- 7/1
println(divideTwoInts(6, 3));     -- 2/1

-- Mixed operands still take the general fraction path
println(1/2 + 30 / 9);            -- 23/6
println(30 / (1/3));              -- 90/1

-- Auto-mapped division over int arrays uses the fused opcode per element
println([30, 20, 10] / 4);
println([1, 2, 3] / [2, 4, 6]);
