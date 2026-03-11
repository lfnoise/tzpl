-- Array and Tuple Arithmetic from Tzopilotl by Example

-- Element-wise operations
let a = [1, 2, 3, 4, 5];
let b = [10, 20, 30, 40, 50];
println(a + b);
println(a * b);
println(b - a);
println(-a);

-- Scalar broadcasting
let c = [100, 200, 300];
println(c + 1);
println(c * 2);
println(10 * c);

-- Tuple arithmetic
let p1 = (1.0, 2.0);
let p2 = (3.0, 4.0);
println(p1 + p2);
println((p1 + p2) * 0.5);
println(p2 - p1);

-- Mixed numeric types in tuples
let m1 = (1, 2.5);
let m2 = (3, 1.5);
println(m1 + m2);

-- Tuple-Array broadcasting
let tup = (10, 20);
let arr = [1, 2, 3];
println(tup + arr);
println(tup * arr);
println(arr + tup);
