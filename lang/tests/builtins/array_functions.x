-- Array builtin functions

let a = [1, 2, 3, 4, 5];

-- reverse
a reverse println;
println(a reverse);

-- push, pop
a push(6) println;
println(a push(6));
a pop println;

-- take, drop
a take(3) println;
println(a take(3));
a drop(2) println;
println(a drop(2));

-- Edge: take 0, take > length
a take(0) println;
a take(10) println;

-- Edge: drop 0, drop > length
a drop(0) println;
a drop(10) println;

-- stride, stutter
let b = [1, 2, 3, 4, 5, 6, 7];
b stride(2) println;
println(b stutter(2));

-- cat
println(cat([1, 2, 3], [4, 5, 6]));

-- map, filter
a map(fn(x Int) { x * x }) println;
println(a map(fn(x Int) { x * 2 }));
a filter(fn(x Int) { x > 2 }) println;

-- fold, scan
a fold(0, fn(acc Int, x Int) { acc + x }) println;
a scan(0, fn(acc Int, x Int) { acc + x }) println;

-- fold1, scan1
a fold1(fn(acc Int, x Int) { acc + x }) println;
a scan1(fn(acc Int, x Int) { acc + x }) println;

-- find
a find(fn(x Int) { x > 3 }) println;

-- takeWhile, dropWhile
a takeWhile(fn(x Int) { x < 4 }) println;
a dropWhile(fn(x Int) { x < 4 }) println;

-- zip
println(zip([1, 2, 3], [10, 20, 30]));

-- enumerate
a enumerate println;

-- sort
[3, 1, 4, 1, 5, 9, 2, 6] sort println;

-- join (one level)
[[1, 2], [3, 4], [5]] join println;

-- flatten (all levels)
[[1, 2], [3, 4], [5]] flatten println;
[[[1, 2], [3]], [[4, 5, 6]]] flatten println;

-- length
a length println;
println([10, 20, 30] length);

-- $ (concat operator)
println([1, 2, 3] $ [4, 5, 6]);

-- Single element
[42] reverse println;
[42] length println;

-- Empty-ish
a take(0) println;

-- at (cyclic element read, same semantics as a[i])
let ar = [10, 20, 30];
ar at(1) println;
ar at(4) println;
ar at(-1) println;

-- at with an array of indices (gather, mirroring a[[...]])
ar at([2, 0, 0, 1]) println;

-- put! (mutating cyclic element write, same semantics as a[i] = v;
-- returns the array for chaining)
var aw = [1, 2, 3];
aw put!(1, 99);
aw println;
aw put!(3, 10) println;

-- at/put! on Float and String backends
var fw = [1.5, 2.5];
fw put!(0, fw at(1));
fw println;

var sw = ["x", "y"];
sw put!(-1, "z");
sw at(1) println;
