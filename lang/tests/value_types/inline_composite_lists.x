-- Phase 4g.20/4g.21: List/array operations on inline composite element types
-- (Complex, Fraction, inline Tuple, inline Struct).
--
-- Several of these used to crash or print garbage:
--   * List[Complex/Fraction] head/tail (returned pointer-as-f64).
--   * BinopListGen / UnaryListGen on List[Complex/Fraction].
--   * `xs @ println` (genAutoMapCallListVoid) on inline-composite lists.
--   * collect(List[Complex/Fraction], n).
--   * Array+Array binop with inline-tuple elements (generic dispatch path).
--   * Cartesian binop `xs @1 + ys @2` with inline-tuple elements.

struct Pt { x Int; y Int }

-- --- Complex list heads ---
let cs = List(1.0+2.0i, 3.0+4.0i, 5.0+6.0i);
cs head println;
cs tail head println;
cs tail tail head println;

-- --- Fraction list heads ---
let fs = List(1/2, 3/4, 5/6);
fs head println;
fs tail head println;
fs tail tail head println;

-- --- Inline tuple list heads ---
let ts = List((1, 2), (3, 4), (5, 6));
ts head println;
ts tail head println;

-- --- Inline struct list heads ---
let ps = List(Pt{x:1, y:2}, Pt{x:3, y:4});
ps head println;
ps tail head println;

-- --- BinopListGen: List + List (zip) and List + scalar (broadcast) ---
(cs + cs) println;
(cs + (10.0+0.0i)) println;
(fs + fs) println;
(fs + 1/4) println;

-- --- UnaryListGen ---
(- cs) println;
(- fs) println;

-- --- Void-returning auto-map (genAutoMapCallListVoid) ---
-- Each of these used to print real-only/numerator-only garbage.
cs @ println;
fs @ println;
ts @ println;
ps @ println;

-- --- collect ---
collect(cs, 2) println;
collect(fs, 2) println;

-- --- Fraction range toList (FractionRangeListGen native head write) ---
toList((1/2..5/2)) println;
toList((1/4, 1/2..2/1)) println;

-- --- Array+Array binop with inline-tuple elements (dispatchArrayBinop) ---
let a = [(1, 2), (3, 4)];
let b = [(10, 20), (30, 40)];
(a + b) println;

-- --- Cartesian binop with inline-tuple elements (genCartesianBinaryOp) ---
(a @1 + b @2) println;
(a @1 == b @2) println;

-- --- Length on inline-composite lists ---
cs length println;
fs length println;
ts length println;

-- --- Lambda auto-map over a List (genAutoMapLambdaCallList) ---
-- This used to crash unconditionally because genAutoMapLambdaCall only
-- handled Array auto-map.
let int_xs = List(1, 2, 3);
let dbl = fn(x Int) Int { x * 2 };
int_xs dbl println;

let addone = fn(c Complex) Complex { c + (1.0+0.0i) };
cs addone println;

let half = fn(f Fraction) Fraction { f * 1/2 };
fs half println;

-- Closure capturing a local
let n = 10;
let addn = fn(x Int) Int { x + n };
int_xs addn println;

-- --- filter over Array[Complex/Fraction] (getArrayElem PodArray<x64>/<r64>) ---
-- These used to segfault because getArrayElem treated the PodArray as ObjArray.
let arr_c = [1.0+2.0i, 3.0+4.0i];
let arr_f = [1/2, 3/4];
filter(arr_c, fn(c Complex) Bool { true }) println;
filter(arr_f, fn(f Fraction) Bool { true }) println;

-- --- Phase 4g.22: reducer lambdas (fold/scan/fold1/scan1/find) with
--     Complex/Fraction accumulators / list elements. Previously fold crashed
--     reading `fn = vm.reg(ab+2)` when acc was 2-word native at ab+1..ab+2,
--     and find returned -1 because the lambda received the Fraction as a
--     1-word boxed pointer where its body expected 2-word native.
fold(List(1/2, 1/2, 1/2), 0/1, fn(a Fraction, x Fraction) Fraction { a + x }) println;
fold([1/2, 1/4, 1/8], 0/1, fn(a Fraction, x Fraction) Fraction { a + x }) println;
fold(List(1.0+2.0i, 3.0+4.0i), 0.0+0.0i, fn(a Complex, x Complex) Complex { a + x }) println;
fold([1.0+2.0i, 3.0+4.0i], 0.0+0.0i, fn(a Complex, x Complex) Complex { a + x }) println;

scan(List(1/2, 1/4), 0/1, fn(a Fraction, x Fraction) Fraction { a + x }) println;
scan([1/2, 1/4], 0/1, fn(a Fraction, x Fraction) Fraction { a + x }) println;
scan(List(1.0+2.0i, 3.0+4.0i), 0.0+0.0i, fn(a Complex, x Complex) Complex { a + x }) println;

fold1(List(1/2, 1/4, 1/8), fn(a Fraction, x Fraction) Fraction { a + x }) println;
fold1([1/2, 1/4, 1/8], fn(a Fraction, x Fraction) Fraction { a + x }) println;
fold1([1.0+2.0i, 3.0+4.0i], fn(a Complex, x Complex) Complex { a + x }) println;

scan1([1/2, 1/4], fn(a Fraction, x Fraction) Fraction { a + x }) println;

find([1/2, 1/4], fn(f Fraction) Bool { f == 1/4 }) println;
find(List(1/2, 1/4), fn(f Fraction) Bool { f == 1/4 }) println;
find([1.0+2.0i, 3.0+4.0i], fn(c Complex) Bool { c == (3.0+4.0i) }) println;

takeWhile([1/2, 1/4, 3/4], fn(f Fraction) Bool { f < 1/1 }) println;
dropWhile([1/2, 1/4, 3/4], fn(f Fraction) Bool { f < 1/1 }) println;

-- Phase 4g.26: widened lambda ABI -- list-head / array-elem reads flow
-- directly into the lambda's arg slot window without the intermediate
-- box-then-unbox round-trip. Same ops as before; verifying nothing
-- regressed for Complex/Fraction/Inline composite lambdas now that the
-- box helpers are no longer in the hot path.
struct Pq { a Int; b Int }
let csW = List(1.0+2.0i, 3.0+4.0i, 5.0+6.0i);
let fsW = List(1/2, 3/4, 5/6);
let psW = List(Pq{a:1, b:2}, Pq{a:3, b:4}, Pq{a:5, b:6});
map(csW, fn(c Complex) Complex { c * (2.0+0.0i) }) println;
map(fsW, fn(f Fraction) Fraction { f + 1/4 }) println;
map(psW, fn(p Pq) Pq { Pq{a: p.a*10, b: p.b*10} }) println;
filter(csW, fn(c Complex) Bool { c == (3.0+4.0i) }) length println;
find(fsW, fn(f Fraction) Bool { f == 3/4 }) println;
find(psW, fn(p Pq) Bool { p.a == 3 }) println;
takeWhile(fsW, fn(f Fraction) Bool { f < 4/5 }) println;
sort([3.0+4.0i, 1.0+2.0i, 5.0+6.0i], fn(a Complex, b Complex) Bool {
    (a)real < (b)real
}) println;
sort([3/4, 1/2, 5/6], fn(a Fraction, b Fraction) Bool { a < b }) println;
