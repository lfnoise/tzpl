-- Collection Functions from Builtin_Functions.html

-- length
println([1, 2, 3, 4, 5] length);
println(List(1, 2, 3) length);
println([10, 20, 30] length);

-- take
println([1, 2, 3, 4, 5] take(3));
println(List(1, 2, 3, 4, 5) take(3));

-- drop
println([1, 2, 3, 4, 5] drop(2));
println(List(1, 2, 3, 4, 5) drop(2));

-- takeWhile
println([1, 2, 3, 4, 5] takeWhile(fn(x Int) { x < 4 }));

-- dropWhile
println([1, 2, 3, 4, 5] dropWhile(fn(x Int) { x < 4 }));

-- stride
println([1, 2, 3, 4, 5, 6, 7] stride(2));
println([1, 2, 3, 4, 5, 6, 7, 8, 9] stride(3));

-- stutter
println([1, 2, 3] stutter(2));
println([1, 2, 3] stutter(3));

-- map
println([1, 2, 3] map(fn(x Int) { x * x }));
println([1, 2, 3] map(fn(x Int) { x * 2 }));
println(List(1, 2, 3) map(fn(x Int) { x + 10 }));

-- filter
println([1, 2, 3, 4, 5] filter(fn(x Int) { x > 2 }));
println([1, 2, 3, 4, 5] filter(fn(x Int) { x % 2 == 0 }));

-- zip
println(zip([1, 2, 3], [10, 20, 30]));
println(zip(["a", "b"], [1, 2]));

-- enumerate
println(["a", "b", "c"] enumerate);
println([10, 20, 30] enumerate);

-- fold
println([1, 2, 3, 4, 5] fold(0, fn(acc Int, x Int) { acc + x }));
println(["a", "b", "c"] fold("", fn(s String, x String) { s $ x }));

-- scan
println([1, 2, 3, 4, 5] scan(0, fn(a Int, x Int) { a + x }));

-- fold1
println([1, 2, 3, 4, 5] fold1(fn(a Int, b Int) { a + b }));
println([3, 1, 4, 1, 5] fold1(fn(a Int, b Int) { max(a, b) }));

-- scan1
println([1, 2, 3, 4, 5] scan1(fn(a Int, b Int) { a + b }));

-- find
println([10, 20, 30, 40] find(fn(x Int) { x > 25 }));
println([1, 2, 3] find(fn(x Int) { x > 10 }));

-- cat
println(cat([1, 2, 3], [4, 5, 6]));
println(cat(List(1, 2), List(3, 4)));

-- join
println([[1, 2], [3, 4], [5]] join);
println(List(List(1, 2), List(3, 4)) join);

-- flatten
println([[1, 2], [3, 4]] flatten);
println([[[1, 2], [3]], [[4, 5, 6]]] flatten);

-- isEmpty
println([1, 2] isEmpty);
println([Int]() isEmpty);
println(List(1) isEmpty);
println(nil isEmpty);
