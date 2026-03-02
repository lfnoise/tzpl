-- Range builtin functions

-- toArray
(1..5) toArray println;
(5..1) toArray println;
(0,2..10) toArray println;

-- toList
(1..5) toList println;
(5..1) toList println;

-- length
(1..5) length println;
(1..10) length println;
(5..5) length println;

-- Fraction ranges
(1/2..5/2) toArray println;
(1/2..5/2) length println;

-- Stepped fraction range
(1/4, 1/2..2/1) toArray println;

-- Infinite range (lazy)
(1..) toList println;
