-- List builtin functions

let xs = List(1, 2, 3, 4, 5);

-- take, drop
xs take(3) println;
xs drop(2) println;

-- stride, stutter
xs stride(2) println;
xs stutter(2) println;

-- cat
println(cat(List(1, 2, 3), List(4, 5, 6)));

-- map, filter
xs map(fn(x Int) { x * x }) println;
xs filter(fn(x Int) { x > 2 }) println;

-- fold, scan
xs fold(0, fn(acc Int, x Int) { acc + x }) println;
xs scan(0, fn(acc Int, x Int) { acc + x }) println;

-- fold1, scan1
xs fold1(fn(acc Int, x Int) { acc + x }) println;
xs scan1(fn(acc Int, x Int) { acc + x }) println;

-- find
xs find(fn(x Int) { x > 3 }) println;

-- takeWhile, dropWhile
xs takeWhile(fn(x Int) { x < 4 }) println;
xs dropWhile(fn(x Int) { x < 4 }) println;

-- zip
println(zip(List(1, 2, 3), List(10, 20, 30)));

-- enumerate
xs enumerate println;

-- join
List(List(1, 2), List(3, 4)) join println;

-- flatten
List(List(1, 2), List(3, 4)) flatten println;
List(List(List(1, 2), List(3)), List(List(4, 5))) flatten println;

-- iter (infinite repeated application)
iter(1, fn(x Int) Int { x * 2 }) take(10) println;
iter(0, fn(x Int) Int { x + 1 }) take(5) println;
iter("a", fn(s String) String { s $ s }) take(4) println;

-- cyc (infinite cycle, take first 10)
let short = List(1, 2, 3);
short cyc take(10) println;

-- ncyc
short ncyc(3) println;

-- hang
short hang take(8) println;

-- $ (concat)
println(List(1, 2, 3) $ List(4, 5, 6));
println(List(1, 2) $ List(3, 4) $ List(5, 6));

-- length
xs length println;
println(List(42) length);
println(List(1, 2, 3) length);

-- head
xs head println;
List(42) head println;

-- tail
xs tail println;
List(42) tail println;

-- cons
cons(0, xs) println;
cons(99, List(1, 2)) println;
let empty List<Int> = nil;
cons(1, empty) println;

-- isNil / notNil
isNil(xs) println;
isNil(empty) println;
notNil(xs) println;
notNil(empty) println;

-- toList (array to lazy list)
let arr = [10, 20, 30, 40, 50];
arr toList println;
[1, 2, 3] toList take(2) println;
let emptyArr [Int] = [];
emptyArr toList println;

-- collect (list to array, at most n elements)
let ys = List(1, 2, 3, 4, 5);
ys collect(3) println;
ys collect(10) println;
ys collect(0) println;
(1..100) toList collect(5) println;
