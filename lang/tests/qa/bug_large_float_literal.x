-- QA: Large float literal (previously crashed parser, now fixed)

-- Normal large floats
println(1.0e100);
println(1.0e200);
println(1.0e307);
println(1.0e308);

-- Previously crashed, now produces inf:
println(1.0e309);
println(1.0e1000);
println(-1.0e309);

-- Runtime overflow also produces inf
let big = 1.0e100 * 1.0e100 * 1.0e100 * 1.0e8;
println(big);

println(1.0 / 0.0);
println(-1.0 / 0.0);
