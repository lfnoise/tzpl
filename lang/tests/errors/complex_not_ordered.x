-- Complex numbers have no total order: <, <=, >, >= must be rejected
-- at type-check time, including when wrapped in Array/List/Tuple.
let c1 = 1.0 + 2i;
let c2 = 3.0 + 4i;
let arr = [1.0 + 2i, 3.0 + 4i];
println(c1 < c2);
println(arr < c1);
println(arr <= c1);
println(arr > c1);
println(arr >= c1);
