-- min/max reductions, running reductions (sums/products/mins/maxs),
-- and pow(Fraction, Int).

-- min / max of arrays
[3, 1, 4, 1, 5] min println;
[3, 1, 4, 1, 5] max println;
[3.5, -1.25, 9.0] min println;
[3.5, -1.25, 9.0] max println;
["banana", "apple", "cherry"] min println;
["banana", "apple", "cherry"] max println;

-- min / max of lists (lazy sources force as needed)
List(7, 2, 9) min println;
List(7, 2, 9) max println;
(1..100) toList max println;
List("b", "a") min println;

-- the two-arg scalar forms still work alongside the reductions
min(3, 7) println;
max(2.5, 1.5) println;

-- empty collections yield the reduction identity
let e [Int] = [];
e min println;
e max println;
let ef [Float] = [];
ef min println;
ef max println;

-- running sums / products / mins / maxs on arrays
[1, 2, 3, 4] sums println;
[1, 2, 3, 4] products println;
[3, 1, 4, 1, 5] mins println;
[3, 1, 4, 1, 5] maxs println;
[1.5, 2.5, -1.0] sums println;
[2.0, 3.0] products println;
e sums println;

-- running reductions on lists are lazy: works on an infinite list
(1..) toList sums take(10) println;
(1..6) toList products collect(6) println;
List(3, 1, 4, 1, 5) mins collect(5) println;
List(3.0, 1.0, 4.0) maxs collect(3) println;

-- Fraction sum/product/min/max, eager and lazy
[1/2, 1/3, 1/6] sum println;
[1/2, 2/3, 3/4] product println;
[3/4, 1/2, 5/6] min println;
[3/4, 1/2, 5/6] max println;
List(1/2, 1/3, 1/6) sum println;
List(3/4, 1/2) min println;
List(3/4, 1/2) max println;

-- Fraction running reductions
[1/2, 1/3, 1/6] sums println;
[1/2, 2/3, 3/4] products println;
[3/4, 1/2, 5/6] mins println;
[1/2, 3/4, 1/3] maxs println;
List(1/2, 1/3, 1/6) sums collect(3) println;
List(3/4, 1/2, 5/6) mins collect(3) println;

-- Complex sum/product, eager and lazy, plus running forms
[Complex(1.0, 2.0), Complex(3.0, -1.0)] sum println;
[Complex(0.0, 1.0), Complex(0.0, 1.0)] product println;
List(Complex(1.0, 1.0), Complex(2.0, 2.0)) sum println;
List(Complex(0.0, 1.0), Complex(0.0, 1.0), Complex(0.0, 1.0)) product println;
[Complex(1.0, 0.0), Complex(0.0, 1.0), Complex(1.0, 1.0)] sums println;
[Complex(0.0, 1.0), Complex(0.0, 1.0)] products println;
List(Complex(1.0, 0.0), Complex(0.0, 1.0)) sums collect(2) println;

-- pow(Fraction, Int), including negative exponents
pow(3/4, 2) println;
pow(3/4, -2) println;
pow(2/3, 0) println;
pow(-2/3, 3) println;
pow(-2/3, -3) println;
pow(5/1, -1) println;
(1/2) pow(10) println;
