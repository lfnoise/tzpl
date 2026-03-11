-- Integer edge cases

-- Zero operations
println(0 + 0);
println(0 * 0);
println(0 * 1000000);
println(1000000 * 0);
println(0 // 1);
println(0 % 1);
println(0 % 7);

-- Identity operations
println(42 + 0);
println(0 + 42);
println(42 * 1);
println(1 * 42);
println(42 // 1);

-- Double negation
println(-(-1));
println(-(-100));

-- Triple negation
println(-(-(-1)));

-- Large values
println(1000000 * 1000);
println(1000000000 + 1);
