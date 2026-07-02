------------------------------------------------------------------------------
-- Programming examples from Rosetta Code, written in idiomatic Tzopilotl:
-- pipeline syntax, auto-mapping, lazy lists -- no explicit loops.
-- http://rosettacode.org
------------------------------------------------------------------------------

------------------------------------------------------------------------------
-- Helpers used throughout. (An earlier revision also defined sum / product /
-- mean / concat helpers here; those are builtins now -- see the notes at the
-- bottom of this file.)

-- The decimal digits of a non-negative integer.
fn digits(n Int) List<Int> = n toString codePoints - 48;

fn sq(x Int) Int = x * x;

fn isqrt(n Int) Int = n sqrt round toInt;

------------------------------------------------------------------------------
-- 100 doors              http://rosettacode.org/wiki/100_doors

"== 100 doors ==" println;

-- Unoptimized: a door ends open iff its divisor count is odd.
fn doorIsOpen(door Int) Bool =
    (1..100) toArray filter(fn(pass Int) { door % pass == 0 }) length % 2 == 1;
(1..100) toArray filter(doorIsOpen) println;

-- Optimized: exactly the perfect squares.
(1..10) toArray sq println;

------------------------------------------------------------------------------
-- Power set               http://rosettacode.org/wiki/Power_set

"== power set ==" println;

fn powerset(s [Int]) [[Int]] {
    if (s length == 0) {
        let empty [Int] = [];
        return [empty];
    }
    let rest = powerset(s drop(1));
    rest $ rest map(fn(x [Int]) { [s[0]] $ x })
}
powerset([1, 2, 3, 4]) println;

------------------------------------------------------------------------------
-- FizzBuzz                http://rosettacode.org/wiki/FizzBuzz

"== fizzbuzz ==" println;

fn fizzbuzz(n Int) String =
    n % 15 == 0 ? "FizzBuzz" :
    n % 3 == 0 ? "Fizz" :
    n % 5 == 0 ? "Buzz" : n toString;
(1..100) toArray fizzbuzz @ println;

------------------------------------------------------------------------------
-- Factorial               http://rosettacode.org/wiki/Factorial

"== factorial ==" println;

fn factorialRec(n Int) Int = n < 2 ? 1 : n * factorialRec(n - 1);
factorialRec(7) println;

fn factorial(n Int) Int = (1..n) toList product;
factorial(7) println;

------------------------------------------------------------------------------
-- Anonymous recursion     http://rosettacode.org/wiki/Anonymous_recursion
-- The helper is local to fibon, so the recursive worker never escapes
-- into the enclosing namespace.

"== anonymous recursion ==" println;

fn fibon(n Int) Int {
    fn f(k Int) Int = k < 2 ? k : f(k - 1) + f(k - 2);
    if (n < 0) {
        "fibon: out of range" println;
        return 0;
    }
    f(n)
}
fibon(9) println;

------------------------------------------------------------------------------
-- Accumulator factory     http://rosettacode.org/wiki/Accumulator_factory
-- Closures capture by value, so shared mutable state goes through a Ref.

"== accumulator factory ==" println;

fn accumulator(x Float) (Float) Float {
    let r = &x;
    fn(y Float) Float { r <- *r + y; *r }
}
let acc = accumulator(5.0);
acc(3.0) println;
acc(2.5) println;

------------------------------------------------------------------------------
-- Happy numbers           http://rosettacode.org/wiki/Happy_numbers

"== happy numbers ==" println;

fn sumSqDigits(n Int) Int = digits(n) sq sum;
-- Iterating sumSqDigits always falls into 1 (happy) or the cycle through 4.
fn isHappy(n Int) Bool =
    iter(n, sumSqDigits) dropWhile(fn(x Int) { x != 1 && x != 4 }) head == 1;
(1..) toList filter(isHappy) collect(8) println;

------------------------------------------------------------------------------
-- Harshad sequence        http://rosettacode.org/wiki/Harshad_or_Niven_series

"== harshad sequence ==" println;

fn sumDigits(n Int) Int = digits(n) sum;
let harshads = (1..) toList filter(fn(n Int) { n % sumDigits(n) == 0 });
harshads collect(20) println;
harshads dropWhile(fn(n Int) { n <= 1000 }) head println;

------------------------------------------------------------------------------
-- Hailstone sequence      http://rosettacode.org/wiki/Hailstone_sequence

"== hailstone sequence ==" println;

