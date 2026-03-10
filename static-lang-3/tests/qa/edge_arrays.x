-- QA: Edge cases in array operations

-- Empty array
let empty [Int] = [];
empty println;
empty length println;
empty reverse println;

-- Cyclic indexing (documented feature)
let a = [10, 20, 30];
a[0] println;
a[3] println;   -- should wrap to 10
a[-1] println;  -- should be 30
a[-2] println;  -- should be 20
a[-3] println;  -- should be 10
a[6] println;   -- should wrap to 10
a[-4] println;  -- should wrap to 20

-- Single element array operations
let single = [42];
single reverse println;
single take(0) println;
single take(1) println;
single drop(0) println;
single drop(1) println;

-- Array sort - already sorted
[1, 2, 3, 4, 5] sort println;

-- Array sort - reverse sorted
[5, 4, 3, 2, 1] sort println;

-- Array sort - all same
[3, 3, 3, 3] sort println;

-- Array sort - single element
[42] sort println;

-- Array sort - two elements
[5, 3] sort println;

-- Nested array operations
let nested = [[1, 2], [3, 4], [5, 6]];
nested length println;
nested[0] println;
nested[0][0] println;
nested[2][1] println;

-- flatten on deeply nested
[[[1], [2, 3]], [[4, 5], [6]]] flatten println;

-- join on nested arrays
[[1, 2], [3, 4], [5]] join println;

-- push on empty array
let e [Int] = [];
e push(1) println;

-- pop on single element array
[42] pop println;

-- stride on array smaller than stride
[1, 2] stride(5) println;

-- stutter 1
[1, 2, 3] stutter(1) println;

-- Array of booleans
let bools = [true, false, true, false];
bools reverse println;
bools length println;

-- Concatenation edge cases
let empty2 [Int] = [];
println(empty2 $ [1, 2, 3]);
println([1, 2, 3] $ empty2);

-- zip with different lengths
zip([1, 2, 3], [10, 20]) println;
zip([1], [10, 20, 30]) println;

-- enumerate
[10, 20, 30] enumerate println;

-- map with identity
[1, 2, 3] map(fn(x Int) { x }) println;

-- filter all pass
[1, 2, 3] filter(fn(x Int) { true }) println;

-- filter none pass
[1, 2, 3] filter(fn(x Int) { false }) println;

-- fold1 on single element
[42] fold1(fn(a Int, b Int) { a + b }) println;

-- takeWhile on empty condition
[1, 2, 3] takeWhile(fn(x Int) { false }) println;

-- dropWhile all true
[1, 2, 3] dropWhile(fn(x Int) { true }) println;

-- find - not found
[1, 2, 3] find(fn(x Int) { x > 10 }) println;

-- Chained operations
[5, 3, 1, 4, 2] sort reverse take(3) println;

-- map then filter
[1, 2, 3, 4, 5] map(fn(x Int) { x * x }) filter(fn(x Int) { x > 10 }) println;

-- Array of arrays - flatten then sort
[[3, 1], [2, 4]] flatten sort println;

-- Negative indexing patterns
let arr5 = [10, 20, 30, 40, 50];
arr5[-1] println;  -- 50
arr5[-5] println;  -- 10

-- Array with single element repeated operations
[1] push(2) push(3) println;
