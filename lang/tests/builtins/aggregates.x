-- sum / product / mean / any / all / contains over arrays and lists,
-- plus integer pow.

-- sum
[1, 2, 3, 4] sum println;
[1.5, 2.5, 3.0] sum println;
(1..100) toList sum println;
(1..4) toList map(fn(x Int) Float { x toFloat }) sum println;
let se [Int] = [];
se sum println;

-- product
[1, 2, 3, 4] product println;
[0.5, 4.0] product println;
(1..5) toList product println;
se product println;

-- mean
[1, 2, 3, 4] mean println;
[1.0, 2.0, 6.0] mean println;
(1..9) toList mean println;

-- any / all with a predicate
[1, 2, 3] any(fn(x Int) { x > 2 }) println;
[1, 2, 3] any(fn(x Int) { x > 5 }) println;
[1, 2, 3] all(fn(x Int) { x > 0 }) println;
[1, 2, 3] all(fn(x Int) { x > 2 }) println;
se any(fn(x Int) { true }) println;
se all(fn(x Int) { false }) println;

-- any / all short-circuit on an infinite lazy list
(1..) toList any(fn(x Int) { x > 5 }) println;
(1..) toList all(fn(x Int) { x < 5 }) println;

-- any / all directly over Bool collections
[true, false, false] any println;
[true, false, false] all println;
[true, true] all println;
List(false, true) any println;
List(true, true) all println;

-- contains for arrays and lists
[1, 2, 3] contains(2) println;
[1, 2, 3] contains(9) println;
[1.5, 2.5] contains(2.5) println;
["ab", "cd"] contains("cd") println;
["ab", "cd"] contains("zz") println;
List(1, 2, 3) contains(3) println;
List((1, 2), (3, 4)) contains((3, 4)) println;
List((1, 2), (3, 4)) contains((3, 5)) println;

-- integer pow
pow(2, 10) println;
pow(10, 0) println;
pow(-3, 3) println;
pow(2, -1) println;
pow(1, -5) println;
pow(-1, -3) println;
2 pow(62) println;
