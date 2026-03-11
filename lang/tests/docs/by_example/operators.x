-- Operators from Tzopilotl by Example

-- Arithmetic
println(3 + 4);
println(10 - 3);
println(6 * 7);
println(1 / 3);
println(10 // 3);
println(10 % 3);
println(-42);

-- Comparison
println(3 == 3);
println(3 != 4);
println(3 < 5);
println(3 <= 3);
println(5 > 3);
println(5 >= 5);

-- Comparison on strings (lexicographic)
println("apple" < "banana");
println("abc" >= "abc");

-- Structural equality
println((1, 2) == (1, 2));
println([1, 2, 3] == [1, 2, 3]);
println('foo == 'foo);

-- Logical
println(true && true);
println(true && false);
println(true || false);
println(false || false);
println(!true);
println(!false);

-- Bitwise
println(255 & 15);
println(240 | 15);
println(255 ^ 15);
println(~0);
println(1 << 10);
println(1024 >> 5);
println((-1) >>> 1);

-- Concatenation ($)
println("Hello" $ ", " $ "World");
println([1, 2] $ [3, 4]);
println(List(1, 2) $ List(3, 4));
println((1, 2) $ (3, 4));
