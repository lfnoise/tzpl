-- Float math builtins

-- sqrt
4.0 sqrt println;
9.0 sqrt println;
2.0 sqrt println;
0.0 sqrt println;
1.0 sqrt println;

-- abs
-5.0 abs println;
5.0 abs println;
0.0 abs println;

-- floor, ceil, round
3.7 floor println;
3.2 floor println;
-1.5 floor println;
3.2 ceil println;
3.7 ceil println;
-1.5 ceil println;
3.5 round println;
3.4 round println;
-1.5 round println;

-- min, max
println(min(3.0, 7.0));
println(max(3.0, 7.0));
println(min(-1.0, 1.0));
println(max(-1.0, 1.0));

-- pow
println(pow(2.0, 10.0));
println(pow(3.0, 3.0));
println(pow(10.0, 0.0));

-- log, exp
0.0 exp println;
1.0 exp println;
1.0 log println;

-- Int abs
-42 abs println;
42 abs println;
0 abs println;
