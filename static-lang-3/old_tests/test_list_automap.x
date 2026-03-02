-- Test list auto-mapping in numeric tower

-- List + scalar = lazy mapped list
let a = List(1, 2, 3) + 10;
println(a);

-- scalar + List
let b = 100 + List(1, 2, 3);
println(b);

-- List * scalar
let c = List(10, 20, 30) * 2;
println(c);

-- List - scalar
let d = List(10, 20, 30) - 5;
println(d);

-- List + List (zip)
let e = List(1, 2, 3) + List(10, 20, 30);
println(e);

-- Negation of list
let f = -List(1, 2, 3);
println(f);

-- List print limit
let old = setListPrintLimit(3);
let big = List(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
println(big);
setListPrintLimit(old);

-- getListPrintLimit
let lim = getListPrintLimit();
println(lim);
