-- Range tests

-- Basic integer range
let r = (1..5);
r println;
r length println;
r toArray println;

-- Descending range
let r2 = (5..1);
r2 println;
r2 length println;
r2 toArray println;

-- Stepped range
let r3 = (0,2..10);
r3 toArray println;

-- Stepped descending
let r4 = (10,8..2);
r4 toArray println;

-- Large step
let r5 = (0,7..100);
r5 toArray println;

-- toList (lazy)
(1..5) toList println;
(5..1) toList println;
(0,2..10) toList println;

-- Single element range
let r6 = (5..5);
r6 length println;
r6 toArray println;

-- Fraction ranges
for (i : (1/2..5/2)) {
    i println;
}

-- Fraction range as value
let fr = (1/2..5/2);
fr length println;
fr toArray println;

-- Fraction stepped range
let fr2 = (1/4, 1/2..2/1);
fr2 toArray println;

-- Infinite range (lazy, show first few)
let linf = (1..) toList;
linf println;

-- Indexing: finite ranges index cyclically like arrays
let ri = (0..9);
ri[3] println;
ri[-1] println;
ri[13] println;

-- Indexing stepped and descending ranges
(0,2..10)[3] println;
(5..1)[1] println;

-- Indexing infinite ranges uses the absolute value of the index
let rinf = (0..);
rinf[5] println;
rinf[-3] println;
(1,3..)[-2] println;

-- Indexing fraction ranges
(1/2..5/2)[1] println;
(1/2..5/2)[-1] println;
(1/2..)[-2] println;

-- Single-element range: cyclic from any index
(5..5)[0] println;
(5..5)[7] println;

-- Indexing by a collection of indices
(0..9)[[3, 1, 2]] println;
(10,8..2)[[0, -1]] println;
(0..9)[(1..3) toList] println;

-- Explicit @ auto-map over containers of ranges
[(0..9), (10..19)] @ [2] println;
[(0..9), (10..19)] @ [[0, 3]] println;
