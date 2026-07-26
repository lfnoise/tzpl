-- Operator-as-function sugar: an operator token directly followed by ','
-- or ')' in a call-argument position is the operator as a function value,
-- desugared to the equivalent untyped lambda (fold(0, +) is
-- fold(0, fn(a, b) { a + b })).

-- arithmetic reducers
[1, 2, 3, 4] fold(0, +) println;
[1, 2, 3, 4] fold(1, *) println;
[1, 2, 3, 4] fold1(-) println;
[8, 4, 2] fold1(//) println;
[8.0, 4.0, 2.0] fold1(/) println;
[1/2, 1/4] fold1(/) println;
[20, 7] fold1(%) println;
(1..5) toList fold(0, +) println;
(1..5) toList fold1(*) println;

-- string concat
["a", "b", "c"] fold1($) println;

-- comparisons as sort/grade comparators
[3, 1, 2] sort(<) println;
[3, 1, 2] sort(>) println;
[30, 10, 20] grade(<) println;
[1.5, 0.5, 2.5] sort(>) println;
["b", "c", "a"] sort(<) println;

-- logical and bitwise
[true, true, false] fold1(&&) println;
[false, false, true] fold1(||) println;
[12, 10] fold1(&) println;
[12, 10] fold1(|) println;
[12, 10] fold1(^) println;

-- scans
[1, 2, 3, 4] scan(0, +) println;
[1, 2, 3, 4] scan1(+) println;

-- unary-only operators
[true, false, true] map(!) println;
[1, 2, 3] map(~) println;

-- first or middle argument position, not just last
zip([1, 2], [10, 20]) println;
fold([1, 2, 3], 0, +) println;

-- ordinary prefix expressions in argument position are unaffected
max(1, -2) println;
let x = 7;
min(3, -x) println;
min(3, -x + 1) println;

-- the assignment arrows are overloadable functions, so they pass too
fn <-(a Int, b Int) Int { a * 10 + b }
fn ->(a Int, b Int) Int { a + b * 10 }
[1, 2, 3] fold(0, <-) println;
[1, 2, 3] fold(0, ->) println;
[1, 2, 3, 4] fold1(<-) println;
