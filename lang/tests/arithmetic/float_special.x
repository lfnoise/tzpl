-- Float special values (IEEE 754)

-- Infinity
println(1.0 / 0.0);
println(-1.0 / 0.0);

-- Infinity arithmetic
let inf = 1.0 / 0.0;
println(inf + 1.0);
println(inf * 2.0);
println(inf + inf);
println(-inf + -inf);

-- NaN
println(0.0 / 0.0);

-- NaN propagation
let nan = 0.0 / 0.0;
println(nan + 1.0);
println(nan * 2.0);
println(nan - nan);

-- NaN comparison (NaN != NaN per IEEE)
println(nan == nan);
println(nan != nan);

-- Predicates
nan isNan println;
1.0 isNan println;
inf isNan println;
inf isInf println;
-inf isInf println;
1.0 isInf println;
nan isInf println;
