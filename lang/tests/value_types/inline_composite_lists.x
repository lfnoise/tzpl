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
