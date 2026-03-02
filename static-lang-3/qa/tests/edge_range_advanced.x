-- QA: Advanced range edge cases

-- Fraction ranges
(1/4, 1/2..2/1) toArray println;

-- Range with map
(1..5) toArray map(fn(x Int) Int { x * x }) println;

-- Range with filter
(1..10) toArray filter(fn(x Int) Bool { x % 2 == 0 }) println;

-- Range with fold
(1..10) toArray fold(0, fn(a Int, b Int) Int { a + b }) println;

-- Range in nested for loops
var sum = 0;
for (i : (1..3)) {
    for (j : (1..3)) {
        sum = sum + i * j;
    }
}
sum println;   -- (1+2+3)*(1+2+3) = 36

-- Range toList then toArray then reverse
(1..5) toArray reverse println;

-- Stepped range edge cases
(0,3..12) toArray println;        -- [0, 3, 6, 9, 12]
(1,3..10) toArray println;        -- [1, 3, 5, 7, 9]

-- Large range (just length, don't print all)
(1..1000) length println;         -- 1000

-- Range as value
let r = (1..10);
r length println;
r toArray println;

-- Infinite range operations
(0..) toList take(3) println;
(10..) toList take(3) println;
(0..) toList drop(5) take(3) println;

-- Negative range
(-5..5) toArray println;
(-5..-1) toArray println;
