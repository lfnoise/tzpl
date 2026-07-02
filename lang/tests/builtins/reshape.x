-- clump / spread / ncyc / toSet / fromCodePoints

-- clump: group into fixed-size rows, short remainder kept
(1..10) toArray clump(3) println;
(1..6) toArray clump(2) println;
[1, 2] clump(5) println;
[1, 2] clump(0) println;
["a", "b", "c"] clump(2) println;

-- spread: replicate element i counts[i] times (counts index cyclically)
[10, 20, 30] spread([1, 0, 2]) println;
[7, 8] spread([3]) println;
[1, 2, 3, 4] spread([2, 0]) println;
let e [Int] = [];
e spread([2]) println;

-- ncyc on arrays
[1, 2, 3] ncyc(3) println;
[1.5] ncyc(2) println;
["x", "y"] ncyc(2) println;
[1, 2] ncyc(0) println;

-- toSet (print via length/contains; element order is unspecified)
[1, 2, 2, 3, 1] toSet length println;
[1, 2, 3] toSet contains(2) println;
["a", "b", "a"] toSet length println;
List((1, 2), (1, 2), (3, 4)) toSet length println;
(1..5) toList toSet contains(5) println;

-- fromCodePoints: inverse of codePoints
[72, 105, 33] fromCodePoints println;
"héllo😀" codePoints fromCodePoints println;
"abc" codePoints fromCodePoints println;
e fromCodePoints length println;
