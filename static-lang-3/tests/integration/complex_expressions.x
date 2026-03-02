-- Complex expression integration

-- Nested function calls
fn add(a Int, b Int) Int = a + b;
fn mul(a Int, b Int) Int = a * b;
println(add(mul(2, 3), mul(4, 5)));

-- Ternary in function call
fn clamp(x Int, lo Int, hi Int) Int = x < lo ? lo : x > hi ? hi : x;
println(clamp(true ? 7 : 3, 0, 5));
println(clamp(false ? 7 : 3, 0, 5));

-- Array operations chained
let arr = [5, 3, 1, 4, 2];
arr sort println;
arr sort reverse println;
arr sort take(3) println;

-- Pipeline with math
fn square(x Int) Int = x * x;
println(3 square add(4 square));

-- Fold to sum
[1, 2, 3, 4, 5] fold(0, fn(acc Int, x Int) { acc + x }) println;

-- Map then fold (sum of squares)
let sum_sq = [1, 2, 3, 4, 5] map(fn(x Int) { x * x }) fold(0, fn(acc Int, x Int) { acc + x });
sum_sq println;

-- Nested data structures
struct Point { x Int, y Int }
let points = [Point{1, 2}, Point{3, 4}, Point{5, 6}];
points.x println;
points.y println;
points.x fold(0, fn(acc Int, x Int) { acc + x }) println;

-- Complex conditional logic
fn fizzbuzz(n Int) String {
    if (n % 15 == 0) { return "FizzBuzz"; }
    if (n % 3 == 0) { return "Fizz"; }
    if (n % 5 == 0) { return "Buzz"; }
    return "";
}
15 fizzbuzz println;
9 fizzbuzz println;
10 fizzbuzz println;
7 fizzbuzz println;
