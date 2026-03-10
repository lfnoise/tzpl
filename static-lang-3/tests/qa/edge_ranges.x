-- QA: Range edge cases

-- Basic range
(1..5) toArray println;

-- Range in for loop
var sum = 0;
for (i : (1..5)) { sum = sum + i; }
sum println;  -- 15

-- Single element range
(3..3) toArray println;

-- Descending range
(5..1) toArray println;

-- Stepped range
(0,2..10) toArray println;

-- Descending stepped range
(10,8..2) toArray println;

-- Range length
(1..10) length println;   -- 10

-- Single element range length
(5..5) length println;    -- 1

-- Infinite range with take
(1..) toList take(5) println;

-- Infinite range length
(1..) length println;  -- should be -1 or infinity indicator

-- Range toList
(1..5) toList println;

-- Range with negative numbers
(-3..3) toArray println;

-- Large step range
(0,10..50) toArray println;
