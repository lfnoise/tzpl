-- Ternary operator

-- Basic
println(true ? 1 : 2);
println(false ? 1 : 2);

-- With expressions
println(1 < 2 ? 10 : 20);
println(1 > 2 ? 10 : 20);

-- Nested ternary
println(true ? true ? 1 : 2 : 3);
println(true ? false ? 1 : 2 : 3);
println(false ? 1 : true ? 2 : 3);
println(false ? 1 : false ? 2 : 3);

-- With variables
let x = 10;
println(x > 5 ? "big" : "small");
println(x < 5 ? "big" : "small");

-- In function
fn abs_val(x Int) Int = x < 0 ? -x : x;
-7 abs_val println;
7 abs_val println;
0 abs_val println;

-- String result
fn classify(n Int) String = n > 0 ? "positive" : n < 0 ? "negative" : "zero";
5 classify println;
-3 classify println;
0 classify println;
