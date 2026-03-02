-- Test @ auto-mapping on binary operator operands

-- Basic: scalar + array @
let a = 10 + [1, 2, 3] @;
println(a);
-- Expected: [11, 12, 13]

-- array @ + scalar
let b = [1, 2, 3] @ + 10;
println(b);
-- Expected: [11, 12, 13]

-- Zip: both operands auto-mapped
let c = [1, 2, 3] @ + [10, 20, 30] @;
println(c);
-- Expected: [11, 22, 33]

-- Subtraction
let d = [10, 20, 30] @ - 5;
println(d);
-- Expected: [5, 15, 25]

-- Multiplication
let e = [1, 2, 3] @ * [10, 10, 10] @;
println(e);
-- Expected: [10, 20, 30]

-- Comparison
let f = [1, 5, 3] @ > 2;
println(f);
-- Expected: [false, true, true]

-- Tuple + array @
let g = (10, 20) + [1, 2, 3] @;
println(g);
-- Expected: [(11, 21), (12, 22), (13, 23)]

-- array @ + tuple
let h = [1, 2, 3] @ + (10, 20);
println(h);
-- Expected: [(11, 21), (12, 22), (13, 23)]

-- Deep map: @@ on binary ops
let nested = [[1, 2, 3], [4, 5, 6]];
let i = nested @@ + 10;
println(i);
-- Expected: [[11, 12, 13], [14, 15, 16]]

-- Cartesian: @1 and @2 on binary ops
let j = [1, 2] @1 + [10, 20] @2;
println(j);
-- Expected: [[11, 21], [12, 22]]

-- Operator overload with @
struct Pt { x Int, y Int }
fn +(a Pt, b Pt) Pt = Pt { x: a.x + b.x, y: a.y + b.y };
let pts = [Pt{x: 1, y: 2}, Pt{x: 3, y: 4}];
let k = pts @ + Pt{x: 10, y: 20};
println(k);
-- Expected: [Pt { x: 11, y: 22 }, Pt { x: 13, y: 24 }]

-- List + scalar @ (list result)
let lst = List(1, 2, 3);
let m = lst @ + 10;
println(m);
-- Expected: List(11, 12, 13)

-- Mixed: array @ with division
let n = [10, 20, 30] @ / 2;
println(n);
-- Expected: [5/1, 10/1, 15/1]
