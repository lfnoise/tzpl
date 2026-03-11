-- General Numeric Functions from Builtin_Functions.html

-- abs
println(-5 abs);
println(-3.14 abs);
println((3.0 + 4i) abs);

-- clamp
println(15 clamp(0, 10));
println(-5 clamp(0, 10));
println(5 clamp(0, 10));

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
