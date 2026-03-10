-- QA: Collection functions

-- toString
println(toString(42));
println(toString(3.14));
println(toString(true));
println(toString(false));
println(toString("hello"));
println(toString([1, 2, 3]));

-- collect (List -> Array with count)
println((1..) toList collect(5));

-- Array flatten
println([[1, 2], [3, 4], [5]] flatten);

-- String contains
println("hello world" contains("world"));  -- true
println("hello" contains("xyz"));          -- false

-- Set/Map contains (not arrays)
println(Set(1, 2, 3) contains(2));   -- true
println(Set(1, 2, 3) contains(5));   -- false
println(["a": 1, "b": 2] contains("a"));  -- true
println(["a": 1, "b": 2] contains("c"));  -- false

-- grade (requires comparator function)
grade([30, 10, 20], fn(a Int, b Int) Bool { a < b }) println;
grade([1, 2, 3], fn(a Int, b Int) Bool { a < b }) println;
grade([3, 2, 1], fn(a Int, b Int) Bool { a < b }) println;

-- find (returns Option)
[1, 2, 3, 4, 5] find(fn(x Int) Bool { x > 3 }) println;
[1, 2, 3] find(fn(x Int) Bool { x > 10 }) println;

-- scan1
[1, 2, 3, 4, 5] scan1(fn(a Int, b Int) Int { a + b }) println;

-- fold1
[1, 2, 3, 4, 5] fold1(fn(a Int, b Int) Int { a + b }) println;

-- enumerate
[10, 20, 30] enumerate println;

-- iter (infinite list from seed + function)
let powers = iter(1, fn(x Int) Int { x * 2 });
powers take(6) println;

-- push / pop
let arr = [1, 2, 3];
println(arr push(4));
println(arr pop);

-- cat (concatenate arrays)
println(cat([1, 2], [3, 4]));

-- join (flatten nested arrays, same as flatten)
println([["a", "b"], ["c", "d"]] join);

-- ncyc
ncyc(List(1, 2), 3) take(6) println;

-- stutter
List(1, 2, 3) stutter(2) take(6) println;

-- stride
List(1, 2, 3, 4, 5, 6) stride(2) take(3) println;
