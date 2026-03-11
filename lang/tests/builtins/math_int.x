-- Integer math builtins

-- abs
-42 abs println;
42 abs println;
0 abs println;

-- min, max
println(min(3, 7));
println(max(3, 7));
println(min(-5, 5));
println(max(-5, 5));
println(min(0, 0));

-- cmp
println(cmp(1, 2));
println(cmp(2, 1));
println(cmp(3, 3));

-- sign
42 sign println;
-42 sign println;
0 sign println;

-- gcd
println(gcd(12, 8));
println(gcd(7, 13));
println(gcd(0, 5));
println(gcd(-12, 8));

-- lcm
println(lcm(4, 6));
println(lcm(7, 13));
println(lcm(0, 5));
println(lcm(-4, 6));

-- Integer division and modulo
println(10 // 3);
println(10 % 3);
println(-10 // 3);
println(-10 % 3);
println(7 // 1);
println(7 % 7);
