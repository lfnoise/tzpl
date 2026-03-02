-- Operator precedence: unary > multiplicative > additive > shift > comparison > logical > ternary > pipeline

-- Unary > multiplicative
println(-2 * 3);
println(-(2 * 3));

-- Multiplicative > additive
println(2 + 3 * 4);
println(2 * 3 + 4);
println(10 - 2 * 3);

-- Shift operators
println(1 << 3 + 1);
println((1 << 3) + 1);

-- Comparison > logical
println(1 < 2 && 3 > 0);
println(1 < 2 || 3 < 0);

-- Logical AND > logical OR
println(true || false && false);
println((true || false) && false);

-- Ternary binds loosely
println(true ? 1 : 2 + 3);
println(false ? 1 : 2 + 3);

-- Nested ternary
println(true ? false ? 1 : 2 : 3);
println(false ? 1 : true ? 2 : 3);

-- Equality and comparison
println(1 + 1 == 2);
println(2 * 3 > 5);
println(2 * 3 >= 6);

-- Parenthesized override
println((1 + 2) * 3);
println(2 * (3 + 4));
println((1 + 2) * (3 + 4));
