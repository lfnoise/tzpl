-- Test program for Language X

--- Simple functions ---
fn square(x Int) Int = x * x;

fn max(a Int, b Int) Int = a > b ? a : b;

fn abs(x Int) Int = x < 0 ? -x : x;

--- Basic arithmetic ---
let a = 10;
let b = 20;
let c = a + b;
println(c);

--- Function calls ---
let sq = square(7);
println(sq);

let m = max(42, 17);
println(m);

let neg = abs(-99);
println(neg);

--- While loop with float ---
var sum = 0.0;
var i = 1;
while (i <= 100) {
    sum = sum + 1.0 / i;
    i = i + 1;
}
println(sum);

--- Boolean ---
let flag = true;
if (flag) {
    println(1);
} else {
    println(0);
}

--- Strings ---
let greeting = "Hello";
let name = "World";
let message = greeting $ ", " $ name $ "!";
println(message);

-- String equality
if (greeting == "Hello") {
    println("equal");
} else {
    println("not equal");
}

if (greeting != name) {
    println("different");
} else {
    println("same");
}

-- String in function
fn greet(who String) String = "Hi, " $ who $ "!";

let g = greet("Alice");
println(g);

-- String variable
var s = "first";
println(s);
s = "second";
println(s);

"a" < "b" |> println;

(1/2..5/2) toArray println;
(0/1,1/3..4/1) toArray println;
(0,1/3..4) toArray println;

fn bop() List<Int> = List(1, 2, 3);

bop() println;

"123" $ "456" |> println;
[1,2,3] $ [4,5,6] |> println;
(1,2,3) $ (4,5,6) |> println;
List(1,2,3) $ List(4,5,6) |> println;



-- List([1,2,3],[4,5,6]) join println;

let a = [1, 2, 3, 4, 5];
println(fold1(a, fn(acc Int, x Int) { acc + x }));


--fn f(a List<[Int]>, b List<[Int]>)[Int] {a $ b}

let f = fn (a, b) {a $ b};
let l = List([1,2,3],[4,5,6],[7,8,9]);
fold1(l, f) println;

fn myJoin<T>(x [T]) = x fold1(fn(a T, b T) T {a $ b});
[[1,2,3],[4,5,6],[7,8,9]] myJoin println;

let r = &0;
let s = r <- 1;
r println;
*r println;
s println;

let a = [&1, &2, &3];
a[1] <- 20;
a println;
