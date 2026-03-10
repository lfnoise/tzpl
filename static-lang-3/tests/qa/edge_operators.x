-- QA: Edge cases in operators

-- Bitwise operators
println(0xFF & 0x0F);    -- 15
println(0xFF | 0x100);   -- 511
println(0xFF ^ 0xFF);    -- 0
println(1 << 10);        -- 1024
println(1024 >> 10);     -- 1
println(-1 >>> 63);      -- 1 (logical right shift)

-- Bitwise on zero
println(0 & 0);
println(0 | 0);
println(0 ^ 0);
println(0 << 1);
println(0 >> 1);

-- Shift edge cases
println(1 << 0);   -- 1
println(1 << 63);  -- MIN_INT or large positive?

-- Comparison operators with same values
println(1 == 1);
println(1 != 1);
println(1 < 1);
println(1 <= 1);
println(1 > 1);
println(1 >= 1);

-- Float comparison edge cases
println(0.1 + 0.2 == 0.3);  -- likely false due to floating point
println(0.1 + 0.2);

-- String concatenation
println("hello" $ " " $ "world");
println("" $ "abc");
println("abc" $ "");

-- Operator precedence
println(2 + 3 * 4);     -- 14, not 20
println((2 + 3) * 4);   -- 20
println(10 - 3 - 2);    -- 5 (left-to-right)
println(10 - (3 - 2));  -- 9

-- Logical operator precedence
println(true || false && false);   -- true (AND binds tighter)
println((true || false) && false); -- false

-- Comparison chaining
println(1 < 2 && 2 < 3);

-- Unary negation precedence
println(-2 * 3);    -- -6
println(-(2 * 3));  -- -6

-- Boolean negation
println(!true);
println(!false);
println(!!true);
println(!!!true);

-- Equality of different numeric types
-- println(1 == 1.0);  -- Does this work? Int vs Float

-- Array element-wise comparison
println([1, 2, 3] == [1, 2, 3]);
println([1, 2, 3] == [1, 2, 4]);
println([1, 2, 3] != [1, 2, 4]);

-- Tuple comparison
println((1, 2) == (1, 2));
println((1, 2) == (1, 3));
println((1, 2) != (1, 3));
