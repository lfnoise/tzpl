-- Test: numeric downcasts via as(Type)

-- Float -> Int (truncation toward zero)
let f1 = 3.7;
let i1 = f1 as(Int);
println(i1);

let f2 = -3.7;
let i2 = f2 as(Int);
println(i2);

let f3 = 0.0;
let i3 = f3 as(Int);
println(i3);

-- Fraction -> Int (truncation toward zero)
let r1 = 7/2;
let i4 = r1 as(Int);
println(i4);

let r2 = -7/2;
let i5 = r2 as(Int);
println(i5);

-- Complex -> Float (real part)
let c1 = 3.5 + 2.0i;
let f4 = c1 as(Float);
println(f4);

-- Complex -> Int (real part, truncated)
let c2 = 7.9 + 1.0i;
let i6 = c2 as(Int);
println(i6);

-- Complex -> Fraction (real part, truncated to integer fraction)
let c3 = 5.7 + 3.0i;
let r3 = c3 as(Fraction);
println(r3);

-- Chained: Complex -> Float -> Int
let c4 = 9.3 + 4.0i;
let i7 = c4 as(Float) as(Int);
println(i7);

-- Auto-mapped: [Float] -> [Int]
let arr1 = [1.2, 2.4, 3.6, 4.8];
println(arr1 as(Int));

-- Auto-mapped: [Complex] -> [Float]
let arr2 = [1.5 + 2.0i, 3.5 + 4.0i];
println(arr2 as(Float));

-- Auto-mapped: nested [[Float]] -> [[Int]]
let arr3 = [[1.1, 2.9], [3.5, 4.7]];
println(arr3 as(Int));
