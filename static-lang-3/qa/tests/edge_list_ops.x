-- QA: Edge cases in List operations

-- isNil / notNil
let xs = List(1, 2, 3);
let empty List<Int> = nil;

isNil(empty) println;
isNil(xs) println;
notNil(xs) println;
notNil(empty) println;

-- head / tail of single element list
let single = List(42);
head(single) println;
tail(single) println;  -- should be nil

-- Cons builds correctly
let built = 1 :: 2 :: 3 :: nil;
built println;
head(built) println;
tail(built) println;

-- List length
xs length println;
single length println;
empty length println;

-- List map
List(1, 2, 3) map(fn(x Int) { x * 10 }) println;

-- List filter
List(1, 2, 3, 4, 5) filter(fn(x Int) { x % 2 == 0 }) println;

-- List fold
List(1, 2, 3, 4, 5) fold(0, fn(acc Int, x Int) { acc + x }) println;

-- List scan
List(1, 2, 3, 4, 5) scan(0, fn(acc Int, x Int) { acc + x }) println;

-- List take
List(1, 2, 3, 4, 5) take(3) println;
List(1, 2, 3) take(0) println;
List(1, 2, 3) take(10) println;

-- List drop
List(1, 2, 3, 4, 5) drop(3) println;
List(1, 2, 3) drop(0) println;
List(1, 2, 3) drop(10) println;

-- List takeWhile
List(1, 2, 3, 4, 5) takeWhile(fn(x Int) { x < 4 }) println;
List(1, 2, 3) takeWhile(fn(x Int) { false }) println;
List(1, 2, 3) takeWhile(fn(x Int) { true }) println;

-- List dropWhile
List(1, 2, 3, 4, 5) dropWhile(fn(x Int) { x < 4 }) println;
List(1, 2, 3) dropWhile(fn(x Int) { false }) println;
List(1, 2, 3) dropWhile(fn(x Int) { true }) println;

-- List concatenation using $
let l1 = List(1, 2);
let l2 = List(3, 4);
println(l1 $ l2);

-- List zip
zip(List(1, 2, 3), List(10, 20, 30)) println;
zip(List(1, 2), List(10, 20, 30)) println;  -- mismatched lengths

-- List enumerate
List(10, 20, 30) enumerate println;

-- List from range (lazy)
(1..5) toList println;

-- Infinite list operations
let nats = (1..) toList;
nats take(5) println;

-- List find
List(1, 2, 3, 4, 5) find(fn(x Int) { x > 3 }) println;
List(1, 2, 3) find(fn(x Int) { x > 10 }) println;

-- List cycle (cyc)
let cyc_list = cyc(List(1, 2, 3));
cyc_list take(7) println;

-- List stutter
List(1, 2, 3) stutter(2) println;

-- List stride
List(1, 2, 3, 4, 5, 6) stride(2) println;

-- List: fold1 / scan1
List(1, 2, 3, 4, 5) fold1(fn(a Int, b Int) { a + b }) println;
List(1, 2, 3, 4, 5) scan1(fn(a Int, b Int) { a + b }) println;

-- List: cat
println(cat(List(1, 2), List(3, 4)));
