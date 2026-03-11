-- Trigonometric builtins

-- sin, cos, tan at 0
0.0 sin println;
0.0 cos println;
0.0 tan println;

-- sin, cos at pi/2
let pi = 3.14159265358979323846;
println(round(sin(pi / 2.0) * 1000.0) / 1000.0);
println(round(cos(pi / 2.0) * 1000000.0));

-- atan2
println(atan2(1.0, 1.0));
println(atan2(0.0, 1.0));
println(atan2(1.0, 0.0));

-- asin, acos
0.0 asin println;
1.0 acos println;

-- Hyperbolic
0.0 sinh println;
0.0 cosh println;
0.0 tanh println;
