-- QA: Edge cases in arithmetic operations

-- Integer overflow: MAX_INT + 1
let max_int = 9223372036854775807;
max_int println;

-- What happens with MAX_INT + 1? Overflow or error?
println(max_int + 1);

-- MIN_INT
let min_int = -9223372036854775807;
min_int println;

-- MIN_INT - 1
println(min_int - 1);

-- Integer division by zero
-- println(1 // 0);  -- should this error?

-- Modulo by zero
-- println(1 % 0);  -- should this error?

-- Modulo with negative numbers
println(7 % 3);
println(-7 % 3);
println(7 % -3);
println(-7 % -3);

-- Integer division with negative numbers
println(7 // 3);
println(-7 // 3);
println(7 // -3);
println(-7 // -3);

-- Float division produces fractions - chain of operations
println(1 / 3 + 1 / 3 + 1 / 3);

-- Fraction * 0
println((1/3) * 0);

-- Fraction with large numerator/denominator
println(999999999 / 1000000000);
println(999999999 / 1000000000 + 1 / 1000000000);

-- Negative fraction
println(-1 / 3);
println(1 / -3);

-- Complex arithmetic edge cases
println(0i);
println(0.0 + 0i);
println((0.0 + 1i) * (0.0 + 1i));

-- Fraction to float promotion
println(1/3 + 0.5);
println(1/3 * 1.0);

-- Parenthesized negative
println(-(-(-(1))));
println(-(-1));

-- Exponentiation via pow
println(pow(2.0, 0.0));
println(pow(0.0, 0.0));
println(pow(0.0, 1.0));

-- Large integer multiplication
println(1000000 * 1000000);
println(1000000000 * 1000000000);

-- Division: 0 / 0 as integers (produces fraction?)
println(0 / 1);
