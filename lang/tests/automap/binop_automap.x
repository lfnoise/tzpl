-- @ auto-mapping on binary operator operands

-- scalar + array @
println(10 + [1, 2, 3] @);

-- array @ + scalar
println([1, 2, 3] @ + 10);

-- Zip: both @
println([1, 2, 3] @ + [10, 20, 30] @);

-- Subtraction
println([10, 20, 30] @ - 5);

-- Multiplication
println([1, 2, 3] @ * [10, 10, 10] @);

-- Comparison
println([1, 5, 3] @ > 2);

-- Tuple + array @
println((10, 20) + [1, 2, 3] @);

-- array @ + tuple
println([1, 2, 3] @ + (10, 20));

-- Deep map: @@ on binary ops
let nested = [[1, 2, 3], [4, 5, 6]];
println(nested @@ + 10);

-- Deep map @@ with composite (tuple) result
println([[(1, 2), (3, 4)], [(5, 6)]] @@ + (10, 20));

-- Cartesian: @1 and @2 on binary ops
println([1, 2] @1 + [10, 20] @2);

-- Operator overload with @
struct Pt { x Int, y Int }
fn +(a Pt, b Pt) Pt = Pt { x: a.x + b.x, y: a.y + b.y };
let pts = [Pt{x: 1, y: 2}, Pt{x: 3, y: 4}];
println(pts @ + Pt{x: 10, y: 20});

-- Deep map @@ with operator overload
println([[Pt{x: 1, y: 1}], [Pt{x: 2, y: 2}]] @@ + Pt{x: 10, y: 20});

-- List + scalar @
let lst = List(1, 2, 3);
println(lst @ + 10);
