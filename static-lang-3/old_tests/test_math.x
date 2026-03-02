-- Array and tuple arithmetic tests

-- Array arithmetic
let a = [1, 2, 3, 4, 5];
let b = [10, 20, 30, 40, 50];

-- Array + Array
let sum = a + b;
println(sum);

-- Array - Array
let diff = b - a;
println(diff);

-- Array * Array (element-wise)
let prod = a * b;
println(prod);

-- Array + scalar (broadcast)
let shifted = a + 100;
println(shifted);

-- Scalar + Array (broadcast)
let shifted2 = 1000 + a;
println(shifted2);

-- Array * scalar
let scaled = a * 3;
println(scaled);

-- Scalar * Array
let scaled2 = 10 * b;
println(scaled2);

-- Negation of array
let neg = -a;
println(neg);

-- Tuple arithmetic
let p = (3, 4);
let q = (10, 20);

-- Tuple + Tuple
let tsum = p + q;
println(tsum);

-- Tuple * Tuple
let tprod = p * q;
println(tprod);

-- Tuple - Tuple
let tdiff = q - p;
println(tdiff);

-- Tuple + scalar (broadcast)
let tshift = p + 100;
println(tshift);

-- Negation of tuple
let tneg = -p;
println(tneg);

-- Mixed-type tuple
let m = (1, 2.5, 3);
let n = (10, 0.5, 7);
let mixed = m + n;
println(mixed);

-- Float array
let fa = [1.5, 2.5, 3.5];
let fb = [0.5, 0.5, 0.5];
let fsum = fa + fb;
println(fsum);

let fdiff = fa - fb;
println(fdiff);

let fprod = fa * fb;
println(fprod);

println(1<<4);
println(10 ^ 8);
