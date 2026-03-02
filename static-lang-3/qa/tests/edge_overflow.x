-- QA: Integer overflow behavior (silent wraparound)

-- MAX_INT
let max_int = 9223372036854775807;
max_int println;

-- MAX_INT + 1 wraps to MIN_INT (silent overflow!)
let overflow = max_int + 1;
overflow println;  -- -9223372036854775808

-- Is it actually negative?
println(overflow < 0);  -- true!

-- MIN_INT
let min_int = -9223372036854775807;
min_int println;

-- MIN_INT - 1 wraps (silent underflow!)
let underflow = min_int - 1;
underflow println;

-- Multiplication overflow
println(max_int * 2);

-- These are all technically undefined behavior in C
-- A safe language should either:
-- 1. Detect and error on overflow
-- 2. Use wrapping arithmetic explicitly
-- 3. Use big integers

-- Verify normal operations still work
println(1000000 * 1000000);
println(1000000000 * 1000000000);
