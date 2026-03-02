-- QA: find, filter, takeWhile, dropWhile edge cases

-- find on array
println([1, 2, 3, 4, 5] find(fn(x Int) Bool { x > 3 }));    -- some(4)
println([1, 2, 3] find(fn(x Int) Bool { x > 10 }));          -- none
println([Int]() find(fn(x Int) Bool { x > 0 }));              -- none

-- filter on array
println([1, 2, 3, 4, 5] filter(fn(x Int) Bool { x % 2 == 0 }));  -- [2, 4]
println([1, 2, 3] filter(fn(x Int) Bool { x > 10 }));             -- []
println([Int]() filter(fn(x Int) Bool { x > 0 }));                 -- []
println([1, 2, 3] filter(fn(x Int) Bool { true }));                -- [1, 2, 3]

-- filter on list
println(List(1, 2, 3, 4, 5) filter(fn(x Int) Bool { x % 2 == 0 }) take(3));

-- takeWhile on list
println(List(1, 2, 3, 4, 5) takeWhile(fn(x Int) Bool { x < 4 }));
println(List(1, 2, 3) takeWhile(fn(x Int) Bool { true }));
println(List(1, 2, 3) takeWhile(fn(x Int) Bool { false }));

-- dropWhile on list
println(List(1, 2, 3, 4, 5) dropWhile(fn(x Int) Bool { x < 3 }) take(3));
println(List(1, 2, 3) dropWhile(fn(x Int) Bool { false }));
println(List(1, 2, 3) dropWhile(fn(x Int) Bool { true }));

-- Chained filter + map
println([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    filter(fn(x Int) Bool { x % 3 == 0 })
    map(fn(x Int) Int { x * x }));

-- filter on infinite list
(1..) toList filter(fn(x Int) Bool { x % 7 == 0 }) take(5) println;

-- takeWhile on infinite list
(1..) toList takeWhile(fn(x Int) Bool { x <= 5 }) println;

-- find on list
println(List(10, 20, 30, 40) find(fn(x Int) Bool { x > 25 }));
println(List(10, 20) find(fn(x Int) Bool { x > 100 }));