fn hailNext(n Int) Int = n % 2 == 0 ? n // 2 : 3 * n + 1;
fn hail(n Int) List<Int> = iter(n, hailNext) takeWhile(fn(x Int) { x != 1 }) $ List(1);
hail(27) length println;
hail(27) take(4) println;

fn longestHail(limit Int) Void {
    let lens = (1..limit) toArray map(fn(n Int) { hail(n) length });
    let best = lens max;
    "under %^: n = %^ with length %^" fmt(limit, lens find(fn(l Int) { l == best }) + 1, best) println;
}
longestHail(1000);
longestHail(10000);
longestHail(100000);

------------------------------------------------------------------------------
-- Flatten a list          http://rosettacode.org/wiki/Flatten_a_list
-- (The Rosetta task's mixed-depth list needs a dynamic type; with static
-- typing we demonstrate on a uniformly nested array.)

"== flatten a list ==" println;

[[[1], [2, 3]], [[4, 5]], [[6]]] flatten println;

------------------------------------------------------------------------------
-- Floyd's triangle        http://rosettacode.org/wiki/Floyd%27s_triangle

"== floyd's triangle ==" println;

fn floydRow(i Int) [Int] = (i * (i - 1) // 2 + 1 .. i * (i + 1) // 2) toArray;
(1..10) toArray floydRow @ println;

------------------------------------------------------------------------------
-- Fibonacci sequence      http://rosettacode.org/wiki/Fibonacci_sequence

"== fibonacci sequence ==" println;

let fibs = iter((0, 1), fn(p (Int, Int)) (Int, Int) { (p.1, p.0 + p.1) })
    map(fn(p (Int, Int)) Int { p.0 });
fibs collect(15) println;

------------------------------------------------------------------------------
-- Sort an integer array   http://rosettacode.org/wiki/Sort_an_integer_array

"== sort integers ==" println;

[27, 37, 94, 84, 12, 93, 87, 66, 79, 29, 81, 29, 68, 81, 48] sort println;

------------------------------------------------------------------------------
-- Reverse a string        http://rosettacode.org/wiki/Reverse_a_string

"== reverse a string ==" println;

"reverse a string" reverse println;

------------------------------------------------------------------------------
-- Sum of a series         http://rosettacode.org/wiki/Sum_of_a_series
-- sum(1/x^2) for x = 1..10000

"== sum of a series ==" println;

let series = (1..10000) toArray toFloat;
(1.0 / (series * series)) sum println;

------------------------------------------------------------------------------
-- Catalan numbers         http://rosettacode.org/wiki/Catalan_numbers
-- c(n+1) = c(n) * 2(2n+1) / (n+2)

"== catalan numbers ==" println;

iter((1, 0), fn(s (Int, Int)) (Int, Int) { (s.0 * 2 * (2 * s.1 + 1) // (s.1 + 2), s.1 + 1) })
    map(fn(s (Int, Int)) Int { s.0 })
    collect(15) println;

------------------------------------------------------------------------------
-- Pascal's triangle       http://rosettacode.org/wiki/Pascal%27s_triangle
-- Each row is the elementwise sum of the previous row shifted both ways.

"== pascal's triangle ==" println;

iter([1], fn(row [Int]) [Int] { (row $ [0]) + ([0] $ row) }) take(10) @ println;

------------------------------------------------------------------------------
-- Sum of squares          http://rosettacode.org/wiki/Sum_of_squares

"== sum of squares ==" println;

let v = [1, 2, 3, 4, 5, 6];
(v * v) sum println;

------------------------------------------------------------------------------
-- Hamming numbers         http://rosettacode.org/wiki/Hamming_numbers
-- Generate all 2^i * 3^j * 5^k up to a limit by a Cartesian product of
-- exponents, computed in Float (exact below 2^53), then sort.

"== hamming numbers ==" println;

fn hammingUpTo(limit Float) [Int] =
    (0..31) toArray map(fn(i Int) {
        (0..20) toArray map(fn(j Int) {
            (0..13) toArray map(fn(k Int) {
                2.0 pow(i) * 3.0 pow(j) * 5.0 pow(k)
            })
        })
    }) flatten filter(fn(x Float) { x <= limit }) sort toInt;

let hams = hammingUpTo(2147483648.0);
hams take(20) println;
hams[1690] println;    -- the 1691st hamming number

------------------------------------------------------------------------------
-- Kaprekar numbers        http://rosettacode.org/wiki/Kaprekar_numbers

"== kaprekar numbers ==" println;

fn isKaprekar(n Int) Bool {
    if (n == 1) { return true; }
    let nsq = n * n;
    (1..nsq toString length) toArray any(fn(d Int) {
        let p = 10 pow(d);
        nsq % p > 0 && nsq // p + nsq % p == n
    })
}
(1..10000) toArray filter(isKaprekar) println;

------------------------------------------------------------------------------
-- Perfect numbers         http://rosettacode.org/wiki/Perfect_numbers

"== perfect numbers ==" println;

fn isPerfect(n Int) Bool =
    (1..n // 2) toArray filter(fn(d Int) { n % d == 0 }) sum == n;
(2..10000) toArray filter(isPerfect) println;

------------------------------------------------------------------------------
-- Pythagorean triples     http://rosettacode.org/wiki/Pythagorean_triples

"== pythagorean triples ==" println;

fn triples(n Int) [[Int]] =
    (1..n // 3) toArray map(fn(a Int) {
        (a + 1 .. n // 2) toArray
            filter(fn(b Int) {
                let c = isqrt(a * a + b * b);
                c * c == a * a + b * b && a + b + c <= n
            })
            map(fn(b Int) { [a, b, isqrt(a * a + b * b)] })
    }) join;

fn primitiveTriples(n Int) [[Int]] =
    triples(n) filter(fn(t [Int]) { gcd(t[0], t[1]) == 1 });

triples(100) length println;
primitiveTriples(100) length println;
primitiveTriples(100) @ println;

------------------------------------------------------------------------------
-- Forward difference      http://rosettacode.org/wiki/Forward_difference

"== forward difference ==" println;

fn fwdDiff(xs [Int]) [Int] = xs drop(1) - xs take(xs length - 1);
iter([90, 47, 58, 29, 22, 32, 55, 5, 55, 73], fwdDiff)
    takeWhile(fn(x [Int]) { x length > 0 }) @ println;

------------------------------------------------------------------------------
-- Vector products         http://rosettacode.org/wiki/Vector_products

"== vector products ==" println;

fn dot(a [Int], b [Int]) Int = (a * b) sum;
fn cross(a [Int], b [Int]) [Int] =
    a[[1, 2, 0]] * b[[2, 0, 1]] - a[[2, 0, 1]] * b[[1, 2, 0]];
fn scalarTriple(a [Int], b [Int], c [Int]) Int = a dot(cross(b, c));
fn vectorTriple(a [Int], b [Int], c [Int]) [Int] = a cross(cross(b, c));

let va = [3, 4, 5];
let vb = [4, 3, 5];
let vc = [-5, -12, -13];
va dot(vb) println;
va cross(vb) println;
scalarTriple(va, vb, vc) println;
vectorTriple(va, vb, vc) println;

------------------------------------------------------------------------------
-- Largest int from concatenated ints
--                         http://rosettacode.org/wiki/Largest_int_from_concatenated_ints

"== largest int from concatenated ints ==" println;

fn largestCat(xs [Int]) String =
    xs @ toString sort(fn(a String, b String) { a $ b > b $ a }) join;
largestCat([1, 34, 3, 98, 9, 76, 45, 4]) println;
largestCat([54, 546, 548, 60]) println;

------------------------------------------------------------------------------
-- Spiral matrix           http://rosettacode.org/wiki/Spiral_matrix
-- Walk the spiral as flat-index steps, cumulative-sum to get the visit
-- order, then grade to invert the permutation into cell values.

"== spiral matrix ==" println;

fn spiral(n Int) [[Int]] {
    let runLens = [n - 1] $ ((1..n - 1) toArray reverse stutter(2));
    let dirs = [1, n, -1, -n] ncyc(n) take(2 * n - 1);   -- right, down, left, up, ...
    let visitOrder = ([0] $ dirs spread(runLens)) sums;
    visitOrder grade(fn(a Int, b Int) { a < b }) clump(n)
}
spiral(5) @ println;

------------------------------------------------------------------------------
-- Price fraction          http://rosettacode.org/wiki/Price_fraction

"== price fraction ==" println;

let priceTop = [0.06, 0.11, 0.16, 0.21, 0.26, 0.31, 0.36, 0.41, 0.46, 0.51,
                0.56, 0.61, 0.66, 0.71, 0.76, 0.81, 0.86, 0.91, 0.96, 1.01];
let priceOut = [0.10, 0.18, 0.26, 0.32, 0.38, 0.44, 0.50, 0.54, 0.58, 0.62,
                0.66, 0.70, 0.74, 0.78, 0.82, 0.86, 0.90, 0.94, 0.98, 1.00];
fn priceFrac(v Float) Float = priceOut[priceTop find(fn(t Float) { v < t })];
[0.7388727, 0.8593103, 0.826687, 0.3444635, 0.0491907] priceFrac println;

------------------------------------------------------------------------------
-- Constrained random points on a circle
--                         http://rosettacode.org/wiki/Constrained_random_points_on_a_circle

"== constrained random points on a circle ==" println;

let points = zip(irands(-15, 15), irands(-15, 15))
    filter(fn(p (Int, Int)) {
        let h = hypot(p.0, p.1);
        h >= 10.0 && h <= 15.0
    })
    take(100);

let pointSet = points toSet;
(-15..15) toArray map(fn(y Int) {
    (-15..15) toArray map(fn(x Int) { pointSet contains((x, y)) ? "* " : "  " }) join
}) @ println;

------------------------------------------------------------------------------
-- Repeat a string         http://rosettacode.org/wiki/Repeat_a_string

"== repeat a string ==" println;

repeat("ha", 5) join println;
repeat("*", 5) join println;

------------------------------------------------------------------------------
-- Arithmetic mean         http://rosettacode.org/wiki/Averages/Arithmetic_mean

"== arithmetic mean ==" println;

[3.0, 1.0, 4.0, 1.5, 9.0, 2.6] mean println;

------------------------------------------------------------------------------
-- Best shuffle            http://rosettacode.org/wiki/Best_shuffle
-- Try random shuffles and keep the one with fewest fixed points.

"== best shuffle ==" println;

fn bestShuffle(s String) Void {
    let cs = s split("");
    let tries = (1..100) toArray map(fn(i Int) { cs muss });
    let scores = tries map(fn(t [String]) {
        zip(t, cs) filter(fn(p (String, String)) { p.0 == p.1 }) length
    });
    let order = scores grade(fn(a Int, b Int) { a < b });
    "%^, %^, (%^)" fmt(s, tries[order[0]] join, scores[order[0]]) println;
}
["a", "up", "elk", "seesaw", "grrrrrr", "abracadabra"] @ bestShuffle;

------------------------------------------------------------------------------
-- Roman numerals (encode) http://rosettacode.org/wiki/Roman_numerals/Encode

"== roman numerals ==" println;

fn toRoman(x Int) String {
    let hundreds = ["", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM"];
    let tens = ["", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC"];
    let ones = ["", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX"];
    repeat("M", x // 1000) join
        $ hundreds[x // 100 % 10]
        $ tens[x // 10 % 10]
        $ ones[x % 10]
}
[1987, 2026, 3999, 44] toRoman println;

------------------------------------------------------------------------------
-- Builtin notes -- gaps found while writing these examples, and how they
-- were resolved (July 2026):
--
-- Already existed (misread the library at first):
--  * join is the one-level flatten ([[T]] -> [T], List<List<T>> -> List<T>,
--    and [String] -> String); flatten collapses ALL levels.
--  * Per-element toString is `xs @ toString` (plain `xs toString` renders
--    the whole array as one string via the array overload).
--
-- Implemented as builtins while writing this file:
--  * sum / product / mean over [Int]/[Float]/List<Int>/List<Float>.
--  * min / max of a collection (Int / Float / String elements).
--  * sums / products / mins / maxs -- running reductions; the List forms
--    are lazy, so they compose with infinite lists.
--  * pow(Fraction, Int) -> Fraction, exact, with negative exponents.
--  * any / all -- predicate form over arrays and lists, and directly over
--    [Bool] / List<Bool>. Lazy lists short-circuit.
--  * contains([T], x) and contains(List<T>, x).
--  * pow(Int, Int) -> Int.
--  * clump([T], n) -> [[T]] -- fixed-size rows, short remainder kept.
--  * spread([T], [Int]) -> [T] -- replicate element i counts[i] times
--    (counts index cyclically); sapf's spread.
--  * ncyc for arrays (cyc stays List-only: an infinite array is infeasible).
--  * fromCodePoints ([Int] or List<Int> -> String), inverse of codePoints.
--  * toSet over arrays and lists.
--
-- Still open:
--  * Operators as function values, e.g. fold(0, +) or fold1(`+`) -- a
--    language feature rather than a builtin.
--  * The new builtins have no persistent-vector (#[...]) variants yet.
--  * clump / spread are array-only (a lazy List clump would be a generator).
------------------------------------------------------------------------------
