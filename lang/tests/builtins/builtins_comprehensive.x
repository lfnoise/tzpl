-- Test built-in functions for arrays and lists

--- Array: reverse ---
let a = [1, 2, 3, 4, 5];
println(reverse(a));
println(a reverse);

"@ reverse" println;
[[1,2,3],[4,5,6],[7,8,9]] @ reverse println;


--- Array: push, pop ---
println(push(a, 6));
println(a push(6));
println(pop(a));

--- Array: take, drop ---
println(take(a, 3));
println(a take(3));
println(drop(a, 2));
println(a drop(2));

--- Array: stride, stutter ---
let b = [1, 2, 3, 4, 5, 6, 7];
println(stride(b, 2));
println(b stutter(2));

--- Array: cat ---
println(cat([1, 2, 3], [4, 5, 6]));

--- Array: map, filter ---
println(map(a, fn(x Int) { x * x }));
println(a map(fn(x Int) { x * 2 }));
println(filter(a, fn(x Int) { x > 2 }));

--- Array: fold, scan ---
println(fold(a, 0, fn(acc Int, x Int) { acc + x }));
println(scan(a, 0, fn(acc Int, x Int) { acc + x }));

--- Array: fold1, scan1 ---
println(fold1(a, fn(acc Int, x Int) { acc + x }));
println(scan1(a, fn(acc Int, x Int) { acc + x }));

--- Array: find ---
println(find(a, fn(x Int) { x > 3 }));

--- Array: takeWhile, dropWhile ---
println(takeWhile(a, fn(x Int) { x < 4 }));
println(dropWhile(a, fn(x Int) { x < 4 }));

--- Array: zip ---
println(zip([1, 2, 3], [10, 20, 30]));

--- Array: enumerate ---
println(enumerate(a));

--- Array: sort ---
println(sort([3, 1, 4, 1, 5, 9, 2, 6]));

--- Array: muss (scramble) ---
let sorted = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
let scrambled = muss(sorted);
println(length(scrambled));
println(sort(scrambled));

--- Array: join (one level) ---
println(join([[1, 2], [3, 4], [5]]));

--- Array: flatten (all levels) ---
println(flatten([[1, 2], [3, 4], [5]]));
println(flatten([[[1, 2], [3]], [[4, 5, 6]]]));

--- List: take, drop ---
let xs = List(1, 2, 3, 4, 5);
println(take(xs, 3));
println(drop(xs, 2));

--- List: stride, stutter ---
println(stride(xs, 2));
println(stutter(xs, 2));

--- List: cat ---
println(cat(List(1, 2, 3), List(4, 5, 6)));

--- List: map, filter ---
println(map(xs, fn(x Int) { x * x }));
println(filter(xs, fn(x Int) { x > 2 }));

--- List: fold, scan ---
println(fold(xs, 0, fn(acc Int, x Int) { acc + x }));
println(scan(xs, 0, fn(acc Int, x Int) { acc + x }));

--- List: fold1, scan1 ---
println(fold1(xs, fn(acc Int, x Int) { acc + x }));
println(scan1(xs, fn(acc Int, x Int) { acc + x }));

--- List: find ---
println(find(xs, fn(x Int) { x > 3 }));

--- List: takeWhile, dropWhile ---
println(takeWhile(xs, fn(x Int) { x < 4 }));
println(dropWhile(xs, fn(x Int) { x < 4 }));

--- List: zip ---
println(zip(List(1, 2, 3), List(10, 20, 30)));

--- List: enumerate ---
println(enumerate(xs));

--- List: join (one level) ---
println(join(List(List(1, 2), List(3, 4))));

--- List: flatten (all levels) ---
println(flatten(List(List(1, 2), List(3, 4))));
println(flatten(List(List(List(1, 2), List(3)), List(List(4, 5)))));

--- List: cyc (infinite cycle, take first 10) ---
let short = List(1, 2, 3);
println(take(cyc(short), 10));

--- List: ncyc ---
println(ncyc(short, 3));

--- List: hang ---
println(take(hang(short), 8));

--- List: $ (concat) ---
println(List(1, 2, 3) $ List(4, 5, 6));
println(List(1, 2) $ List(3, 4) $ List(5, 6));
let empty = List(1, 2, 3) drop(3);
println(empty $ List(7, 8));
println(List(7, 8) $ empty);

--- Array: length ---
println(length([1, 2, 3, 4, 5]));
println([10, 20, 30] length);

--- List: length ---
println(length(List(1, 2, 3, 4, 5)));
println(length(List(42)));
println(List(1, 2, 3) length);
